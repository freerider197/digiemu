# Build, test and deliver

Everything below was done in a Linux sandbox (Debian/Ubuntu, Python 3.12) during the claude.ai sessions. On Jack's Mac
(macOS 12, Apple silicon) the only unverified part is the m68k cross toolchain; check elekloader's `docs/BUILDING.md`.
**Ask Jack whether he wants to build on his Mac or have the build done elsewhere** (his stated preference for new work).

## 1. Versions used

| Tool | Version / commit |
|---|---|
| elekloader | github.com/irpina/elekloader, checkout `69707b3` ("core-dn1 3.0 ...", = tag `core-dn1-v2.3` + 3 commits; `pyproject.toml` says 0.4.0). JACK18c (and the diagnostic builds of 2026-10-11) were built with it; earlier sessions used the checkout of their day. Released: v0.4.0 (2 Oct 2026) |
| core | `core-dn1-2.0a-os1.44.elemod`, sha256 `5707a38e98047854eb12e5870430fb323126224f25b2a10b725badb93b97a41a` (v0.4.0 release asset; copy in `build/elemods/`) |
| digiemu (emulator) | github.com/irpina/digiemu, tag `v0.3.0` (adds Digitone OS 1.44); has its own `CLAUDE.md` - read it |
| Stock OS | `Digitone_and_Digitone_Keys_OS1.44.syx` from elektron.se (Jack supplies it; not in this repo) |
| Cross toolchain | Debian: `binutils-m68k-linux-gnu gcc-m68k-linux-gnu` (provides `m68k-linux-gnu-gcc`, `-objdump`) |
| Python extras | whatever digiemu's requirements install (the emulator runs on `unicorn`); the driver's `png` action also imports `Pillow` |

elekloader's own `AGENTS.md` and `docs/ADAPTING.md` describe the mod rules, every refusal message and its fix, and the
definition of done. Read them before changing a `mod.json`.

## 2. Build, lint, link

Run from the repo root with elekloader importable (`cd` into its checkout or `pip install -e` it).

```bash
STOCK=Digitone_and_Digitone_Keys_OS1.44.syx
CORE=build/elemods/core-2.0a-os1.44.elemod
MODS="cc-hold-2.0 ob6-nrpn chord-knobs lfo-vel rand-seq"

for d in $MODS; do python3 -m elekloader.sdk.build src/$d --stock $STOCK --out src/$d/out; done

E="src/cc-hold-2.0/out/cc-hold-2.0.elemod src/ob6-nrpn/out/ob6-nrpn-1.4.elemod src/chord-knobs/out/chord-knobs-2.13.elemod src/lfo-vel/out/lfo-vel-2.1.elemod src/rand-seq/out/rand-seq-5.2.elemod"

# lint every mod together with the core and all the others
bad=0
for f in $E; do
  W=""; for o in $E; do [ "$o" != "$f" ] && W="$W --with $o"; done
  python3 -m elekloader.lint $f --stock $STOCK --with $CORE $W >/dev/null 2>&1 || { echo "LINT PROBLEM: $f"; bad=1; }
done
[ $bad = 0 ] && echo "lint all five: exit 0"

python3 -m elekloader.patch --stock $STOCK --mod $CORE $(for f in $E; do printf -- "--mod %s " $f; done) \
  --out Digitone_OS1.44-JACK18c.syx --version JACK
shasum -a 256 Digitone_OS1.44-JACK18c.syx      # expect 7868c91f...
```

- File names change with the version numbers inside each `mod.json` (the `out/` folder holds e.g. `rand-seq-5.2.elemod`).
- `patch` prints `verified: sections ... byte for byte`, the RAM left, and refuses overlapping mods. It also writes
  `<out>.syx.json` and `<out>.syx.map.json`; the map lists every non-static global of the mods (`rs_lenp`, `rs_nroot`, ...)
  with its address, which the emulator tests use to read mod state.
- Reproducibility check: your `rand-seq-5.2.elemod` should hash to `dfdc7507aa9827471a8a74a0c917d3e0ed2d1a505232aa4b58ca458c629fee1a`
  (`build/elemods/SHA256SUMS.txt`). If it does not, your toolchain differs - sort that out before trusting any build.
- **Raising a mod's version**: edit `mod.json` `version` (and its `description` if the feature changed), rebuild, relink.
  The header comment inside the `.c` file is not kept in sync.

## 3. The emulator (digiemu) and the driver

```bash
git clone https://github.com/irpina/digiemu && cd digiemu && git checkout v0.3.0     # then follow its README / CLAUDE.md to set up the venv
python -m emu.portable --add /path/Digitone_OS1.44-JACK18c.syx --yes     # 1-4 min; creates portable/firmware/dn1-jack-<sha8>/ with a settled snapshot
```

