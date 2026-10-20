# Octopus SD update failures

## Lost confirmation after activation - 2026-10-20.2

The October 20 diagnostic attached after the failed Full update shows Octopus 1
reached **127,820 / 127,820 bytes**, restarted and reported an October 20 build
instead of its previous October 17 build. ESP1 then restarted with a new boot ID
and `External System` reset reason. The master waited for the SD job's receipt,
but the bridge restored its older ESP application receipt. The gateway eventually
reported no progress; the master's later error was a five-minute confirmation
timeout. The trace supports activation followed by lost confirmation, rather
than an SD write failing at that byte count. It does not establish what caused
the external bridge reset. Full update stopped at Octopus 1, so Octopus 2 and
the dashboard were not attempted in that sequence.

Version **2026-10-20.2** saves the exact Octopus job, target, SHA-256, size, CRC,
revision and machine identity in the bridge's LittleFS after full SD readback
verification, before sending the one-shot activation command. After a bridge
reset it restores the pending confirmation and waits for the running Octopus
to identify the expected revision. Recovery never replays activation. If the
receipt cannot be saved, activation is not requested.

The master accepts completion only for the matching job and image metadata,
with a fresh report of SD update support and the expected running Octopus
revision. A changed bridge boot with a missing receipt reports that specific
condition instead of waiting for the generic five-minute timeout. Diagnostics
now expose each board's `octopusSoftware` separately from the ESP software.

Host tests reproduce a bridge reset between activation and confirmation, lost
HTTP acknowledgements, incorrect receipt metadata, old running firmware,
truncated/invalid receipt files and failed atomic receipt replacement. All
firmware roles are built before release; a physical update still needs to be
verified on the clock.

Install the new master and both bridges before retrying the SD updates. Full
update and `python local_update.py --target all` already use this order. Keep
power and both cards connected; formatting the cards is not indicated by this
trace. Start a new update job: an expired job's missing old receipt cannot be
reconstructed by installing this fix.

## SD block ACK timeout - 2026-10-19.5

The October 19 cloud record shows successful ESP1 and ESP2 updates to
2026-10-19.4, followed by `Octopus SD acknowledgement timed out (phase 2)` on
Octopus 1. Its last acknowledged offset was **512 / 127,820 bytes**. Phase 2
means waiting for the next block ACK, before whole-file verification or
activation. The running Octopus application was not replaced by that attempt.

512 bytes is an SD sector/cache boundary. The next partial-block write may
flush the preceding sector. That makes SD latency relevant, but the old trace
does not prove a defective card, SDIO failure, native reset or lost serial data.

The sender also had a concrete transport weakness: 64 binary bytes expand to
128 hexadecimal characters, plus the command/offset/CRC. The resulting UART
line is over 150 characters. The pinned STM32 Arduino core defaults to a
64-byte RX ring, with only 63 usable unread bytes. If Marlin's main loop is
busy, the interrupt handler silently drops bytes when that ring is full.

## Changes

- Send **16 binary bytes per command**, keeping the entire command including
  worst-case offset, CRC and newline within 63 bytes. The existing native
  receiver accepts blocks up to 64 bytes, so this works with its installed
  firmware and does not require a new wired bootstrap.
- Prefix the initial/abort handshake with a newline to finish any truncated
  previous command. Fence stale parse errors until `TIMIAC_FW_ABORTED` proves
  that a fresh session can begin.
- Allow 10 seconds for block/begin ACKs and 30 seconds for complete SD readback.
  Retries remain bounded and blocks remain offset/CRC checked and idempotent.
  Activation is still sent once, only after complete verification.
- Record the last Octopus reply, byte offset and named stage on failure. A
  native startup banner during transfer now produces a specific reset error
  and updates the reset/position tracking instead of disappearing into a timeout.

A regression test failed with the old sender against a 63-byte receive queue.
With the new sender, both production protocol ends complete a 32 KB image,
CRC/readback verification and activation while the simulated CPU pauses for
each whole command. It also starts with an abandoned partial line. Other tests
cover delayed ACKs, reset detection, exhausted retries, SD faults and activation
without a final ACK. These do not replace a physical test of the SD hardware.

## Install

The new ESP bridge firmware must be installed **before** retrying Octopus SD
updates. Full update already uses that order. After this release is pushed, use
**Full update** again, or pull the prepared images while online, connect the PC
to Timiac Wi-Fi and run:

```powershell
python local_update.py --target all
```

Keep both cards inserted and power connected. Afterwards use **Calibrate arms**
and **Show time**. If the failure repeats, the new offset/last-reply/reset error
will distinguish more of the possible causes. Do not format or replace a card
solely on the evidence of the original generic timeout.
