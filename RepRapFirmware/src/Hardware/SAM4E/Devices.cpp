/*
 * Devices.cpp
 *
 *  Created on: 11 Aug 2020
 *      Author: David
 */

#include "Devices.h"
#include <RepRapFirmware.h>
#include <AnalogIn.h>
#include <AnalogOut.h>

AsyncSerial lpcUart(UART1, UART1_IRQn, ID_UART1, 256, 256,
	[](AsyncSerial*) noexcept { }, [](AsyncSerial*) noexcept { });
#if HAS_WIFI_NETWORKING && HAS_WIFI_UART
AsyncSerial serialWiFi(UART0, UART0_IRQn, ID_UART0, 512, 512,
	[](AsyncSerial*) noexcept
	{
		SetPinFunction(APIN_SerialWiFi_RXD, SerialWiFiPeriphMode);
		SetPinFunction(APIN_SerialWiFi_TXD, SerialWiFiPeriphMode);
		EnablePullup(APIN_SerialWiFi_RXD);
	},
	[](AsyncSerial*) noexcept { });

void UART0_Handler() noexcept
{
	serialWiFi.IrqHandler();
}
#endif

void UART1_Handler() noexcept
{
	lpcUart.IrqHandler();
}

static void LpcUartInit() noexcept
{
	SetPinFunction(LpcUartRxPin, LpcUartPinFunction);
	SetPinFunction(LpcUartTxPin, LpcUartPinFunction);
	EnablePullup(LpcUartRxPin);
	lpcUart.begin(LpcUartBaudRate);
}

SerialCDC serialUSB;

void SdhcInit() noexcept
{
	SetPinFunction(HsmciClockPin, HsmciPinsFunction);
	for (Pin p : HsmciOtherPins)
	{
		SetPinFunction(p, HsmciPinsFunction);
		EnablePullup(p);
	}
}

// Device initialisation
void DeviceInit() noexcept
{
	LegacyAnalogIn::AnalogInInit();
	AnalogOut::Init();
	LpcUartInit();
	SdhcInit();
}

void StopAnalogTask() noexcept
{
}

void StopUsbTask() noexcept
{
}

// End
