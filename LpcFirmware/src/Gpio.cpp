#include "Gpio.h"
#include "Lpc1115.h"

namespace Gpio
{

struct PinDefinition
{
	uint8_t id;
	uint16_t ioconOffset;
};

struct PinState
{
	LpcProtocol::GpioMode mode;
	bool lastValue;
};

static constexpr PinDefinition pins[] = {
	{ 0x27, 0x020 }, // PIO2_7, filament runout
	{ 0x21, 0x028 }, // PIO2_1, extruder rotation sensor
	{ 0x06, 0x04C }, // PIO0_6, hotend filament sensor
	{ 0x2A, 0x058 }, // PIO2_10, status LED
	{ 0x04, 0x030 }, // PIO0_4, NFC SCL
	{ 0x05, 0x034 }, // PIO0_5, NFC SDA
	{ 0x30, 0x084 }, // PIO3_0, NFC TX
	{ 0x31, 0x088 }, // PIO3_1, NFC RX
	{ 0x09, 0x064 }, // PIO0_9, heater
	{ 0x25, 0x044 }, // PIO2_5, hotend fan
	{ 0x1A, 0x06C }, // PIO1_10, reflow fan
	{ 0x10, 0x078 }  // PIO1_0, hotend NTC/AD1
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
	for (PinState& state : states)
	{
		state.mode = LpcProtocol::GpioMode::disabled;
		state.lastValue = false;
	}
}

bool Configure(uint8_t pin, LpcProtocol::GpioMode mode, bool initialValue) noexcept
{
	const int index = FindPin(pin);
	if (index < 0)
	{
		return false;
	}

	volatile uint32_t& iocon = Iocon(static_cast<unsigned int>(index));
	if (mode == LpcProtocol::GpioMode::analog)
	{
		iocon = (iocon & ~0x9Fu) | 0x02u;
		states[index].mode = mode;
		return true;
	}
	iocon &= ~((0x07u) | (0x03u << 3));
	if (mode == LpcProtocol::GpioMode::inputPullup)
	{
		iocon |= 0x02u << 3;
	}

	const uint32_t mask = 1u << (pin & 0x0Fu);
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
