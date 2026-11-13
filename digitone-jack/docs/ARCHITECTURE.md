# Architecture

How the JACK firmware is put together. Addresses are for Digitone / Digitone Keys **OS 1.44**
(`FIRMWARE_REFERENCE.md` has the tables). Hardware: ColdFire (m68k) CPU, the OS is a flat image
loaded at 0x40000400.

## 1. Layers

```
stock OS file (Elektron, user supplied)   Digitone_and_Digitone_Keys_OS1.44.syx
        |   elekloader.patch: re-packs the OS, verifies every byte it writes, never touches the bootloader
        v
core (core-dn1 2.0a, OS 1.44 build)       boot copier + "hook bus": calls mods at fixed events
        v
mods (.elemod, linked C / asm)            subscribe to events and/or patch code "sites"
```

- A **mod** is a folder (`src/<mod>/`) with `mod.json` plus C/asm sources. `python -m elekloader.sdk.build`
  compiles it (m68k cross gcc) to an `.elemod`; `elekloader.lint` checks it against the stock file and the other
  mods (overlapping bytes, memory, resources, requirements); `elekloader.patch` links core + mods into one `.syx`.
- `mod.json` has two ways to change the firmware:
  - **`subscribe`**: functions the core calls on events (`ev_enc`, `ev_key`, `ev_draw`, `ev_tick`, ...), each with an
    `order` (40 for input handlers, 60 for draw/tick in all our mods).
  - **`sites`**: bytes patched in the stock code, either a `jsr` to a mod function (via `glue.s`) or a literal
    byte replacement (`cc-hold`: one site, `data` sites for strings/tables).
- The core's RAM image lives at 0x47BE0000 (code and `.bss` of all mods); `lint`/`patch` print the RAM left
  (about 44 KB with rand-seq 5.2).
- Mods are **freestanding**: no libc; the core zeroes `.bss` at boot. Raw
  pointers to firmware addresses and firmware functions are used directly (`#define FILLRECT ((void(*)(...))0x400dd506UL)`).

## 2. Event handlers

| Event | Signature | Notes |
|---|---|---|
| `ev_enc` | `int fn(void *brain, void *ev)` | `ev+12` = encoder id (1..8 = A..H), `ev+16` = signed delta (counts). Return **1 to swallow** (stock never sees it), 0 to pass. |
| `ev_key` | `int fn(void *brain, void *ev)` | `ev+12` = key id, `ev+16` = flags: bit 1 press, bit 2 FUNC held, bit 8 auto-repeat (once, ~0.4 s into a held key). Knob pushes A..H are ids 46..53. |
| `ev_draw` | `void fn(void *bmp, void *ctrl)` | Called **after** the stock drew the frame. `bmp+4` width (128), `bmp+8` height (64), `bmp+0x10` pixel buffer (1 bit per pixel). |
| `ev_tick` | `void fn(void *ctrl)` | ~30 Hz. Setting `*((unsigned char *)ctrl + 0x20) = 1` forces a redraw. |

Order of business in every mod's handlers: (1) call `page_key()` / `page_tick()`, (2) bail out unless the mod's slots are
visible, (3) act, (4) return 1 only for events that belong to the mod's own knobs.

## 3. UI model (the part most mistakes come from)

- The screen is 128x64, 1 bit per pixel. Knob slots are 17x17 boxes centred at x = 33, 59, 85, 111 and y = 21
  (top row) or y = 48 (bottom row). An **empty** stock slot is drawn as a dotted box (every 2nd pixel lit).
- `slot_stock(px, cx, cy)` samples 32 points on that dotted outline and returns true when >= 28 are lit. This is the
  **visibility test**: our knobs are drawn and take events only while the stock box is present underneath. Windows
  (SONG, Save project), popups and the black power-saving screen fail the test, so the knobs disappear with them.
- Drawing helpers live in each `.c` (`knob()`, `line()`, `setpix()`, `draw_switch()`, `hdr_popup()`); firmware routines
  used: text `0x400de120(bmp, font, x, y, flags, fmt, ...)` (flags 0 left, 0x22 centred) and `fillrect 0x400dd506`.
  Note the firmware's drawing calls use a vertically flipped y axis compared with direct pixel access
  (`hdr_popup` fills y 54..63 with the firmware call but touches rows 0 and 9 directly): copy an existing helper
  rather than guessing coordinates.
- **Header popup**: turning one of our knobs shows `Name=value` in the header bar for ~3 s (`SHOW_TICKS = 90` ticks),
  like the stock knobs. After the popup the knob label returns (`LEN+`, `SCL`, ...), while turning the label shows the value.
- **Visible-layout pattern** (rand-seq): `rs_draw` decides which `struct layout` is on screen this frame
  (`cands()` + `slot_stock()` on every slot of the layout) and stores it in `vis_lay`; `rs_enc`/`rs_key` only act if
  `vis_lay` is set. Layouts: `lay_midi` (MIDI Source page, bottom row + reset switch in PROG + LEN+ in SBNK),
  `lay_synth` (synth Amplitude page 2/2, top row), `lay_sres` (synth Amplitude page 1/2, reset switch only).
