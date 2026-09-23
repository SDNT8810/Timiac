; TIMIAC X-only smoke test -- home, then 10 slow X0.5 <-> X48 round trips
; Requires a verified X_MIN sensor: M119 must show open/released and TRIGGERED/pressed.
G21
G90
M211 S1
M118 TIMIAC SMOKE TEST: HOMING X
G28 X
G1 X0.5 F369.6
M808 L10
G1 X39.5 F369.6
G1 X0.0 F369.6
M808
M400
M18 X
M118 TIMIAC SMOKE TEST COMPLETE
