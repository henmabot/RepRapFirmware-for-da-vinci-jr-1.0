#ifndef SRC_HEATING_SENSORS_DAVINCIJRTEMPERATURESENSOR_H_
#define SRC_HEATING_SENSORS_DAVINCIJRTEMPERATURESENSOR_H_

#include "TemperatureSensor.h"

#if defined(DA_VINCI_JR)

class DaVinciJrTemperatureSensor : public TemperatureSensor
{
public:
	explicit DaVinciJrTemperatureSensor(unsigned int sensorNum) noexcept;
	~DaVinciJrTemperatureSensor() noexcept override;

	void Poll() noexcept override;
	const char *_ecv_array GetShortSensorType() const noexcept override { return TypeName; }

	static constexpr const char *_ecv_array TypeName = "davinci-ntc";

private:
	static SensorTypeDescriptor typeDescriptor;
};

#endif

#endif
