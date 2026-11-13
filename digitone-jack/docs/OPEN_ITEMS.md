# Open items (state at 2026-10-15)

## 1. Waiting for Jack's report on the unit

- **JACK18c / 18b / 18 (LEN+) are untried.** Things to ask about: do the lengthened trigs really play longer (the emulator cannot show
  note lengths)? Does the held-step LEN knob show the new length (this would confirm that the `+0xC0` array is the per-step LEN lock)?
  Does the 0 % press restore everything, including trigs that had their own length lock? Is "Len=0%: restored" readable?
- **JACK17 (current on his unit):** did the page-change flicker disappear (HUM on SYN1 -> TRIG, SYN1 knobs from FLTR)?
- The THRU control check was never reported (irrelevant now, the idea is dropped).

## 2. Stale or missing verification

- Full cold-boot comparison against stock: last run on JACK13, passed. Jack asked not to rerun for now. Mention it when reporting.
- How to run it: digiemu's own checker, about 25 minutes, best started in the background (command in `BUILD_AND_TEST.md`, recipe 8).
  Run it only if Jack asks.
- Emulator limits (see BUILD_AND_TEST.md): held-step views, real MIDI-track output/lengths, anything timing-critical on hardware.

## 3. Known quirks (accepted or by design)

- The LFO destination box still shows "AT" after selecting velocity.
- UNDO lives in the old aftertouch slot (VEL slot of the MIDI Source page); fine because he never uses aftertouch.
- All randomizer state (scale, RND/PIT/LEN+ percentages, note memory, undo history) is per track, **not saved to the project** and
  reset at power-on. He always uses a single pattern.
- Reset removes all trigs of the track and cannot be undone.
- LEN+: a trig at the maximum length (value 126 = 128 steps) cannot be lengthened, INF (127) is left alone; near the top the available lengths
  are coarse (a 64-step trig has only one length within +4 steps).
- HUM test-suite edge case (LFO +20 with HUM 5, range 115..125) was removed from `cputest` as minor.
- Free RAM is about 44 KB; the undo timeline arrays (8 tracks x 8 entries) dominate. Check the `patch` output before adding state.
- Header comments in the `.c` files carry old version numbers; `mod.json` is authoritative.

## 3. Tool and update status (web check, 2026-10-15; Jack's installed versions of Transfer/Overbridge are unknown)

- **Digitone OS:** 1.44 is the newest found (elekloader's v0.4.0 notes call it Elektron's release of 2026-09-30). Nothing newer seen.
- **elekloader:** release v0.4.0 (2 Oct; kit 0.4.0 on 6 Oct). Its Digitone 1.44 core is `core-dn1-2.0a-os1.44.elemod`, identical to the one used here.
- **Correction to an earlier statement in the chat:** the chat said the newer core (2.2) was published only for OS 1.43. The elekloader repository
  (checkout `69707b3`) has since moved on - core-dn1 **2.3** (a Mod Menu) and **3.0** ("the Digitone's firmware locations as exports"), and
  `mods/core-dn1/README.md` says OS 1.43 **and** 1.44. Whether 3.0 is compatible with these mods or improves stability is **unknown**;
  nothing was upgraded. If Jack wants to try it: build a copy of the five mods against it, lint, and test in the emulator first.
- **Elektron Transfer:** 1.10.4 on Elektron's page. **Overbridge:** 2.25.7 (27 Apr 2026) listed; an older listing said 2.18.5 supports macOS 11-15 -
  the macOS minimum of 2.25.x was not verified against his macOS 12.
- **digiemu:** v0.3.0 adds Digitone OS 1.44.

## 4. Ideas Jack mentioned that were never built (none requested right now)

- FUNC + knob on the MIDI CC VAL page jumping to 0 / 64 / 127 (asked earlier; not in the current builds; status not recorded).
- Bringing Digitone II machines to the MK1 (curiosity).
- A YouTube demo video of the firmware ("Digitone MK1: Custom Firmware Demo", with a short caption-style feature list).

## 5. Documentation that exists outside this repo

- A Claude Doc "Jack Firmware for Digitone MK1: handover" in Jack's claude.ai account (rebuild commands, firmware reference, next steps),
  updated through JACK18c. This repo supersedes it for Claude Code work.
- `dn_session_notes.md` (here as `docs/SESSION_NOTES.md`): older engineering log, ends at JACK16b.
- `Digitone_JACK_builds.txt`: the short user-facing build list in his OS folder (through JACK18c).
- The long claude.ai transcripts of the sessions are the deep history; this repo should make them unnecessary.
