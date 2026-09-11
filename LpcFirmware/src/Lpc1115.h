#ifndef LPC1115_H
#define LPC1115_H

#include <stdint.h>

#define SYST_CSR (*(volatile uint32_t *)0xE000E010u)
#define SYST_RVR (*(volatile uint32_t *)0xE000E014u)
#define SYST_CVR (*(volatile uint32_t *)0xE000E018u)


#define LPC_SYSCON_BASE 0x40048000u
#define LPC_IOCON_BASE  0x40044000u
#define LPC_UART_BASE   0x40008000u
#define LPC_ADC_BASE    0x4001C000u

#define LPC_SYSCON_SYSAHBCLKCTRL (*(volatile uint32_t *)(LPC_SYSCON_BASE + 0x080u))
#define LPC_SYSCON_UARTCLKDIV    (*(volatile uint32_t *)(LPC_SYSCON_BASE + 0x098u))
#define LPC_SYSCON_MAINCLKSEL    (*(volatile uint32_t *)(LPC_SYSCON_BASE + 0x070u))
#define LPC_SYSCON_MAINCLKUEN    (*(volatile uint32_t *)(LPC_SYSCON_BASE + 0x074u))
#define LPC_SYSCON_SYSAHBCLKDIV  (*(volatile uint32_t *)(LPC_SYSCON_BASE + 0x078u))
#define LPC_SYSCON_PDRUNCFG      (*(volatile uint32_t *)(LPC_SYSCON_BASE + 0x238u))

#define LPC_IOCON_PIO1_6         (*(volatile uint32_t *)(LPC_IOCON_BASE + 0x0A4u))
#define LPC_IOCON_PIO1_7         (*(volatile uint32_t *)(LPC_IOCON_BASE + 0x0A8u))
#define LPC_IOCON_PIO1_0         (*(volatile uint32_t *)(LPC_IOCON_BASE + 0x078u))

#define LPC_UART_RBR             (*(volatile uint32_t *)(LPC_UART_BASE + 0x000u))
#define LPC_UART_THR             (*(volatile uint32_t *)(LPC_UART_BASE + 0x000u))
#define LPC_UART_DLL             (*(volatile uint32_t *)(LPC_UART_BASE + 0x000u))
#define LPC_UART_DLM             (*(volatile uint32_t *)(LPC_UART_BASE + 0x004u))
#define LPC_UART_FCR             (*(volatile uint32_t *)(LPC_UART_BASE + 0x008u))
#define LPC_UART_LCR             (*(volatile uint32_t *)(LPC_UART_BASE + 0x00Cu))
#define LPC_UART_LSR             (*(volatile uint32_t *)(LPC_UART_BASE + 0x014u))
#define LPC_UART_FDR             (*(volatile uint32_t *)(LPC_UART_BASE + 0x028u))
#define LPC_UART_TER             (*(volatile uint32_t *)(LPC_UART_BASE + 0x030u))

#endif

#define LPC_ADC_CR               (*(volatile uint32_t *)(LPC_ADC_BASE + 0x000u))
#define LPC_ADC_DR1              (*(volatile uint32_t *)(LPC_ADC_BASE + 0x014u))
