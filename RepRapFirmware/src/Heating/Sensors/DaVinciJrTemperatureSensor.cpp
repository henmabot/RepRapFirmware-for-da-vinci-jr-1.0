#include "DaVinciJrTemperatureSensor.h"

#if defined(DA_VINCI_JR)

#include <Hardware/SAM4E/LpcInterface.h>

TemperatureSensor::SensorTypeDescriptor DaVinciJrTemperatureSensor::typeDescriptor(
	TypeName,
	[](unsigned int sensorNum) noexcept -> TemperatureSensor *_ecv_from { return new DaVinciJrTemperatureSensor(sensorNum); }
);

DaVinciJrTemperatureSensor::DaVinciJrTemperatureSensor(unsigned int sensorNum) noexcept
	: TemperatureSensor(sensorNum, "Da Vinci Jr stock hotend thermistor")
{
	LpcInterface::ConfigureThermistor(sensorNum);
}

DaVinciJrTemperatureSensor::~DaVinciJrTemperatureSensor() noexcept
{
	LpcInterface::UnregisterThermistor(GetSensorNumber());
}

void DaVinciJrTemperatureSensor::Poll() noexcept
{
	LpcInterface::ThermalStatus status;
	if (!LpcInterface::GetThermalStatus(status))
	{
		SetResult(TemperatureError::timeout);
		return;
	}

	switch (status.error)
	{
	case LpcProtocol::ThermalError::notConfigured:
		SetResult(TemperatureError::notReady);
		break;
	case LpcProtocol::ThermalError::adcTimeout:
	case LpcProtocol::ThermalError::linkTimeout:
		SetResult(TemperatureError::timeout);
		break;
	case LpcProtocol::ThermalError::shortCircuit:
		SetResult(TemperatureError::shortCircuit);
		break;
	case LpcProtocol::ThermalError::openCircuit:
		SetResult(TemperatureError::openCircuit);
		break;
	default:
		SetResult(status.temperature, TemperatureError::ok);
		break;
	}
}

#endif
