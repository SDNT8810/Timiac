# Dodeca Motion Lab

A browser simulator for a twelve-finger kinetic clock. `mechanism.jpeg` is a side-view motion study of one arm overlaid at three different angles. The clock uses twelve identical copies of that complete arm, like twelve fingers. Each arm pivots at its base knuckle out of the clock-face plane; the main viewport shows all twelve front projections, while the inset shows one selected arm live against three ghosted reference angles.

## Run

```powershell
npm install
npm run dev
```

Open <http://localhost:4000>.

Use the **3D** viewport to orbit, pan, and zoom around all twelve mechanisms while G-code is running. Switch to **FRONT** for the clock-face projection. The 3D kinematic path is calibrated so actuator positions `0`, `50`, and `100` pass through `close.jpeg`, `middle.jpeg`, and `open.jpeg` respectively.

## Simulator G-code dialect

- `A` through `L` are arms 1–12, clockwise from 12 o'clock.
- Axis values are linear actuator stroke in millimetres: `0` folds the finger up out of the face plane and `100` lays it open in the face plane.
- `G0`/`G1` move one or more arms simultaneously.
- `F` is feed rate in millimetres per minute.
- `G4 P500` dwells for 500 ms; `G4 S1` dwells for one second.
- `G90` uses absolute positions; `G91` uses relative positions.
- `G28` retracts every arm to home.

Example:

```gcode
G90
G1 A10 B10 C10 D10 E10 F10 G10 H10 I10 J10 K10 L10 F1200
G1 A100 G75 F600
G4 P800
G28
```

The files in [`gcodes/`](gcodes/) are editable motion studies. The simulator uses a slider-crank approximation calibrated visually from `mechanism.jpeg`; exact pivot spacing, drive-link length, finger length, and actuator stroke are needed for a dimensionally exact model. The `A–L` convention is a simulator-level planning dialect; map these axes to the actual stepper names/macros used by your Octopus firmware before sending a file to hardware. Add homing, acceleration, end-stop, current, and travel limits for the physical mechanism.
