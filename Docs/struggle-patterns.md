# Seven Struggle studies

In the simulator, choose a number from **Struggle studies** at the top left,
then press **PREVIEW**. You can change studies immediately during playback.
The main **STRUGGLE** button, random sequence card and **S** key choose randomly.
Playback uses the same fixed **75%** speed as the hardware. There is no speed selector.

| # | Pattern | Movement character |
|---|---|---|
| 1 | Scramble | Different scattered legs kick and recoil in changing groups. |
| 2 | Limping scuttle | An uneven cluster crawls around one side with a dragging leg. |
| 3 | Caught wing | One side flutters twice while the other legs make small twitches. |
| 4 | Kick and recoil | Two unequal legs kick, pull back and kick again with answering twitches. |
| 5 | Body shiver | Small irregular leg tremors break into one larger kick. |
| 6 | Escape crawl | A broken wave changes direction and tries to crawl away. |
| 7 | Last wriggle | A lopsided twist decays into smaller loose movements. |

Each pattern uses three movement beats, with **0–20 mm openings** and no
programmed pause, then returns directly to the selected time. In that final
pose, only the time arms may open beyond 20 mm; all other arms close fully.
Entry follows the current pose, so an arm already open beyond 20 mm first
moves down into the permitted Struggle range. There is no position jump.

The physical **Struggle** button and GPIO27 hand sensor use the same random
selection. The previous pattern is excluded; otherwise each of the remaining
six is eligible. The first selection can be any of the seven. A selected
pattern is fixed for the whole job, including validation and command retries.
A held hand does not repeat; withdraw it after the job to rearm the sensor.

Both entry points require homing and valid clock time. A button command pauses
automatic clock tracking, as before. A sensor animation leaves tracking running
and returns to the captured time. Open also keeps the clock parked; it moves to full opening without homing.

Frames are authored in millimetres in [shared/struggle-patterns.json](../shared/struggle-patterns.json).
`python tools/generate_presets.py` generates the C++ frames and simulator data
together. Every Struggle segment runs 30% faster, including the final time pose:
the vector-feed ceiling is 520 mm/min before the fixed 75% override (390 mm/min
in use). Current and 20 mm/s² acceleration remain unchanged. Simulator timing follows
the same board feed convention but does not model acceleration exactly.

Run locally:

```powershell
cd Simulator
npm install
npm run dev
```

Open <http://localhost:4000>. After reviewing the patterns, flash the master with
`python run.py master_release --port COM10` from the project root. This package also doubles homing speed and changes the hardware identity.
Update both bridges and both Octopus boards locally too; follow [instruction.md](../instruction.md).
