# Version history

Every `.syx` is stock OS 1.44 + the mods of that build. `sha8` = first 8 characters of `shasum -a 256`.
"Jack" column = what he reported (from the chat); "not recorded" means nothing was said about that build.
Jack keeps all of them; never delete old files. The files live in his OS folder; they are **not** in this repo.

From JACK16 on, mod versions are known: chord-knobs 2.12 and lfo-vel 2.0 until JACK16e, 2.13 / 2.1 from JACK17;
cc-hold 2.0 and ob6-nrpn 1.4 throughout. rand-seq versions are in the table.

## CC HOLD and OB-6 NRPN (before the "JACK" series)

| Build | sha8 | What | Jack |
|---|---|---|---|
| cchold (CCH1) | 4f9727d1 | cc-hold 1.0 | flashed and ran; CC resend on restart still inconsistent (manual STOP/PLAY and Ableton transport via Overbridge) |
| cchold2 (CCH2) | cc5ee3b4 | cc-hold 2.0 (one-byte patch) | seems to work |
| cchold-ob6nrpn (NRP1) | de1b6e69 | first OB-6 NRPN build | CCs became NRPNs and moved the right parameters, but many ranges only reached about halfway |
| JFCC | 5fbfb21d | "Jack Firmware CC" boot label build | not recorded |
| JACK | 800a8ed6 | first build labelled JACK | not recorded |
| JACK2 | ee0078c9 | full 99/98/6/38 NRPN sequence every time | OB-6 cutoff still stopped at ~2 kHz at max; his own capture showed the NRPN reaching 255 |
| JACK3 | 67f48172 | no details recorded | |
| JACK4 | 6c415f3b | CC HOLD + OB-6 NRPN with 0-255 ranges and the midpoint-glitch fix | works |

## Chord knobs, velocity LFO, HUM

| Build | sha8 | What | Jack |
|---|---|---|---|
| JACK5c | 0596d5c6 | NOT2/NOT3 first version | NOT2/NOT3 vanished while a step was held: did not work |
| JACK6 / 6b / 6c | ffb74172 / 53fb4b54 / 1f598331 | NOT2/NOT3 on the right-most free TRIG slots G/H, editable on held steps, highlighted like ROOT | 6c seems to work |
| JACK7 | cfde826b | MIDI-track LFO destination "AT" modulates velocity | works; nits: DEST box still says "AT", NOT2/NOT3 highlight slightly early |
| JACK7c | 1d7e30b3 | highlight timing tweaks | inconsistent (7d also) |
| JACK7e | 758a3deb | highlight synced to the stock knobs | works |
| JACK7f | c1ac4132 | pressing a NOT2/NOT3 knob resets it to off | works |
| JACK7g | f3da7c55 | slower NOT2/NOT3 knobs | works |
| JACK7h | c6178fbf | no details recorded | |
| JACK8 | 81952c32 | HUM knob (slot E, MIDI TRIG page), max 40 | works; wanted it faster and stock-looking |
| JACK8b / 8c | 382f16df / 70a30ee7 | HUM faster / drawn as a stock-style knob | work |
| JACK8d | e7caf0f4 | fix: VEL edit on a held step made NOT2/NOT3 vanish | confirmed fixed |

## Randomizers (rand-seq) and later

| Build | sha8 | rand-seq | What | Jack |
|---|---|---|---|---|
| JACK9 | eb276dd2 | | RND (knob G) and PIT (knob H) on the MIDI Source page | seems to work |
| JACK10 | e873301b | | header popups (name + value) on NOT2/NOT3, HUM, RND, PIT | later: all seems to work |
| JACK11 | 24f5959e | | PIT quantised to a scale; scale knob in the PB slot | works; HUM still slightly too central |
| JACK11b | 9d5fe385 | | humanizer curve broadened | not recorded |
| JACK11c | 4d260048 | | no details recorded | |
| JACK12 | e79588b8 | | PIT gravity, chords stay in key, UNDO knob (PROG slot) | seems to work; knobs stayed drawn over windows and the black screen |
| JACK12b | 0927c65c | | knobs hide when a window covers the page; MIDI Source knobs on SYN1 only | not recorded |
| JACK13 | 3372699f | | synth-track randomizers (Amplitude 2/2), same order on both pages, UNDO by turning left; **last full stock comparison (passed)** | not recorded |
| JACK14 | 797ed5a5 | 3.x | undo AND redo (8 deep), per-step note memory (RND/PIT independent), hidden on the black screen | works |
| JACK15 | ad0d7e1e | | PIT leans upward (nothing below C2); HUM keeps consecutive trigs >= 15 apart | seems to work |
| JACK16 | 1bb28812 | 4.1 | reset switch RST (MIDI PROG slot, synth Amp 1/2) | works except the drawing |
| JACK16b | ed60b0a6 | 4.2 | switch drawn like LFO.T (off = lever left on black, on = filled) | works |
| JACK16c | 47950add | 4.4 | exact ROOT note comes back ~1 in 10 | (used with 16d) |
| JACK16d | 0333c1ed | 4.5 | reset no longer resets the scale | works; wanted no key-repeat and a faster spring |
| JACK16e | 1096d774 | 4.6 | one press = one action; spring 0.27 s | works; then reported late-appearing / flickering knobs on page changes |
| **JACK17** | 273a8af5 | 4.7 | page-flicker fix (chord-knobs 2.13, lfo-vel 2.1, rand-seq 4.7) | **running on his unit**; result of the flicker fix not reported |
| THRUTEST | 0822c996 | (thru-test 0.1) | experiment: MIDI notes on the THRU sync pins | nothing came out; idea dropped; he went back to JACK17 |
| JACK18 | b519ccdc | 5.0 | LEN+ knob (SBNK slot above RND) | not tried |
| JACK18b | 50a10ffc | 5.1 | LEN+ picks any length from just above the original up to +4 steps | not tried |
| **JACK18c** | 7868c91f | 5.2 | LEN+ at 0 % restores the original lengths | **not tried** |

Notes
- The text file `Digitone_JACK_builds.txt` (kept in Jack's OS folder) is a shorter user-facing version of this list;
  update both when a new build is made, if he asks.
- Intermediate rand-seq numbers (1.x, 3.x) are approximate; only 4.1 onward are certain.
- Mod binaries of JACK18c: `build/elemods/` with `SHA256SUMS.txt`.
