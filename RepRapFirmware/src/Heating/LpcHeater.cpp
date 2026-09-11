#include "LpcHeater.h"

#include "Heat.h"
#include <Hardware/SAM4E/LpcInterface.h>
#include <Platform/RepRap.h>
#include <Platform/Event.h>
#include <Tools/Tool.h>

static HeaterFaultType GetHeaterFaultType(LpcProtocol::ThermalError error) noexcept
{
	switch (error)
	{
	case LpcProtocol::ThermalError::adcTimeout:
	case LpcProtocol::ThermalError::shortCircuit:
	case LpcProtocol::ThermalError::openCircuit:
		return HeaterFaultType::failedToReadSensor;
	case LpcProtocol::ThermalError::heatingTooSlow:
		return HeaterFaultType::temperatureRisingTooSlowly;
	case LpcProtocol::ThermalError::temperatureExcursion:
		return HeaterFaultType::exceededAllowedExcursion;
	case LpcProtocol::ThermalError::overTemperature:
	case LpcProtocol::ThermalError::underTemperature:
		return HeaterFaultType::monitorTriggered;
	default:
		return HeaterFaultType::heaterFaultTypeLimit;
	}
}

static const char* GetThermalErrorText(LpcProtocol::ThermalError error) noexcept
{
	switch (error)
	{
	case LpcProtocol::ThermalError::none: return "none";
	case LpcProtocol::ThermalError::notConfigured: return "LPC thermal controller not configured";
	case LpcProtocol::ThermalError::adcTimeout: return "LPC thermistor ADC timeout";
	case LpcProtocol::ThermalError::shortCircuit: return "LPC thermistor short circuit";
	case LpcProtocol::ThermalError::openCircuit: return "LPC thermistor open circuit";
	case LpcProtocol::ThermalError::overTemperature: return "LPC upper temperature limit exceeded";
	case LpcProtocol::ThermalError::underTemperature: return "LPC lower temperature limit exceeded";
	case LpcProtocol::ThermalError::linkTimeout: return "LPC link timeout";
	case LpcProtocol::ThermalError::controlFault: return "LPC thermal control fault";
	case LpcProtocol::ThermalError::heatingTooSlow: return "LPC temperature rising too slowly";
	case LpcProtocol::ThermalError::temperatureExcursion: return "LPC temperature excursion exceeded";
	}
	return "LPC unknown thermal fault";
}

LpcHeater::LpcHeater(unsigned int heaterNum) noexcept
	: Heater(heaterNum), frequency(DefaultHeaterPwmFreq), mode(HeaterMode::off),
	  temperature(BadErrorTemperature), averagePwm(0.0f)
{
}

LpcHeater::~LpcHeater() noexcept
{
	SwitchOff();
}

GCodeResult LpcHeater::ConfigurePortAndSensor(const char *_ecv_array portName, PwmFrequency freq, unsigned int sn, const StringRef& reply)
{
	if (!StringEqualsIgnoreCase(portName, "lpc.heater"))
	{
		reply.printf("Unsupported LPC heater port '%s'", portName);
		return GCodeResult::error;
	}

	frequency = freq;
	SetSensorNumber(sn);
	SendConfiguration();
	if (reprap.GetHeat().FindSensor(sn).IsNull())
	{
		reply.printf("Sensor number %u has not been defined", sn);
		return GCodeResult::warning;
	}
	return GCodeResult::ok;
}

GCodeResult LpcHeater::SetPwmFrequency(PwmFrequency freq, const StringRef& reply) noexcept
{
	frequency = min<PwmFrequency>(freq, MaxHeaterPwmFrequency);
	SendConfiguration();
	return GCodeResult::ok;
}

GCodeResult LpcHeater::ReportDetails(const StringRef& reply) const noexcept
{
	reply.printf("Heater %u pin lpc.heater frequency %uHz, sensor %d", GetHeaterNumber(), frequency, GetSensorNumber());
	return GCodeResult::ok;
}

void LpcHeater::Spin() noexcept
{
	LpcInterface::ThermalStatus status;
	if (LpcInterface::GetThermalStatus(status))
	{
		const HeaterMode previousMode = mode;
		temperature = status.temperature;
		averagePwm = status.averagePwm;
		mode = static_cast<HeaterMode>(status.state);
		if (mode == HeaterMode::fault && previousMode != HeaterMode::fault)
		{
			Tool::FlagTemperatureFault(GetHeaterNumber());
			(void)Event::AddEvent(EventType::heater_fault, static_cast<uint16_t>(GetHeaterFaultType(status.error)),
				CanInterface::GetCanAddress(), GetHeaterNumber(), "%s", GetThermalErrorText(status.error));
		}
	}
	else if (mode != HeaterMode::fault)
	{
		mode = HeaterMode::offline;
		averagePwm = 0.0f;
	}
}

