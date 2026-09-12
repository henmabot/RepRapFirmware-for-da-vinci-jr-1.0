#ifndef DA_VINCI_JR_THERMISTOR_H
#define DA_VINCI_JR_THERMISTOR_H

#include <stdint.h>

namespace DaVinciJrThermistor
{

float ConvertAdc(uint16_t rawAdc) noexcept;

}

#endif
