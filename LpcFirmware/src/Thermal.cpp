#include "Thermal.h"

#include "Gpio.h"
#include "Lpc1115.h"
#include "Pwm.h"

#include <math.h>
#include <string.h>

namespace Thermal
{

constexpr uint32_t CoreClock = 12000000u;
constexpr uint32_t SampleIntervalMillis = 250u;
constexpr uint32_t LinkTimeoutMillis = 2500u;
constexpr float AbsoluteZero = -273.15f;
constexpr float MinimumConnectedTemperature = -5.0f;
constexpr float NormalAmbientTemperature = 25.0f;
constexpr float TemperatureCloseEnough = 1.5f;
constexpr float MaxAmbientTemperature = 45.0f;
constexpr uint8_t HeaterPin = 0x09;
constexpr uint16_t AdcRange = 1024u;

struct Model
{
	float heatingRate;
	float basicCoolingRate;
	float fanCoolingRate;
	float coolingRateExponent;
	float deadTime;
	float maxPwm;
	float overrideKp;
	float overrideRecipTi;
	float overrideTd;
	bool usePid;
	bool inverted;
	bool pidOverridden;
	bool partA;
	bool partB;
	bool partC;
};

struct Pid
{
	float kP;
	float recipTi;
	float tD;
};

static volatile uint32_t millisTicks;
static volatile uint32_t lastHostHeartbeat;
static volatile bool statusDirty;
static uint32_t lastSampleTime;
static uint32_t excursionFaultMillis;
static uint32_t heatingFaultMillis;
static uint32_t heatingReferenceMillis;
static uint32_t timeSetHeating;
static uint16_t lastRawAdc;
static uint16_t heaterFrequency = 250;
static uint8_t maxBadReadings = 3;
static uint8_t badReadings;
static float r25;
static float beta;
static float shC;
static float seriesR;
static float shA;
static float shB;
static float temperature;
static float targetTemperature;
static float upperLimit;
static float lowerLimit;
static float maxTempExcursion;
static float maxFaultTime;
static float fanPwm;
static float extrusionPwmBoost;
static float extrusionTemperatureBoost;
static float integral;
static float averagePwm;
static float lastPwm;
static float heatingReferenceTemperature;
static float previousTemperatures[4];
static uint8_t previousIndex;
static uint8_t goodTemperatureMask;
static bool thermistorConfigured;
static bool heaterConfigured;
static bool reachedTarget;
static Model model;
static LpcProtocol::HeaterState state;
static LpcProtocol::ThermalError error;

static float ReadFloat(const uint8_t* data) noexcept
{
	float value;
	memcpy(&value, data, sizeof(value));
	return value;
}

static int16_t ReadI16(const uint8_t* data) noexcept
{
	return static_cast<int16_t>(static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8));
}