void LpcHeater::SwitchOff() noexcept
{
	LpcInterface::CommandHeater(LpcProtocol::HeaterCommand::off, 0.0f);
	if (mode != HeaterMode::fault)
	{
		mode = HeaterMode::off;
	}
	averagePwm = 0.0f;
	Heater::SwitchOff();
}

GCodeResult LpcHeater::ResetFault(const StringRef& reply) noexcept
{
	if (!LpcInterface::IsOnline())
	{
		reply.copy("LPC heater is offline");
		return GCodeResult::error;
	}
	LpcInterface::CommandHeater(LpcProtocol::HeaterCommand::resetFault, 0.0f);
	mode = HeaterMode::off;
	averagePwm = 0.0f;
	return GCodeResult::ok;
}

float LpcHeater::GetTemperature() const noexcept
{
	LpcInterface::ThermalStatus status;
	if (LpcInterface::GetThermalStatus(status))
	{
		return status.temperature;
	}
	TemperatureError sensorError(TemperatureError::unknownError);
	return reprap.GetHeat().GetSensorTemperature(GetSensorNumber(), sensorError);
}

float LpcHeater::GetAveragePWM() const noexcept
{
	LpcInterface::ThermalStatus status;
	return LpcInterface::GetThermalStatus(status) ? status.averagePwm : 0.0f;
}

void LpcHeater::Suspend(bool sus) noexcept
{
	if (sus)
	{
		LpcInterface::CommandHeater(LpcProtocol::HeaterCommand::suspend, GetTargetTemperature());
		mode = HeaterMode::suspended;
	}
	else
	{
		String<1> dummy;
		(void)SwitchOn(dummy.GetRef());
	}
}

void LpcHeater::SetFanFeedForwardPwm(float pwm) noexcept
{
	if (pwm != lastFanPwm)
	{
		lastFanPwm = pwm;
		SendFeedForward();
	}
}

GCodeResult LpcHeater::SwitchOn(const StringRef& reply) noexcept
{
	if (!GetModel().IsEnabled())
	{
		reply.printf("Heater %u not switched on due to bad model", GetHeaterNumber());
		return GCodeResult::error;
	}
	if (!LpcInterface::IsOnline())
	{
		reply.copy("LPC heater is offline");
		return GCodeResult::error;
	}
	if (GetHighestTemperatureLimit() >= BadErrorTemperature)
	{
		reply.printf("Heater %u has no upper temperature monitor", GetHeaterNumber());
		return GCodeResult::error;
	}

	LpcInterface::ConfigureHeaterModel(GetModel());
	SendConfiguration();
	SendFeedForward();
	LpcInterface::CommandHeater(LpcProtocol::HeaterCommand::on, GetTargetTemperature());
	mode = HeaterMode::heating;
	return GCodeResult::ok;
}

GCodeResult LpcHeater::UpdateModel(const StringRef& reply) noexcept
{
	LpcInterface::ConfigureHeaterModel(GetModel());
	return GCodeResult::ok;
}

GCodeResult LpcHeater::UpdateFaultDetectionParameters(const StringRef& reply) noexcept
{
	SendConfiguration();
	return GCodeResult::ok;
}

GCodeResult LpcHeater::UpdateHeaterMonitors(const StringRef& reply) noexcept
{
	SendConfiguration();
	return GCodeResult::ok;
}

GCodeResult LpcHeater::StartAutoTune(const StringRef& reply, bool seenA, float ambientTemp) noexcept
{
	reply.copy("Auto tuning is not supported by the LPC heater yet");
	return GCodeResult::error;
}

void LpcHeater::ApplyExtrusionFeedForward() noexcept
{
	previousExtrusionPwmBoost = extrusionPwmBoost;
	SendFeedForward();
}

void LpcHeater::SendConfiguration() noexcept
{
	LpcInterface::ConfigureHeater(frequency,
		GetHighestTemperatureLimit(), GetLowestTemperatureLimit(),
		GetMaxTemperatureExcursion(), GetMaxHeatingFaultTime(),
		static_cast<uint8_t>(min<uint32_t>(GetMaxBadTemperatureCount(), 255u)));
}

void LpcHeater::SendFeedForward() noexcept
{
	LpcInterface::ConfigureHeaterFeedForward(lastFanPwm, extrusionPwmBoost, extrusionTemperatureBoost);
}
