# Timiac · Kinetic Clock Studio

The public Timiac simulator combines the approved dense desktop / clock-first mobile appearance with the current movement engine and G-codes. The physical clock design is credited to Andrea Mancuso; the independent website and simulation are by Davoud Nikkhouy.

## Run and verify

Run these commands from `Simulator/`:

```powershell
npm ci
npm run dev
```

Open <http://localhost:4000>. Desktop keeps the large 3D clock, side controls and code editor. Phones open on the clock with **Actual time**, **+1 hr**, **+5 min** and **Struggle** shortcut buttons. **Struggle** starts a new random insect movement directly without opening a panel; **Time**, **Arms**, **G-code** and **About** open popup panels. The older Motion studies catalogue remains temporarily hidden.

Build and run the focused movement tests:

```powershell
npm run test:motion
npm run build
```

With the development or production-preview server running on port 4000, run `npm run test:ui` in a second terminal. Set `APP_URL` when testing another server. The browser tests cover the old shell, current axes and movements, modal accessibility, editor focus behavior and responsive camera fitting.

Only deploy `Simulator/dist/` to the existing Cloudflare Pages project `timiac`. Do not publish the repository root, paired credentials, hardware dashboards or firmware-update code.

## Movement behavior

**Next Hour** fully opens all arms, fully closes all arms, then shows the selected time. It has no flutter or intermediate Struggle; the existing 30% slower animation speed is retained. **Struggle** selects one of seven asymmetric insect patterns without an immediate repeat. In the existing **Clock pose / Time** panel, choose **1–7** and press **PREVIEW**, or use **RANDOM STRUGGLE** / the **S** key outside editable controls and modal navigation.

Each Struggle has three short movement beats with uneven arm groupings and **0–20 mm** openings, then returns directly to the selected time. Only the final time pose lets the minute arm fully extend; non-time arms close fully. The clock stays paused at that pose. Movements start from the visible pose without snapping. You can switch directly between pattern previews.

The speed selector is relative to the current **75% programmed playback base**: its default **1.0×** preserves that base. Nominal Struggle timing follows the two boards' vector-feed convention; the renderer does not model Marlin acceleration exactly.

The seven designs are Scramble, Limping scuttle, Caught wing, Kick and recoil, Body shiver, Escape crawl and Last wriggle. See [the pattern guide](../Docs/struggle-patterns.md). Firmware and simulator frames are generated together from `shared/struggle-patterns.json` using `python tools/generate_presets.py` from the repository root (also run by `build.py`).

Keep `src/clock-motion.js`, the generated hour/Struggle modules and `gcodes/` as the movement source of truth when changing the website appearance. Updating motion must not replace the approved shell or drop its mobile controls. The 3D path is calibrated so extension percentages `0`, `50` and `100` pass through `close.jpeg`, `middle.jpeg` and `open.jpeg` respectively.

## Simulator G-code dialect

- `A B C D E H I J K L U V` are arms 1–12, clockwise from 12 o'clock.
- Axis values are percentages of the physical **50 mm** stroke: `0` is folded and `100` is fully extended.
- `F` is feed rate in percentage points per minute, not an actuator axis.
- `G0` / `G1` move one or more arms simultaneously.
- `G4 P500` dwells for 500 ms; `G4 S1` dwells for one second.
- `G90` / `G91` use absolute / relative positions.
- `G28` retracts every arm to home.

```gcode
G90
G1 A10 B10 C10 D10 E10 H10 I10 J10 K10 L10 U10 V10 F1200
G1 A100 I75 F600
G4 P800
G28
```

The files in [`gcodes/`](gcodes/) are editable simulator motion studies. The renderer uses a slider-crank approximation calibrated visually from `mechanism.jpeg`; exact pivot spacing, drive-link length, finger length and actuator stroke are needed for a dimensionally exact model. Map simulator axes to the real Octopus firmware before sending programs to hardware, and retain appropriate homing, acceleration, end-stop, current and travel limits.
