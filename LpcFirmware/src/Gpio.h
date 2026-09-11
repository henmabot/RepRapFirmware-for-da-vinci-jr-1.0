#ifndef LPC_GPIO_H
#define LPC_GPIO_H

#include <LpcProtocol.h>
#include <stdint.h>

namespace Gpio
{

void Init() noexcept;
bool Configure(uint8_t pin, LpcProtocol::GpioMode mode, bool initialValue) noexcept;
bool Write(uint8_t pin, bool value) noexcept;
bool Poll(uint8_t& pin, bool& value) noexcept;

}

#endif
