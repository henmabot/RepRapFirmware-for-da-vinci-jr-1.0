# Da Vinci Jr 1.0 firmware setup

This port uses Da Vinci Jr 1.0 motion data recovered from the stock firmware together with the traced LPC1115 peripheral and endstop mappings. The supplied `sys/config.g` configures the single extruder, hotend heater and thermistor, both traced LPC-controlled fans, and the filament sensor. It also configures the stock travel and homing geometry and Duet WiFi networking.

WiFi credentials are intentionally not stored in the repository. Configure them with `M587`, then `M552 S1` in `config.g` starts the interface on boot.

## Duet WiFi wiring

The firmware uses the Duet WiFi SPI protocol with these SAM4E connections:

| SAM4E | ESP8266 | Function |
| --- | --- | --- |
| PB2 | GPIO4 | SAM transfer ready |
| PE3 | GPIO0 | ESP data ready / boot strap |
| PA11 | GPIO15 | ESP chip select / boot strap |
| PA12 | GPIO12 | MISO |
| PA13 | GPIO13 | MOSI |
| PA14 | GPIO14 | SCLK |
| PC24 | RST | ESP reset |
| PB14 | ENABLE | ESP enable |

The SAM4E SPI peripheral accepts only NPCS0/PA11 as slave-select in slave mode. Connect ESP GPIO15 to PA11 directly, with no PB2-to-PA11 bridge. Use PB2 as the ordinary GPIO output for the SAM transfer-ready signal. The current board pinout lists both PA11 and PB2 with no visible connection. This does not prove there is no hidden PCB connection, so check continuity on the target board before soldering.

PA26 remains SD DAT2. The SD socket therefore stays in its original four-bit HSMCI mode on PA26..PA31.

The traced board also assigns PC24 and PB14 to the stock right/left laser nets. This configuration dedicates those pins to ESP reset and enable, so the laser functions are unavailable at the same time.

### ESP firmware update path

Normal networking uses only the SPI/control wiring in the preceding section and does not require a SAM-to-ESP UART. This build therefore leaves the optional ESP UART off. In this RepRapFirmware version, `M997 S1` specifically invokes the ESP ROM UART uploader. That command is separate from normal SPI networking and from any external ESP flash-programming method, so this board configuration does not provide it.

### Other LPC inputs

The traced LPC mapping also exposes `lpc.filament_runout` (PIO2_7), `lpc.rotation` (PIO2_1), and `lpc.statusled` (PIO2_10). The default printer configuration leaves them unused. The recovered stock firmware proves the runout level and rotation-edge sources. It does not yet prove the active polarity and print-fault semantics needed to configure them safely in RepRapFirmware. The default enables the hotend IR filament sensor because board tracing independently proves its active-low electrical behavior.