static uint16_t ReadU16(const uint8_t* data) noexcept
{
	return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

static float Clamp(float value, float low, float high) noexcept
{
	return (value < low) ? low : (value > high) ? high : value;
}

static bool ModelReady() noexcept
{
	return model.partA && model.partB && model.partC
		&& isfinite(model.heatingRate) && model.heatingRate > 0.0f
		&& isfinite(model.basicCoolingRate) && model.basicCoolingRate > 0.0f
		&& isfinite(model.fanCoolingRate) && model.fanCoolingRate >= 0.0f
		&& isfinite(model.coolingRateExponent) && model.coolingRateExponent >= 1.0f && model.coolingRateExponent <= 1.6f
		&& isfinite(model.deadTime) && model.deadTime > 0.0f
		&& isfinite(model.maxPwm) && model.maxPwm > 0.0f && model.maxPwm <= 1.0f;
}

static bool LinkAlive() noexcept
{
	return Millis() - lastHostHeartbeat <= LinkTimeoutMillis;
}

static bool Active() noexcept
{
	return state >= LpcProtocol::HeaterState::cooling;
}

static void ApplyHeater(float pwm) noexcept
{
	const float limited = Clamp(pwm, 0.0f, 1.0f);
	const uint16_t duty = static_cast<uint16_t>(limited * 65535.0f + 0.5f);
	(void)Pwm::Set(HeaterPin, duty, heaterFrequency);
}

static void SetFault(LpcProtocol::ThermalError newError) noexcept
{
	ApplyHeater(0.0f);
	state = LpcProtocol::HeaterState::fault;
	error = newError;
	statusDirty = true;
}

static bool ReadAdc(uint16_t& value) noexcept
{
	constexpr uint32_t control = (1u << 1) | (2u << 8); // AD1, 12MHz/(2+1) = 4MHz ADC clock
	LPC_ADC_CR = control | (1u << 24);
	for (uint32_t timeout = 0; timeout < 10000; ++timeout)
	{
		const uint32_t reading = LPC_ADC_DR1;
		if ((reading & (1u << 31)) != 0)
		{
			value = static_cast<uint16_t>((reading >> 6) & 0x03FFu);
			LPC_ADC_CR = control;
			return true;
		}
	}
	LPC_ADC_CR = control;
	return false;
}

static bool SampleTemperature() noexcept
{
	if (!thermistorConfigured)
	{
		error = LpcProtocol::ThermalError::notConfigured;
		return false;
	}

	uint32_t sum = 0;
	for (unsigned int i = 0; i < 8; ++i)
	{
		uint16_t reading;
		if (!ReadAdc(reading))
		{
			error = LpcProtocol::ThermalError::adcTimeout;
			return false;
		}
		sum += reading;
	}
	lastRawAdc = static_cast<uint16_t>((sum + 4u) / 8u);

	if (lastRawAdc == 0)
	{
		error = LpcProtocol::ThermalError::shortCircuit;
		return false;
	}

	const float resistance = seriesR * static_cast<float>(lastRawAdc) / static_cast<float>(AdcRange - lastRawAdc);
	const float logResistance = logf(resistance);
	const float recipT = shA + shB * logResistance + shC * logResistance * logResistance * logResistance;
	if (!(recipT > 0.0f))
	{
		error = LpcProtocol::ThermalError::openCircuit;
		return false;
	}

	temperature = (1.0f / recipT) + AbsoluteZero;
	if (!isfinite(temperature) || (temperature < MinimumConnectedTemperature && resistance > seriesR * 100.0f))
	{
		error = LpcProtocol::ThermalError::openCircuit;
		return false;
	}

	error = LpcProtocol::ThermalError::none;
	return true;
}

static float CoolingRate(float temperatureRise, float pwmFan) noexcept
{
	const float scaledRise = temperatureRise * 0.01f;
	const float base = (scaledRise < 0.0f)
		? -powf(-scaledRise, model.coolingRateExponent)
		: powf(scaledRise, model.coolingRateExponent);
	return model.basicCoolingRate * base + scaledRise * model.fanCoolingRate * pwmFan;
}

static Pid PidForTarget(bool loadMode, float target) noexcept
{
	if (model.pidOverridden)
	{
		return { model.overrideKp, model.overrideRecipTi, model.overrideTd };
	}

	const float rise = (target - NormalAmbientTemperature > 1.0f) ? target - NormalAmbientTemperature : 1.0f;
	const float coolingPerDegree = CoolingRate(rise, 0.2f) / rise;
	const float kP = 0.7f / (model.heatingRate * model.deadTime);
	const float recipTi = loadMode
		? powf(coolingPerDegree, 0.25f) / (1.14f * powf(model.deadTime, 0.75f))
		: sqrtf(coolingPerDegree / model.deadTime);
	return { kP, recipTi, model.deadTime * 0.7f };
}

static void Control() noexcept
{
	const bool faultWasLatched = state == LpcProtocol::HeaterState::fault;
	const LpcProtocol::ThermalError latchedError = error;
	const bool goodTemperature = SampleTemperature();
	if (faultWasLatched)
	{
		error = latchedError;
	}
	goodTemperatureMask = static_cast<uint8_t>(goodTemperatureMask << 1);

	float derivative = 0.0f;
	bool gotDerivative = false;
	if (goodTemperature)
	{
		badReadings = 0;
		if ((goodTemperatureMask & 0x08u) != 0)
		{
			derivative = (temperature - previousTemperatures[previousIndex]);
			gotDerivative = fabsf(derivative) <= 10.0f;
		}
		previousTemperatures[previousIndex] = temperature;
		previousIndex = static_cast<uint8_t>((previousIndex + 1u) & 3u);
		goodTemperatureMask |= 1u;
	}
	else if (badReadings < 0xFFu)
	{
		++badReadings;
	}

	if (!Active())
	{
		ApplyHeater(0.0f);
		averagePwm *= 0.95f;
		statusDirty = true;
		return;
	}

	if (!LinkAlive())
	{
		SetFault(LpcProtocol::ThermalError::linkTimeout);
		return;
	}
	if (!goodTemperature && badReadings > maxBadReadings)
	{
		SetFault(error);
		return;
	}
	if (!goodTemperature)
	{
		statusDirty = true;
		return;
	}
	if (temperature > upperLimit)
	{
		SetFault(LpcProtocol::ThermalError::overTemperature);
		return;
	}
	if (lowerLimit > AbsoluteZero + 1.0f && temperature < lowerLimit)
	{
		SetFault(LpcProtocol::ThermalError::underTemperature);
		return;
	}

	const float adjustedTarget = targetTemperature + extrusionTemperatureBoost;
	const float tempError = adjustedTarget - temperature;
	state = (tempError > TemperatureCloseEnough) ? LpcProtocol::HeaterState::heating
		: (tempError < -TemperatureCloseEnough) ? LpcProtocol::HeaterState::cooling
		: LpcProtocol::HeaterState::stable;

	if (state == LpcProtocol::HeaterState::stable)
	{
		reachedTarget = true;
	}

	const uint32_t now = Millis();
	if (state == LpcProtocol::HeaterState::heating && !reachedTarget)
	{
		if (static_cast<float>(now - timeSetHeating) < model.deadTime * 2000.0f)
		{
			heatingReferenceTemperature = temperature;
			heatingReferenceMillis = now;
			heatingFaultMillis = 0;
		}
		else
		{
			const float temperatureRise = (temperature > 15.0f) ? temperature - 15.0f : 0.0f;
			const float expectedRate = model.heatingRate * lastPwm - CoolingRate(temperatureRise, 1.0f);
			if (expectedRate > 0.0f)
			{
				const uint32_t minimumInterval = static_cast<uint32_t>(3000.0f / expectedRate);
				const uint32_t actualInterval = now - heatingReferenceMillis;
				if (actualInterval >= minimumInterval)
				{
					const float expectedRise = expectedRate * static_cast<float>(actualInterval) * 0.001f;
					const float actualRise = temperature - heatingReferenceTemperature;
					if (actualRise < expectedRise * 0.6f)
					{
						heatingFaultMillis += SampleIntervalMillis;
						if (heatingFaultMillis > static_cast<uint32_t>(maxFaultTime * 1000.0f))
						{
							SetFault(LpcProtocol::ThermalError::heatingTooSlow);
							return;
						}
					}
					else
					{
						heatingReferenceTemperature = temperature;
						heatingReferenceMillis = now;
						heatingFaultMillis = 0;
					}
				}
			}
		}
	}
	else
	{
		heatingFaultMillis = 0;
	}

	if (reachedTarget && fabsf(tempError) > maxTempExcursion && temperature > MaxAmbientTemperature)
	{
		excursionFaultMillis += SampleIntervalMillis;
		if (excursionFaultMillis > static_cast<uint32_t>(maxFaultTime * 1000.0f))
		{
			SetFault(LpcProtocol::ThermalError::temperatureExcursion);
			return;
		}
	}
	else
	{
		excursionFaultMillis = 0;
	}

	float pwm;
	if (model.usePid)
	{
		const bool loadMode = state == LpcProtocol::HeaterState::stable || fabsf(tempError) < 3.0f;
		const Pid pid = PidForTarget(loadMode, adjustedTarget);
		const float errorMinusD = tempError - (gotDerivative ? pid.tD * derivative : 0.0f);
		const float pPlusD = pid.kP * errorMinusD;
		const float expected = (model.heatingRate > 0.0f)
			? CoolingRate(temperature - NormalAmbientTemperature, fanPwm) / model.heatingRate + extrusionPwmBoost
			: 0.0f;
		if (pPlusD + expected >= model.maxPwm)
		{
			pwm = model.maxPwm;
			if (state == LpcProtocol::HeaterState::heating && tempError > 0.0f && derivative > 0.0f)
			{
				integral = Clamp(expected, 0.0f, model.maxPwm);
			}
		}
		else if (pPlusD + expected <= 0.0f)
		{
			pwm = 0.0f;
		}
		else
		{
			integral = Clamp(integral + tempError * pid.kP * pid.recipTi * 0.25f, 0.0f, model.maxPwm);
			pwm = Clamp(pPlusD + integral, 0.0f, model.maxPwm);
		}
	}
	else
	{
		pwm = (tempError > 0.0f) ? model.maxPwm : 0.0f;
	}

	if (model.inverted)
	{
		pwm = model.maxPwm - pwm;
	}
	ApplyHeater(pwm);
	lastPwm = pwm;
	averagePwm = averagePwm * 0.95f + pwm * 0.05f;
	statusDirty = true;
}

void Init() noexcept
{
	state = LpcProtocol::HeaterState::off;
	error = LpcProtocol::ThermalError::notConfigured;
	upperLimit = 2000.0f;
	lowerLimit = AbsoluteZero;
	maxTempExcursion = 15.0f;
	maxFaultTime = 5.0f;

	LPC_SYSCON_SYSAHBCLKCTRL |= (1u << 13) | (1u << 16);
	LPC_SYSCON_PDRUNCFG &= ~(1u << 4);
	LPC_IOCON_PIO1_0 = (LPC_IOCON_PIO1_0 & ~0x9Fu) | 0x02u; // AD1, analog mode, no pulls
	LPC_ADC_CR = (1u << 1) | (2u << 8);
	ApplyHeater(0.0f);

	SYST_RVR = CoreClock / 1000u - 1u;
	SYST_CVR = 0;
	SYST_CSR = 0x07u;
}

void Tick() noexcept
{
	++millisTicks;
	if (Active() && millisTicks - lastHostHeartbeat > LinkTimeoutMillis)
	{
		SetFault(LpcProtocol::ThermalError::linkTimeout);
	}
}

uint32_t Millis() noexcept
{
	return millisTicks;
}

void HostHeartbeat() noexcept
{
	lastHostHeartbeat = Millis();
}

void ConfigureThermistor(const uint8_t* payload, size_t length) noexcept
{
	if (length != 16)
	{
		return;
	}
	r25 = ReadFloat(payload);
	beta = ReadFloat(payload + 4);
	shC = ReadFloat(payload + 8);
	seriesR = ReadFloat(payload + 12);
	if (!(r25 > 0.0f) || !(beta > 0.0f) || !(seriesR > 0.0f))
	{
		thermistorConfigured = false;
		error = LpcProtocol::ThermalError::notConfigured;
		return;
	}
	shB = 1.0f / beta;
	const float lnR25 = logf(r25);
	shA = 1.0f / (25.0f - AbsoluteZero) - shB * lnR25 - shC * lnR25 * lnR25 * lnR25;
	thermistorConfigured = true;
	badReadings = 0;
	statusDirty = true;
}

void ConfigureModelA(const uint8_t* payload, size_t length) noexcept
{
	if (length != 16)
	{
		return;
	}
	model.heatingRate = ReadFloat(payload);
	model.basicCoolingRate = ReadFloat(payload + 4);
	model.fanCoolingRate = ReadFloat(payload + 8);
	model.coolingRateExponent = ReadFloat(payload + 12);
	model.partA = true;
}

void ConfigureModelB(const uint8_t* payload, size_t length) noexcept
{
	if (length != 16)
	{
		return;
	}
	model.deadTime = ReadFloat(payload);
	model.maxPwm = ReadFloat(payload + 4);
	model.overrideKp = ReadFloat(payload + 8);
	model.overrideRecipTi = ReadFloat(payload + 12);
	model.partB = true;
}

void ConfigureModelC(const uint8_t* payload, size_t length) noexcept
{
	if (length != 5)
	{
		return;
	}
	model.overrideTd = ReadFloat(payload);
	const uint8_t flags = payload[4];
	model.usePid = (flags & 0x01u) != 0;
	model.inverted = (flags & 0x02u) != 0;
	model.pidOverridden = (flags & 0x04u) != 0;
	model.partC = true;
}

void ConfigureHeater(const uint8_t* payload, size_t length) noexcept
{
	if (length != 11)
	{
		return;
	}
	heaterFrequency = ReadU16(payload);
	upperLimit = static_cast<float>(ReadI16(payload + 2)) * 0.1f;
	lowerLimit = static_cast<float>(ReadI16(payload + 4)) * 0.1f;
	maxTempExcursion = static_cast<float>(ReadU16(payload + 6)) * 0.01f;
	maxFaultTime = static_cast<float>(ReadU16(payload + 8)) * 0.1f;
	maxBadReadings = payload[10];
	heaterConfigured = heaterFrequency != 0;
	statusDirty = true;
}

void Command(const uint8_t* payload, size_t length) noexcept
{
	if (length != 3)
	{
		return;
	}
	const auto command = static_cast<LpcProtocol::HeaterCommand>(payload[0]);
	const float requestedTarget = static_cast<float>(ReadI16(payload + 1)) * 0.01f;
	switch (command)
	{
	case LpcProtocol::HeaterCommand::off:
		ApplyHeater(0.0f);
		if (state != LpcProtocol::HeaterState::fault)
		{
			state = LpcProtocol::HeaterState::off;
		}
		integral = 0.0f;
		excursionFaultMillis = 0;
		heatingFaultMillis = 0;
		lastPwm = 0.0f;
		reachedTarget = false;
		break;

	case LpcProtocol::HeaterCommand::on:
	case LpcProtocol::HeaterCommand::unsuspend:
		if (!LinkAlive() || !thermistorConfigured || !heaterConfigured || !ModelReady() || state == LpcProtocol::HeaterState::fault || !SampleTemperature())
		{
			if (state != LpcProtocol::HeaterState::fault)
			{
				SetFault(!LinkAlive() ? LpcProtocol::ThermalError::linkTimeout
					: !thermistorConfigured || !heaterConfigured || !ModelReady() ? LpcProtocol::ThermalError::notConfigured
					: error);
			}
			return;
		}
		if (upperLimit >= 1000.0f || requestedTarget > upperLimit || requestedTarget < lowerLimit)
		{
			SetFault(LpcProtocol::ThermalError::controlFault);
			return;
		}
		targetTemperature = requestedTarget;
		state = (temperature + TemperatureCloseEnough < targetTemperature)
			? LpcProtocol::HeaterState::heating
			: (temperature > targetTemperature + TemperatureCloseEnough)
				? LpcProtocol::HeaterState::cooling
				: LpcProtocol::HeaterState::stable;
		timeSetHeating = Millis();
		heatingReferenceMillis = timeSetHeating;
		heatingReferenceTemperature = temperature;
		heatingFaultMillis = 0;
		excursionFaultMillis = 0;
		reachedTarget = state == LpcProtocol::HeaterState::stable;
		break;

	case LpcProtocol::HeaterCommand::suspend:
		ApplyHeater(0.0f);
		if (state != LpcProtocol::HeaterState::fault)
		{
			state = LpcProtocol::HeaterState::suspended;
		}
		lastPwm = 0.0f;
		break;

	case LpcProtocol::HeaterCommand::resetFault:
		if (LinkAlive() && SampleTemperature())
		{
			ApplyHeater(0.0f);
			state = LpcProtocol::HeaterState::off;
			error = LpcProtocol::ThermalError::none;
			badReadings = 0;
			integral = 0.0f;
			lastPwm = 0.0f;
			reachedTarget = false;
			heatingFaultMillis = 0;
			excursionFaultMillis = 0;
		}
		break;
	}
	statusDirty = true;
}

void ConfigureFeedForward(const uint8_t* payload, size_t length) noexcept
{
	if (length != 12)
	{
		return;
	}
	fanPwm = Clamp(ReadFloat(payload), 0.0f, 1.0f);
	extrusionPwmBoost = ReadFloat(payload + 4);
	extrusionTemperatureBoost = ReadFloat(payload + 8);
}

void Spin() noexcept
{
	const uint32_t now = Millis();
	if (now - lastSampleTime >= SampleIntervalMillis)
	{
		lastSampleTime = now;
		Control();
	}
}

bool TakeStatus(uint8_t* payload, size_t& length) noexcept
{
	if (!statusDirty)
	{
		return false;
	}
	statusDirty = false;
	const float boundedTemperature = Clamp(temperature, -327.68f, 327.67f);
	const int16_t temperatureCenti = static_cast<int16_t>(boundedTemperature * 100.0f);
	const uint16_t pwm = static_cast<uint16_t>(Clamp(averagePwm, 0.0f, 1.0f) * 65535.0f + 0.5f);
	payload[0] = static_cast<uint8_t>(temperatureCenti);
	payload[1] = static_cast<uint8_t>(static_cast<uint16_t>(temperatureCenti) >> 8);
	payload[2] = static_cast<uint8_t>(lastRawAdc);
	payload[3] = static_cast<uint8_t>(lastRawAdc >> 8);
	payload[4] = static_cast<uint8_t>(pwm);
	payload[5] = static_cast<uint8_t>(pwm >> 8);
	payload[6] = static_cast<uint8_t>(state);
	payload[7] = static_cast<uint8_t>(error);
	length = 8;
	return true;
}

}

extern "C" void SysTick_Handler() noexcept
{
	Thermal::Tick();
}
