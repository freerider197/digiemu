# THRU jack as a second MIDI output - closed investigation (October 2026)

**Result: not possible from software. Jack ran the test on his unit; nothing came out. He dropped the idea. Do not redo this
unless there is new evidence (for example a schematic of the Digitone's MIDI jacks).**

## The wish

Jack plays percussion with his Digitone and wanted a second MIDI output on the jack that normally is MIDI THRU (which can
also be SYNC B), sending the notes of another channel, only ever one note at a time.

## What the manual says

The Digitone has MIDI IN, **MIDI OUT/SYNC A** and **MIDI THRU/SYNC B**. SETTINGS > MIDI CONFIG > PORT CONFIG has `OUT PORT FUNC`
and `THRU PORT FUNC`, each MIDI / DIN 24 / DIN 48. THRU in MIDI mode forwards MIDI IN. In DIN modes the port sends sync pulses and,
per the manual, no MIDI data goes over that port.

## What the firmware shows (OS 1.44, read from the disassembly)

1. **MIDI output has exactly two sinks.** Messages are staged (`0x419b18ed`), then `0x400f37ce(len, buf)` sends them by the OUTPUT
   TO mask (`0x419c5df4`): bit 0 -> DIN transmit `0x40002a3a` (4 KB ring + DMA on UART `0xEC074000`), bit 1 -> USB. Nothing feeds THRU.
2. **A "second transmitter" exists but is not THRU.** The twin ring/transmit routine (`0x40002634`, UART `0xEC070000`, 156,250 baud =
   5x MIDI) is an internal link: its seven callers send 2-8 byte command frames starting with bytes like `B5`/`B7`. Probably the front
   panel. Writing MIDI bytes into it would corrupt that link - never do it. The firmware uses only these two UARTs.
3. **THRU PORT FUNC is a byte at `0x41368d63`** (0 MIDI, 1 DIN 24, 2 DIN 48), found by diffing RAM while changing the menu in the emulator.
   The *applied* mode words are `0x40242050` (THRU) and `0x40242054` (OUT): 1 = MIDI, 2 = sync.
4. **Sync pulses come from a timer interrupt** that reads that byte and drives GPIO through four tiny routines (`0x400f9898`, `0x400f98bc`,
   `0x400f98de`, `0x400f991e`): two pins per jack, presumably clock and run. The mode setter (~`0x400f9a90`) also clears three pins, makes one
   an output and switches a **select line** (high = MIDI-thru path, low = sync path; the register differs by a hardware flag at
   `0x402296f0`). So the jack has a hardware switch between "MIDI thru" and "sync outputs", driven by GPIO.
5. In MIDI mode the thru path is therefore almost certainly a hardware copy of MIDI IN, not software.

## The test (thru-test 0.1, `src/thru-test`)

Active only while the applied THRU mode word == 2 (DIN 24/48) with the sequencer stopped. Every second it sent one MIDI note (channel 1)
by bit-banging one of the two THRU sync pins, cycling every 10 s through four candidates, each with its own note number:
60 = pin D (port 6 bit 0, set `0xEC09401E`/clear `0xEC09402A`) idle high; 62 = pin D inverted; 64 = pin B (port 8 bit 1, set `0xEC094020`/clear
`0xEC09402C`) idle high; 65 = pin B inverted. Timing from PIT1 (1 us ticks: PCSR `0x33`, PMR 131; 32 ticks per bit at 31,250 baud), interrupts
masked for one byte (~0.3 ms) at a time, every wait guarded so it cannot hang.

Verified in the emulator: all four candidates decode back to `90 nn 64 80 nn 00` twice; no GPIO writes while THRU = MIDI; with real timing waits
361 writes completed with no timeouts and the unit stayed responsive.

## Result on the unit

Jack set THRU PORT FUNC to DIN 24: nothing visibly happened and his MIDI monitor (through a MIDI interface on his Mac) registered no notes in
any phase. A control check (THRU back on MIDI, notes sent into MIDI IN, to prove the cable/interface/monitor chain) was suggested; Jack did
not report it, he dropped the idea instead.

## Interpretation (inference, not proof)

The two sync pins are most likely DIN pins 1 and 3 (clock / run) - not the MIDI data pin 5 - so a MIDI interface cannot see them even if
the bits were perfect. Nothing in software can change which jack pin a GPIO reaches.

## What was not tested

- A third pin (port 4 bit 2, direction set to output in sync mode, cleared by the setter) - left alone on purpose: unknown function.
- Bit-banging in MIDI mode - could disturb the select/enable circuitry; deliberately not done.
- The control check above.

## Alternatives suggested to Jack (no firmware needed)

A small hardware MIDI splitter or channel filter on the OUT jack; daisy-chaining (each device listens on its own channel);
OUTPUT TO = MIDI+USB to send the same stream to USB at the same time.
