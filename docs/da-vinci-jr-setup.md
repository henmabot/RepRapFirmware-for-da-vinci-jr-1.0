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

Two board-specific conflicts need attention before modifying hardware.

First, the SAM4E SPI peripheral can only use NPCS0 as slave-select while it is in slave mode. PB2 is NPCS2, so it cannot directly end Duet WiFi SPI transfers. The firmware keeps PB2 as the logical ESP GPIO15/CS signal, while hardware NSS must reach PA11/NPCS0. A PB2-to-PA11 bridge is the proposed solution. Treat this bridge as experimental until the PA11 net has been traced on the target board.

Current reverse-engineering tentatively labels PA11 as the onboard MX25L3206E flash chip-select, but the hardware notes still mark that trace unresolved. Do not make the PB2-to-PA11 bridge on that assumption alone. Determine the PA11 net first. If PA11 is the flash CS, isolate that CS and hold the flash inactive/high before bridging. PA12 through PA14 already have traced connections to the shared SPI data/clock lines used by the proposed ESP wiring.

Second, PA26 is normally SD DAT2. This port switches the SD socket to one-bit HSMCI and frees PA26 for WiFi. SD remains available through CMD, CLK, and DAT0 on PA28, PA29, and PA30.

The traced board also assigns PC24 and PB14 to the stock right/left laser nets. This configuration dedicates those pins to ESP reset and enable, so the laser functions are unavailable at the same time.

### Proposed ESP UART

Normal networking only needs the preceding SPI/control wiring. This port deliberately does not configure an ESP UART because current hardware notes leave the PA9/PA10 header mapping unresolved. After those connections are traced, UART0 can provide ESP debug output and firmware upload:

- PA9 can serve as the SAM UART0 RX connection to ESP TX.
- PA10 can serve as the SAM UART0 TX connection to ESP RX.

The current firmware leaves those pins untouched and rejects `M997 S1` ESP firmware upload. SPI networking remains available without this UART.

### Other LPC inputs

The traced LPC mapping also exposes `lpc.filament_runout` (PIO2_7), `lpc.rotation` (PIO2_1), and `lpc.statusled` (PIO2_10). The default printer configuration leaves them unused. The recovered stock firmware proves the runout level and rotation-edge sources. It does not yet prove the active polarity and print-fault semantics needed to configure them safely in RepRapFirmware. The default enables the hotend IR filament sensor because board tracing independently proves its active-low electrical behavior.
