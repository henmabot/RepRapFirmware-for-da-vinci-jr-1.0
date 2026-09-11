; Da Vinci Jr 1.0 configuration

M550 P"Da Vinci Jr 1.0"

; Driver 0=X, 1=Y, 2=Z, 3=E1.
M584 X0 Y1 Z2 E3

; The stock TB62269 direction inputs are high for clockwise rotation.
; On the assembled printer, clockwise is +X, +Y, +Z, and positive extrusion.
M569 P0 S1
M569 P1 S1
M569 P2 S1
M569 P3 S1

; Stock mechanics. X/Y/E use 3200 microsteps/revolution. Z uses 6400.
; The TB62269 microstep mode is hardware-set, so there is intentionally no M350.
M92 X80 Y80 Z2560 E145.5

; Conservative bring-up limits, below the printer's published maximum travel speed.
M203 S1 X60 Y60 Z5 E20
M201 X300 Y300 Z30 E100
M566 X300 Y300 Z30 E300

; Measured Da Vinci Jr travel limits and low-end home coordinates.
M208 X-14:165 Y-15:155 Z0:170

; Require homing before ordinary axis motion and enforce the configured limits.
M564 H1 S1

; The three optical home sensors are high when triggered and are all at low end.
M574 X1 S1 P"xstop"
M574 Y2 S1 P"ystop"
M574 Z1 S1 P"zstop"

; LPC1115 peripherals. Only pins marked Verified in the hardware pinout are used here.
; PIO1_0 = hotend NTC, PIO0_9 = heater, PIO2_5 = hotend fan, PIO1_10 = reflow fan.
;
; The stock-conversion firmware identifies the hotend sensor as a 100K
; Semitec 104GT-2/104NT-4 with a 4.7K pull-up (beta 4267).
M308 S0 P"lpc.ntc" Y"thermistor" T100000 B4267 C0 R4700
M950 H0 C"lpc.heater" T0 Q250
M143 H0 S265

M950 F0 C"lpc.fan" Q250
M950 F1 C"lpc.reflowfan" Q250

; Declaring heater 0 as the tool heater enables RRF's default hotend model.
; Fan behavior remains unassigned until the stock fan roles are verified.
M563 P0 D0 H0 F-1
