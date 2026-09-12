#ifndef SRC_HARDWARE_SAM4E_LPCISPUPDATER_H_
#define SRC_HARDWARE_SAM4E_LPCISPUPDATER_H_

#include <RepRapFirmware.h>

namespace LpcIspUpdater
{

constexpr const char *DefaultFirmwareFile = "Lpc1115Firmware.bin";

GCodeResult CheckFirmwareFile(const StringRef& filenameRef, const StringRef& reply) noexcept;
bool Update(const StringRef& filenameRef) noexcept;

}

#endif
