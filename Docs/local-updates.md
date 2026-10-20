# Local hotspot, cloud and USB firmware updates

**2026-10-20.4** adds the Hand sensor panel at control.timiac.it, last trigger
times, ignored-event reasons and persistent sensor diagnostic entries. Install
the prepared master and dashboard with the command below, or use Full update.
The GPIO34 wiring and 40/60 mV sensing introduced in 2026-10-20.3 remain the same.

For the **2026-10-20.3 GPIO34 analog sensor update**, move only the old GPIO27
signal wire to GPIO34, retaining all installed resistors and ground connections.
Pull while online, then connect the PC to Timiac Wi-Fi and run
`python run.py master_release --host 192.168.4.1` to install the prepared master
and dashboard without compiling. See [sensor commissioning](master-wiring.md)
for the 40/60 mV thresholds, ADC-floor handling and actual-reading check.

For the included **2026-10-20.2 Octopus confirmation recovery fix**, update the
complete package so the master and both ESP bridges are installed before the
Octopus SD updates. In the Timiac folder run `git pull` while online, then
connect the PC to **Timiac** Wi-Fi and run:

```powershell
python local_update.py --check
python local_update.py --target all --host 192.168.4.1
```

Both Octopus cards must remain installed and power must stay connected. The
command uses the prepared, checksum-verified images without compiling them.
Version 2026-10-20.2 also includes the earlier cloud-memory/Wi-Fi recovery fix.
See [the SD update diagnosis](sd-update-timeout.md) for the activation/receipt
failure observed on October 20.

On the router LAN use the master's current address, for example
`--host 192.168.1.4`. If `--check` reports missing local-update support or the
master cannot be reached over Wi-Fi, install by USB with
`python run.py master_release --port COM5`, using its actual COM port.

Release **2026-10-19.2** adds local updates alongside the existing cloud and USB
methods. Clock animations, motor settings, wiring, saved router credentials and
the hardware identity remain unchanged. Local and cloud updates share the same
single-update lock, Stop acknowledgements, SHA-256 checking, board restart
verification, parked clock state and persistent diagnostics.

## Install the feature once by USB

From the Timiac project folder, with the master ESP32 connected by USB:

```powershell
git pull
python run.py master_release --port COM5
```

Use the actual master COM port. The command installs both application and local
dashboard. The ESP-12 and Octopus boards do not need another wired installation
for this feature if their existing Wi-Fi/SD update support is already installed.
Keep a microSD card installed in each Octopus.

## Future updates over the local hotspot

First run `git pull` while the PC has internet access. Then connect the PC to
**Timiac** Wi-Fi. No internet or cloud connection is needed during installation.

For the master and its dashboard:

```powershell
python run.py master_release --host 192.168.4.1
```

For every board and the dashboard automatically, in sequence:

```powershell
python local_update.py --target all
```

The complete prepared package is checked before pausing or flashing anything.
The order is master application, ESP1, ESP2, Octopus1, Octopus2, dashboard.
There are no firmware file pickers or credentials to copy. The updater reads
the clock's existing pairing configuration from the repository. To compile
your current source before checking/installing, use
`python local_update.py --target all --build` instead; install the build
requirements/dependencies while internet is available first.

Keep the PC awake and connected until completion. If Windows Firewall asks,
allow Python on this local network. The updater binds a temporary image server
only to the PC interface used to reach the clock; only the selected master IP
can download its randomly named image. It closes the server when finished.

Individual targets are also available:

```powershell
python run.py esp1_release --host 192.168.4.1
python run.py esp2_release --host 192.168.4.1
python local_update.py --target octopus1
python local_update.py --target octopus2
python local_update.py --target dashboard
```

If PC and clock share the router Wi-Fi, replace the host with the clock's router
address, for example `--host 192.168.1.4`. Local update commands work through
either interface. The local dashboard also lists these commands.

After installing, use **Maintenance > Calibrate arms**, then **Show time**.

## Other methods remain available

- **Cloud:** use Full update or the individual component updater at
  https://control.timiac.it/. The master needs a working cloud connection.
- **USB:** all existing `python run.py ... --port COM5` commands remain valid;
  Octopus microSD installation commands remain valid too.

## Recovery and limits

If the updater prints `WinError 10053` after verifying the images, it lost a
connection to the master. Before `Serving firmware...` appears, no installation
has started. The error alone does not identify whether Windows security software,
a network interruption or the device's HTTP connection caused it.

The updater retries read-only GET requests up to three times. It never retries
a Stop or installation POST; a lost install reply is reconciled using the exact
existing job receipt. To check connectivity without pausing/updating the clock:

```powershell
python local_update.py --check
```

On the same PC, open http://192.168.4.1/api/firmware/local in a browser. If this
works but Python still fails, allow the Python executable printed in the error
through the PC firewall/security software and check any VPN. Do not disable the
entire firewall. The image-server firewall permission applies later, when the
master downloads the files from the PC. If using router Wi-Fi, use the current
clock address, for example `python local_update.py --check --host 192.168.1.4`.

An interrupted transfer is not automatically resubmitted. SHA-256, role,
hardware identity, revision and restart receipt must agree before success is
reported. Local master updates also require the replacement image to retain
local-update support. A filesystem-only failure can be repaired with
`--target dashboard`: the local update API is compiled into the application and
does not depend on working dashboard files.

The local request has a boot-bound, one-use HMAC authorization covering all
metadata, the PC port and the image token. The master derives the image host
from the actual requesting PC; arbitrary external image URLs are not accepted.
Router reconnection scans are held during firmware updates to keep the hotspot
stable when the upstream router is unavailable.
Cloud TLS authentication remains enabled. Stop or a conflicting update cannot
activate a partial image or start two flash writers.

This removes USB from normal firmware updates and recovery from cloud outages.
USB can still be necessary if the application cannot boot, flash/bootloader is
damaged, or a future release changes the partition layout or hardware identity.
Automated tests/builds cannot establish successful operation on the physical
clock; confirm the first complete local update after the USB installation.
