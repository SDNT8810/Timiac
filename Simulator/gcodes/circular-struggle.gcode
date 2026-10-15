; Struggle: 30 mm open, 10 mm open, 30 mm open, then directly to time.
; Intermediate extensions are 60%, 20%, 60% of the 50 mm stroke.
; Final example target 01:00: other arms close fully; time arms show the time.
; Retain the 400 mm/min hardware vector feed and 20 mm/s^2 acceleration.
G90
G1 A60 B60 C60 D60 E60 H60 I60 J60 K60 L60 U60 V60 F800
G1 A20 B20 C20 D20 E20 H20 I20 J20 K20 L20 U20 V20 F800
G1 A60 B60 C60 D60 E60 H60 I60 J60 K60 L60 U60 V60 F800
G1 A100 B50 C0 D0 E0 H0 I0 J0 K0 L0 U0 V0 F800
