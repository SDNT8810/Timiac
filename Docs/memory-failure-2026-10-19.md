# Connection-loss investigation - 2026-10-19.4

The supplied cloud diagnostic export records two explicit allocation failures
in master session `acab70c92df4-e1e30cba`, running 2026-10-19.3:

| Master uptime | Recorded event |
| --- | --- |
| 72.066 seconds | `Bridge packet rejected: NoMemory` |
| 73.337 seconds | `TLS -32512: SSL - Memory allocation failed` |
| About 88.9 seconds | Both bridge links disconnect during Struggle; the clock stops |
| 99.117 seconds | Another `Bridge packet rejected: NoMemory` |

The packet decoder explicitly closed a bridge socket on deserialization failure.
That can turn memory pressure into a real link loss and a required safety stop.
The same ESP bridge boot IDs return after reconnection; those reconnects do not
show ESP reboots. This does not prove that every recorded disconnect had the
same cause, or that heat never contributed to earlier failures.

The later snapshot has 122,296 bytes free and a lifetime minimum of 41,868.
Neither value disproves transient allocation failure or heap fragmentation.
ArduinoJson's `NoMemory` can also mean an undersized JSON pool. A representative
full board packet fits the existing pool; the regression test reproduces the
observed failure by denying its dynamic allocation.

Both driver reports have zero current thermal-warning/history masks in this
capture. The strings `143C` and `157C` in M122 diagnostic rows are named threshold
rows, **not measured driver temperatures**. The bridge applications still report
2026-10-17.1, and the native Octopus identification says it was built October 17;
updating the master alone did not install the October 19 holding-current change.

## Firmware changes

- Reserve the 4 KB bridge decoder pool statically at boot. Valid incoming
  heartbeat/status packets no longer need a new JSON heap allocation. Pool
  reuse is serialized; malformed packets retain the existing disconnect/Stop.
- Send the existing cloud body through HTTPClient's byte-buffer overload. Its
  `POST(String)` overload copies the complete payload during TLS negotiation.
- Destroy the 12 KB snapshot document before submitting work to the TLS task.
  Check both free heap and the largest allocatable block before starting TLS.
- Serialize cloud polling and firmware download sessions. Cloud progress still
  resumes while a bridge/Octopus verifies or restarts. Low-memory waits are
  bounded, and local HTTP updates use a smaller memory budget than cloud TLS.
- Include largest-block and cloud connection information in diagnostics, and
  persist rejected-packet events. Large diagnostic downloads defer during cloud
  requests; the compact `?brief=1` response remains available.

Movement current, holding current, animations, thermal protection, clock
scheduling and hardware identity retain their 2026-10-19.3 settings. USB, local
hotspot and cloud update routes remain enabled.

## Install without opening the clock

With the PC online, pull the repository. Then connect it to **Timiac** Wi-Fi and
run this from the Timiac folder:

```powershell
python local_update.py --target all
```

The master already reports local-update support. This installs the master first,
then both bridges, both Octopus boards and the dashboard. Leave both microSD
cards installed, keep power connected, and allow Python through Windows Firewall
if prompted. Afterwards use **Maintenance > Calibrate arms**, then **Show time**.

Regression tests reproduce the allocation failure, exercise 1,000 valid packets
with JSON heap allocations denied, retain malformed-packet stops, and verify
snapshot ownership, memory deferral, cloud/OTA exclusion and progress reporting.
Builds and host tests cannot establish the clock's physical thermal margin or
replace an endurance run on the installed hardware.

References: [ArduinoJson NoMemory](https://arduinojson.org/v6/api/misc/deserializationerror/)
and [ESP-IDF heap diagnostics](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/system/heap_debug.html).
