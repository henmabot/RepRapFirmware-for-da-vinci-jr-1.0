; Da Vinci Jr 1.0 configuration

M550 P"Da Vinci Jr 1.0"

; Driver 0=X, 1=Y, 2=Z, 3=E1.
M584 X0 Y1 Z2 E3

; The stock TB62269 direction inputs are high for clockwise rotation.
; Y is intentionally reversed so positive Y uses the opposite motor direction.
M569 P0 S1
M569 P1 S0
M569 P2 S1
M569 P3 S1

; Stock mechanics. X/Y/E use 3200 microsteps/revolution. Z uses 6400.
; The TB62269 microstep mode is hardware-set, so there is intentionally no M350.
M92 X80 Y80 Z2560 E145.5

; Conservative bring-up limits, below the printer's published maximum travel speed.
M203 S1 X60 Y60 Z5 E20
M201 X300 Y300 Z30 E100
M566 X300 Y300 Z30 E300

; Measured Da Vinci Jr travel limits.
M208 X-14:165 Y-15:155 Z0:170

; Require homing before ordinary axis motion and enforce the configured limits.
M564 H1 S1

; The optical home sensors are high when triggered. X/Z are low-end; Y is high-end.
M574 X1 S1 P"xstop"
M574 Y2 S1 P"ystop"
M574 Z1 S1 P"zstop"

; Verified LPC1115 peripherals only; NFC is intentionally not exposed.
; PIO1_0 = hotend NTC, PIO0_9 = heater, PIO2_5 = hotend fan, PIO1_10 = reflow fan.
; The hotend NTC is 100K, connected from the ADC node to ground with the MCU side pulled up.
; B4267/R4700 retain the existing Da Vinci Jr conversion profile until measured directly.
M308 S0 P"lpc.ntc" Y"thermistor" T100000 B4267 C0 R4700
M950 H0 C"lpc.heater" T0 Q250
M143 H0 S265

M950 F0 C"lpc.fan" Q250
M106 P0 C"Hotend fan" S0
M950 F1 C"lpc.reflowfan" Q250
M106 P1 C"Reflow fan" S0

; Declaring heater 0 as the tool heater enables RRF's default hotend model.
; Neither verified fan is assumed to be the slicer's part-cooling fan.
M563 P0 D0 H0 F-1
