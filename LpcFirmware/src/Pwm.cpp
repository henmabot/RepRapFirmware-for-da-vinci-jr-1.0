#include "Pwm.h"
#include "Gpio.h"
#include "Lpc1115.h"

namespace Pwm
{

constexpr uint32_t CoreClock = 12000000u;
constexpr uintptr_t Ct16b1Base = 0x40010000u;
constexpr uintptr_t Ct32b0Base = 0x40014000u;

struct Channel
{
	uint8_t pin;
	uint16_t ioconOffset;
	uintptr_t timerBase;
	uint32_t maxPeriod;
	uint8_t clockBit;
	uint8_t matchChannel;
	uint8_t pinFunction;
	bool analogCapable;
};

static constexpr Channel channels[] = {
	{ 0x09, 0x064, 0x4000C000u, 0x0000FFFFu, 7, 1, 2, false }, // PIO0_9/CT16B0_MAT1 heater
	{ 0x25, 0x044, Ct32b0Base, 0xFFFFFFFFu, 9, 0, 1, false }, // PIO2_5/CT32B0_MAT0 hotend fan
	{ 0x1A, 0x06C, Ct16b1Base, 0x0000FFFFu, 8, 1, 2, true }   // PIO1_10/CT16B1_MAT1 reflow fan
};

static volatile uint32_t& TimerRegister(uintptr_t base, uintptr_t offset) noexcept
{
	return *reinterpret_cast<volatile uint32_t*>(base + offset);
}

static volatile uint32_t& Iocon(const Channel& channel) noexcept
{
	return *reinterpret_cast<volatile uint32_t*>(LPC_IOCON_BASE + channel.ioconOffset);
}

static const Channel* Find(uint8_t pin) noexcept
{
	for (const Channel& channel : channels)
	{
		if (channel.pin == pin)
		{
			return &channel;
		}
	}
	return nullptr;
}

static void SetConstant(const Channel& channel, bool high) noexcept
{
	TimerRegister(channel.timerBase, 0x004) = 0;
	Gpio::Configure(channel.pin, LpcProtocol::GpioMode::pwm, high);
}


bool Set(uint8_t pin, uint16_t duty, uint16_t frequency) noexcept
{
	const Channel* const channel = Find(pin);
	if (channel == nullptr || frequency == 0)
	{
		return false;
	}

	if (duty == 0 || duty == 65535u)
	{
		SetConstant(*channel, duty != 0);
		return true;
	}

	const uint64_t maxTimerClock = static_cast<uint64_t>(frequency) * channel->maxPeriod;
	const uint32_t divider = (maxTimerClock >= CoreClock)
		? 1u
		: static_cast<uint32_t>((CoreClock + maxTimerClock - 1u) / maxTimerClock);
	const uint32_t timerClock = CoreClock / divider;
	const uint32_t period = timerClock / frequency;
	if (period < 2 || period > channel->maxPeriod)
	{
		return false;
	}

	const uint32_t highTicks = static_cast<uint32_t>((static_cast<uint64_t>(period) * duty + 32767u) / 65535u);
	const uint32_t match = period - highTicks;

	LPC_SYSCON_SYSAHBCLKCTRL |= 1u << channel->clockBit;
	volatile uint32_t& iocon = Iocon(*channel);
	iocon = (iocon & ~0x07u) | channel->pinFunction;
	if (channel->analogCapable)
	{
		iocon |= 1u << 7;
	}

	TimerRegister(channel->timerBase, 0x004) = 0x02u; // reset timer
	TimerRegister(channel->timerBase, 0x00C) = divider - 1u;
	TimerRegister(channel->timerBase, 0x018 + 4u * channel->matchChannel) = match;
	TimerRegister(channel->timerBase, 0x024) = period; // MR3 sets the PWM cycle
	TimerRegister(channel->timerBase, 0x014) = 1u << 10; // reset on MR3
	TimerRegister(channel->timerBase, 0x074) = (1u << 3) | (1u << channel->matchChannel);
	TimerRegister(channel->timerBase, 0x004) = 0x01u;
	return true;
}

}
