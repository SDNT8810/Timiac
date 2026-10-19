# Recover and diagnose firmware updates

The 2026-10-17.1 release fixes repeated Stop messages cancelling an update,
partial TCP writes being treated as upload failure, and secure download setup
blocking the master's control loop. A bridge also saves the exact successful
job/hash before reboot, so losing its final HTTP reply does not hide success.
Octopus activation is sent once and gets up to two minutes to restart.

Full update builds and validates the complete bundle, then installs the master
application first, ESP1, ESP2, Octopus1, Octopus2 and finally the master dashboard.
Progress extends a three-minute transfer lease; a stuck phase does not. The
overall per-board transfer limit is twenty minutes. A power cycle cancels an
unconfirmed job rather than replaying it against a new controller session.

## If the old master is offline or cannot finish its own update

Make one USB installation from the current repository:

```powershell
git pull
python run.py master_release --port COM10
```

Use the master's actual COM port. This installs the prepared application and
matching local dashboard; the saved router settings in NVS remain available.
If it remains offline, connect to the master's local Wi-Fi, open
`http://192.168.4.1`, and check **Router / hotspot Wi-Fi** for router connectivity.
Save `/api/status` and `/api/diagnostics` before another reset; the status includes
the exact gateway error.
Verify the master reports `2026-10-19.1`, then use **Full update** to install the
rest. Leave both Octopus microSD cards installed. After the complete update,
use **Maintenance → Calibrate arms**, then **Show time**.

## Logs that survive a reboot

The master retains the latest 24 critical events in a checksummed NVS journal.
Boot/reset reason, bridge link loss, faults and OTA checkpoints survive a power
cycle. The last update checkpoint includes job, target, phase, received bytes,
size, hash, memory and boot ID. Phase changes are saved immediately; transfer
progress is saved every 64 KiB or ten seconds of changing progress. A checkpoint
is saved before final verification/activation. ESP bridges retain their last
update checkpoint in LittleFS. Normal heartbeat traffic is not written to flash.

The authenticated remote panel archives received events for 30 days, including
legacy firmware's reported update phases and byte counts. **Maintenance →
Download diagnostic log** includes the last known device status, event history
and update receipts, even when the master is currently offline. Saved NVS events
are sent in rotating batches after reconnect, so allow about forty seconds for
all retained events to appear. The local equivalent is
`http://192.168.4.1/api/diagnostics`.

RAM messages never saved or uploaded by older firmware cannot be recovered after
that firmware loses power. The journal is bounded and a failed flash write can
lose its newest record; earlier records are checked and retained independently.
