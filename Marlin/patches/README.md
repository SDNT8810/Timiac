# Concurrent homing port

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
seek distances and per-motor stopping behavior are the same as the port.
