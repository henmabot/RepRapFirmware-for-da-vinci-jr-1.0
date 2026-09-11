#ifndef LPC_PROTOCOL_H
#define LPC_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

namespace LpcProtocol
{

constexpr uint8_t Version = 3;
constexpr uint8_t SyncByte = 0xA5;
constexpr size_t MaxPayload = 16;
constexpr size_t MaxEncodedFrame = MaxPayload + 4;

namespace Pins
{
	constexpr uint8_t FilamentRunout = 0x27;
	constexpr uint8_t Rotation = 0x21;
	constexpr uint8_t HotendFilament = 0x06;
	constexpr uint8_t StatusLed = 0x2A;
	constexpr uint8_t NfcScl = 0x04;
	constexpr uint8_t NfcSda = 0x05;
	constexpr uint8_t NfcTx = 0x30;
	constexpr uint8_t NfcRx = 0x31;
	constexpr uint8_t Heater = 0x09;
	constexpr uint8_t HotendFan = 0x25;
	constexpr uint8_t ReflowFan = 0x1A;
	constexpr uint8_t HotendNtc = 0x10;
}

enum class MessageType : uint8_t
{
	ping = 1,
	pong = 2,
	gpioConfig = 3,
	gpioWrite = 4,
	gpioState = 5,
	pwmWrite = 6,
	thermistorConfig = 7,
	heaterModelA = 8,
	heaterModelB = 9,
	heaterModelC = 10,
	heaterConfig = 11,
	heaterCommand = 12,
	thermalStatus = 13,
	heaterFeedForward = 14,
	configurationReset = 15,
	configurationComplete = 16
};

enum class GpioMode : uint8_t
{
	disabled = 0,
	input = 1,
	inputPullup = 2,
	output = 3,
	pwm = 4,
	analog = 5
};

enum class HeaterCommand : uint8_t
{
	off = 0,
	on = 1,
	suspend = 2,
	resetFault = 3
};

enum class HeaterState : uint8_t
{
	fault = 0,
	offline = 1,
	off = 2,
	suspended = 3,
	cooling = 4,
	stable = 5,
	heating = 6
};

enum class ThermalError : uint8_t
{
	none = 0,
	notConfigured = 1,
	adcTimeout = 2,
	shortCircuit = 3,
	openCircuit = 4,
	overTemperature = 5,
	underTemperature = 6,
	linkTimeout = 7,
	controlFault = 8,
	heatingTooSlow = 9,
	temperatureExcursion = 10
};

struct Frame
{
	MessageType type;
	uint8_t length;
	uint8_t payload[MaxPayload];
};

struct Decoder
{
	Frame frame;
	uint8_t state;
	uint8_t index;
	uint8_t crc;
};

void Reset(Decoder& decoder) noexcept;
bool Feed(Decoder& decoder, uint8_t byte, Frame& frame) noexcept;
size_t Encode(MessageType type, const uint8_t* payload, uint8_t length, uint8_t* output) noexcept;

}

#endif