- **Page byte lag**: the "current page" byte (`0x41368ce7`) changes one frame *after* the new page is on screen.
  Reading it directly made knobs appear a frame late and leave a one-frame ghost when leaving a page. Fix in all mods:
  `page_key()` records the page of any page-key press (ids 20..25 = TRIG, SYN1, SYN2, FLTR, AMP, LFO; not while FUNC is
  held) in `pg_pend`; `page_now()` returns it until the byte catches up (`page_tick()` clears it after the byte matches or
  6 ticks). Measured with a per-frame diagnostic build (see BUILD_AND_TEST.md).
- **main_screen()**: the screen list lives in the UI "brain" at `0x41a0a7a0` (`+0x58` / `+0x5c`); the main track-page
  screen is recognised by its method table `0x40198694` (object = list node - 16).
- On a MIDI track, SYN1 and SYN2 open the *same* stock page. rand-seq deliberately lives on **SYN1 only** (page byte 1).

## 4. The mods

### cc-hold 2.0 (`src/cc-hold-2.0`)
One code site `0x400f44ca`: `48780001` -> `48780000`. In the resync loop that resends MIDI-track CC values after
STOP/PLAY, the "force" flag is pushed as 1; making it 0 means unchanged values are not resent (protects his OB-6's
knob settings). No C code. (cc-hold 1.0, `src/cc-hold-1.0-archived`, patched two other sites and was inconsistent.)

### ob6-nrpn 1.4 (`src/ob6-nrpn`)
Site `0x400f3d72`: the `jsr` that appends a 3-byte message `(status, cc, value)` to the MIDI staging buffer
(`0x400f3c1a(buf, len)`, buffer at `0x419b18ed`) is redirected through `glue.s` to `ob6nrpn.c`. On MIDI channel 14
(`CH = 13`) every CC becomes the OB-6's NRPN sequence: CC 99 / 98 (address from `table.h`), then CC 6 / 38 (value),
all four messages every time (from JACK2 on). The 0-127 knob is scaled to each parameter's own range; until JACK4
the ranges only reached about halfway, then the continuous parameters were given 0-255 (his own OB-6 capture of the
cutoff knob shows the NRPN going up to 255, not the manual's 164). The OB-6
applies each data-entry half as it arrives, so when both halves change the order is chosen to avoid a spike to the top
(the 127<->128 midpoint glitch, see the comment in the source). Other channels and unmapped CCs pass through unchanged.

### chord-knobs 2.13 (`src/chord-knobs`)
MIDI TRIG page, knob slots G (enc 7, x=85) and H (enc 8, x=111) = **NOT2 / NOT3**, +/-12 semitones, 0 = off. Site
`0x40035b0a` sits in the loop that clears a new trig's per-step note offsets: it stamps the current NOT2/NOT3 into the
step's note row (root + 7 signed semitone offsets). No step held: values go into every *new* trig (existing trigs do not
follow later changes). Step(s) held: the knobs show the first held step's chord and edit all held steps (hold state:
64-bit mask at `0x41a5b7f8/fc`); drawn highlighted like ROOT while a step is held. Pressing a knob resets that note to
off; the knobs turn slowly on purpose. Fixed bug (JACK8d): changing VEL on a held step made NOT2/NOT3 vanish.

### lfo-vel 2.1 (`src/lfo-vel`)
1. **LFO -> velocity**: the MIDI-track LFO destination "AT" (dest 22 in the modulation slots at
   `0x419eb1fc + 80*track + 24`: `+0` destination, `+4` amount) is turned into a velocity modulation at the note-on
   site `0x400f4846` (second site `0x400f4792`). Two data sites (`0x4018f6b4`, `0x4018f6bc`) adjust the destination list so it shows velocity (the destination
   box still says "AT" after selecting it - accepted by Jack). He never uses aftertouch.
