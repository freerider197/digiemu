# Firmware reference - Digitone / Digitone Keys OS 1.44

Everything here was found by reading the 1.44 disassembly and by watching the emulator; nothing comes from Elektron
documentation. Status words: **verified** (seen in code and/or emulator data), **inferred** (reasoned from structure),
**unconfirmed on hardware**.

## 0. Regenerate the disassembly (you will need it)

The main OS is section 3 of the stock `.syx` (2,736,304 bytes for 1.44, sha256 starts `fce648a97c6c5d93`), loaded at
0x40000400. The CPU is a ColdFire V4e (MCF5441x); the Digitone also has a second processor for the voices.

```bash
python3 - <<'EOF'
import elekloader.syx as s          # run inside the elekloader checkout
x = s.Syx.load('Digitone_and_Digitone_Keys_OS1.44.syx')
open('dn144_main.bin', 'wb').write(x.section(3))
EOF
m68k-linux-gnu-objdump -D -b binary -m m68k:cfv4e --adjust-vma=0x40000400 dn144_main.bin > dn144.dis
```

The disassembly is a flat listing: `grep -n "^400f4846:" dn144.dis` then `awk 'NR>=N-30 && NR<=N+40'`. Constant
pools and tables appear as bogus instructions; read data straight from `dn144_main.bin` at `addr - 0x40000400`.

## 1. UI state

| What | Address | Notes |
|---|---|---|
| Active track | `0x41368ce3` (byte) | 0-3 synth tracks, 4-7 MIDI tracks |
| Current page | `0x41368ce7` (byte) | TRIG 0, SYN1 1, SYN2 2, FLTR 3, AMP 4 (LFO presumably 5, unconfirmed). **Lags the screen by one frame.** AMP pages 1/2 vs 2/2 are not distinguished by this byte |
| UI "brain" | `0x41a0a7a0` | `+0x58` SCR_A, `+0x5c` SCR_B (screen list nodes; object = node - 16) |
| Main track-page screen method table | `0x40198694` | `main_screen()` compares the list object's first word with this |
| Held-step mask | `0x41a5b7f8` / `0x41a5b7fc` | 64-bit, one bit per held step |
| Settings block (global) | around `0x41368ce0..0x41368d90` | e.g. THRU PORT FUNC byte `0x41368d63` |

Key ids (the same numbers the emulator driver uses): FUNC 1, MIDI 2, KEYBOARD 3, PTN 4, BANK 5, SONG 6, GLOBAL (settings)
7, VOICE 8, TEMPO 9, RECORD 10, PLAY 11, STOP 12, YES 13, NO 14, UP 15, DOWN 16, LEFT 17, RIGHT 18, PAGE 19, TRIG 20,
SYN1 21, SYN2 22, FLTR 23, AMP 24, LFO 25, TRIG1..TRIG16 = 26..41, T1..T4 = 42..45, knob pushes A..H = 46..53, LEVEL 54.
Encoders 1..8 = A..H. Key event flags: 1 press, 2 FUNC held, 8 auto-repeat.

Screen: 128x64 1-bit. Slot centres x = 33, 59, 85, 111; y = 21 (top row), 48 (bottom row); a slot box is 17x17.

Page layouts used by the mods (stock labels):
- **MIDI TRIG page**: top row ROOT, VEL, LEN, PROB (COND replaces PROB while a step is held); bottom row HUM (E, ours),
  LFO.T (F, stock toggle), NOT2 (G, ours), NOT3 (H, ours).
- **MIDI Source page** (SYN1 or SYN2 key): top row CHAN, BANK, SBNK (ours: LEN+), PROG (ours: RST switch); bottom row
  PB (ours: SCL), VEL / old AT (ours: UNDO), MW (ours: RND), BC (ours: PIT).
- **Synth Amplitude 2/2**: top row four empty slots (ours: SCL, UNDO, RND, PIT). **Amplitude 1/2**: empty slot F (59,48) = RST.

## 2. Track data blocks (verified in emulator)

Editor objects: `0x41a73294 + 0x58*t` (t = 0..7). In each: `+0x10` pointer to the **live** block, `+0x20` pointer to the
**saved** block (the copy written to the project), `+0x38` track index.

| Block | Address |
|---|---|
| Live, synth tracks 0-3 | `0x407fd414`, `0x407fd7e4`, `0x407fdbb4`, `0x407fdf84` |
| Live, MIDI tracks 4-7 | `0x407fe354`, `0x407fe724`, `0x407feaf4`, `0x407feec4` (MIDI 1 = `0x407fe354`) |
| Saved | `0x405561f8 + 0x3D0*t` (MIDI 1, t = 4: `0x40557138`). Block stride is `0x3D0` for live and saved |

Layout inside a block (offsets in bytes; `s` = step 0..63). Synth and MIDI tracks use the same layout:

