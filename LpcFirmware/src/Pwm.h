#ifndef LPC_PWM_H
#define LPC_PWM_H

#include <stdint.h>

namespace Pwm
{

bool Set(uint8_t pin, uint16_t duty, uint16_t frequency) noexcept;

}

#endif
