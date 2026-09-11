#ifndef LPC_UART_H
#define LPC_UART_H

#include <stddef.h>
#include <stdint.h>

namespace Uart
{

void Init() noexcept;
bool Read(uint8_t& byte) noexcept;
void Write(const uint8_t* data, size_t length) noexcept;

}

#endif
