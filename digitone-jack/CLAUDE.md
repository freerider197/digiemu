# Digitone MK1 custom firmware ("JACK") - guide for Claude Code

Read this first. Details live in `docs/` (index at the bottom). This file was written on
2026-10-15 from a long claude.ai chat; paths like `/tmp/...` in the old notes were a sandbox
that no longer exists.

## What this is

Custom firmware for **Jack Fresia's Elektron Digitone MK1, OS 1.44** (DJ/producer, Amsterdam, Mac M1 on
macOS 12). The firmware is the stock OS file plus small C/assembly "mods" linked by **elekloader**
(github.com/irpina/elekloader). Everything here is MIDI-track / UI work; nothing touches the audio engine.
One person's instrument: every feature was requested by Jack, tested in an emulator, then flashed by him.

Mods (details in `docs/ARCHITECTURE.md`):

| Mod | Version | What it does |
|---|---|---|
| cc-hold | 2.0 | one-byte patch: STOP then PLAY no longer resends unchanged CCs |
| ob6-nrpn | 1.4 | on MIDI channel 14 every CC becomes a full NRPN for his OB-6 (0-255 ranges) |
| chord-knobs | 2.13 | MIDI TRIG page: NOT2 / NOT3 knobs (+/-12 semitones) in slots G and H |
| lfo-vel | 2.1 | LFO destination "AT" modulates velocity; HUM velocity humanizer knob (slot E) |
| rand-seq | 5.2 | one-press randomizers SCL / UNDO / RND / PIT, reset switch, LEN+ (random longer trig lengths) |

**Current build: JACK18c** (sha256 `7868c91f...`). Last build confirmed on his unit: JACK17 (he is running it;
whether the page-flicker fix worked was not reported). JACK18, 18b, 18c are **not yet confirmed on the unit**.
Full list: `docs/VERSION_HISTORY.md`.

## Hard rules

1. **Never put his Digitone at risk.** elekloader never touches the bootloader; recovery is always
   hold FUNC while powering on, press TRIG 4, send the **stock** `Digitone_and_Digitone_Keys_OS1.44.syx`
   with Elektron Transfer. Nothing gets flashed that has not passed build + lint + emulator tests.
2. **Say what was verified and what was assumed.** The emulator reliably shows the screen and the firmware's data
   structures, but real note lengths, velocities and MIDI-track output could not be confirmed in it (one check on
   2026-10-15 saw no MIDI messages from a playing MIDI track - inconclusive, the pattern had a single trig and the OUTPUT TO
   setting was not verified). Treat those as hardware-only until shown otherwise, and say so every time.
3. **Keep every superseded build** (he keeps them as backup history). Never delete or tidy old `.syx` files.
4. Elektron's firmware is not ours to distribute: `.syx` files are git-ignored. He supplies the stock file.
5. The full cold-boot comparison against stock was last run on **JACK13**. He asked not to rerun it for now;
   do not run it unasked, but mention that it is stale when reporting a build.

## Commands

Prereqs: Python 3.9+, an elekloader checkout (see `docs/BUILD_AND_TEST.md` for the exact version used), the m68k
cross toolchain (`binutils-m68k-linux-gnu gcc-m68k-linux-gnu` on Debian/Ubuntu; on macOS check elekloader's
`docs/BUILDING.md`), and the stock file. Core: `build/elemods/core-2.0a-os1.44.elemod` (sha256 `5707a38e...`, the
elekloader v0.4.0 release asset `core-dn1-2.0a-os1.44.elemod`).

```bash
STOCK=Digitone_and_Digitone_Keys_OS1.44.syx
for d in cc-hold-2.0 ob6-nrpn chord-knobs lfo-vel rand-seq; do
  python3 -m elekloader.sdk.build src/$d --stock $STOCK --out src/$d/out
done
python3 -m elekloader.patch --stock $STOCK \
  --mod build/elemods/core-2.0a-os1.44.elemod \
  --mod src/cc-hold-2.0/out/cc-hold-2.0.elemod \
  --mod src/ob6-nrpn/out/ob6-nrpn-1.4.elemod \
  --mod src/chord-knobs/out/chord-knobs-2.13.elemod \
  --mod src/lfo-vel/out/lfo-vel-2.1.elemod \
  --mod src/rand-seq/out/rand-seq-5.2.elemod \
  --out Digitone_OS1.44-JACK18c.syx --version JACK
```

