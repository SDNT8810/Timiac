; User example, including explicit homing first.
; Use the 1 mm smoke test before this larger movement.
; Inverted visual targets: 0 mm = open/home, 48 mm = closed.
G28 X1 X2
G90 X1 38 X2 13 F350
G4 P500
G90 X1 48 X2 48 F180