`portable/firmware/dn1-1.44-d4f200d0` is the stock 1.44 setup; every added build gets its own `dn1-jack-<first 8 of sha256>`.
Headless runs need a tkinter stub on the path: `PYTHONPATH=tools/fake_tk`.

```bash
FWDIR=$PWD/portable/firmware/dn1-jack-7868c91f \
SNAP=$(ls $PWD/portable/firmware/dn1-jack-7868c91f/snapshots/*/gui.snap) \
SYX=$(ls $PWD/portable/firmware/dn1-jack-7868c91f/*.syx) \
DIGIEMU_DIR=$PWD WORK_DIR=/tmp/work RT=1 PYTHONPATH=/path/to/repo/tools/fake_tk \
  python /path/to/repo/tools/drive.py wait:2 qtap:MIDI:0.1 wait:0.6 qtap:SYN1:0.1 wait:3.5 png:look
```

Use **bash**, not `sh`: lists and `$(...)` are used heavily. Run long sequences with a generous `timeout` (see pitfalls).

### Environment variables of `tools/drive.py`

| Variable | Meaning |
|---|---|
| `FWDIR`, `SNAP`, `SYX` | firmware directory, its `gui.snap`, the `.syx` (defaults point at the stock 1.44 setup) |
| `DIGIEMU_DIR`, `WORK_DIR`, `MAIN_BIN` | digiemu checkout (default `/tmp/digiemu`), output folder (`/tmp/work`; PNGs go to `WORK_DIR/shots`), `dn144_main.bin` (only for `ts`) |
| `RT=1` | run in real time (needed for timers, ticks and popups) |
| `WW=lo-hi` (hex) | log every write to that address range with the code address (`idx addr pc WRITE size n value v`) |
| `MAXC=N` | how many `WW` lines are printed (default **60**; raise it or the log silently stops) |
| `AUDIO=1` | record audio (`wav:NAME`, `apos:NAME`) |
| `DRV`, `WATCH`, `RW`, `FAST`, `DUMPARG`, `TRACE_OUT` | tracing options of the original session; see the source |

### Actions (arguments of the driver, executed in order)

| Action | Effect |
|---|---|
| `wait:S` | sleep S seconds (emulator keeps running) |
| `qtap:KEY[:DUR]` | press KEY, wait DUR (default 0.05), release all, then wait **1.2 s** |
| `hold:KEY[+KEY]` / `rel` | press and hold (waits 0.5 s) / release all (waits 0.6 s). Holding a step key did **not** produce the held-step view on 2026-10-15 (also on stock) |
| `chord:FUNC+EA` | key chord (see source) |
| `enc:N:D` | one encoder event on encoder N (1..8 = A..H) with delta D, then wait **0.6 s**. About 75-80 events of +1 turn LEN+ from 0 to 100 % (80 did) |
| `png:NAME` | save the screen as a 4x PNG (`WORK_DIR/shots/NAME.png`) |
| `mem:ADDR:LABEL:LEN` | print `MEM ADDR LABEL <hex bytes>` (read RAM) |
| `dump:NAME:LO-HI` | pickle `{base: bytes}` of all mapped RAM in the range to `WORK_DIR/NAME.pkl` |
| `poke:ADDR:VAL` / `pokeb:ADDR:VAL` | write 4 bytes (big-endian) / 1 byte |
| `mark:NAME` | put a marker in the log (write-watch lines carry the marker index, so you can split phases) |
| `min:HEX` | send MIDI bytes into the DIN input |
| `show`, `lab`, `view`, `view2`, `leds`, `ts`, `px`, `rwon`, `rwoff`, `tron`, `troff` | debugging helpers; read `drive.py` |

Key names: `FUNC MIDI KEYBOARD PTN BANK SONG GLOBAL VOICE TEMPO RECORD PLAY STOP YES NO UP DOWN LEFT RIGHT PAGE TRIG SYN1 SYN2
FLTR AMP LFO T1..T4 TRIG1..TRIG16 EA..EH` (EA..EH = knob pushes A..H) and `ELEV`.

Typical idioms: select the MIDI Source page with `qtap:MIDI:0.1 wait:0.6 qtap:SYN1:0.1 wait:3.5`; place trigs with
`qtap:RECORD:0.4 wait:0.4 qtap:TRIG1:0.1 wait:0.3 ... qtap:RECORD:0.1`; the first MIDI track's live block is at `0x407fe354`
(flags), `+0xC0` = LEN bytes (`0x407fe414`), saved copy at `0x40557138` (LEN bytes `0x405571f8`).
Open the settings menu with `qtap:GLOBAL`; MIDI CONFIG is the 5th row; PORT CONFIG inside it holds OUTPUT TO (default
USB in the emulator) and the THRU/OUT PORT FUNC rows.

