; Next Hour: all open, all closed, then show the selected time.
; Example target 01:00. Target <=40 s at 50%, <=20 s at 100%.
G90
G1 A100 B100 C100 D100 E100 H100 I100 J100 K100 L100 U100 V100 F1000
G1 A0 B0 C0 D0 E0 H0 I0 J0 K0 L0 U0 V0 F1000
G1 A100 B50 C0 D0 E0 H0 I0 J0 K0 L0 U0 V0 F1000
