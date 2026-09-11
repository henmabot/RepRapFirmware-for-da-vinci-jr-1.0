#ifndef SRC_HARDWARE_SAM4E_LPCINTERFACE_H_
#define SRC_HARDWARE_SAM4E_LPCINTERFACE_H_

#include <CoreIO.h>
#include <LpcProtocol.h>

class FopDt;

namespace LpcInterface
{

struct ThermalStatus
{
	float temperature;
	uint16_t rawAdc;
	float averagePwm;
	LpcProtocol::HeaterState state;
	LpcProtocol::ThermalError error;
	uint32_t receivedAt;
};

void Init() noexcept;
void Spin() noexcept;
bool IsOnline() noexcept;
bool SetPinMode(Pin pin, PinMode mode) noexcept;
bool ReadPin(Pin pin) noexcept;
uint16_t ReadAnalog(Pin pin) noexcept;
void WritePin(Pin pin, bool high) noexcept;
void WritePwm(Pin pin, float duty, uint16_t frequency) noexcept;

void ConfigureThermistor(float r25, float beta, float coefficientC, float seriesResistance) noexcept;
void ConfigureHeaterModel(const FopDt& model) noexcept;
void ConfigureHeater(uint16_t frequency, float upperLimit, float lowerLimit, float maxExcursion, float maxFaultTime, uint8_t maxBadReadings) noexcept;
void CommandHeater(LpcProtocol::HeaterCommand command, float targetTemperature) noexcept;
void ConfigureHeaterFeedForward(float fanPwm, float extrusionPwmBoost, float extrusionTemperatureBoost) noexcept;
bool GetThermalStatus(ThermalStatus& status) noexcept;

}

#endif