2. **HUM**: per-track velocity humanizer, knob E (enc 5, x=33) on the MIDI TRIG page, 0..60. Each trig gets a random
   offset in -HUM..+HUM with a bell-shaped distribution (mean of two uniform draws, reflected at the limits so nothing
   clips), and **consecutive trigs differ by at least 15 in velocity** (`HUM_GAP`: re-draw, then step +/-15 toward the
   trig's own velocity). All notes of a chord share their trig's velocity. Pressing the knob resets it to 0.
   `cputest.py` / `cputest2.py` run the draw logic on a CPU emulator; one edge case (LFO +20 with HUM 5, range
   115..125) was dropped from the suite as minor.

### rand-seq 5.2 (`src/rand-seq/randseq.c`, ~800 lines)
The randomizer suite. Same four controls, left to right **SCL | UNDO | RND | PIT**, on two pages:
- **MIDI tracks**: MIDI Source page (SYN1), bottom row = the stock PB / VEL / MW / BC slots (he uses none of them).
- **Synth tracks 1-4**: Amplitude page 2/2, top row of four empty slots.
- **RST** reset switch (spring-loaded toggle drawn like the stock LFO.T): MIDI Source PROG slot (top right) and synth
  Amplitude page 1/2 empty slot F (59,48).
- **LEN+** (MIDI tracks only): MIDI Source SBNK slot (85,21), directly above RND.

Per-track state (arrays sized `NTR = 8`, reset at power-on, **not saved to the project**): `rs_scale`, `rs_rnd`, `rs_pit`,
`rs_lenp`, the step note memory `rs_nroot`, the LEN+ memory `rs_lorig/rs_lnew/rs_lflag`, and the undo timeline.

Actions (each is a *press*; auto-repeat ignored):
- **RND** (`do_scatter`): clears the track's trigs, then places `pct * pattern_length` trigs at random using the stock
  placement routine `0x4003673a(editor, step, mask, on)`. A new trig takes the note remembered for its step.
- **PIT** (`do_pitch`): *every* step (trig or not) gets a new note within +/-PIT semitones of the TRIG page ROOT, never
  below C2 (MIDI 24), leaning upward (top of the range 1.6x as likely as the bottom), with "gravity" (root 22, fifth 17,
  third 15, others 8) and an extra pull for the exact ROOT note (`ROOT_PULL` 7 %, its octaves `OCT_PULL` 2 %, about 10 %
  of notes). With a scale set, chords follow in scale steps and stay in key; chords he enters by hand are never snapped.
  Writes `rs_nroot` for all 64 steps, so RND later reuses those pitches (RND and PIT are independent).
- **SCL**: scale for PIT, 0 = off ... 9. Masks: Off `0xFFF`, Major `0xAB5`, Minor `0x5AD`, Dorian `0x6AD`, Lydian `0xAD5`,
  Mixolydian `0x6B5`, Harmonic minor `0x9AD`, Pentatonic major `0x295`, Pentatonic minor `0x4A9`, Blues `0x4E9`.
  Pressing SCL = scale off. The scale is deliberately **not** part of undo or reset.
- **UNDO**: turn left = undo, right = redo (12 counts per step, >= 6 ticks between steps, direction change restarts the
  count), FUNC+press = undo. 8 deep per track.
- **RST** (`do_reset`): firm turn right (24 counts) throws it, it springs back after `rst_ticks = 8` ticks (~0.27 s).
  Resets RND/PIT/LEN+ percentages, wipes `rs_nroot`, empties the undo history and removes **all trigs**. Keeps the scale.
  Not undoable. Left turns do nothing; a half-hearted turn fades (`racc_age`).
- **LEN+** (`do_lenplus`): turn = percent of trigs (0-100, steps of 2); press = exactly `round(trigs * pct)` trigs, chosen at
  random, get a random longer length (strictly longer, at most +4 steps; picked uniformly in time over the length
  table). Always starts from the trig's **original** length remembered in `rs_lorig` (so presses never accumulate); a
  length changed by hand since (`aux != rs_lnew`) becomes the new original. **At 0 % a press restores** every length LEN+
  changed (hand edits stay). The length byte is `live[0xC0 + step]` (0xFF = "use the page LEN default", `live[962]`);
  written to live **and** saved blocks. Undoable. **Caveat:** that the `+0xC0` array is the per-step LEN lock was derived from the
  parameter table (LEN = parameter index 2, array = `128 + 64*(index-1)`, default 14) and checked in the emulator's data;
  the real note-length effect on the unit has not been confirmed yet.

**Undo timeline** (`snap_take` / `snap_push` / `tl_step`): swap-based. Before a press the affected data is copied to
`tmp_*`; after it, only the steps that actually changed are stored in the next history slot together with a bitmask.
Undo/redo **swap** the stored data with the pattern, so the same code does both directions. Stored per changed step:
trig flags (2 bytes), aux/LEN byte, note row (8 bytes, live + saved), `rs_nroot`, and the LEN+ memory (`rs_lorig`,
`rs_lnew`, `rs_lflag`). A new press while stepped back drops the redo branch. `kind` per entry (1 RND, 2 PIT, 3 LEN+)
only selects the popup text ("Undo: Random Hits / Pitch / Len").

### thru-test 0.1 (`src/thru-test`, archived experiment)
Bit-banged MIDI notes on the two THRU/SYNC B pins. Its result closed the idea; see `THRU_INVESTIGATION.md`. Not part of
any JACK build.

## 5. Why things are shaped this way (short)

- Press-only actions and per-track memory: he wants one-button "sequencer-style" randomizers that can be repeated until
  something sounds good, with an exact way back (undo, 0 %, reset).
- Everything sits in slots the stock UI leaves empty for his workflow (PB/VEL/MW/BC, PROG, SBNK, Amplitude 2/2), found
  by checking that the stock slot box is present, so no stock control is ever covered.
- `DECISIONS.md` has the reasons and the rejected alternatives.
