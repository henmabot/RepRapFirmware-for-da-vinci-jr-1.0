#ifndef LPC_THERMAL_H
#define LPC_THERMAL_H

#include <LpcProtocol.h>
#include <stddef.h>
#include <stdint.h>

namespace Thermal
{

void Init() noexcept;
void ResetConfiguration() noexcept;
void HostHeartbeat() noexcept;
void HandleFrame(const LpcProtocol::Frame& frame) noexcept;

void Spin() noexcept;
bool TakeStatus(uint8_t* payload, size_t& length) noexcept;

}

#endif
