; TIMIAC X-only endurance test -- home, then X0.5 <-> X48 forever
; Requires a verified X_MIN sensor.
G21
G90
M211 S1
M118 TIMIAC AUTOSTART: HOMING IN 10 SECONDS
G4 S10
G28 X
G1 X0.0 F369.6
M808 L0
G1 X39.5 F369.6
G1 X0.1 F369.6
M808
