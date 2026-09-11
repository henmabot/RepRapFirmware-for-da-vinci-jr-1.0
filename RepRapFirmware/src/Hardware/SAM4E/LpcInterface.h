#ifndef SRC_HARDWARE_SAM4E_LPCINTERFACE_H_
#define SRC_HARDWARE_SAM4E_LPCINTERFACE_H_

namespace LpcInterface
{

void Init() noexcept;
void Spin() noexcept;
bool IsOnline() noexcept;

}

#endif
