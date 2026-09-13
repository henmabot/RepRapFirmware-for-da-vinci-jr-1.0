# Da Vinci Jr 1.0 firmware setup

This port uses Da Vinci Jr 1.0 motion data recovered from the stock firmware together with the traced LPC1115 peripheral and endstop mappings. The supplied `sys/config.g` configures the single extruder, hotend heater and thermistor, both traced LPC-controlled fans, and the filament sensor. It also configures the stock travel and homing geometry and Duet WiFi networking.

WiFi credentials are intentionally not stored in the repository. Configure them with `M587`, then `M552 S1` in `config.g` starts the interface on boot.

## Duet WiFi wiring

The firmware uses the Duet WiFi SPI protocol with these SAM4E connections:

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

Two board-specific conflicts require physical changes.

First, the SAM4E SPI peripheral can only use NPCS0 as slave-select while it is in slave mode. PB2 is NPCS2, so it cannot directly terminate Duet WiFi SPI transfers. The firmware therefore keeps PB2 as the logical ESP GPIO15/CS signal, but the same signal must also reach PA11/NPCS0. Bridge PB2 to PA11.

The board reverse-engineering maps PA11 to the onboard MX25L3206E flash chip-select and PA12 through PA14 to the same flash bus. Do not bridge PB2 to PA11 while the flash can still respond to CS. Isolate the flash CS from PA11 and hold it inactive/high. Leave the flash itself in place. Once its CS is permanently inactive, sharing PA12 through PA14 is safe and RepRapFirmware does not use that flash.

Second, PA26 is normally SD DAT2. This port switches the SD socket to one-bit HSMCI and frees PA26 for WiFi. SD remains available through CMD, CLK, and DAT0 on PA28, PA29, and PA30.

The traced board also assigns PC24 and PB14 to the stock right/left laser nets. This configuration dedicates those pins to ESP reset and enable, so the laser functions are unavailable at the same time.

### Optional ESP UART

Normal networking only needs the SPI/control wiring above. The firmware reserves UART0 for optional ESP debug output and ESP firmware upload:

- PA9 is the SAM UART0 RX pin and must connect to ESP TX.
- PA10 is the SAM UART0 TX pin and must connect to ESP RX.

Without these two optional UART wires, the Duet WiFi network transport still works, but `M997 S1` cannot upload `DuetWiFiServer.bin` to the ESP from the SAM.

### Other LPC inputs

The traced LPC mapping also exposes `lpc.filament_runout` (PIO2_7), `lpc.rotation` (PIO2_1), and `lpc.statusled` (PIO2_10). The default printer configuration leaves them unused. The recovered stock firmware proves the runout level and rotation-edge sources. It does not yet prove the active polarity and print-fault semantics needed to configure them safely in RepRapFirmware. The default enables the hotend IR filament sensor because board tracing independently proves its active-low electrical behavior.

## Proposed LPC1115 firmware update path

This PR does not implement LPC1115 firmware updating. SWD remains the supported programming path for the LPC1115. The traced board wiring does, however, provide the signals needed for a future SAM4E-hosted UART ISP updater:

| SAM4E | LPC1115 | Purpose |
| --- | --- | --- |
| PC15 | PIO0_0 | Reset |
| PC13 | PIO0_1 | ISP entry strap |
| PA5 UART1 RX | PIO1_7 UART TX | LPC to SAM data |
| PA6 UART1 TX | PIO1_6 UART RX | SAM to LPC data |

A future implementation could assign the LPC1115 to an additional `M997` module, for example `M997 S2`. RepRapFirmware would first switch off heaters and drives, validate the uploaded LPC image, and temporarily reserve the SAM-to-LPC UART. The SAM could then hold PIO0_1 low while resetting PIO0_0 to enter the LPC1115 ROM UART ISP bootloader.

The proposed updater should use the ROM ISP synchronization, unlock, and part-ID flow; program through RAM in supported blocks; and verify every programmed block. It should program sector zero last, with the vector/checksum block at address zero last of all, so interrupted updates preferentially remain recoverable through ROM ISP instead of booting a partial application.

Image validation should reject invalid Cortex-M vector tables and LPC Code Read Protection magic values at flash offset `0x2FC`. The LPC build should also explicitly reserve a safe CRP word before this proposal is implemented.

With Duet WiFi present, WiFi could be the user-facing file transport: upload the LPC image to the SAM and request the proposed update from the web console. The actual SAM-to-LPC programming transport would still be UART ROM ISP. SWD should remain available as the lowest-level recovery path.
