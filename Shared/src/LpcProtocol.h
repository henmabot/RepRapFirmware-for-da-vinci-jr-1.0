#ifndef LPC_PROTOCOL_H
#define LPC_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

namespace LpcProtocol
{

constexpr uint8_t Version = 1;
constexpr uint8_t SyncByte = 0xA5;
constexpr size_t MaxPayload = 16;
constexpr size_t MaxEncodedFrame = MaxPayload + 4;

enum class MessageType : uint8_t
{
	ping = 1,
	pong = 2,
	gpioConfig = 3,
	gpioWrite = 4,
	gpioState = 5
};

enum class GpioMode : uint8_t
{
	disabled = 0,
	input = 1,
	inputPullup = 2,
	output = 3
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
