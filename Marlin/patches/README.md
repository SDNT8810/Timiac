# Timiac Marlin patches

`legacy-working-tree.patch` archives all three local edits in the pinned Marlin
checkout. It is stored with the original UTF-8 text and verified against that
checkout. The production build uses clean upstream sources and the active
profiles and patches below. To restore the archived edits in a clean checkout:

```powershell
git -C Marlin/marlin-2.1.2.7 apply ../patches/legacy-working-tree.patch
```

`motion-speed.patch` makes the operation's `M220 S20` through `S100` override
apply to G28 too. G28 captures the percentage before disabling normal feed
scaling, applies it through `homing_feedrate()` to concurrent release/seek and
single-axis homing/bump, and restores the temporary homing scale on every exit.
100% preserves the existing 120 mm/min homing profile. This requires the
protocol 7 machine package; the bridges send M220 before every operation.

`concurrent-homing.patch` ports Lumiac's `SPIDER_CONCURRENT_HOMING` to the
clean Marlin 2.1.2.7 sources staged by `build.py`. Origin file hashes are in
`lumiac-homing-source.json`; Lumiac was read but not edited. No sibling
project is required to rebuild Timiac.

The port includes G28 routing, initially pressed switch release, per-axis
step-pulse latches, validation of all six switch hits, and quick-stop
cancellation tracking. It does not include Lumiac's lighting, fan, SPI,
Bluetooth, or realtime pause changes. Timiac retains UART TMC2209 drivers,
physical switches, disabled SD runtime, and emergency M112 during homing.

The compile-time contract checks six independent axes and switch pins.
Host tests exercise the patched homing routine with pressed, stuck, missing,
and interrupted switches; latch tests cover all 720 trigger orders.
The firmware build still requires physical validation on the installation.

Timiac additionally names the affected X/Y/Z/A/B/C axes in release, stuck-switch,
and missing-switch diagnostics. This changes reporting only; the release and
seek behavior and per-motor stopping remain as in the port; the configured
initial release distance is now 5 mm.

`six-axis-junctions.patch` fixes direction normalization in the pinned Marlin
planner. Its XYZ-first feed distance cannot normalize a vector that also includes
independent A/B/C motion. Selecting Marlin's full-vector normalization for more
than three axes keeps the junction dot product in range and avoids false full
reversals. Feed-distance conventions and per-axis acceleration/speed limits are
unchanged. `tests/test_six_axis_junctions.py` applies this patch to pinned source
and compiles its actual normalization code against mixed-axis regression cases.
The current profile pairs this fix with 0.3 mm junction deviation for less braking
between animation frames.
