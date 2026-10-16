; Next Hour: all arms fully open, all arms fully closed, then show the selected time.
; Each phase retains the existing 30% slower speed. No flutter or dwell.
G90
G1 A100 B100 C100 D100 E100 H100 I100 J100 K100 L100 U100 V100 F700
G1 A0 B0 C0 D0 E0 H0 I0 J0 K0 L0 U0 V0 F700
G1 A100 B50 C0 D0 E0 H0 I0 J0 K0 L0 U0 V0 F700
