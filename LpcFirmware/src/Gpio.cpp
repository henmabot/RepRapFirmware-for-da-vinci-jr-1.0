#include "Gpio.h"
#include "Lpc1115.h"

namespace Gpio
{

struct PinDefinition
{
	uint8_t id;
	uint16_t ioconOffset;
	uint8_t gpioFunction;
	bool analogCapable;
};

struct PinState
{
	LpcProtocol::GpioMode mode;
	bool lastValue;
};

static constexpr PinDefinition pins[] = {
	{ LpcProtocol::Pins::FilamentRunout, 0x020, 0, false }, // PIO2_7
	{ LpcProtocol::Pins::Rotation, 0x028, 0, false },        // PIO2_1
	{ LpcProtocol::Pins::HotendFilament, 0x04C, 0, false }, // PIO0_6
	{ LpcProtocol::Pins::StatusLed, 0x058, 0, false },       // PIO2_10
	{ LpcProtocol::Pins::NfcScl, 0x030, 0, false },          // PIO0_4
	{ LpcProtocol::Pins::NfcSda, 0x034, 0, false },          // PIO0_5
	{ LpcProtocol::Pins::NfcTx, 0x084, 0, false },           // PIO3_0
	{ LpcProtocol::Pins::NfcRx, 0x088, 0, false },           // PIO3_1
	{ LpcProtocol::Pins::Heater, 0x064, 0, false },          // PIO0_9
	{ LpcProtocol::Pins::HotendFan, 0x044, 0, false },       // PIO2_5
	{ LpcProtocol::Pins::ReflowFan, 0x06C, 0, true },        // PIO1_10/AD6
	{ LpcProtocol::Pins::HotendNtc, 0x078, 1, true }         // R/PIO1_0/AD1
};

static PinState states[sizeof(pins) / sizeof(pins[0])];

static int FindPin(uint8_t pin) noexcept
{
	for (unsigned int i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i)
	{
		if (pins[i].id == pin)
		{
			return static_cast<int>(i);
		}
	}
	return -1;
}

static volatile uint32_t& Iocon(unsigned int index) noexcept
{
	return *reinterpret_cast<volatile uint32_t*>(LPC_IOCON_BASE + pins[index].ioconOffset);
}

static volatile uint32_t& Direction(uint8_t pin) noexcept
{
	const uintptr_t base = 0x50000000u + static_cast<uintptr_t>(pin >> 4) * 0x10000u;
	return *reinterpret_cast<volatile uint32_t*>(base + 0x8000u);
}

static volatile uint32_t& MaskedData(uint8_t pin) noexcept
{
	const uint32_t mask = 1u << (pin & 0x0Fu);
	const uintptr_t base = 0x50000000u + static_cast<uintptr_t>(pin >> 4) * 0x10000u;
	return *reinterpret_cast<volatile uint32_t*>(base + (static_cast<uintptr_t>(mask) << 2));
}

static bool ReadRaw(uint8_t pin) noexcept
{
	const uint32_t mask = 1u << (pin & 0x0Fu);
	return (MaskedData(pin) & mask) != 0u;
}

static void WriteRaw(uint8_t pin, bool value) noexcept
{
	const uint32_t mask = 1u << (pin & 0x0Fu);
	MaskedData(pin) = value ? mask : 0u;
}

void Init() noexcept
{
	LPC_SYSCON_SYSAHBCLKCTRL |= (1u << 16) | (1u << 6);
	for (unsigned int i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i)
	{
		states[i].mode = LpcProtocol::GpioMode::disabled;
		states[i].lastValue = false;
		(void)Configure(pins[i].id, LpcProtocol::GpioMode::disabled, false);
	}
}

bool Configure(uint8_t pin, LpcProtocol::GpioMode mode, bool initialValue) noexcept
{
	const int index = FindPin(pin);
	if (index < 0)
	{
		return false;
	}

	const uint32_t mask = 1u << (pin & 0x0Fu);
	volatile uint32_t& iocon = Iocon(static_cast<unsigned int>(index));
	if (mode == LpcProtocol::GpioMode::analog)
	{
		iocon = (iocon & ~0x9Fu) | 0x02u;
		Direction(pin) &= ~mask;
		states[index].mode = mode;
		return true;
	}
	iocon = (iocon & ~((0x07u) | (0x03u << 3))) | pins[index].gpioFunction;
	if (pins[index].analogCapable)
	{
		iocon |= 1u << 7;
	}
	if (mode == LpcProtocol::GpioMode::inputPullup)
	{
		iocon |= 0x02u << 3;
	}

	if (mode == LpcProtocol::GpioMode::output || mode == LpcProtocol::GpioMode::pwm)
	{
		WriteRaw(pin, initialValue);
		Direction(pin) |= mask;
	}
	else
	{
		Direction(pin) &= ~mask;
	}

	PinState& state = states[index];
	state.mode = mode;
	if (mode == LpcProtocol::GpioMode::input || mode == LpcProtocol::GpioMode::inputPullup)
	{
		state.lastValue = !ReadRaw(pin);
	}
	return true;
}

bool Write(uint8_t pin, bool value) noexcept
{
	const int index = FindPin(pin);
	if (index < 0 || states[index].mode != LpcProtocol::GpioMode::output)
	{
		return false;
	}
	WriteRaw(pin, value);
	return true;
}

bool Poll(uint8_t& pin, bool& value) noexcept
{
	for (unsigned int i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i)
	{
		const LpcProtocol::GpioMode mode = states[i].mode;
		if (mode != LpcProtocol::GpioMode::input && mode != LpcProtocol::GpioMode::inputPullup)
		{
			continue;
		}

		const bool current = ReadRaw(pins[i].id);
		if (current != states[i].lastValue)
		{
			states[i].lastValue = current;
			pin = pins[i].id;
			value = current;
			return true;
		}
	}
	return false;
}

}
