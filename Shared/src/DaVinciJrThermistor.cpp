#include "DaVinciJrThermistor.h"

#include <math.h>
#include <limits>

namespace DaVinciJrThermistor
{

namespace
{

// The recovered ROM table is consistent with this common 100K/B4250 NTC
// divider. These standard values track the stock samples closely while avoiding
// the original five-degree range clamp in the replacement firmware.
constexpr float NominalResistance = 100000.0f;
constexpr float NominalTemperatureKelvin = 25.0f + 273.15f;
constexpr float Beta = 4250.0f;
constexpr float PullupResistance = 820.0f;
constexpr float ScaledAdcFullScale = 4095.0f;
constexpr float KelvinOffset = 273.15f;

}

float ConvertScaledRaw(uint16_t scaledRaw) noexcept
{
	if (scaledRaw == 0)
	{
		return std::numeric_limits<float>::infinity();
	}
	if (scaledRaw >= static_cast<uint16_t>(ScaledAdcFullScale))
	{
		return -KelvinOffset;
	}

	const float raw = static_cast<float>(scaledRaw);
	const float resistance = PullupResistance * raw / (ScaledAdcFullScale - raw);
	const float inverseKelvin = (1.0f / NominalTemperatureKelvin)
		+ logf(resistance / NominalResistance) / Beta;
	return (1.0f / inverseKelvin) - KelvinOffset;
}

float ConvertAdc(uint16_t rawAdc) noexcept
{
	return ConvertScaledRaw(static_cast<uint16_t>(rawAdc * 4u));
}

}
