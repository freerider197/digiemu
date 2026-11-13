# Decision log

What was decided, why, and what was rejected. Roughly chronological within each group. "Jack" = the owner/requester.
Where a decision was Jack's explicit instruction it says so; otherwise it was Claude's engineering choice that Jack accepted.

## A. Foundations

**A1. Linkable elekloader mods on core 2.0a, not hand-patched binaries.** elekloader verifies every byte it writes,
checks mods against each other and never touches the bootloader, so the stock OS file always recovers the unit.
Mods stay independent (five small ones), so a problem can be bisected by leaving one out. (elekloader was the starting point of the project;
no alternative loader was evaluated.)

**A2. Stay on core-dn1 2.0a for OS 1.44.** It is the release asset of elekloader v0.4.0 and every build uses it
(`SHA256SUMS.txt`). The elekloader repo has since moved to core-dn1 2.3 and 3.0 (README: 1.43 and 1.44); what they change
for our mods is unknown, so nothing was upgraded. Revisit only if there is a reason (see OPEN_ITEMS).

**A3. Boot label "JACK".** Jack asked for a recognisable firmware name ("Jack Firmware CC", later "Jack Firmware" /
"JACK FW" were fine); no boot animation. Builds are named `Digitone_OS1.44-JACK<n><letter>.syx`.

**A4. Safety workflow.** Build -> lint all mods together -> emulator tests -> hand to Jack with a sha256 prefix.
Jack flashes himself and reports. Every superseded build is kept. The full cold-boot comparison with stock was last run
on JACK13 and is not rerun unless he asks.

## B. MIDI behaviour mods

**B1. CC HOLD = a one-byte patch** (`0x400f44ca`, force flag 1 -> 0). Goal: restarting the sequencer must not resend
MIDI-track CCs that overwrite the OB-6 knob settings. The first attempt (cc-hold 1.0, two other sites) was inconsistent
on his unit, both with manual STOP/PLAY and Ableton transport via Overbridge; 2.0 works.

**B2. OB-6 NRPN: always send the full four-message sequence, scaled per parameter.** NRPN on the OB-6 is the only way to
reach e.g. the noise level. 0-255 ranges after his own capture showed the cutoff NRPN reaching 255; a data-entry-order
rule avoids the OB-6's 127<->128 spike. Only MIDI channel 14 is touched.

**B3. NOT2 / NOT3 chord knobs on the MIDI TRIG page, slots G and H** (Octatrack-style). Values are *stamped into a trig
when it is placed* (existing trigs do not follow later knob changes); hold a step to edit its chord (p-lock style).
Slots G/H because Jack asked for the two right-most free TRIG-page slots after JACK5c's first placement made the knobs
vanish while a step was held. Pressing a knob resets that note to off; knobs turn slowly; highlighted like ROOT when a
step is held.

**B4. Velocity as an LFO destination by repurposing "AT".** He never uses aftertouch. Accepted nit: the DEST box still
shows "AT" after selecting it.

**B5. HUM (velocity humanizer) as a knob in TRIG slot E.** Range grew from 40 to 60 and the distribution became bell-shaped
(mean of two draws, reflected at the limits) because the first version had "too many extremes, not enough middle";
`HUM_GAP = 15` keeps consecutive trigs at least 15 apart in velocity. Chord notes share their trig's velocity.

## C. Randomizers (rand-seq)

**C1. One-press, sequencer-style, MIDI-first.** Jack likes old 8/16-step random sequencers: scatter trigs (RND) and re-roll
pitches (PIT). Later extended to the synth tracks, **only** on Amplitude page 2/2, nowhere else.
*Rejected:* a "walk" mode (he called it just a scale run).

**C2. Slots and order.** Same four controls left to right on both pages: SCL | UNDO | RND | PIT. On MIDI tracks they use the
stock PB / VEL / MW / BC slots (all unused by him) of the MIDI Source page, reached from **SYN1 only**; SYN2 stays the
stock page (he did not want the page duplicated). UNDO therefore sits in the old aftertouch slot.

