# Cloud and saved Wi-Fi recovery — 2026-10-20.1

The supplied diagnostics from software 2026-10-19.4 show the router connected
at 192.168.1.4 with RSSI -64, while cloud HTTPS fails with
`TLS -32512: SSL - Memory allocation failed`. There were 684 failed syncs and
the last successful sync was approximately 5 hours 9 minutes earlier. Forgetting
and re-entering the router credentials cannot correct this TLS allocation failure.

The previous memory checks used `ESP.getFreeHeap()` and `getMaxAllocHeap()`.
In the pinned Arduino ESP32 SDK these count all internal RAM, including RAM
that only supports aligned 32-bit accesses. The TLS allocator requires internal,
byte-addressable RAM. Consequently, the reported 120,628 free bytes and 38,900-byte
largest block did not establish that TLS could allocate its buffers. General-heap
fragmentation and concurrent status traffic further reduce usable memory.
See the [SDK allocator](https://github.com/espressif/esp-idf/blob/v4.4.7/components/mbedtls/port/esp_mem.c).

This release installs the supported Mbed TLS allocation hooks once at startup.
Two full TLS records (33,434 bytes with the pinned SDK) have dedicated storage;
cloud sync and secure firmware downloads share them without overlapping TLS
sessions. Certificate and crypto allocations use the SDK's internal byte-memory
capabilities. Network admission leaves at least 48 KB of usable heap and an 8 KB
block for the remaining handshake and local traffic. Both network worker stacks
are 10 KB; the measured cloud stack had over 6 KB unused with the old 12 KB stack.
Local status requests preserve additional memory during a cloud exchange.

Wi-Fi retries continue indefinitely with 15/30/60-second backoff after failed
15-second attempts. Retries wait for active motion/Stop handling to finish and
pause during firmware updates. A paused/uploading program no longer prevents
recovery indefinitely. The saved credentials survive failures and reboot;
only Forget removes them. A new router connection resets the cloud backoff.

## Install

Only the master and its matching dashboard need this fix. From the Timiac folder,
connect the PC to the clock's hotspot or router LAN and run:

```powershell
python run.py master_release --host 192.168.4.1
```

Use `--host 192.168.1.4` when that is still the clock's router address. A local
update requires local-update support in the installed firmware. If it is missing
or the master is unreachable, connect its USB cable and use its actual COM port:

```powershell
python run.py master_release --port COM5
```

For development mode use `master_dev`. Both methods preserve saved router
credentials. The update pauses motion; resume the clock after installation.
No ESP bridge or Octopus update is required for this change.

## Verify on the clock

Check that the dashboard reports software `2026-10-20.1` and cloud connected.
Reboot the router, leave it off for several minutes, then restore it. The master
should reconnect without re-entering credentials once motion is idle, then sync
the cloud immediately. Allow up to 75 seconds for the capped retry/attempt cycle,
plus DHCP and cloud handshake time. Repeat while a program is paused.

Diagnostics now report byte-addressable heap values and
`cloud.tlsReservedBytes`, `cloud.tlsAllocationFailures`, and
`cloud.tlsLastFailedAllocationBytes`; Wi-Fi status includes attempt/backoff data.
The allocation counter should remain unchanged through the router test.

Host tests cover failed general-heap allocation, concurrent record cleanup,
1,000 record reuse cycles, saved-network recovery after a long outage, paused
programs, firmware-update exclusion, Forget, timer wrap, and immediate cloud
retry. Firmware builds and host tests do not replace the router test on hardware.
