#include "Uart.h"
#include <LpcProtocol.h>

extern "C" int main() noexcept
{
	Uart::Init();

	LpcProtocol::Decoder decoder{};
	LpcProtocol::Reset(decoder);
	LpcProtocol::Frame frame{};
	uint8_t encoded[LpcProtocol::MaxEncodedFrame];

	for (;;)
	{
		uint8_t byte;
		while (Uart::Read(byte))
		{
			if (!LpcProtocol::Feed(decoder, byte, frame))
			{
				continue;
			}

			if (frame.type == LpcProtocol::MessageType::ping)
			{
				const uint8_t payload[] = { LpcProtocol::Version };
				const size_t length = LpcProtocol::Encode(LpcProtocol::MessageType::pong, payload, sizeof(payload), encoded);
				Uart::Write(encoded, length);
			}
		}
	}
}
