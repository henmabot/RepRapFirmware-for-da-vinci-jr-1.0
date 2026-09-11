#ifndef LPC_THERMAL_H
#define LPC_THERMAL_H

#include <LpcProtocol.h>
#include <stddef.h>
#include <stdint.h>

namespace Thermal
{

void Init() noexcept;
void Tick() noexcept;
uint32_t Millis() noexcept;
void HostHeartbeat() noexcept;

void ConfigureThermistor(const uint8_t* payload, size_t length) noexcept;
void ConfigureModelA(const uint8_t* payload, size_t length) noexcept;
void ConfigureModelB(const uint8_t* payload, size_t length) noexcept;
void ConfigureModelC(const uint8_t* payload, size_t length) noexcept;
void ConfigureHeater(const uint8_t* payload, size_t length) noexcept;
void Command(const uint8_t* payload, size_t length) noexcept;
void ConfigureFeedForward(const uint8_t* payload, size_t length) noexcept;

void Spin() noexcept;
bool TakeStatus(uint8_t* payload, size_t& length) noexcept;

}

extern "C" void SysTick_Handler() noexcept;

#endif
