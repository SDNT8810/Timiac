; Hardware dialect: inverted mechanism, 0 mm = open/home, 48 mm = closed.
; Close all arms, then open opposing pairs by 2.4 mm (5% of the stroke).
; G28 requires all twelve correctly wired and verified home sensors.
G28 X1 Y1 Z1 A1 B1 C1 X2 Y2 Z2 A2 B2 C2
G90 X1 48 Y1 48 Z1 48 A1 48 B1 48 C1 48 X2 48 Y2 48 Z2 48 A2 48 B2 48 C2 48 F144
G1 X1 45.6 X2 45.6 F144
G1 X1 48 X2 48 Y1 45.6 Y2 45.6 F144
G1 Y1 48 Y2 48 Z1 45.6 Z2 45.6 F144
G1 Z1 48 Z2 48 A1 45.6 A2 45.6 F144
G1 A1 48 A2 48 B1 45.6 B2 45.6 F144
G1 B1 48 B2 48 C1 45.6 C2 45.6 F144
G1 C1 48 C2 48 F144
M400
