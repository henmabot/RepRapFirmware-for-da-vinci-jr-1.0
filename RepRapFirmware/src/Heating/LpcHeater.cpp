#include "LpcHeater.h"

#include "Heat.h"
#include <Hardware/SAM4E/LpcInterface.h>
#include <Platform/RepRap.h>
#include <Platform/Event.h>
#include <Tools/Tool.h>

static_assert(static_cast<uint8_t>(LpcProtocol::HeaterState::fault) == static_cast<uint8_t>(HeaterMode::fault)
	&& static_cast<uint8_t>(LpcProtocol::HeaterState::offline) == static_cast<uint8_t>(HeaterMode::offline)
	&& static_cast<uint8_t>(LpcProtocol::HeaterState::off) == static_cast<uint8_t>(HeaterMode::off)
	&& static_cast<uint8_t>(LpcProtocol::HeaterState::suspended) == static_cast<uint8_t>(HeaterMode::suspended)
	&& static_cast<uint8_t>(LpcProtocol::HeaterState::cooling) == static_cast<uint8_t>(HeaterMode::cooling)
	&& static_cast<uint8_t>(LpcProtocol::HeaterState::stable) == static_cast<uint8_t>(HeaterMode::stable)
	&& static_cast<uint8_t>(LpcProtocol::HeaterState::heating) == static_cast<uint8_t>(HeaterMode::heating));

static HeaterFaultType GetHeaterFaultType(LpcProtocol::ThermalError error) noexcept
{
	switch (error)
	{
	case LpcProtocol::ThermalError::adcTimeout:
	case LpcProtocol::ThermalError::shortCircuit:
	case LpcProtocol::ThermalError::openCircuit:
	case LpcProtocol::ThermalError::linkTimeout:
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
	  connectionGeneration(LpcInterface::GetConnectionGeneration())
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
	const uint32_t currentGeneration = LpcInterface::GetConnectionGeneration();
	if (currentGeneration != connectionGeneration)
	{
		connectionGeneration = currentGeneration;
		if (mode >= HeaterMode::suspended)
		{
			RaiseFault(LpcProtocol::ThermalError::linkTimeout);
		}
	}
	LpcInterface::ThermalStatus status;
	if (LpcInterface::GetThermalStatus(status))
	{
		if (mode != HeaterMode::fault)
		{
			mode = static_cast<HeaterMode>(status.state);
			if (mode == HeaterMode::fault)
			{
				RaiseFault(status.error);
			}
		}
	}
	else if (mode != HeaterMode::fault)
	{
		if (mode >= HeaterMode::suspended)
		{
			RaiseFault(LpcProtocol::ThermalError::linkTimeout);
		}
		else
		{
			mode = HeaterMode::offline;
		}
	}
}

void LpcHeater::SwitchOff() noexcept
{
	LpcInterface::CommandHeater(LpcProtocol::HeaterCommand::off, 0.0f);
	if (mode != HeaterMode::fault)
	{
		mode = HeaterMode::off;
	}
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
	if (sus && (mode == HeaterMode::stable || mode == HeaterMode::heating || mode == HeaterMode::cooling))
	{
		LpcInterface::CommandHeater(LpcProtocol::HeaterCommand::suspend, GetTargetTemperature());
		mode = HeaterMode::suspended;
	}
	else if (!sus && mode == HeaterMode::suspended)
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
	if (!LpcInterface::IsThermistorSensor(GetSensorNumber()))
	{
		reply.printf("Heater %u must use the NTC configured on lpc.ntc", GetHeaterNumber());
		return GCodeResult::error;
	}
	if (!Succeeded(ValidateMonitors(reply)))
	{
		return GCodeResult::error;
	}
	LpcInterface::ThermalStatus status;
	if (!LpcInterface::GetThermalStatus(status))
	{
		reply.copy("LPC thermal status is unavailable");
		return GCodeResult::error;
	}
	if (status.state == LpcProtocol::HeaterState::fault)
	{
		reply.printf("LPC heater is in fault state: %s", GetThermalErrorText(status.error));
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
	const GCodeResult result = ValidateMonitors(reply);
	if (!Succeeded(result))
	{
		return result;
	}
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
	SendFeedForward();
}

void LpcHeater::SendConfiguration() noexcept
{
	float upperLimit = BadErrorTemperature;
	float lowerLimit = ABS_ZERO;
	for (const HeaterMonitor& monitor : monitors)
	{
		switch (monitor.GetTrigger())
		{
		case HeaterMonitorTrigger::TemperatureExceeded:
			upperLimit = min(upperLimit, monitor.GetTemperatureLimit());
			break;
		case HeaterMonitorTrigger::TemperatureTooLow:
			lowerLimit = max(lowerLimit, monitor.GetTemperatureLimit());
			break;
		default:
			break;
		}
	}
	LpcInterface::ConfigureHeater(frequency,
		upperLimit, lowerLimit,
		GetMaxTemperatureExcursion(), GetMaxHeatingFaultTime(),
		static_cast<uint8_t>(min<uint32_t>(GetMaxBadTemperatureCount(), 255u)));
}

void LpcHeater::SendFeedForward() noexcept
{
	LpcInterface::ConfigureHeaterFeedForward(lastFanPwm, extrusionPwmBoost, extrusionTemperatureBoost);
}

void LpcHeater::RaiseFault(LpcProtocol::ThermalError error) noexcept
{
	mode = HeaterMode::fault;
	LpcInterface::CommandHeater(LpcProtocol::HeaterCommand::off, 0.0f);
	Tool::FlagTemperatureFault(GetHeaterNumber());
	(void)Event::AddEvent(EventType::heater_fault, static_cast<uint16_t>(GetHeaterFaultType(error)),
		CanInterface::GetCanAddress(), GetHeaterNumber(), "%s", GetThermalErrorText(error));
}

GCodeResult LpcHeater::ValidateMonitors(const StringRef& reply) const noexcept
{
	for (const HeaterMonitor& monitor : monitors)
	{
		if (monitor.GetTrigger() == HeaterMonitorTrigger::Disabled)
		{
			continue;
		}
		if (monitor.GetSensorNumber() != GetSensorNumber())
		{
			reply.copy("LPC heater monitors must use the heater's lpc.ntc sensor");
			return GCodeResult::error;
		}
		if (monitor.GetAction() != HeaterMonitorAction::GenerateFault)
		{
			reply.copy("LPC heater monitors only support the generate-fault action");
			return GCodeResult::error;
		}
	}
	return GCodeResult::ok;
}
