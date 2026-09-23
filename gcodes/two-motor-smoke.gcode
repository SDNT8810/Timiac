; Verify both X home sensors and motor directions before running.
; Master file only. Do not put this dialect on the Octopus microSD.
; Actuator calibration test: close 1 mm from open/home, then reopen to zero.
G28 X1 X2
G90 X1 1 X2 1 F180
G4 P500
G90 X1 0 X2 0 F180
