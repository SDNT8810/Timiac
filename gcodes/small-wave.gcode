; Hardware dialect: inverted mechanism, 0 mm = open/home, 50 mm = closed.
; Close all arms, then open opposing pairs by 2.5 mm (5% of the stroke).
; G28 requires all twelve correctly wired and verified home sensors.
G28 X1 Y1 Z1 A1 B1 C1 X2 Y2 Z2 A2 B2 C2
G90 X1 50 Y1 50 Z1 50 A1 50 B1 50 C1 50 X2 50 Y2 50 Z2 50 A2 50 B2 50 C2 50 F120
G1 X1 47.5 X2 47.5 F120
G1 X1 50 X2 50 Y1 47.5 Y2 47.5 F120
G1 Y1 50 Y2 50 Z1 47.5 Z2 47.5 F120
G1 Z1 50 Z2 50 A1 47.5 A2 47.5 F120
G1 A1 50 A2 50 B1 47.5 B2 47.5 F120
G1 B1 50 B2 50 C1 47.5 C2 47.5 F120
G1 C1 50 C2 50 F120
M400
