#ifndef SRC_HARDWARE_SAM4E_LPCINTERFACE_H_
#define SRC_HARDWARE_SAM4E_LPCINTERFACE_H_

#include <CoreIO.h>

namespace LpcInterface
{

void Init() noexcept;
void Spin() noexcept;
bool IsOnline() noexcept;
bool SetPinMode(Pin pin, PinMode mode) noexcept;
bool ReadPin(Pin pin) noexcept;
void WritePin(Pin pin, bool high) noexcept;

}

#endif
