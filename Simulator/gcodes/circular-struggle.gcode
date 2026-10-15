; Struggle: two continuous out-and-back cycles across 10-90% of stroke.
; On the 50 mm mechanism, each sweep is 40 mm between actuator positions 5 and 45 mm.
; Runtime enters at each arm's nearer edge. This example starts fully open.
; Retain the 400 mm/min hardware vector feed and 20 mm/s^2 acceleration.
G90
G1 A90 B90 C90 D90 E90 H90 I90 J90 K90 L90 U90 V90 F800
G1 A10 B10 C10 D10 E10 H10 I10 J10 K10 L10 U10 V10 F800
G1 A90 B90 C90 D90 E90 H90 I90 J90 K90 L90 U90 V90 F800
G1 A10 B10 C10 D10 E10 H10 I10 J10 K10 L10 U10 V10 F800
G1 A90 B90 C90 D90 E90 H90 I90 J90 K90 L90 U90 V90 F800
