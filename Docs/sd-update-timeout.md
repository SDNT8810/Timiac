# SD update ACK timeout - 2026-10-19.5

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