**C3. Visibility by stock slot box.** Knobs are drawn and active only if the dotted stock box is still visible under
them. This hides them under the Save-project and SONG windows, popups and the black power-saving screen with one rule.
(Not recorded as a comparison: this was Claude's design, chosen after the knobs were first found drawn on top of windows.)

**C4. Header popups for all added knobs** (name + value for ~3 s), like stock knobs.

**C5. One press = one action.** The firmware sends one auto-repeat (bit 8) ~0.4 s into a held key; counting it gave two
creations and two undo steps. Repeats are swallowed.

**C6. RND and PIT are independent through a per-step note memory.** A generated pitch belongs to a *step*, not a trig:
PIT writes all 64 steps, RND takes the remembered note for each new trig and remembers the notes of trigs it removes.
So the same notes can be heard against different trig patterns (Jack's request). Memory is per track, not saved to the
project (he always uses a single pattern, so carry-over across patterns does not matter).

**C7. PIT shape.** +/-PIT around the TRIG page ROOT; never below C2 (MIDI 24); upward lean (top 1.6x as likely as the
bottom); gravity root 22 / fifth 17 / third 15 / others 8 (built in, no separate knob); chords follow in scale steps and
chords entered by hand are never snapped; the exact ROOT note and its octaves get an extra pull (7 % / 2 % of the other
weights, about 10 % of notes) because it came up too rarely ("a reminder, not a lot").

**C8. The scale is not part of undo and not part of reset.** He picks one scale and sticks to it. (He first wrote
"undo" but meant "reset"; undo never touched it - tested, and the notes undo restores keep the scale they were made in.)

**C9. Undo: swap-based timeline, 8 deep per track.** Turning UNDO left = undo, right = redo (so he can step back and forth
without regenerating); FUNC+press = undo. A new press while stepped back drops the redo branch. Everything a press
changes is in the entry (trig flags, LEN bytes, note rows, note memory, LEN+ memory), and only for steps that actually changed.

**C10. Reset switch (RST), spring-loaded.** A toggle drawn like the stock LFO.T: at rest the stock OFF picture (small
lever left, black background), the stock ON picture (filled) while thrown. A firm 24-count turn right throws it, a left turn
does nothing, it springs back after ~0.27 s (Jack: 0.8 s felt slow). It resets RND/PIT/LEN+ values, wipes the remembered
notes and the undo history and removes **all trigs** (Jack agreed: "like after a boot"); keeps the scale; not undoable.
Placed in the MIDI Source PROG slot and on the synth Amplitude 1/2 empty slot F. The artwork was copied from real stock
toggles on the synth TRIG page (PORT = off, FLT.T = on) - the LFO.T in the emulator project happened to be ON, which is why
the first drawing was wrong.

**C11. LEN+ (random trig length), MIDI only, press not live.** Chosen by Jack over a "live" variant (random extra length per
played note). Knob in the SBNK slot above RND. Percent = the share of trigs that get longer (exactly that count, random
choice). Each of those gets a random length **strictly longer** than its original and at most +4 steps (his wording: "between
the original and up to 4 steps longer"; JACK18's whole steps 1-4 were replaced by this in JACK18b). Always computed from the
remembered original so presses never accumulate; a hand-edited length becomes the new original. **At 0 % a press restores
the originals** (JACK18c), because otherwise he could not get back to the TRIG page lengths without a full reset. Hand
edits made since LEN+ stay. Undoable.

## D. Display

**D1. Page-byte lag fix (JACK17).** The page byte changes one frame after the screen. All three UI mods now learn the new page
from the page keys (ids 20-25) first. Diagnosed by logging, per drawn frame, why each knob was or was not drawn.

**D2. No letter "g" in popup/label text.** The firmware font draws g like a 9 ("Len9th"). Use "Len", not "Length".

## E. Closed and rejected

**E1. MIDI THRU jack as a second MIDI output - dropped.** Software can only reach the jack's two sync pins; the test build
produced no MIDI on his unit. See THRU_INVESTIGATION.md.

**E2. How the LEN array was identified.** Holding a step in the emulator did not produce the held-step view (also on the stock
firmware, 2026-10-15), so planting values per array and reading the knobs was not possible. The LEN array was identified from
the parameter table instead (LEN = parameter index 2 -> array at `+192`); the real effect is unconfirmed until Jack tests JACK18c.

## F. Process decisions (Jack's instructions)

- Concise answers; one best-guess build for small tweaks; a summary + token estimate before larger builds; ask whether he
  builds or Claude builds for any new firmware work; no unrequested extras (ideas listed at the end); "move" means move.
- Update checks: when he asks what is new, track the watch list and compatibility yourself and hand him only the usual
  command sequence; keep each device's discussion in its own chat (this project also covers an Octatrack and a Rytm, in
  other chats - out of scope here).
