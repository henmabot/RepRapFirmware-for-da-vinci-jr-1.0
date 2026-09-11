#include "LpcInterface.h"

#if defined(DA_VINCI_JR)
#include "Devices.h"
#include <LpcProtocol.h>
#include <RepRapFirmware.h>

namespace LpcInterface
{

static LpcProtocol::Decoder decoder;
static LpcProtocol::GpioMode pinModes[NumLpcPins];
static bool pinValues[NumLpcPins];
static uint16_t pwmValues[NumLpcPins];
static uint16_t pwmFrequencies[NumLpcPins];
static bool online;
static uint32_t lastPingSent;
static uint32_t lastPongReceived;
static Mutex transmitMutex;

static void Send(LpcProtocol::MessageType type, const uint8_t* payload, uint8_t payloadLength) noexcept
{
	uint8_t encoded[LpcProtocol::MaxEncodedFrame];
	const size_t length = LpcProtocol::Encode(type, payload, payloadLength, encoded);
	MutexLocker lock(transmitMutex);
	lpcUart.write(encoded, length);
}

static void SendPing() noexcept
{
	Send(LpcProtocol::MessageType::ping, nullptr, 0);
	lastPingSent = millis();
}

static void SendGpioConfig(size_t index) noexcept
{
	const uint8_t payload[] = {
		GetLpcPinId(FirstLpcPin + index),
		static_cast<uint8_t>(pinModes[index]),
		static_cast<uint8_t>(pinValues[index])
	};
	Send(LpcProtocol::MessageType::gpioConfig, payload, sizeof(payload));
}

static void SendPwm(size_t index) noexcept
{
	const uint16_t duty = pwmValues[index];
	const uint16_t frequency = pwmFrequencies[index];
	const uint8_t payload[] = {
		GetLpcPinId(FirstLpcPin + index),
		static_cast<uint8_t>(duty),
		static_cast<uint8_t>(duty >> 8),
		static_cast<uint8_t>(frequency),
		static_cast<uint8_t>(frequency >> 8)
	};
	Send(LpcProtocol::MessageType::pwmWrite, payload, sizeof(payload));
}

static void HandlePong(const LpcProtocol::Frame& frame) noexcept
{
	if (frame.length != 1 || frame.payload[0] != LpcProtocol::Version)
	{
		return;
	}

	const bool reconnected = !online;
	online = true;
	lastPongReceived = millis();
	if (reconnected)
	{
		for (size_t i = 0; i < NumLpcPins; ++i)
		{
			if (pinModes[i] != LpcProtocol::GpioMode::disabled)
			{
				SendGpioConfig(i);
				if (pinModes[i] == LpcProtocol::GpioMode::pwm && pwmFrequencies[i] != 0)
				{
					SendPwm(i);
				}
			}
		}
	}
}

static void HandleGpioState(const LpcProtocol::Frame& frame) noexcept
{
	if (frame.length != 2)
	{
		return;
	}
	for (size_t i = 0; i < NumLpcPins; ++i)
	{
		if (GetLpcPinId(FirstLpcPin + i) == frame.payload[0])
		{
			pinValues[i] = frame.payload[1] != 0;
			return;
		}
	}
}

void Init() noexcept
{
	LpcProtocol::Reset(decoder);
	for (size_t i = 0; i < NumLpcPins; ++i)
	{
		pinModes[i] = LpcProtocol::GpioMode::disabled;
		pinValues[i] = false;
		pwmValues[i] = 0;
		pwmFrequencies[i] = 0;
	}
	transmitMutex.Create("LPC");
	online = false;
	lastPongReceived = 0;
	SendPing();
}

void Spin() noexcept
{
	LpcProtocol::Frame frame;
	while (lpcUart.available() != 0)
	{
		const int value = lpcUart.read();
		if (value < 0 || !LpcProtocol::Feed(decoder, static_cast<uint8_t>(value), frame))
		{
			continue;
		}

		switch (frame.type)
		{
		case LpcProtocol::MessageType::pong:
			HandlePong(frame);
			break;

		case LpcProtocol::MessageType::gpioState:
			HandleGpioState(frame);
			break;

		default:
			break;
		}
	}

	const uint32_t now = millis();
	if (online && now - lastPongReceived >= 3000)
	{
		online = false;
	}
	if (now - lastPingSent >= 1000)
	{
		SendPing();
	}
}

bool IsOnline() noexcept
{
	return online;
}

bool SetPinMode(Pin pin, PinMode mode) noexcept
{
	if (!IsLpcPin(pin))
	{
		return false;
	}

	const size_t index = pin - FirstLpcPin;
	switch (mode)
	{
	case INPUT:
		pinModes[index] = LpcProtocol::GpioMode::input;
		break;
	case INPUT_PULLUP:
		pinModes[index] = LpcProtocol::GpioMode::inputPullup;
		break;
	case OUTPUT_LOW:
		pinModes[index] = LpcProtocol::GpioMode::output;
		pinValues[index] = false;
		break;
	case OUTPUT_HIGH:
		pinModes[index] = LpcProtocol::GpioMode::output;
		pinValues[index] = true;
		break;
	case OUTPUT_PWM_LOW:
		pinModes[index] = LpcProtocol::GpioMode::pwm;
		pinValues[index] = false;
		break;
	case OUTPUT_PWM_HIGH:
		pinModes[index] = LpcProtocol::GpioMode::pwm;
		pinValues[index] = true;
		break;
	default:
		return false;
	}

	if (online)
	{
		SendGpioConfig(index);
	}
	return true;
}

bool ReadPin(Pin pin) noexcept
{
	return IsLpcPin(pin) && pinValues[pin - FirstLpcPin];
}

void WritePin(Pin pin, bool high) noexcept
{
	if (!IsLpcPin(pin))
	{
		return;
	}
	const size_t index = pin - FirstLpcPin;
	pinValues[index] = high;
	if (online)
	{
		const uint8_t payload[] = { GetLpcPinId(pin), static_cast<uint8_t>(high) };
		Send(LpcProtocol::MessageType::gpioWrite, payload, sizeof(payload));
	}
}

void WritePwm(Pin pin, float duty, uint16_t frequency) noexcept
{
	if (!IsLpcPin(pin))
	{
		return;
	}

	const size_t index = pin - FirstLpcPin;
	const float constrainedDuty = (duty <= 0.0) ? 0.0 : (duty >= 1.0) ? 1.0 : duty;
	pwmValues[index] = static_cast<uint16_t>(constrainedDuty * 65535.0 + 0.5);
	pwmFrequencies[index] = frequency;
	if (online)
	{
		SendPwm(index);
	}
}

}

#endif