| Offset | Content |
|---|---|
| `+2*s` | trig flag halfword; bit 0 = trig (other bits kept untouched) |
| `+128+s` | per-step lock array, parameter index 1 (VEL): 0xFF = no lock |
| `+192+s` ("aux") | per-step lock array, parameter index 2 = **LEN**: value 0..127; 0xFF = no lock (use `+962`); empty steps hold the default 14 as a template, placing a trig stores 0xFF **(inferred from the parameter table, unconfirmed on hardware)** |
| `+256+s`, `+320+s`, `+384+s` | further lock arrays (parameter indices 3, 4, 5 = COND ...), defaults 0x00 / 0xFF / 0xFF. Array offset = `128 + 64*(index-1)` |
| live `+448 + 8*s` | note row: root note + 7 signed semitone offsets (the chord). In the **saved** block the rows are at `+384 + 8*s` |
| `+960` | page value ROOT (default 60) |
| `+961` | page VEL (default 100) |
| `+962` | page LEN default (default 14 = 1 step) |
| `+966` | pattern length (byte, e.g. 16 or 64) |

Stock trig placement/removal: `0x4003673a(editor, step, mask, on)`; place = `(ed, s, 1, 1)`, remove = `(ed, s, 1, 0)` then
`(ed, s, 2, 0)`. Placing a trig sets the aux byte to 0xFF; removing leaves it alone.

## 3. Parameters and the LEN scale

Parameter records: `0x4018d454 + 60*i`; `+0x28` long-name pointer, `+0x30` short-name pointer, value fields as
8.8 fixed point. Seen: ROOT idx 0 (default 60), VEL idx 1 (100), LEN idx 2 (default 14, range 0..127), COND idx 5,
PROB idx 8. LEN has its own formatter `0x40013dbe`, which indexes a table of 128 label pointers at `0x402421f0`.

Length in 1/16 of a step for LEN value `v` (checked against the 128 labels; value 14 = exactly 1 step, "1/16"):

```
v   0..14 : v + 2                 (0.125 .. 1.0 steps, 1/16 step per value)
v  15..30 : 16 + (v-14)           (.. 2.0)
v  31..46 : 32 + 2*(v-30)         (.. 4.0)
v  47..62 : 64 + 4*(v-46)         (.. 8.0)
v  63..78 : 128 + 8*(v-62)        (.. 16.0)
v  79..94 : 256 + 16*(v-78)       (.. 32.0)
v 95..110 : 512 + 32*(v-94)       (.. 64.0)
v 111..126: 1024 + 64*(v-110)     (.. 128.0)
v 127     : INF
```

## 4. Event hook sites (core 2.0a, OS 1.44) and mod sites

Core: boot call `0x40000538`; tick `0x4001900c`; draw `0x40019072`; key `0x40019d9c`; encoder `0x40019de4`; SETTINGS
`0x40072a54`; render in/out `0x4009d128` / `0x4009e53c`.

| Mod | Site | Meaning |
|---|---|---|
| cc-hold | `0x400f44ca` `48780001` -> `48780000` | resync "force" flag 1 -> 0 |
| ob6-nrpn | `0x400f3d72` | the `jsr 0x400f3c1a` that appends a message to the MIDI staging buffer |
| chord-knobs | `0x40035b0a` | per-step note-offset clearing loop |
| lfo-vel | `0x400f4846`, `0x400f4792`, data `0x4018f6b4`, `0x4018f6bc` | note-on velocity; LFO destination list |

Other useful addresses: modulation slots `0x419eb1fc + 80*t + 24` (`+0` destination, `+4` amount); text draw
`0x400de120`; `fw_fillrect 0x400dd506` (bmp, x0, y0, x1, y1, colour 1 set / 0 clear / -1 xor); stock font
`0x402319c8` (the mods use the font object at `0x418e934c`).

## 5. MIDI output path (verified by reading code)

Message builders call `0x400f3c1a(buf, len)`, which appends to a staging buffer (`0x419b18ed` = byte count,
data from `0x419b18ee`). The flusher (near `0x400f4c96`) calls `0x400f37ce(len, buf)`, which reads the **output-port mask**
at `0x419c5df4` (the OUTPUT TO setting): bit 0 -> DIN transmit `0x40002a3a` (4 KB ring buffer at `0x405285b0..`, DMA
channel, UART `0xEC074000`); bit 1 -> USB `0x40004bcc`. There is no third destination.

## 6. Hardware facts found while researching the THRU jack (see THRU_INVESTIGATION.md)

- Only **two UARTs** are used: `0xEC074000` (DIN MIDI) and `0xEC070000` (an internal link at 156,250 baud whose callers send
  2-8 byte command frames; init `0x40002866`, transmit `0x40002634`, ring `0x405285f4..`). UART clock 132 MHz.
- THRU PORT FUNC: byte `0x41368d63` (low byte of the long at `0x41368d60`): 0 MIDI, 1 DIN 24, 2 DIN 48. The *applied*
  modes are the words `0x40242050` (THRU) and `0x40242054` (OUT): 1 = MIDI, 2 = sync; setter near `0x400f9a90`.
- Sync pulses come from a timer interrupt (`~0x40093a58`), which sets/clears GPIO pins through four tiny routines:
  `0x400f9898`, `0x400f98bc`, `0x400f98de`, `0x400f991e` (GPIO registers `0xEC094010..0xEC09402C`).
- PIT1 (`0xFC084000`) is configured as a 1 us tick (modulus 131 at 132 MHz): the firmware's own microsecond delay.
- GPIO write idiom: set via the `0xEC0940xx` "set" register with a 1-mask, clear via the matching register with a 0-mask.
