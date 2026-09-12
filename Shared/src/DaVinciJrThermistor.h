#ifndef DA_VINCI_JR_THERMISTOR_H
#define DA_VINCI_JR_THERMISTOR_H

#include <stdint.h>

namespace DaVinciJrThermistor
{

// Convert the LPC1115 10-bit ADC result to degrees Celsius using the physical
// NTC divider model inferred from the stock ROM calibration table.
float ConvertAdc(uint16_t rawAdc) noexcept;

// Exposed for calibration tests against the stock SAM4E table, whose entries
// use the LPC ADC result multiplied by four.
float ConvertScaledRaw(uint16_t scaledRaw) noexcept;

}

#endif