## 4. Test recipes that were used

1. **Look at it.** Screenshots of every page the mods touch (MIDI Source, MIDI TRIG, synth Amplitude 1/2 and 2/2), under the
   SONG window (`qtap:SONG`), after turning knobs (popup) - make a contact sheet with Pillow and look at it.
2. **State machine tests.** Read the mod's data with `mem:` / `dump:` before and after each action and compare with a
   model written in Python. The undo/redo test applies 14 operations (RND, PIT, LEN+ presses; undo, redo; a press that cuts the
   redo branch) and checks live rows == saved rows and the exact predicted state after every step (0 mismatches on JACK18).
3. **LEN+ test.** 8 trigs, LEN+ at 50 %: exactly 4 lengthened per press over 12 presses, values within the window, nothing
   shorter, no accumulation, saved == live, undo/redo exact, a hand-edited step becomes the new base, 0 % restores originals,
   reset clears the memory. Pokes must write both live and saved copies.
4. **Per-frame diagnostics.** To find the one-frame page lag, a temporary copy of the mods logged, per drawn frame, a code
   (not this page / screen list not main / bad bitmap / slot box missing / drawn) with the raw page byte into a small
   change-log array; the array was read back with `mem:` after a scripted page tour. Use the same trick for any "appears late /
   flickers" report - the emulator's own screenshots cannot see single frames.
5. **Pin-level decode.** For the THRU experiment `WW=ec09401e-ec09402d` logged the GPIO writes; they were decoded back into MIDI bytes
   with a small UART decoder (start bit, 8 data bits LSB first, stop bit).
6. **CPU-level tests** for pure logic: `src/lfo-vel/cputest*.py` and `src/ob6-nrpn/cputest.py` run the mod functions on a CPU emulator.
7. **Do not** rely on the emulator for real MIDI-track output, note lengths or timing; those are confirmed on the unit.
8. **Full firmware check against stock** (digiemu's `emu.fwcheck`; ~25 minutes, so start it in the background; last run on JACK13, only on request):

   ```bash
   cd digiemu && setsid nohup bash -c 'python -m emu.fwcheck /path/Digitone_OS1.44-JACK18c.syx \
     --baseline /path/Digitone_and_Digitone_Keys_OS1.44.syx --out /path/check --no-timing > /path/fwcheck.log 2>&1; \
     echo "EXIT $?" >> /path/fwcheck.log' > /dev/null 2>&1 < /dev/null &
   ```

   It boots the baseline and the custom image in the emulator and compares them (container, bootloader, boot, ...); read `fwcheck.log` and
   `check/report.json`. A warning that SysEx/content checksums were not checked is normal for this copy of digiemu.

## 5. Pitfalls (each cost time once)

- `qtap` costs >= 1.3 s and `enc` 0.6 s plus your `wait`; a 80-turn sequence takes ~1 minute. Use `timeout 300` or more, and
  remember that a killed run leaves **stale PNG/pkl files** from earlier runs - delete the file before a run you will inspect.
- `/bin/sh` does not support `<(...)` or arrays; use `bash`.
- Popups are shown ~3 s after a knob event; screenshot right after, not later (and not mid-sequence).
- Held-step views and held-step knob edits could not be produced in the emulator; `hold:` did not engage the held view on 2026-10-15.
- The emulator's default OUTPUT TO is USB; DIN MIDI output needs the settings change in the menu.
- `poke` writes four bytes; use `pokeb` when neighbours must not change.
- The write-watch log is capped by `MAXC` (default 60).
- Header comments in the C sources are stale; `mod.json` versions are the truth.
- Popup/label text must avoid the letter g (drawn like 9).

## 6. Delivering a build to Jack

Checklist (his workflow - do not add extra steps):
1. Summary of what you understood + rough effort estimate, wait for his go (larger changes).
2. Build, lint all five together, emulator tests for the change; state **verified vs assumed**.
3. Hand over `Digitone_OS1.44-JACK<n>.syx` and the sha256 prefix, plus the source zip if asked. Name the next build
   `JACK<n+1>` (or add a letter for a small fix) and **keep the old files**.
4. Update `docs/VERSION_HISTORY.md` and `docs/OPEN_ITEMS.md` (and `Digitone_JACK_builds.txt` in his OS folder) *only when he asks*,
   otherwise offer.
5. Flashing is his: Elektron Transfer. Recovery path in `CLAUDE.md`.
