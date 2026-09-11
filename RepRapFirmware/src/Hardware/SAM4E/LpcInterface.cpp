#include "LpcInterface.h"

#if defined(DA_VINCI_JR)
#include "Devices.h"
#include <LpcProtocol.h>
#include <RepRapFirmware.h>

namespace LpcInterface
{

static LpcProtocol::Decoder decoder;
static bool online;
static uint32_t lastPingSent;
static uint32_t lastPongReceived;

static void SendPing() noexcept
{
	uint8_t encoded[LpcProtocol::MaxEncodedFrame];
	const size_t length = LpcProtocol::Encode(LpcProtocol::MessageType::ping, nullptr, 0, encoded);
	lpcUart.write(encoded, length);
	lastPingSent = millis();
}

void Init() noexcept
{
	LpcProtocol::Reset(decoder);
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
		if (value >= 0 && LpcProtocol::Feed(decoder, static_cast<uint8_t>(value), frame)
			&& frame.type == LpcProtocol::MessageType::pong
			&& frame.length == 1
			&& frame.payload[0] == LpcProtocol::Version)
		{
			online = true;
			lastPongReceived = millis();
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

}

#endif