Lint every mod against the others before handing anything over (loop in `docs/BUILD_AND_TEST.md`), then
`shasum -a 256` the result and give Jack the first 8 characters to compare.

## Working agreement with Jack (his stated preferences)

- **Short, straight to the point.** He dislikes verbosity and has twice said Claude wasted tokens. Small tweaks:
  one best-guess build, not several test variants.
- **Before any larger build:** a short summary of what you understood plus a rough token/effort estimate, then
  wait for his go. For new firmware work ask whether **he builds on his Mac or Claude builds and sends the file**.
- Do what was asked, verify it, report, stop. **No unrequested features, tests, files or doc updates** - list ideas at
  the end instead. When he says "move" a control, move it (do not copy it); state which of two readings you take.
- Commands go in **one plain bash code block** for the whole sequence, not step by step. Verify compatibility
  before handing over build commands.
- Hand-written UI text: **avoid the letter g in header popups** (this firmware's font draws a lowercase g like a 9,
  "Length" showed as "Len9th"). Existing strings are chosen with that in mind.
- He plays percussion/single notes, always works with **one pattern**, never uses aftertouch, never sends program
  change/SBNK/BANK from MIDI tracks. Those facts justify several design decisions.

## Code conventions that bite (learned the hard way)

- `mod.json` is authoritative for versions. Header comments inside the `.c` files are stale (e.g. rand-seq says 4.3).
- Every added knob draws, and takes events, **only while its slot still shows the stock dotted slot box**
  (`slot_stock()`): that is how windows, popups and the black power-saving screen are handled.
- The firmware's "current page" byte lags the screen by **one frame**; mods use the page keys (ids 20-25) to know
  the new page early (`page_now()`, `page_key()`, `page_tick()` in each mod). Do not read `PAGE` directly.
- Key events: bit 1 = press, bit 2 = FUNC held, bit 8 = auto-repeat. **Ignore repeats** so one press = one action.
- Per-step data must be written to both the **live** block (`ed+0x10`) and the **saved** block (`ed+0x20`).
- Mods are freestanding (no libc); use the `cp()` helper with volatile stores for copies; RAM is tight
  (about 44.8 KB spare after rand-seq 5.2, the undo timeline dominates).
- Each added control is per track (`NTR = 8` arrays); LEN+ is MIDI tracks only (4-7).

## Closed investigations - do not redo

- **MIDI THRU jack as a second MIDI output: impossible from software** (tested on his unit, nothing came out).
  Evidence and what is left in `docs/THRU_INVESTIGATION.md`. He dropped the idea.

## Resuming: first steps

1. Read `docs/OPEN_ITEMS.md` (what is pending) and `docs/DECISIONS.md` (why things are the way they are).
2. Ask what Jack reported on JACK18c (LEN+ lengths on the unit, the 0% restore, anything odd) before building anything.
3. Check the toolchain works: build the five mods and compare your `rand-seq-5.2.elemod` hash with
   `build/elemods/SHA256SUMS.txt` (a reproducible build is the sanity check for your setup).

## Docs index

| File | Contents |
|---|---|
| `docs/ARCHITECTURE.md` | how the mods hook into the firmware, UI/draw model, per-mod design, undo timeline |
| `docs/FIRMWARE_REFERENCE.md` | OS 1.44 addresses, data layouts, formulas (LEN table, scales), key codes |
| `docs/DECISIONS.md` | decision log with reasons and rejected alternatives |
| `docs/BUILD_AND_TEST.md` | toolchain, build/lint/link, digiemu emulator + `tools/drive.py`, test recipes, pitfalls |
| `docs/VERSION_HISTORY.md` | every build, what it added, sha256 prefixes, what Jack confirmed |
| `docs/THRU_INVESTIGATION.md` | the closed THRU-jack research |
| `docs/OPEN_ITEMS.md` | pending checks, known quirks, ideas not done, tool/update status |
| `docs/SESSION_NOTES.md` | older verbatim engineering log (reverse-engineering detail; partly superseded) |
| `tools/` | `drive.py` (headless emulator driver), `fake_tk/` (tkinter stub it needs), `dn_run_play.sh` (example) |
| `build/elemods/` | the exact JACK18c mod binaries plus the core, with `SHA256SUMS.txt` |
