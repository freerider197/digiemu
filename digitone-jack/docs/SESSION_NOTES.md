# Digitone 1.44 CC-on-start mod: session notes (2026-10-03)

Stock file: Digitone_and_Digitone_Keys_OS1.44.syx, sha256 d4f200d0... (matches elekloader's 1.44 profile)
Main OS: 2,736,304 bytes, loads at 0x40000400, sha256 fce648a9...

## Setup that works in the sandbox
- elekloader (git clone irpina/elekloader), m68k binutils (binutils-m68k-linux-gnu)
- digiemu (git clone irpina/digiemu), `uv sync`, `pip install cmake`, `tools/install-patched-unicorn.sh`
- `python -m emu.portable --add <1.44.syx>`: boots and settles in ~50 s
- headless driving: stub tkinter (fake module on PYTHONPATH), run dn_drive.py with actions

## Findings
- MIDI OUT (DIN) buffer driver: 0x40002a3a (same address as 1.43); jump-table entry 0x400005be
- Callers of the driver: MIDI port layer around 0x400ea48e-0x400ea96a, plus 0x400ee0d0, 0x400ee1dc, 0x400f37ea
- MIDI track pages on 1.44: SYN2 = SRC (CHAN BANK SBNK PROG / PB AT MW BC), FLTR = CC VAL 1-8, AMP = CC SEL 1-8
- SETTINGS = control code 7 ("GLOBAL"), opens only on a QUICK tap (qtap:GLOBAL:0.02)
- SETTINGS list: PROJECT, SONG, PATTERN, SOUNDS, MIDI CONFIG, ... ; MIDI CONFIG = SYNC, PORT CONFIG, CHANNELS
- In the emulator, one qtap of UP/DOWN in lists moves two items; DOWN x2 + YES reaches MIDI CONFIG

## Next
1. Reach PORT CONFIG > OUTPUT TO and set it to MIDI (try LEVEL/DATA encoder for single steps)
2. MIDI track: CHAN 14, CC SEL 1-8 = 102,103,47,51,46,76,8,9, CC VAL set
3. PLAY, log DIN bytes and driver return addresses -> the CC-on-start routine
4. Patch it out as an elekloader mod on the 1.44 core; check in the emulator: no CC burst on PLAY, knob/p-lock CCs still sent

## Session 2 (same day)
- Run with RT=1 (real-time pacing): one arrow press = one step. Without it, taps double-step or get lost.
- SETTINGS: qtap GLOBAL; DOWN x4 = MIDI CONFIG; YES; DOWN = PORT CONFIG; YES; DOWN x4 = OUTPUT TO; LEFT = MIDI, RIGHT = MIDI+USB; NO x3 to leave.
- OUTPUT TO mask in RAM at 0x419c5df4 (bit0 DIN, bit1 USB); verified 3 after RIGHT.
- MIDI track params start DISABLED: "Func: press to enable" -> chord FUNC + encoder push (EA..EH = codes 46..53).
- Encoder turns are unreliable (dead zone, +/-30 clamp): send several small turns.
- T1 in MIDI mode probably leaves MIDI mode: don't press it; the MIDI key alone lands on MIDI track 1.
- Transport word at 0x4138f208 (1 playing, 0 stopped); 'ts' action reads it.
- Port router: 0x400f37ce(buf, len) -> DIN driver 0x40002a3a if bit0, USB 0x40004bcc if bit1. Callers: 0x400f4040, 0x400f4c90, 0x400f4caa.
- Live KEYBOARD notes from the MIDI track reach DIN through the router (90 42 64 / 90 42 00), but with no CCs attached.
- Sequencer playback with trigs on the MIDI track (LED confirms trig, transport = 1): NOTHING reaches the router, not even notes. Open question: emulator limitation or a setting.
- Synth-parameter CC sender at ~0x400f3c40 uses a per-channel last-sent cache at 0x4321f19c [ch*128+cc]; it is only reset by module init (~0x400f4d36), so it is NOT the CC-on-start culprit.

## Next
- Find why sequencer MIDI-track output doesn't reach 0x400f37ce in the emulator (trace from the sequencer tick on a MIDI-track trig; check callers 0x400f4c90/0x400f4caa).
- Alternative: ask on the digiemu repo whether sequencer MIDI out from MIDI tracks works on Digitone.

## Session 3: CC HOLD built (cc-hold 1.0, OS 1.44)
- Root cause: STOP handler (call at 0x40095410) -> pattern re-init 0x40094f14 -> 0x400f396a (store kit ptr 0x419c5b10, clear sync flags 0x419c5b14..20).
  A zero flag makes the next trig on that MIDI track do a full resync (0x400f4424 path): force-sends BANK/PROG, PB/AT/MW/BC, CC VAL 1-8 (minus p-locked).
  0x400f3750(track_src, track) sets flag[track] = pointer to the kit's track data.
- Mod: site 0x40095410 (4ebafb02 4fef001c) -> jsr cch_stop (raise suppress, call 0x40094f14, lower, lea 28(sp)); site 0x40094f92 (4eb9400f396a) -> jsr cch_kitset (if suppress: store kit ptr only; else jmp stock).
- Built: cc-hold-1.0.elemod + core-2.0a-os1.44.elemod -> Digitone_OS1.44-cchold.syx, version CCH1, sha256 4f9727d1...
- digiemu fwcheck vs stock: build PASS; all 10 screen snaps identical; audio = timing-shift residue only.
- Emulator behaviour test: stock STOP clears flags; patched STOP keeps them; patched pattern change still clears them.
- Not tested: transport started/stopped by external MIDI/Overbridge (may use a different STOP path).

## Session 4: CCH1 inconsistent on hardware -> CC HOLD 2.0 (CCH2)
- CCH1 (STOP-path hook) worked in the emulator but resent "randomly" on hardware, with manual STOP and Ableton/Overbridge.
- Emulator map (stock): every STOP (also STOP while stopped) -> 0x40094f14 from 0x40095414 with the same kit ptr; PLAY touches nothing;
  pattern change -> 0x40094f14 from 0x40095116 with a different kit ptr (per-pattern kit pointers, e.g. 41306294 / 41306c1c).
- With CCH1, no front-panel action wrote the flags in the emulator, BUT the emulator never runs the MIDI-track engine (no sequencer MIDI there),
  so the engine's own reset at 0x400f40cc (clears flags when a type-2 message carries a different kit ptr) is unobservable. Likely race on hardware.
- CC sender 0x400f3c4a(arg1, ch, param, value<<8, force): force=0 -> compare with last-sent cache 0x4321f19c[ch*128+cc], skip if equal;
  every send stores the value in the cache. Cache = 0xFF at boot (module init ~0x400f4d36).
- CCH2 = one site: 0x400f44ca 48780001 -> 48780000 (full-resync loop: force=1 -> 0). Path-independent. Knob-turn loop (0x400f4576) and p-lock loop (0x400f4634) keep force=1.
- Built: Digitone_OS1.44-cchold2.syx, version CCH2, sha256 cc5ee3b4...; fwcheck vs stock: PASS, 10/10 screens identical, audio timing residue only.
- Not verifiable in emulator: the resync itself (engine doesn't run there). Hardware test decides.

## Session 5: OB-6 NRPN 1.0 + CC HOLD 2.0 (NRP1)
- CC emit point (type-1 params, all MIDI-track CC sends): 0x400f3d72 jsr 0x400f3c1a(len=3, buf) + 0x400f3d76 moveb d3,(a2,d5) (last-sent cache).
- Append 0x400f3c1a(len, buf): buffer 0x419b18ee, length BYTE at 0x419b18ed, no bounds check (mod refuses to add when len > 240).
- Mod ob6-nrpn 1.0: site 0x400f3d72 (4ebafea6 15835800) -> jsr ob6_nrpn_hook; C translator: ch 14 only, CC -> NRPN 99/98/6/38 scaled to range,
  address skipped when unchanged, 98-101 pass through and reset the address memory. Table = CC map doc (midi.guide data).
- Unit test: real linked binary run on Unicorn ColdFire, 900 cases, 0 mismatches (scaling, address skip, other channels, buffer-full path, stack).
- Built: Digitone_OS1.44-cchold-ob6nrpn.syx, version NRP1, sha256 de1b6e69...; fwcheck vs stock: PASS, 10/10 screens identical, audio timing residue only.
- Not verified: NRPN numbers vs Sequential's own appendix (PDF text cut off before it); OB-6 behaviour with Param Rcv = NRPN.

## Session 6: JACK build (CC HOLD 2.0 + OB-6 NRPN 1.1)
- Hardware feedback on NRP1: everything works except CC 106 filter mode covered only half the sweep -> OB-6 filter-mode NRPN (0/49) is really 0-255. Fixed in ob6-nrpn 1.1.
- Boot name: the OS shows the container's version field, exactly 4 characters (no version string in the main OS) -> "JACK". Longer text needs a boot-screen mod.
- Built: Digitone_OS1.44-JACK.syx, version JACK, sha256 800a8ed6...; CPU test 900/900.
- fwcheck: stock reference failed to finish first boot (slow sandbox); build run flagged a read at 0x12a from pc 0x4009de90 = the same stock quirk that failed stock in both earlier checks. NRP1 (same code but one table byte) passed fully.

## Session 7: JACK2 (CC HOLD 2.0 + OB-6 NRPN 1.2)
- Hardware: cutoff (CC 102) at 127 stopped ~2 kHz vs 20 kHz by hand; all params short. Digitone capture showed ch14 values exactly per manual (127 -> 6=1, 38=36 = 164),
  address 99/98 only sent once (cached). Also a ch10 CC 70 stream (Digitone's own CC VAL echo) on the same cable.
- OB-6 manual v1.2 appendix C read from Jack's upload: every NRPN number and range in the table matches.
- ob6-nrpn 1.2: always sends the full 99/98/6/38 message; filter mode back to the manual's 0-127.
- Built: Digitone_OS1.44-JACK2.syx, version JACK, sha256 ee0078c9...; CPU test 900/900; fwcheck boot pass, run flags the known stock read at pc 0x4009de90.
- Open: OB-6 MIDI Channel global (14 or All?); OB-6's own NRPN output at full cutoff not yet captured.

## Session 8: JACK3 (CC HOLD 2.0 + OB-6 NRPN 1.3)
- OB-6 capture (Param Xmit NR, cutoff knob swept): NRPN 0/45 goes to coarse 1 / fine 127 = 255. The manual's 0-164 is wrong for this unit.
- Digitone JACK2 capture: full 99/98/6/38 every time, max 164 -> explains cutoff stopping ~2 kHz.
- ob6-nrpn 1.3: all continuous knobs 0-255 (8, 9, 20, 21, 22, 28, 29, 30, 31, 40, 43-46, 50-53, 69, 74, 77, 94, 102, 106, 117);
  switches, modes, osc pitch 0-60, tempo unchanged; 0-254 params (env amt, X-Mod amts, LFO rate, AT amt, osc2 fine/shape) unchanged.
- Built: Digitone_OS1.44-JACK3.syx, version JACK, sha256 67f48172...; CPU test 900/900 (cutoff 127 -> 1/127). Code identical to JACK2, table data only.
- Measured so far: cutoff only. Other knobs set to 255 by the same pattern; verify on hardware.

## NEXT: JACK4 = midpoint-glitch mitigation (Jack hears it on JACK3)
- OB-6 applies each data-entry half on arrival, combined with the stored other half -> at 127<->128 a transient (forum.sequential.com topic 3405).
- Plan in ob6nrpn.c: remember last value sent per CC (static unsigned short last[128], 0xFFFF = unknown).
  Same MSB as last: send 99/98, 6, 38 (no glitch possible).
  MSB going UP: send 99/98, then 38 (LSB) FIRST, then 6 (MSB) -> transient = oldMSB*128+newLSB = low (dip, not spike).
  MSB going DOWN: send 99/98, then 6 (MSB) FIRST, then 38 -> transient = newMSB*128+oldLSB = low (dip).
- Then: update cputest expectations, rebuild ob6-nrpn 1.4, link --version JACK, CPU test, package.

## Session 9: JACK4 (CC HOLD 2.0 + OB-6 NRPN 1.4) - midpoint glitch
- Built as planned, refined: tracks the data-entry halves last SENT (global, like the OB-6's registers), not per CC.
  Same MSB as held -> LSB first (exact); same LSB -> MSB first (exact); else the order with the lower in-between value.
- CPU test 900/900; simulated OB-6 (registers applied per half) cutoff sweep 0->127 and 127->0: 0 spike above the move (JACK3: 255 at midpoint).
- Built: Digitone_OS1.44-JACK4.syx, version JACK, sha256 6c415f3b...

## Session 10: NOT2/NOT3 chord knobs - research, part 1
- Design (Jack): TRIG page is a STAMP. Page shows ROOT (+ new NOT2/NOT3 offsets); placing a trig copies the page values into that trig;
  later page changes don't touch existing trigs; hold trig + turn = p-lock. Offsets -12..+12 semitones, 0 = off. Chords go into the
  existing per-trig chord storage, so projects stay stock-compatible.
- Stock MIDI TRIG page (emulator screenshot): A ROOT, B VEL, C LEN, D PROB, E empty, F LFO.T, G empty, H empty -> 3 free slots (E, G, H).
- Parameter records: 60 bytes each, table base 0x4018d454, looked up by index (code 0x400207b8, index < 182).
  Record: +0 long name, +8 short name, +0xc formatter, +0x14 group, +0x18 index in group, +0x1c min<<8, +0x20 max<<8, +0x24 default<<8,
  +0x30 id, +0x34 small id. Trig group 0x10: 1 ROOT (0x4018d878, id 0x181), 2 VEL, 3 LEN, 4 COND, 5 uTM, 7 FLT.T, 8 PORT; index 6 unused.
  PROB = group 2 index 0x37 (0x4018da94); LFO.T = group 3 index 0x4d (0x4018d9e0).
- MIDI engine note loop (0x400f4932..): per trig event a byte list of notes (+14..) with a count (+16), + transpose 0x419c5e08.
- Driver additions: png:NAME (screenshot), dump:NAME:LO-HI (mapped RAM to pickle), RW=lo-hi + rwon/rwoff (read tracing).
- Chord entry in the emulator: scripted "RECORD, TRIG1, hold TRIG1 + KEYBOARD, TRIG5, TRIG8, YES" did not show the note-entry screen;
  RAM diff too noisy. Next: find the right key sequence (manual 10.2.3/10.2.4), screenshot each step, then diff only around the pattern data.
- Next steps: (1) per-trig chord storage layout; (2) where a new trig gets stamped (grid record trig placement);
  (3) TRIG page item list + draw/encoder handlers for MIDI tracks (via core hook bus ev_draw / ev_enc if possible).

## Session 10b: chord storage FOUND
- Note-entry screen: grid record, hold step + KEYBOARD -> "YES: ENTER NOTES" (piano, SCALE E / ROOT F / CHORD G); notes via trig keyboard
  (TRIG9.. = white keys C D E...), YES stores.
- STORED per-trig notes (pattern RAM): 8 bytes = root note + 7 SEMITONE OFFSETS, 0 = none.
  Step 1 of MIDI track 1 in the emulator project: 0x405572b8 (second copy 0x407fe514), preceded by ff ff ff ff.
  Plain trig: 3c 00 00 00 00 00 00 00; after C-E-G chord: 3c 04 07 00 00 00 00 00.
  => NOT2 / NOT3 = offset bytes 1 / 2. Exactly the Octatrack model, already in the stock format.
- Writer: one routine writes root (0x40008e1c) then the 7 offsets (loop at 0x40008e56); used both by trig placement and by chord entry.
- Playback event (separate, transient): record with count at +0x10 and ABSOLUTE notes at +0x14 (e.g. 0x419f1fb4), built at 0x40094b00..bb6.
- Next: read the routine around 0x40008e1c (its arguments/source), find the trig-placement caller, inject page NOT2/NOT3 offsets there;
  then the TRIG page knobs (slots E/G/H) via parameter records + page item list.
- Routine ~0x40008dc6..0x40008eda is a generic "copy trig data from a SOURCE template (a2/a4) to the pattern step (a3/a5)"
  for masked steps: per-field byte copies (a4@(128)->a5@(128), 192, 256, 320, ...) and the note bytes a1@(448+..) -> a0@(384+..).
  => Placing a trig copies the page/track template INCLUDING the 7 offset bytes (normally 0) into the step.
  => The mod may only need TRIG-page knobs that edit the TEMPLATE's offset bytes 1/2; stock code then stamps them into new trigs.
  To verify next: find the template (a2 at placement), poke its offset bytes, place a trig, check the step gets 3c 04 07.

## Session 11: stamp path mapped (emulator, MIDI track 1)
- Trig placement: copy routine 0x40008dc6.. copies TEMPLATE obj (a2 = 0x407fe354) -> pattern track (a3 = 0x40557138).
  Note bytes per step: base + 448 + 8*step (step 0..63): byte0 root, bytes1..7 offsets.
- Template is rebuilt per placement by setStepNote 0x4003666c(obj a2, step d2, flag d3):
  root = obj+960 byte0 (the TRIG PAGE values block; flag -> 0xff), then LOOP 0x400366c6..d6 FORCE-CLEARS offsets 1..7 to 0.
- obj+960 (0x407fe714 for track 1) = TRIG page values: 3c 64 0e 06 00 00 10 02 (ROOT, VEL 100, LEN, ...). Turning ROOT -> 3b. 
  NO existing slot for default offsets -> mod keeps NOT2/NOT3 page values in its own RAM (per track; reset at power-on).
- Offset setter used by note entry: ~0x40035850.. (obj a2, step d2, index d3 1..7 clamped, value d6): writes obj+448+8*step+index.
- PLAN: (1) patch loop 0x400366c6 to write mod offsets (per track) instead of 0 -> stamping for free;
  (2) knobs on MIDI TRIG page slots E/G: via core hooks ev_enc / ev_draw: detect MIDI TRIG page + track + held step;
      no step held -> edit mod page value; step held -> edit that step's offsets (pattern obj, like note entry).
  Needed RE: obj -> track index; "MIDI TRIG page visible" flag; held-step state; draw API for labels/values.

## Session 12: JACK5 = CC HOLD 2.0 + OB-6 NRPN 1.4 + CHORD KNOBS 1.0
- UI state: active track 0x41368ce3 (0-3 synth, 4-7 MIDI), page 0x41368ce7 (0 TRIG, 2 SYN2, 3 FLTR).
- Key event: ev+12 id (TRIG1..16 = 26..41), ev+16 flags (1 press, 9 repeat, 0x10 release). Encoder: ev+12 = 1..8 (A..H), ev+16 = +/-4 per detent.
- Text: 0x400de120 printf-style (bmp, font, x, y, flags 0x22 = centred, fmt, ...); font 0x418e934c (page labels). In ev_draw, y counts from the bottom.
  Slot centres x: E 33, G 85, H 111; value y 14, label y 2.
- Placement: setStepNote 0x4003666c sets root, then loop at 0x40035b06 calls setStepOffset 0x40035858(obj, step, value=0, index 1..7) -> clears chord.
  chord-knobs hooks 0x40035b0a ("move.l d2,-(sp); move.l a2,-(sp); jsr (a3)") and substitutes NOT2/NOT3 for index 1/2 on MIDI tracks.
- Emulator tests: page shows NOT2/NOT3 in slots E/G; +4/+7 -> new trigs store 3c 04 07; held step: knobs left to firmware, labels hidden.
- Built: Digitone_OS1.44-JACK5.syx (version JACK), sha256 e2a6794a...; booted and settled in the emulator; full fwcheck not run.
- Not yet: editing a HELD step's chord with E/G (use stock note entry: hold step + KEYBOARD); NOT4 on slot H; note names; negative offsets on hardware.

## Session 13: test campaign -> JACK5c (chord-knobs 1.2)
- BUG in JACK5 (1.0): NOT2/NOT3 drawn over SETTINGS and TEMPO, and knob E (swing on TEMPO) taken by the mod. JACK5 file withdrawn.
- 0x41378640 "mode" (4 on MIDI TRIG page) is NOT reliable: flips during knob turns / screen changes (labels vanished, turns lost). Dropped.
- Reliable: UI brain 0x41a0a7a0, screen list at +0x58 / +0x5c: entries DIFFER on track pages (incl. during turns), EQUAL when a full screen
  (TEMPO, SETTINGS) is on top, from the first frame. chord-knobs 1.2: ACT 4..7 && PAGE == 0 && SCR_A != SCR_B.
- 0x41a5d6cc "overlay" byte: inconsistent. 0x419782c5: key-LED state, not a screen id.
- JACK5c tests (emulator): stamp 3c 04 07; clamp +/-12; per-track values (MIDI track 2 independent); synth tracks untouched;
  stock note entry wins (single note stays single); held step: knobs step aside; TEMPO/SETTINGS identical to stock, knob E reaches firmware;
  PTN popup ok; live recording records played notes only (no stamp). NRPN cputest 900/900 + no midpoint spike on the JACK5c binary.
- fwcheck JACK5c: PASS, 10/10 screens identical; audio windows 4.5-7.4 s same levels, diff ~1.4x level = time offset (stock run crashed on its quirk).
- Built: Digitone_OS1.44-JACK5c.syx (version JACK), sha256 0596d5c6...
- Untested on hardware: held-step ROOT change; negative offsets playing; cancelling note entry on an empty step (chord probably stays).

## Session 14: JACK5c hardware feedback -> plan for chord-knobs 2.0
- Jack: stamping WORKS on hardware (chords audible). But knobs vanish when a step is held and often NEVER come back
  (so own held-key tracking via ev_key gets stuck on hardware, or the SCR_A != SCR_B check fails in some state).
  Wants NOT2/NOT3 on the two RIGHT-MOST free slots (G = enc 7, x 85; H = enc 8, x 111), not E/G around LFO.T.
  Wants: hold step(s) + turn NOT2/NOT3 = edit those steps' chords (p-lock behaviour), knobs always visible.
- Emulator limit: with a step held, encoder events DO reach the firmware (ENCDISP called, enc 1 delta -4/-12, key 0x1f flags 9),
  but even STOCK ROOT does not change on screen or in the step row -> held-step edits can't be traced dynamically here.
- Raw key bitmap 0x41973dd0..: bits per held key (TRIG1 byte3 0x02, TRIG2 byte4 0x01, TRIG3 byte5 0x08, TRIG7 byte5 0x80,
  TRIG9 byte5 0x02, TRIG16 byte6 0x20) - candidate authoritative held state (needs full 16-key map).
- Track editor objects: 0x41a73294 + 0x58 * track (0-3 synth, 4-7 MIDI); setStepOffset(obj, step, value, index) writes the
  TEMPLATE (obj vfunc +40 -> 0x407fe354 for MIDI 1). Placement then copies template -> pattern (0x40557138 for MIDI 1) via 0x40008dc6.
- TODO 2.0: (1) slots G/H; (2) never hide; held state from firmware (key bitmap or UI held-trig state), not ev_key;
  (3) held-step edit: find what playback reads (template or pattern) and how stock commits held-step ROOT edits (static: callers of
  0x40008dc6 / setStepNote outside placement); write offsets for all held steps; show the first held step's offsets.

## Session 15: JACK6 = CC HOLD 2.0 + OB-6 NRPN 1.4 + CHORD KNOBS 2.0
- Held-step mask (firmware UI): 64-bit at 0x41a5b7f8 (hi) / 0x41a5b7fc (lo), bit = step 0..63. Replaces own ev_key tracking (got stuck on hardware).
- Track editor objects 0x41a73294 + 0x58*t: +0x10 live data (notes +448, what the sequencer reads), +0x20 saved copy (notes +384),
  +0x38 track index, vtable 0x40196e5c (+0x44 = commit 0x40036f1a(editor, dest, x): job copy live -> saved for a step mask).
- chord-knobs 2.0: NOT2/NOT3 on G (enc 7, x 85) / H (enc 8, x 111). No step held: page stamp values. Held: show first held step's
  offsets; turning sets the same value on every held step that has a trig: setStepOffset(editor, s, v, idx) (live) + saved row byte.
- Emulator: placement 3c 04 07 (live + saved); hold 1 + NOT3 -> 3c 04 04 (both copies), step 5 untouched; hold 1+5 + NOT2 -> both 05,
  NOT3 kept per step; release -> page values back. Built JACK6 sha256 ffb74172...; full fwcheck not rerun (chord mod only changed).

## Session 16: JACK6b (chord-knobs 2.1) - overlay fix
- Bug (Jack): TRIG page -> FUNC+PAGE -> hold FUNC: NOT2/NOT3 drawn over the length/scale screen.
  Cause: FUNC hint overlay pushed on top, so SCR_A != SCR_B again.
- Screen objects = list node - 16; first word = method table: main track-page screen 0x40198694, FUNC+PAGE 0x401985ec, SETTINGS 0x40191c24.
- chord-knobs 2.1: visible/active only if *(SCR_B - 16) == 0x40198694 (main screen at the bottom of the list). Verified: TRIG, TRIG+FUNC,
  FUNC+PAGE, FUNC+PAGE+FUNC held, TEMPO, back. JACK6b sha256 53fb4b54...
- NEXT: MIDI track LFO -> velocity destination.

## Session 16b: MIDI LFO -> velocity, research part 1
- MIDI track LFO page: SPD MULT FADE DEST / WAVE SPH MODE DEP. DEST list today: META:None, SRC:Pitch Bend, SRC:Aftertouch, SRC:Mod Wheel,
  SRC:BC, CC:CC1..CC8 Value. (Emulator can't scroll the list: knob D drops steps, arrows change octave.)
- Parameter records: table 0x4018d454, 60 bytes, index < 182. CORRECT layout: +0x00 group, +0x04 index in group, +0x08 min, +0x0c max,
  +0x10 default, +0x14 ?, +0x18, +0x1c id, +0x20 small id, +0x24 MOD-DEST FLAGS, +0x28 long name, +0x2c category name, +0x30 short name,
  +0x34 formatter, +0x38 ?. (Earlier notes read records shifted by one: name is at +0x28.)
  VEL = record 18 (group 0x10, flags 0). PB 145, AT 146, MW 147, BC 148, VAL1..8 150..157: flags 0x0e00, category "SRC"/"CC".
  Synth LFO1 DEST = record 52 (flags bit 17), LFO2 DEST = record 61 (bit 16).
- DEST list builder 0x40021cec(track, mask): for slot 0..78: p = track->vt[+80](track, slot) (track-type candidate list);
  include p if (flags(p) & mask) == mask. LFO1 mask 0x0e00, LFO2 mask 0x0600 (page code 0x40021f3e..).
  => setting VEL flags alone is NOT enough (tested: not listed): VEL must also be in the MIDI track's candidate list (vt+80).
- Apply side: MIDI engine modulation loop 0x400f474a (a4 stride 40, value + a4@(4) modulation) feeds the CC sender; note-on velocity
  comes from the trig event (a2@(35), ~0x400f499a). Velocity mod needs new code at note-on.
- PLAN: (1) MIDI track vt+80 candidate list: add VEL (index 18) + flags 0x0e00 + a category label ("TRIG");
  (2) find where the MIDI LFO's current output and DEST are stored per MIDI track;
  (3) at note-on: if DEST == VEL, velocity = clamp(vel + lfo * depth); (4) display name in the list.
- Jack's simplification: REPLACE the Aftertouch destination with velocity (no list changes needed).
- Modulation slots: base 0x419eb1fc (from 0x400fe9d8), per MIDI track t (0-3): slot1 = base + 80*t + 24, slot2 = +40.
  slot+0 = destination param (MIDI numbering: BANK 18, SBNK 19, PROG 20, PB 21, AT 22, MW 23, BC 24, VAL1..8 25..32),
  slot+4 = current modulation amount (signed, <<8 scale).
- Engine loop 0x400f474a..0x400f47a0: for each slot with dest != 0 and dest enabled (0x400f37a2): value = param + amount (clamp 0..0x7f00)
  -> sender 0x400f3c4a(track, ch, dest, value, 0) at 0x400f4792 ("2044 4e90" + "4fef 0014").
- 0x400f499a.. is the NOTE-OFF build (vel 0); the note-on build is earlier in the same function (velocity from the trig event).
- PLAN: (1) wrap the sender call at 0x400f4792 (8 bytes incl. lea 20(sp)): skip when dest == 22 (no AT messages from the LFO);
  (2) at note-on: vel = clamp(vel + (amount >> 8) for slots with dest 22, 1..127);
  (3) optional: AT record (146) long name -> "Velocity" (0x401d6667) so the list reads SRC:Velocity (also renames the SRC page AT popup).
  Test: Unicorn unit test of the note-on hook (engine doesn't run in the emulator); Jack checks with MIDI Monitor.

## Session 17: JACK6c (chord-knobs 2.2) - highlight
- Text routine 0x400de120 flags: 0x22 normal (centred), +0x08 (= 0x2a) INVERTED (white box, black text) = the highlight look.
  (Tested 0x22 62 2a 32 23 26 a2 42; only 0x2a inverts.)
- chord-knobs 2.2: NOT2/NOT3 values drawn with 0x2a while a step is held (like ROOT), 0x22 otherwise.
- JACK6c = core + CC HOLD 2.0 + OB-6 NRPN 1.4 + chord-knobs 2.2, sha256 1f598331...

## Session 18: JACK7 = CC HOLD 2.0 + OB-6 NRPN 1.4 + CHORD KNOBS 2.2 + LFO VELOCITY 1.0
- Note-on velocity: 0x400f4846 "move.b 35(a2),71(sp)" (a2 = trig event, d2 = MIDI track 0-3); loop per chord note.
- lfo-vel 1.0: hook there -> vel + (slot amount >> 8) for slots with dest 22 (AT), clamp 1..127;
  wrap 0x400f4792 (sender call in the modulation loop) -> skip when dest == 22 (no AT messages from the LFO);
  data site 0x4018f6b4: AT record long name 0x401d6c43 "Aftertouch" -> 0x401d6667 "Velocity" (list shows SRC:Velocity; SRC page short name AT unchanged).
- CPU test (real binary): 15/15 (no LFO, +/- amounts, clamps, slot 2, other dest, per-track, AT not sent, CC still sent, stack +24).
- Emulator: JACK7 boots; DEST list shows SRC:Velocity. Engine doesn't run in the emulator -> velocity behaviour needs MIDI Monitor.
- Open: does the LFO compute the AT slot amount while AT is disabled (X) on the SRC page? If not, AT must be enabled.
- Built JACK7 sha256 cfde826b...

## Session 19: JACK7 hardware report -> JACK7c (chord-knobs 2.4)
- Jack: LFO -> Velocity WORKS on hardware. Minor: after selecting SRC:Velocity, the LFO DEST knob box shows "AT" (short name of the AT
  record, shared with the MIDI Source page AT knob). Left as is (renaming short name would also rename the MIDI Source AT knob).
- Minor: NOT2/NOT3 highlighted ~0.5 s before ROOT/VEL/LEN (stock switches to its held view ~1 s after the press).
  0x41378678 looked like a held-view flag but BLINKS while held (cursor) -> unusable.
  ev_tick runs at 30 ticks/s (measured). chord-knobs 2.4: held values + highlight only after the held mask has been set for
  chord_hold_ticks = 15 ticks (0.5 s); edits while held still apply immediately. Emulator: in sync with stock at 0.5/0.8/1.8/2.8 s.
- Built JACK7c = core + CC HOLD 2.0 + OB-6 NRPN 1.4 + CHORD KNOBS 2.4 + LFO VELOCITY 1.0, sha256 1d7e30b3...

## Session 20: JACK7d = JACK7c with chord_hold_ticks 9 (0.3 s) (chord-knobs 2.5). Jack: 15 was late, 0 was early.

## Session 21: JACK7e (chord-knobs 2.6) - highlight in sync by construction
- Jack: timer versions (0, 15, 9 ticks) inconsistent: the firmware times its held view from the key press, the held mask appears at a varying moment.
- ev_draw bitmap object: +0 vtable 0x401ccd7c, +4 width 128, +8 height 64, +0xc 2, +0x10 pixels (double buffer 0x419cede4 / 0x419cf1e4).
  Pixels: column-major, 8 bytes per column, big-endian 64-bit column, bit y = row y (0 = top). Verified 100% against the screen.
- Held view = ROOT box inverted: pixels (22,20), (46,20), (22,28) white in every held frame, black in every normal frame.
- chord-knobs 2.6: in ev_draw, hv = held mask && those 3 pixels set in the frame being drawn -> show held values + highlight in the same frame.
  Timer removed. Emulator: 52 frames over two holds + releases, ROOT and NOT2/NOT3 highlight agree in all 52.
- JACK7e sha256 758a3deb...

## Session 22: JACK7f (chord-knobs 2.7) - knob press resets NOT2/NOT3
- Knob pushes arrive as key events: G = id 52, H = id 53 (emulator codes EG/EH), flags 1 press / 9 repeat / 0x10 release.
- chord-knobs 2.7: ev_key on the MIDI TRIG page: press G/H -> NOT2/NOT3 = 0 for all held steps with a trig (live + saved), else the page value.
  Encoder and press share chord_set(k, v, relative). Emulator: held step 1 + G -> 3c 00 05 (step 5 untouched); H, nothing held -> new trig 3c 03 00.
- JACK7f sha256 c1ac4132...
- Next asked by Jack: FUNC + knob 0/64/127 jumps on the MIDI CC VAL page (check first whether it's a per-parameter record flag).

## Session 23: FUNC + knob 0/64/127 on the MIDI CC VAL page - quick check
- Records: +0x14 is 1 on FREQ, RESO, DRV, PAN, FX sends, LFO SPD/DEP, PB; 0 on CC VAL1..8, AT, CHAN, VEL, LEV, filter ATK/DEC.
  But NOTHING reads +0x14: its getter 0x40007b36 has no callers/pointers, and no inline 0x4018d468 access. So it's not a "FUNC jump" switch.
- Field getters: 0x40007b36 -> +0x14, 0x40007b5a -> +0x18 (word), 0x40007b7e -> +0x1a (word), 0x40007ba2 -> +0x1c, 0x40007bc6 -> +0x24.
- Conclusion: no cheap data fix found. Options: (a) find the stock FUNC+turn jump code (block-trace FUNC+turn on a synth param vs CC VAL)
  and see why CC VAL is excluded; (b) do it in a mod (ev_enc on MIDI FLTR page with FUNC held -> jump), needs FUNC state + a safe
  "set CC VAL" path that also sends the CC.
- getRange 0x40007a50 (copies min/max/default from record +8) is used on every knob turn (0x4001fefa range check, 0x4002084e),
  with or without FUNC -> FUNC changes what happens after; 261 FUNC-only blocks in a FUNC+turn trace (mostly UI hints), jump code not isolated.
- Emulator test: MIDI FLTR (CC Value) page, VAL1 enabled, FUNC + turn: value stays 0 with stock default AND with default poked to 0x4000
  (record 150 +0x10 = 0x4018f78c) -> inconclusive: the emulator doesn't apply these knob turns.
- Options: (1) test build with CC VAL1..8 default 0x4000 for Jack to try FUNC+turn on hardware (data-only, side effect: new slots start at 64);
  (2) mod route (FUNC state + safe CC set path).

## Session 24: JACK7g (chord-knobs 2.8): knob speed 12 encoder counts per semitone (was 4; Jack: -12..+12 in a quarter turn was too fast). sha256 in chat.

## Session 25: JACK7h = JACK7g + lfo-vel 1.1 (AT record short name -> VEL: data site 0x4018f6bc 401e31cc -> 401dcb24; MIDI Source AT knob also reads VEL).
fwcheck vs stock: PASS, 10/10 screens identical, audio: timing offset only. sha256 c6178fbf...

## Session 26: JACK8 = JACK7h + HUM (lfo-vel 1.2)
- Note-on loop: d3 = note index in the trig's chord (cmpl a2@(16),d3; notes a2@(14+d3)). Hook now passes it.
- lfo-vel 1.2: HUM per MIDI track (knob E = enc 5, x 33, label HUM; press E = key 50 resets), 0..40, 12 counts/step.
  Note-on: vel + LFO (AT slots) + random -HUM..+HUM (xorshift32), new random on chord index 0, reused for the other notes; clamp 1..127.
- CPU test (real binary): all old cases + HUM range/spread/chord reuse/clamp/tracks. Emulator: HUM shows/turns/resets, TEMPO clean.
- JACK8 sha256 81952c32...; full stock comparison not rerun (JACK7h passed it).

## Session 27: JACK8b (lfo-vel 1.3): HUM knob speed 6 counts/step (was 12). 

## Session 28: JACK8c (lfo-vel 1.4): HUM drawn as a stock-style knob
- Stock knob (LEN): radius-8 circle (ring offsets in lfovel.c), 1-px pointer from the centre; slot E box x25..41 y40..56 -> knob centre (33,48).
- Pointer: 270 deg sweep, 225 deg (lower-left, 0) .. -45 deg (lower-right, 40), length 7, table hum_ptr[41]; Bresenham line.
- Label "HUM"; shows the value for 75 ticks (2.5 s) after a turn or reset press, then back to HUM. Pixels written directly (setpix).
- JACK8c sha256 70a30ee7...

## Session 29: JACK8d (chord-knobs 2.10) - bug: held step + VEL edit -> NOT2/NOT3 gone
- Cause: editing VEL on a held step makes the step's root byte 0xFF ("track root", ROOT not locked). chord-knobs treated 0xFF as
  "no trig": stopped drawing/editing; and the held-view test used the ROOT box inversion, which the firmware only draws when ROOT is locked.
- Fix: no 0xFF test (a held step always shows/edits its offsets); held view = COND box bottom edge pixels (106,29),(111,29),(116,29)
  (white in every held frame incl. root 0xFF, black in every normal frame).
- Emulator: simulated root 0xFF on a held chord step (emulator doesn't apply held VEL edits): JACK8c reproduced (G ignored);
  JACK8d: G resets, NOT2/NOT3 highlighted with the step's values while ROOT is not. JACK8d sha256 e7caf0f4...

## Session 30: RANDOMIZE (MIDI scatter + pitch) - phase 1 research (no builds)
- Per-step data of a track, same layout in live (ed+0x10) and saved (ed+0x20) copies (ed = 0x41a73294 + 0x58*track):
  +2*step: halfword step flags, bit0 = TRIG (1 placed, 0 none)      [verified by diffing placement / removal]
  +0xC0+step: byte, 0x0E (empty) -> 0xFF when a trig is placed (stays 0xFF after removal)
  notes row: live +448+8*step, saved +384+8*step: root + 7 signed offsets; row[0] 0xFF after removal
  length (steps) = live +966 (byte, 16 -> 15 verified on FUNC+PAGE); page values block +960: ROOT, VEL, LEN, ... (+966 = length)
- Placing a trig by hand changes exactly 3 bytes per copy: flag, +0xC0+step = 0xFF, note row root; removal: flag 0, row[0] 0xFF.
- Stock place/remove = setStepFlag 0x4003673a(editor, step, mask, value): place (ed, step, 1, 1); remove (ed, step, 1, 0) then (ed, step, 2, 0).
  Other callers use masks 0x200 / 0x400 with value -1/0. Inside: flag write 0x400367fa, setStepNote 0x4003666c (+ chord stamp hook), commit job 0x40036f1a (live -> saved).
  Calling it from a mod = same effects as pressing a trig key (incl. NOT2/NOT3 stamp).
- Copy routine live->saved (0x40008db2) writes the flag as a halfword at 0x40008e1c.
- Pitch randomize = write row[0] (live + saved) of steps with flag bit0; chord offsets untouched.
- Scatter = per step call setStepFlag; density from a knob. No undo yet (idea: snapshot flags+roots to RAM, FUNC+press = undo).
- MIDI Source page (SYN2, page byte 2): slots G (MW) / H (BC) free for RND / PIT knobs; knob push = key 52 / 53 (as NOT2/NOT3).
- Not verified: sequencer reads flags/roots straight from the live copy (engine not observable in the emulator); p-lock bytes left on a removed step.

## Session 31: JACK9 = JACK8d + RANDOMIZE (rand-seq 1.0)
- New mod, no firmware patch sites: only events (ev_enc/ev_key/ev_draw/ev_tick). MIDI Source page (page byte 2, same main-screen test as the chord mod).
  Knob G (enc 7, x 85) = RND 0..100 % in steps of 2 (6 counts/step), knob H (enc 8, x 111) = PIT 0..36; pushes = key 52 / 53. Stock labels MW / BC and the X boxes are overdrawn.
- Press G: clear trigs in 0..len-1 with the stock routine 0x4003673a(editor, step, 1, 0) + (.., 2, 0), then place n = round(len*pct/100) random distinct steps with (.., 1, 1);
  then copy flag halfword, aux byte (+0xC0+s) and note row live -> saved for all steps in 0..len-1 (idempotent with the stock commit job).
- Press H: for every step with flag bit0, root = TRIG-page ROOT (live[960]) + rand(-PIT..+PIT), clamped so root+offsets stay in 0..127; written to live row[0] and saved row[0].
- 0 = a press does nothing. Step flag halfword: bit0 = trig; other bits (e.g. 0x10 on odd steps) are preserved.
- Emulator: RND 50% on 16 steps -> 8 trigs each press, new positions each time, NOT2 +3 stamped, saved == live; PIT 12 -> roots 49..66 around 60, offsets (3,7) kept;
  presses at 0 change nothing; FUNC held and synth tracks untouched. Full stock comparison NOT rerun.
- JACK9 sha256 eb276dd2...

## Session 32: review of elekloader docs/ADAPTING.md (clone updated to 69707b3: core-dn1 2.1 -> 3.0)
- ev_key flags, measured on the Digitone by elekloader's author: 1 pressed, 2 FUNC held, 4 double press, 8 repeat (once, ~0.4 s into a key held alone), 0x10 release.
  => the FUNC state IS in the key flags (matters for a FUNC + knob feature). ev_enc has no FUNC flag (ev+12 encoder 1-8, ev+16 delta).
  My handlers test fl == 1: FUNC + press (3) and the 2nd press of a double press (5) are swallowed without action. Intended / harmless.
- Newer events: ev_hold (core-dn1 2.2): a track key held on its own; unless a handler takes it, core opens the Mod Menu.
  ev_voice_on (2.1): a synth voice starts a note; its pitch word fw_voice_pitch (1.44: 0x41392f80 + 4*voice, note << 16) may be changed.
  ev_settings: a mod can add rows to the SETTINGS menu (a place for e.g. the OB-6 channel, HUM/RND limits).
- core-dn1 3.0 firmware locations, 1.44 (RAM moved +0x1000 vs 1.43): fw_fillrect 0x400dd506 (bmp,x0,y0,x1,y1,colour; <0 XOR, 1 set, 0 clear),
  fw_framerect 0x400dd2ea, fw_textf 0x400de0dc (bmp,font,x,y,maxlen,fmt,...; ORs pixels; my DRAWTEXT 0x400de120 is the inner routine with flags),
  fw_font5 0x402319c8 (stock 5-pixel font), fw_blit 0x400de4c0, fw_op_new 0x400e96c0, fw_lfo_state 0x419eb314 (0x50 bytes per voice: LFO1 at +0x50*v,
  LFO2 +0x28 on; destination slot +0x40, modulation 8.8 at +0x44; same family as my MIDI modulation slots at 0x419eb1fc), fw_kit 0x4138f220,
  fw_ev_alloc/free/queue 0x400ffff2 / 0x40100028 / 0x40100178 (the note queue).
- Core-owned patch sites on 1.44: 0x40000538, 0x4001900c, 0x40019072, 0x40019d9c, 0x40019de4, 0x40072a54, 0x4009d128, 0x4009e53c, 0x4009e948.
- Lint (the doc's "definition of done"): all five mods exit 0 with core 2.0a and with each other; combined RAM 7276 of 131072 bytes.
- To share a mod: declare named resources in mod.json (resources.names, e.g. key combinations "ui:<page>:<combo>", settings rows) - my knob-press handlers are NOT declared yet;
  one OS per file: a mod with patch sites needs a "ports" entry for another OS (mine are 1.44 only); a mod with no sites that uses only core exports builds for 1.43 and 1.44
  with an empty port ("resources": {"core": "3.0"}, #include digitone-mk1/core3.h).
- The newer core (parameter slots struct core_param, pages core_page, project data core_projdata saved in the project, Mod Menu) would allow NOT2/NOT3/HUM/RND/PIT
  as real saved parameters; moving all mods onto it means re-testing everything.
- Header popup (stock-style "Name=Value" in the title bar for NOT2/NOT3/HUM/RND/PIT): estimate revised from ~20-25 % to ~10-15 % of a session
  (fw_fillrect + text routine + font5 exist); remaining unknown = which font and offset the stock title bar uses (one test build with poked font/coords + screenshot). Jack: skipped for now.

## Session 33: JACK10 = JACK9 + stock-style header popups (chord-knobs 2.11, lfo-vel 1.5, rand-seq 1.1)
- Stock popup = title bar after the A01 box: white bar x 17..127, rows 0..9 (y from bottom 54..63), corner pixels (17,0),(127,0),(17,9),(127,9) black, text black, 3 s.
- Recipe: fw_fillrect(bmp,17,54,127,63,0); DRAWTEXT 0x400de120(bmp, font 0x418e934c, x 20, y 56, flags 0 = left, fmt, v); fw_fillrect(...,-1) (XOR -> black text on white); clear the 4 corners.
  DRAWTEXT flags: 2 = centred (0x22 centred + 0x20), 0 = left. fw_textf + font5 (0x402319c8) is bolder and does NOT match; the label font matches. Font quirk: lowercase g draws like a 9 -> avoid g in names.
- Texts: Chord Note 2=+3 / =Off, Chord Note 3=..., Velocity Humanize=N, Random Hits=N%, Random Pitch=N. Shown on every turn and press for 90 ticks (3 s); the label under HUM/RND/PIT also 3 s now.
- Emulator: popups on NOT2/NOT3/HUM/RND/PIT turn + press, header back to the pattern name 3.6 s later, TEMPO / FUNC untouched. Throwaway font-search mod hdrtest not shipped.
- Lint: all five mods exit 0 with core 2.0a and each other. JACK10 sha256 below in the chat; stock comparison NOT rerun (draw-only change).

## Session 34: JACK11 = JACK10 + bell-curve HUM (lfo-vel 1.6) + scale-quantized PIT (rand-seq 1.2)
- HUM: random = mean of three flat draws (always within -HUM..+HUM) instead of one flat draw; LFO part still clamps first; the humanize term REFLECTS at the limits (254 - v, 2 - v).
  Real compiled code, 6000 draws at HUM 40: outer half of the range 49.9 % -> 13.7 %; within +/-13 of the centre 33 % -> 66 %; piled on exactly 127 at VEL 110: 29.8 % -> 1.4 %, at VEL 127: 51.3 % -> 2.7 %.
- SCL knob = knob E (enc 5, x 33) on the MIDI Source page (PB slot), press E (key 50) = scale off; 12 counts per step; 0 Off, 1 Major, 2 Minor, 3 Dorian, 4 Lydian, 5 Mixolydian, 6 Harm Minor, 7 Penta Major, 8 Penta Minor, 9 Blues
  (masks 0xFFF 0xAB5 0x5AD 0x6AD 0xAD5 0x6B5 0x9AD 0x295 0x4A9 0x4E9; Phrygian dropped: the label font draws g like 9). Popup "Scale=Name", label under the knob 3 letters.
- PIT: root = TRIG-page ROOT; every trig gets a uniform pick among the scale notes in [ROOT-PIT, ROOT+PIT] (chord offsets keep all notes in 0..127); Off = uniform chromatic (no pile-up at the ends). Chord offsets are not quantized.
- Emulator: page ROOT 63, 8 trigs, 112 presses-samples per scale: Major / Penta Minor / Blues / Off: all in scale, all degrees seen, range exactly +/-24. Scatter regression ok. Lint: all five exit 0. Stock comparison not rerun.

## Session 35: JACK11b (lfo-vel 1.7) - HUM slightly broader
- Jack: JACK11 humanizer a tiny bit too close to the centre. Weights of the three flat draws 1:1:1 -> 4:2:1 (r = (4a+2b+c+3)/7 - h, a,b,c in 0..2h; still within -h..+h).
- Real compiled code, 6000 draws, HUM 40, VEL 64: outer half 13.7 % -> 20.3 % (flat 50 %), within +/-13: 66 % -> 56 %, reached 26..103 (limit 24..104); VEL 110: exactly 127 1.9 %.
  Simulation of candidates (300k draws): rms 13.5 -> 15.4 (+14 %), within 4 of the limit 0.2 % -> 0.4 % (flat 10 %).
- Alternative left untouched: raise HUM_MAX (40) in lfovel.c. JACK11b sha256 9d5fe385...

## Session 36: JACK11c (lfo-vel 1.8) - HUM broader, max 60
- Jack: JACK11b still too medium. HUM_MAX 40 -> 60; curve = mean of two flat draws (triangle, r = (a+b+1)/2 - h); reflect at the limits kept; knob pointer = (v*40 + 30)/60.
- Real compiled code, VEL 64: HUM 40 outer half 20.3 % -> 25.2 %; HUM 60 reaches 4..124 (outer half 23.7 %). Triangle: within 10 % of the limit ~1.6 % of notes (flat 10 %).
- Next knobs if still not enough: HUM_MAX or a flatter curve (flat draw = 50 % outer half). JACK11c sha256 below in the chat.

## Session 37: JACK12 = JACK11c + rand-seq 1.5 (gravity, chords in key, UNDO knob, SYN1 fix)
- MIDI Source page: page byte 1 (SYN1) and 2 (SYN2) show the SAME stock page; rand-seq used to act on 2 only -> now both (knobs also from SYN1).
- Knobs: D (enc 4, PROG slot, x 111 / y 22, key 49) = UNDO, E (enc 5, key 50) = SCL, G (enc 7, key 52) = RND, H (enc 8, key 53) = PIT. Top-row stock label rows 32..36 (y 29 from bottom); UNDO knob drawn before the bottom row (its label clear ends row 38).
- Key flags on a press: 1 pressed, 2 FUNC held (3 = FUNC + press). UNDO = FUNC + press on D; plain press / turn = hint "FUNC + press = Undo". Plain presses on E/G/H ignore FUNC+press.
- UNDO: per MIDI track, 8 presses deep, RND and PIT only (not manual steps). Before a press: copy flags (128 B), aux (+0xC0, 64 B), note rows (512 B) of the live copy; after: compare, keep the old data and a 64-bit mask of CHANGED steps; undo restores only those steps (live + saved).
  Manual edits on other steps survive an undo (test: 4 manual trigs, PIT, manual trig on step 3, undo: roots restored, step 3 kept). ~23 KB .bss. Copies use volatile stores (no memcpy import).
- PIT gravity (scale on): weight 30 root / 22 fifth / 19 third / 7 other; scale Off = flat. 400 roots in Major: root 31.8 %, fifth 21.8 %, third 17.2 % (expected 35/21/18).
- Chords in key (scale on): chord notes are snapped ONCE to the scale (nearest, tie down, never the root), stored as scale-step distances from the root, and re-applied to every new root (no drift).
  Major: 192/192 chords keep the (2,4) triad shape; Penta Minor: shape constant per trig; scale Off leaves chord offsets untouched. First attempt (semitone snapping every press) degenerated to (+2,+2): replaced.
- JACK12 sha256 in the chat. Lint: all five exit 0. Stock comparison not rerun.

## Session 38: catch-up JACK12b -> JACK16b (rand-seq 1.6 -> 4.2; chord-knobs 2.12; lfo-vel 2.0). Jack: JACK16b works on his unit.
- Windows / black screen: firmware windows (SONG, PTN popup) are drawn BEFORE ev_draw, so the mods used to paint over them. Fix = each mod draws, and takes knob events, only while its slots
  still show the STOCK SLOT BOX: dotted outline, lit at (cx-7+2m, cy-8), (cx-7+2m, cy+8), (cx-8, cy-7+2m), (cx+8, cy-7+2m) for m = 0..7, need >= 28 of 32.
  Slot centres cx 33 / 59 / 85 / 111; bottom row cy 48, top row cy 21. A black screen fails the check. Draw sets vis_lay; handlers require it.
  The firmware closes a PTN popup / SONG window on the first knob event: window tests must send ONE event.
- Page byte 0x41368ce7: TRIG 0, SYN1 1, SYN2 2, FLTR 3, AMP 4 (AMP 1/2 vs 2/2 NOT distinguished -> told apart by the slot check). Active track 0x41368ce3: 0-3 synth, 4-7 MIDI.
  On a MIDI track SYN1 and SYN2 open the SAME MIDI Source page; the mod uses SYN1 only (page byte 1), SYN2 stays stock.
- Track editor table 0x41a73294 + 0x58*t: live ptr +0x10, saved ptr +0x20, track index +0x38. Live blocks: synth 0x407fd414, 0x407fd7e4, 0x407fdbb4, 0x407fdf84; MIDI 0x407fe354, 0x407fe724, 0x407feaf4, 0x407feec4
  (stride 0x3D0); saved 0x405561f8 + 0x3D0*t. Synth and MIDI tracks use the SAME layout: step flag halfword at +2*step (bit 0 = trig, other bits kept), aux byte +0xC0+step,
  note row live +448+8*step / saved +384+8*step (root + 7 signed offsets), page values +960.. (ROOT, VEL, LEN...), pattern length byte +966 (synth track 1 = 64 in the demo project).
- Stock place / remove: 0x4003673a(editor, step, mask, on): place (ed, s, 1, 1); remove (ed, s, 1, 0) then (ed, s, 2, 0). Key flags: 1 press, 2 FUNC held. Knob pushes A..H = ids 46..53, encoders 1..8.
- Layouts (one shared mod, a layout table per page): MIDI SYN1 bottom row E..H = SCL, UNDO, RND, PIT (pushes 50..53); synth Amplitude 2/2 top row A..D = SCL, UNDO, RND, PIT (pushes 46..49).
  Reset switch: MIDI PROG slot (enc 4, key 49, 111/21); synth Amplitude 1/2 empty slot F (59/48, enc 6, key 51).
- Timeline (rand-seq 3.0): 8 entries per track + cursor. An entry keeps the OLD data (flags, aux, rows, note memory) of the steps a press changed; undo and redo SWAP it with the pattern (live + saved).
  A new press drops the redo branch. UNDO knob: turn left = undo, right = redo, 12 counts per step, >= 6 ticks between steps, a change of direction restarts the count; FUNC + press = undo.
- Note memory rs_nroot[8][64] (root + 1, 0 = none): PIT gives EVERY step a note (trig or not); RND captures the roots of the trigs it removes, places new trigs with the stock routine, then moves the stamped
  chord to the remembered root (scale steps if a scale is on). RND never touches the memory otherwise. PIT: floor C2 (24), upward tilt 1.0 -> 1.6, gravity weights root 22 / fifth 17 / third 15 / other 8,
  scale off keeps chord notes <= 127.
- HUM (lfo-vel 2.0): mean of two flat draws, reflect at 1/127, HUM_MAX 60; two trigs in a row >= 15 apart (4 redraws, then step +/-15 toward the trig's own velocity); chord notes share the trig's velocity.
- Reset switch (rand-seq 4.2): tg_off / tg_on = 17x9 pixels copied from stock toggles on the synth TRIG page (PORT = off: small lever left, black background; FLT.T = on: filled). The LFO.T of the emulator
  project is ON and the emulator does not toggle it, which is why the first version drew the wrong rest picture. Throw = 24 counts right, shown ON for 0.8 s, then back. It wipes the track's trigs (stock remove),
  zeroes RND/PIT/SCL, the note memory and the history; not undoable.
- Tools: dn_drive.py now has AUDIO=1 + wav:NAME + apos:NAME (audio capture) and view2; enc:N:k sends k*4 counts. Test pattern used: read live flags/rows/saved rows/note memory after every step and compare with the state
  the model predicts (timeline test: 26 steps, 0 mismatches). Test harness mapping for the mods' RAM must be >= 256 KB (rand-seq alone uses ~51 KB .bss).
- Builds: JACK12b 0927c65c, JACK13 3372699f (full stock comparison PASS = the last one run), JACK14 797ed5a5, JACK15 ad0d7e1e, JACK16 1bb28812, JACK16b ed60b0a6. No stock comparison since JACK13 (Jack: not yet).
