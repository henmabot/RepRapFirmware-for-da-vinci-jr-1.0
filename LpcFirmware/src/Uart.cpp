#include "Uart.h"
#include "Lpc1115.h"

namespace Uart
{

void Init() noexcept
{
	LPC_SYSCON_MAINCLKSEL = 0u;
	LPC_SYSCON_MAINCLKUEN = 0u;
	LPC_SYSCON_MAINCLKUEN = 1u;
	LPC_SYSCON_SYSAHBCLKDIV = 1u;

	LPC_SYSCON_SYSAHBCLKCTRL |= (1u << 16) | (1u << 12);
	LPC_SYSCON_UARTCLKDIV = 1u;

	LPC_IOCON_PIO1_6 = (LPC_IOCON_PIO1_6 & ~0x07u) | 0x01u;
	LPC_IOCON_PIO1_7 = (LPC_IOCON_PIO1_7 & ~0x07u) | 0x01u;

	// 12MHz IRC: divisor 4 with DIVADD=5/MUL=8 gives 115384 baud.
	LPC_UART_LCR = 0x83u;
	LPC_UART_DLL = 4u;
	LPC_UART_DLM = 0u;
	LPC_UART_FDR = 0x85u;
	LPC_UART_LCR = 0x03u;
	LPC_UART_FCR = 0x07u;
	LPC_UART_TER = 0x80u;
}

bool Read(uint8_t& byte) noexcept
{
	if ((LPC_UART_LSR & 0x01u) == 0u)
	{
		return false;
	}
	byte = static_cast<uint8_t>(LPC_UART_RBR);
	return true;
}

void Write(const uint8_t* data, size_t length) noexcept
{
	for (size_t i = 0; i < length; ++i)
	{
		while ((LPC_UART_LSR & (1u << 5)) == 0u)
		{
		}
		LPC_UART_THR = data[i];
	}
}

}
