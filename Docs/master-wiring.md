# Master board: E3F-DS30B2 NPN NC sensor

![Complete master-board wiring](master-wiring.svg)

[Open the scalable wiring diagram](master-wiring.svg) or [PNG image](master-wiring.png).

Keep the existing **24 V input and 24 V → 3.3 V converter**. The ESP32 and DS3231
stay on 3.3 V. Remove/unplug the ultrasonic module. Its existing **GPIO18 and
GPIO19 PCB traces stay unused**; the new sensor uses a new wire to **GPIO27**.
GPIO numbers mean the labels printed on the ESP32 board, not header positions.

## All connections to the master

| Connection | Destination |
|---|---|
| 24 V PSU positive | Existing master PCB input and converter IN+ |
| PSU 0 V | Existing PCB ground and converter IN− |
| Converter OUT+ (regulated 3.3 V) | ESP32 3V3, DS3231 VCC |
| Converter OUT− | ESP32 GND, DS3231 GND, sensor blue / PSU 0 V; all share ground |
| ESP32 GPIO21 | DS3231 SDA |
| ESP32 GPIO22 | DS3231 SCL |
| ESP32 GPIO27 | R2/R3 junction in the new sensor interface below |
| GPIO18, GPIO19 | Unused; do not repurpose the existing ultrasonic traces |
| RTC SQW / 32K | Not connected |
| Master → ESP-12 bridges | Wi-Fi; no UART/control wires between them |

The DS3231 and its I²C pull-ups stay at **3.3 V**, not 5 V. Do not feed 24 V
into an ESP32 pin. The existing converter, not the ESP32 board, receives 24 V.
Switch off/disconnect 24 V before powering the board from USB for flashing,
unless the PCB has verified isolation between its USB and external supplies.

## Three-resistor sensor interface

Use **1% tolerance, ¼ W** resistors. No optocoupler is needed for this fixed
24 V circuit. It does not provide electrical isolation or surge protection.

```text
24 V + ───────────── brown: sensor +V
  │
  └── R1 4.7 kΩ ── A ── R2 68 kΩ ── B ───────── GPIO27
                  │                 │
                  │                 R3 10 kΩ
                  │                 │
               black: OUT           GND

PSU 0 V / PCB GND ── blue: sensor 0 V
```

**Only B connects to GPIO27.** A is the black sensor-output node and can be
around 22–24 V. Sensor brown goes to 24 V; it must not be powered from the
3.3 V converter. Sensor blue, PSU 0 V, converter output ground and ESP32 GND
must have a common reference. There is no internal GPIO pull-up in this firmware.

For an NPN normally-closed sensor:

| Sensor state | Output transistor | GPIO27 test point B |
|---|---|---|
| Clear / no hand | Conducting to ground | Near 0 V: LOW |
| Hand detected | Open / not conducting | About 2.90 V: HIGH |

When the output is open, all three resistors form the supply-to-ground chain:
`24 × 10 / (4.7 + 68 + 10) = 2.90 V`. The 68 kΩ/10 kΩ divider also keeps an
unexpected 24 V on A to about 3.08 V at B. R1 dissipates about 0.123 W while
the sensor conducts. These values are for the existing **regulated 24 V**
supply, not an arbitrary supply anywhere in the sensor's 10–30 V rating.

Before connecting B to the ESP32, power the converter and sensor, measure the
converter output at 3.3 V, and measure B relative to PCB GND. With no hand B
must be below 0.8 V. With a hand at the intended distance B should be 2.6–3.2 V.
Check the actual unit is **NPN NC**, not PNP or NO. Do not connect B if these
checks fail. Do not swap brown and blue to change the switching polarity.

## Detection and commissioning

This Heschen is a **diffuse-reflection infrared switch**, not a laser/LiDAR
distance sensor. It supplies one detected/clear signal; it cannot report
centimetres, hand direction, or gesture shape. The supplier advertises adjustable
10–30 cm detection, calibrated with a white paper target. Set it using an actual
hand at the intended distance. The adjustment changes range/sensitivity, not the
beam angle. A narrow, unobstructed optical path still has to be checked on the clock.

Heschen's product title and wiring photograph identify NPN NC; its description
also contains an inconsistent NO entry. Follow the **actual NC label and the
voltage check above**. Test that nearby metal and all stationary clock poses
leave the sensor clear. Reflections, target colour and mechanical obstructions
can affect this sensor; an optical sensing-angle specification was not provided.

The firmware reads GPIO27 every 20 ms. After homing, and while the release clock
is running, clear the sensor continuously for **500 ms**, then present a hand
for **200 ms**. This starts **Curious creature** once. A held hand cannot repeat
it. Withdraw the hand after the animation to rearm. Hand arrivals during motion,
Stop, faults, lost board connection, or a paused release clock are discarded.
The development master also supports gestures while idle and homed.

Both local dashboards show **Hand sensor: Detected / Clear**, including before
homing and during movement. `/api/status` includes `proximity.rawHigh`,
`detected`, `armed`, `ready`, and the GPIO number. `distanceCm` is always null.
`rangeMaxCm: 30` is the nominal rating, not a measured distance.

Fit the complete resistor interface, including R3, before running this firmware.
Without the adapter, GPIO27 is floating and its reading is not meaningful.
An open/broken NC output wire or sensor power loss can look like a detected
hand. The single signal cannot distinguish those faults. Clear-first arming
prevents a trigger on startup with a missing sensor when the adapter is intact,
but a wire failure after
arming could trigger once. This is a gesture input, not a safety interlock.

Only flash the master for this change:

```powershell
git pull
python run.py master_release --port COM10
```

The ESP-12/Octopus wiring and firmware stay unchanged. The new default is
`TIMIAC_PROXIMITY_PIN=27`, `TIMIAC_PROXIMITY_ACTIVE_HIGH=1`. Disable gestures with
`TIMIAC_PROXIMITY_ENABLED=0` and rebuild if the new sensor is not yet connected.

Sources: [Heschen product and wiring photographs](https://heschen.com/products/heschen-e3f-ds30b2),
[ESP32 electrical characteristics](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf),
[DS3231 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/ds3231.pdf).
