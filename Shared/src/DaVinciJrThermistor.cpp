#include "DaVinciJrThermistor.h"

#include <cstddef>

namespace DaVinciJrThermistor
{

namespace
{

struct CalibrationPoint
{
	uint16_t raw;
	float temperature;
};

// Stock SAM4E ROM table at 0x00456770. The stock firmware multiplies the
// LPC1115 10-bit ADC result by four before applying this table.
constexpr CalibrationPoint StockTable[] = {
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
constexpr size_t StockTableCount = sizeof(StockTable) / sizeof(StockTable[0]);

float Interpolate(uint16_t raw, const CalibrationPoint& hotter, const CalibrationPoint& colder) noexcept
{
	if (hotter.raw == colder.raw)
	{
		return colder.temperature;
	}
	const float fraction = (static_cast<float>(raw) - static_cast<float>(hotter.raw))
		/ (static_cast<float>(colder.raw) - static_cast<float>(hotter.raw));
	return hotter.temperature + fraction * (colder.temperature - hotter.temperature);
}

float ConvertScaledRaw(uint16_t raw) noexcept
{
	// The control path continues the nearest recovered segment beyond the table
	// so electrical faults and out-of-range conditions remain distinguishable.
	if (raw < StockTable[0].raw)
	{
		return Interpolate(raw, StockTable[0], StockTable[1]);
	}
	for (size_t i = 1; i < StockTableCount; ++i)
	{
		if (raw <= StockTable[i].raw)
		{
			return Interpolate(raw, StockTable[i - 1], StockTable[i]);
		}
	}
	return Interpolate(raw, StockTable[StockTableCount - 2], StockTable[StockTableCount - 1]);
}

}

float ConvertAdc(uint16_t rawAdc) noexcept
{
	return ConvertScaledRaw(static_cast<uint16_t>(rawAdc * 4u));
}

float ClampReportedTemperature(float temperature) noexcept
{
	const float minimum = StockTable[StockTableCount - 1].temperature;
	return (temperature < minimum) ? minimum : temperature;
}

}
