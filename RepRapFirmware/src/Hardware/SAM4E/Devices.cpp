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
	SdhcInit();
}

void StopAnalogTask() noexcept
{
}

void StopUsbTask() noexcept
{
}

// End
