# Da Vinci Jr 1.0 firmware setup

This port uses Da Vinci Jr 1.0 motion data recovered from the stock firmware together with the traced LPC1115 peripheral and endstop mappings. The supplied `sys/config.g` configures the single extruder, hotend heater and thermistor, both traced LPC-controlled fans, the filament sensor, and the recovered travel and homing geometry.

## Duet WiFi wiring proposal

Duet WiFi is not enabled in the current board configuration. The previously proposed wiring was:

| SAM4E | ESP8266 | Function |
| --- | --- | --- |
| PA26 | GPIO4 | SAM transfer ready |
| PE3 | GPIO0 | ESP data ready / boot strap |
| PB2 | GPIO15 | ESP chip select / boot strap |
| PA12 | GPIO12 | MISO |
| PA13 | GPIO13 | MOSI |
| PA14 | GPIO14 | SCLK |
| PC24 | RST | ESP reset |
| PB14 | ENABLE | ESP enable |

The SAM4E SPI peripheral accepts only NPCS0/PA11 as its hardware slave-select input in slave mode. The earlier "PB2-to-PA11 bridge" proposal meant a literal jumper between PB2 and PA11. Such a jumper mirrors the ESP GPIO15/CS signal onto PA11 so the SPI peripheral can see the required hardware NSS signal. PA11 is already used and unavailable on this board, so the proposal is invalid. This port does not implement or recommend that bridge.

PA26 is SD DAT2 and remains part of the original four-bit HSMCI bus. This branch no longer changes the SD bus width. A future WiFi patch needs an approved free GPIO for ESP GPIO4/SAM transfer-ready. This repository does not pick one speculatively.

The traced board also assigns PC24 and PB14 to the stock right/left laser nets. The proposal uses those pins for ESP reset and enable, so the laser functions cannot be used at the same time.

This proposal does not add PA9/PA10 UART wiring. The intended ESP12 runtime and firmware-update design is SPI-based, so UART0 is not part of the board wiring discussion.

### Other LPC inputs

The traced LPC mapping also exposes `lpc.filament_runout` (PIO2_7), `lpc.rotation` (PIO2_1), and `lpc.statusled` (PIO2_10). The default printer configuration leaves them unused. The recovered stock firmware proves the runout level and rotation-edge sources. It does not yet prove the active polarity and print-fault semantics needed to configure them safely in RepRapFirmware. The default enables the hotend IR filament sensor because board tracing independently proves its active-low electrical behavior.
