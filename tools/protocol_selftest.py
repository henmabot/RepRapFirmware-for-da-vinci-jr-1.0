#!/usr/bin/env python3
from __future__ import annotations

import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build" / "protocol-selftest"
SOURCE = BUILD_DIR / "main.cpp"
BINARY = BUILD_DIR / "protocol-selftest"

TEST_PROGRAM = r'''
#include <DaVinciJrThermistor.h>
#include <LpcProtocol.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cmath>

using namespace LpcProtocol;

static void RoundTrip(MessageType type, const uint8_t* payload, uint8_t length)
{
    uint8_t encoded[MaxEncodedFrame] = {};
    const size_t encodedLength = Encode(type, payload, length, encoded);
    assert(encodedLength == static_cast<size_t>(length) + 4u);
    assert(encoded[0] == SyncByte);

    Decoder decoder{};
    Reset(decoder);
    Frame decoded{};
    for (size_t i = 0; i + 1 < encodedLength; ++i)
    {
        assert(!Feed(decoder, encoded[i], decoded));
    }
    assert(Feed(decoder, encoded[encodedLength - 1], decoded));
    assert(decoded.type == type);
    assert(decoded.length == length);
    for (uint8_t i = 0; i < length; ++i)
    {
        assert(decoded.payload[i] == payload[i]);
    }
}

int main()
{
    uint8_t payload[MaxPayload] = {};
    for (uint8_t i = 0; i < MaxPayload; ++i)
    {
        payload[i] = static_cast<uint8_t>(i * 17u + 3u);
    }

    const uint8_t version[] = { Version };
    const uint8_t pong[] = { Version, 1 };
    RoundTrip(MessageType::ping, version, sizeof(version));
    RoundTrip(MessageType::pong, pong, sizeof(pong));
    RoundTrip(MessageType::configurationReset, nullptr, 0);
    RoundTrip(MessageType::configurationComplete, nullptr, 0);
    RoundTrip(MessageType::gpioState, payload, 2);
    RoundTrip(MessageType::pwmWrite, payload, 5);
    RoundTrip(MessageType::thermalStatus, payload, 8);
    RoundTrip(MessageType::thermistorConfig, nullptr, 0);

    struct CalibrationPoint { uint16_t raw; float temperature; };
    static constexpr CalibrationPoint stockTable[] = {
        {856, 250}, {893, 245}, {943, 240}, {1017, 235}, {1104, 230},
        {1179, 225}, {1216, 220}, {1290, 215}, {1414, 210}, {1501, 205},
        {1576, 200}, {1675, 195}, {1762, 190}, {1886, 185}, {1998, 180},
        {2122, 175}, {2209, 170}, {2321, 165}, {2445, 160}, {2519, 155},
        {2643, 150}, {2767, 145}, {2829, 140}, {2954, 135}, {3065, 130},
        {3152, 125}, {3227, 120}, {3376, 115}, {3376, 115}, {3424, 110},
        {3504, 105}, {3584, 100}, {3648, 95}, {3712, 90}, {3760, 85},
        {3808, 80}, {3840, 75}, {3888, 70}, {3904, 65}, {3936, 60},
        {3968, 55}, {3984, 50}, {4000, 45}, {4018, 40}, {4034, 35},
        {4049, 30}, {4058, 25}, {4068, 20}, {4073, 15}, {4077, 10},
    };
    for (const CalibrationPoint& point : stockTable)
    {
        const uint16_t adc = static_cast<uint16_t>((point.raw + 2u) / 4u);
        // The stock table is 12-bit-scaled while the physical LPC ADC is only
        // 10-bit, so nearest-ADC quantization is the only permitted error.
        assert(std::fabs(DaVinciJrThermistor::ConvertAdc(adc) - point.temperature) <= 1.25f);
    }

    // These stock points are exactly representable by the 10-bit ADC and must
    // therefore remain exact after interpolation.
    assert(std::fabs(DaVinciJrThermistor::ConvertAdc(1000) - 45.0f) < 0.001f);
    assert(std::fabs(DaVinciJrThermistor::ConvertAdc(996) - 50.0f) < 0.001f);
    assert(std::fabs(DaVinciJrThermistor::ConvertAdc(992) - 55.0f) < 0.001f);

    // Interpolation is continuous rather than limited to the stock table's
    // five-degree entries, and edge extrapolation extends beyond 10..250C.
    assert(DaVinciJrThermistor::ConvertAdc(100) > 300.0f);
    assert(DaVinciJrThermistor::ConvertAdc(1022) < 0.0f);
    float previous = DaVinciJrThermistor::ConvertAdc(1);
    for (uint16_t raw = 2; raw < 1023; ++raw)
    {
        const float current = DaVinciJrThermistor::ConvertAdc(raw);
        assert(current < previous);
        previous = current;
    }

    uint8_t encoded[MaxEncodedFrame] = {};
    size_t length = Encode(MessageType::gpioWrite, payload, 2, encoded);
    assert(length != 0);
    encoded[length - 1] ^= 0x01u;
    Decoder decoder{};
    Reset(decoder);
    Frame decoded{};
    for (size_t i = 0; i < length; ++i)
    {
        assert(!Feed(decoder, encoded[i], decoded));
    }

    assert(Encode(MessageType::ping, payload, static_cast<uint8_t>(MaxPayload + 1u), encoded) == 0);

    Reset(decoder);
    assert(!Feed(decoder, SyncByte, decoded));
    assert(!Feed(decoder, static_cast<uint8_t>(MessageType::ping), decoded));
    assert(!Feed(decoder, static_cast<uint8_t>(MaxPayload + 1u), decoded));
    length = Encode(MessageType::pong, payload, 1, encoded);
    bool received = false;
    for (size_t i = 0; i < length; ++i)
    {
        received = Feed(decoder, encoded[i], decoded) || received;
    }
    assert(received);
    assert(decoded.type == MessageType::pong);
    assert(decoded.length == 1);
    assert(decoded.payload[0] == payload[0]);
    return 0;
}
'''


def main() -> int:
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    SOURCE.write_text(TEST_PROGRAM)
    subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT / "Shared" / "src"),
            str(SOURCE),
            str(ROOT / "Shared" / "src" / "DaVinciJrThermistor.cpp"),
            str(ROOT / "Shared" / "src" / "LpcProtocol.cpp"),
            "-o",
            str(BINARY),
        ],
        check=True,
    )
    subprocess.run([str(BINARY)], check=True)
    print("LPC protocol self-test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
