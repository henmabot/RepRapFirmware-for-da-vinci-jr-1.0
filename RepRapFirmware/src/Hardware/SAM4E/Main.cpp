/*
 * Main.cpp
 *  Program entry point
 *  Created on: 11 Jul 2020
 *      Author: David
 *  License: GNU GPL version 3
 */

#include <CoreIO.h>
#include <RepRapFirmware.h>

// Program initialisation
void AppInit() noexcept
{
	// Put every motor driver in a known disabled state before the main firmware starts.
	for (size_t drive = 0; drive < NumDirectDrivers; ++drive)
	{
		SetPinMode(STEP_PINS[drive], OUTPUT_LOW);
		SetPinMode(DriverEnablePins[drive], DriverEnableActiveHigh ? OUTPUT_LOW : OUTPUT_HIGH);
	}
}

// End
