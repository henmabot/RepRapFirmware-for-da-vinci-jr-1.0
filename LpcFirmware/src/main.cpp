#include "Gpio.h"
#include "Pwm.h"
#include "Uart.h"
#include <LpcProtocol.h>

static void Send(LpcProtocol::MessageType type, const uint8_t* payload, uint8_t payloadLength) noexcept
{
	uint8_t encoded[LpcProtocol::MaxEncodedFrame];
	const size_t length = LpcProtocol::Encode(type, payload, payloadLength, encoded);
	Uart::Write(encoded, length);
}

extern "C" int main() noexcept
{
	Uart::Init();
	Gpio::Init();

	LpcProtocol::Decoder decoder{};
	LpcProtocol::Reset(decoder);
	LpcProtocol::Frame frame{};

	for (;;)
	{
		uint8_t byte;
		while (Uart::Read(byte))
		{
			if (!LpcProtocol::Feed(decoder, byte, frame))
			{
				continue;
			}

			switch (frame.type)
			{
			case LpcProtocol::MessageType::ping:
				{
					const uint8_t payload[] = { LpcProtocol::Version };
					Send(LpcProtocol::MessageType::pong, payload, sizeof(payload));
				}
				break;

			case LpcProtocol::MessageType::gpioConfig:
				if (frame.length == 3)
				{
					Gpio::Configure(frame.payload[0], static_cast<LpcProtocol::GpioMode>(frame.payload[1]), frame.payload[2] != 0);
				}
				break;

			case LpcProtocol::MessageType::gpioWrite:
				if (frame.length == 2)
				{
					Gpio::Write(frame.payload[0], frame.payload[1] != 0);
				}
				break;

			case LpcProtocol::MessageType::pwmWrite:
				if (frame.length == 5)
				{
					const uint16_t duty = static_cast<uint16_t>(frame.payload[1] | (static_cast<uint16_t>(frame.payload[2]) << 8));
					const uint16_t frequency = static_cast<uint16_t>(frame.payload[3] | (static_cast<uint16_t>(frame.payload[4]) << 8));
					Pwm::Set(frame.payload[0], duty, frequency);
				}
				break;

			default:
				break;
			}
		}

		uint8_t pin;
		bool value;
		while (Gpio::Poll(pin, value))
		{
			const uint8_t payload[] = { pin, static_cast<uint8_t>(value) };
			Send(LpcProtocol::MessageType::gpioState, payload, sizeof(payload));
		}
	}
}
