#include "LpcProtocol.h"

namespace LpcProtocol
{

static uint8_t UpdateCrc(uint8_t crc, uint8_t value) noexcept
{
	crc ^= value;
	for (unsigned int bit = 0; bit < 8; ++bit)
	{
		crc = (crc & 0x80u) != 0u ? static_cast<uint8_t>((crc << 1) ^ 0x07u) : static_cast<uint8_t>(crc << 1);
	}
	return crc;
}

void Reset(Decoder& decoder) noexcept
{
	decoder.state = 0;
	decoder.index = 0;
	decoder.crc = 0;
	decoder.frame.length = 0;
}

bool Feed(Decoder& decoder, uint8_t byte, Frame& frame) noexcept
{
	switch (decoder.state)
	{
	case 0:
		if (byte == SyncByte)
		{
			decoder.state = 1;
		}
		break;

	case 1:
		decoder.frame.type = static_cast<MessageType>(byte);
		decoder.crc = UpdateCrc(0, byte);
		decoder.state = 2;
		break;

	case 2:
		if (byte > MaxPayload)
		{
			Reset(decoder);
			break;
		}
		decoder.frame.length = byte;
		decoder.crc = UpdateCrc(decoder.crc, byte);
		decoder.index = 0;
		decoder.state = 3;
		break;

	case 3:
		if (decoder.index < decoder.frame.length)
		{
			decoder.frame.payload[decoder.index++] = byte;
			decoder.crc = UpdateCrc(decoder.crc, byte);
		}
		else
		{
			const bool valid = byte == decoder.crc;
			if (valid)
			{
				frame = decoder.frame;
			}
			Reset(decoder);
			return valid;
		}
		break;
	}
	return false;
}

size_t Encode(MessageType type, const uint8_t* payload, uint8_t length, uint8_t* output) noexcept
{
	if (length > MaxPayload)
	{
		return 0;
	}

	output[0] = SyncByte;
	output[1] = static_cast<uint8_t>(type);
	output[2] = length;
	uint8_t crc = UpdateCrc(0, output[1]);
	crc = UpdateCrc(crc, output[2]);
	for (uint8_t i = 0; i < length; ++i)
	{
		output[3 + i] = payload[i];
		crc = UpdateCrc(crc, payload[i]);
	}
	output[3 + length] = crc;
	return static_cast<size_t>(length) + 4;
}

}
