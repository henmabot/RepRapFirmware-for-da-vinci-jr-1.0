#ifndef DA_VINCI_JR_THERMISTOR_H
#define DA_VINCI_JR_THERMISTOR_H

#include <stdint.h>

namespace DaVinciJrThermistor
{

// Convert the LPC1115 10-bit ADC result to degrees Celsius using the recovered
// stock ROM lookup table with linear interpolation between calibration points.
float ConvertAdc(uint16_t rawAdc) noexcept;

}

#endif
