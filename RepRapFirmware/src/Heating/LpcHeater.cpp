#include "LpcHeater.h"

#include "Heat.h"
#include <Hardware/SAM4E/LpcInterface.h>
#include <Platform/RepRap.h>
#include <Platform/Platform.h>
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
			tuning = false;
			RaiseFault(LpcProtocol::ThermalError::linkTimeout);
		}
	}
	LpcInterface::ThermalStatus status;
	if (LpcInterface::GetThermalStatus(status))
	{
		if (mode != HeaterMode::fault)
		{
			// While tuning, the LPC firmware reports a wire state of 'heating' throughout (it does its
			// own relay control rather than following our targetTemperature), so don't let that
			// overwrite our tuning bookkeeping - just watch for a fault raised during tuning.
			const HeaterMode wireMode = static_cast<HeaterMode>(status.state);
			if (tuning)
			{
				if (wireMode == HeaterMode::fault)
				{
					tuning = false;
					mode = wireMode;
					RaiseFault(status.error);
				}
			}
			else
			{
				mode = wireMode;
				if (mode == HeaterMode::fault)
				{
					RaiseFault(status.error);
				}
			}
		}
	}
	else if (mode != HeaterMode::fault)
	{
		if (mode >= HeaterMode::suspended)
		{
			tuning = false;
			RaiseFault(LpcProtocol::ThermalError::linkTimeout);
		}
		else
		{
			mode = HeaterMode::offline;
		}
	}

	if (tuning)
	{
		PollTuning();
	}
}

void LpcHeater::SwitchOff() noexcept
{
	if (tuning)
	{
		StopTuning();
	}
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
	tuning = false;
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
	if (sus && tuning)
	{
		StopTuning();
	}
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

// Auto tune this heater. The caller (Heater::StartAutoTune) has already checked that no other heater
// is being tuned and has set up tuningTargetTemp, tuningPwm and tuningHysteresis. The LPC firmware
// runs the actual relay-tuning state machine and reports one completed cycle at a time via
// heaterTuningReportA/B; PollTuning() (called from Spin() while 'tuning' is set) consumes those
// reports the way RemoteHeater::UpdateHeaterTuning does for CAN-connected expansion boards.
//
// ambientTemp/seenA are accepted for interface compatibility with Heater::StartAutoTune but are not
// used: unlike LocalHeater/RemoteHeater there is no "wait for starting temperature to settle" phase
// here, since the LPC firmware's relay tuning starts heating immediately once commanded.
GCodeResult LpcHeater::StartAutoTune(const StringRef& reply, bool seenA, float ambientTemp) noexcept
{
	(void)seenA;
	(void)ambientTemp;
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

	ClearCounters();
	tuned = false;
	SendConfiguration();
	LpcInterface::StartHeaterTuning(true, tuningPwm, tuningTargetTemp - tuningHysteresis, tuningTargetTemp, TuningPeakTempDrop);
	tuning = true;
	tuningBeginTime = millis();
	tuningStartTemperature = status.temperature;
	tuningPhase = 1;
	ReportTuningUpdate();
	return GCodeResult::ok;
}

// Cancel tuning (if in progress) and put the heater back into a safe off state.
void LpcHeater::StopTuning() noexcept
{
	tuning = false;
	LpcInterface::StartHeaterTuning(false, 0.0f, 0.0f, 0.0f, 0.0f);
	if (mode != HeaterMode::fault)
	{
		mode = HeaterMode::off;
	}
}

// Cancel tuning because of a timeout/stall, logging why. Mirrors the messages RemoteHeater/LocalHeater
// print for the same situations (see RemoteHeater::Spin()'s TuningState::heatingUp case).
void LpcHeater::CancelTuning(const char *reason) noexcept
{
	reprap.GetPlatform().MessageF(GenericMessage, "Auto tune cancelled because %s\n", reason);
	StopTuning();
}

// Called from Spin() once per tick while tuning is in progress. Consumes at most one newly-completed
// tuning cycle per call, in the same way RemoteHeater::UpdateHeaterTuning does for a CAN report.
//
// The LPC firmware's own relay-tuning state machine (LpcFirmware/src/Thermal.cpp) has no timeout of
// its own while waiting to reach the high threshold - unlike LocalHeater's non-expansion path, its
// ExpansionMode-equivalent phases just wait indefinitely for the temperature to cross a threshold, the
// same as RemoteHeater's CAN-connected expansion boards do. RemoteHeater covers this gap with its own
// host-side timers in Spin()'s TuningState::heatingUp case; we do the same here, since PollTuning()
// only hears from the firmware once a cycle completes and would otherwise never notice a stall.
void LpcHeater::PollTuning() noexcept
{
	LpcInterface::TuningReport report;
	if (!LpcInterface::GetTuningReport(report))
	{
		// No completed cycle yet - watch for the same two stall conditions RemoteHeater checks in its
		// TuningState::heatingUp case (RepRapFirmware/src/Heating/RemoteHeater.cpp), re-expressed
		// against our own host-side timer since the LPC firmware doesn't report progress until a full
		// heating+cooling cycle completes.
		const uint32_t heatingTime = millis() - tuningBeginTime;
		LpcInterface::ThermalStatus status;
		const bool haveStatus = LpcInterface::GetThermalStatus(status);
		if (heatingTime > (uint32_t)((GetModel().GetDeadTime() + 30.0) * SecondsToMillis)
			&& haveStatus && (status.temperature - tuningStartTemperature) < 3.0f)
		{
			CancelTuning("temperature is not increasing");
			return;
		}
		if (heatingTime >= ToolHeaterTuningTargetTemperatureTimeout * 60u * (uint32_t)SecondsToMillis)
		{
			CancelTuning("target temperature was not reached");
		}
		return;
	}

	tOn.Add((float)report.ton);
	tOff.Add((float)report.toff);
	dHigh.Add((float)report.dhigh);
	dLow.Add((float)report.dlow);
	heatingRateAcc.Add(report.heatingRate);
	coolingRateAcc.Add(report.coolingRate);
	tuningVoltage.Add(report.voltage);

	// The LPC firmware itself caps tuning at a small fixed number of cycles (it has no room to run
	// RemoteHeater/LocalHeater's consistency-based early exit), so once it reports the last cycle,
	// finish here rather than waiting for more data that will never arrive.
	if (coolingRateAcc.GetNumSamples() >= MinTuningHeaterCycles || report.cyclesDone >= MinTuningHeaterCycles)
	{
		tuning = false;
		CalculateModel(fanOffParams);
		SetAndReportModelAfterTuning(false);
		if (mode != HeaterMode::fault)
		{
			mode = HeaterMode::off;
		}
	}
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
