/* thru-test 0.1: a ONE-OFF EXPERIMENT. Does the MIDI THRU/SYNC B jack carry MIDI when its sync pins are bit-banged?
 *
 * Only active while THRU PORT FUNC is DIN 24 or DIN 48 (the firmware has then handed the two sync pins of that jack to
 * software: applied mode word 0x40242050 == 2). Keep the sequencer STOPPED, so the firmware's own sync pulses stay quiet.
 * Every second it sends one MIDI note (ch 1, on at +5 ticks, off at +20) on one candidate pin and polarity; every 10 s it
 * moves to the next candidate, and each candidate has its own note number, so a MIDI monitor on THRU shows which one worked:
 *   note 60: pin D (port 6 bit 0), idle high   | note 62: pin D, idle low (inverted)
 *   note 64: pin B (port 8 bit 1), idle high   | note 65: pin B, idle low (inverted)
 * The two pins are exactly the ones the firmware's own sync routines (0x400f991e and 0x400f98bc) set and clear.
 * Timing: PIT1 runs 1 us ticks (the firmware's own delay setup); 32 ticks = one bit at 31250 baud. Interrupts are masked
 * for one byte at a time (about 0.3 ms). Every wait has a guard, so it cannot hang. */
#define PIT_PCSR   (*(volatile unsigned short *)0xFC084000UL)
#define PIT_PMR    (*(volatile unsigned short *)0xFC084002UL)
#define THRU_MODE  (*(volatile unsigned long *)0x40242050UL)     /* applied THRU function: 1 = MIDI, 2 = DIN sync */

unsigned long tt_irq_off(void);
void tt_irq_restore(unsigned long sr);

int tt_fast;                       /* emulator test only: skip the timing waits */
int tt_phase_ticks = 300;          /* 10 s per candidate (30 ticks per second) */
int tt_note_ticks = 30;            /* one note per second */
int tt_fail;                       /* counts timing waits that gave up */
static unsigned long tn;
static int cur = -1;

static void pin(int which, int level)
{
    if (which == 0) {              /* pin D: set via 0xEC09401E, clear via 0xEC09402A (as the firmware does) */
        if (level) *(volatile unsigned char *)0xEC09401EUL = 0x01;
        else       *(volatile unsigned char *)0xEC09402AUL = 0xFE;
    } else {                       /* pin B: set via 0xEC094020, clear via 0xEC09402C */
        if (level) *(volatile unsigned char *)0xEC094020UL = 0x02;
        else       *(volatile unsigned char *)0xEC09402CUL = 0xFD;
    }
}

static int bit_wait(void)          /* 32 ticks of 1 us = one bit at 31250 baud */
{
    int i;
    unsigned long guard;
    if (tt_fast) return 1;
    for (i = 0; i < 32; i++) {
        guard = 100000UL;
        while (!(PIT_PCSR & 4))
            if (--guard == 0) { tt_fail++; return 0; }
        PIT_PCSR = 0x37;           /* clear the flag, keep the timer running */
    }
    return 1;
}

static void send_byte(int which, int inv, unsigned char b)
{
    unsigned long sr = tt_irq_off() & 0xFFFFUL;
    int i, lvl;
    PIT_PCSR = 0x33;               /* enabled, reloading, 1 us ticks (the firmware's own setup) */
    PIT_PMR = 131;
    PIT_PCSR = 0x37;
    for (i = 0; i < 10; i++) {     /* start bit, 8 data bits LSB first, stop bit */
        lvl = i == 0 ? 0 : (i == 9 ? 1 : (b >> (i - 1)) & 1);
        pin(which, inv ? !lvl : lvl);
        if (!bit_wait()) break;
    }
    tt_irq_restore(sr);
}

static void send_note(int which, int inv, int on, int note)
{
    send_byte(which, inv, on ? 0x90 : 0x80);
    send_byte(which, inv, (unsigned char)note);
    send_byte(which, inv, on ? 100 : 0);
}

void tt_tick(void *ctrl)
{
    int ph, loc;
    if (THRU_MODE != 2) {          /* not in sync mode: hands off */
        if (cur >= 0) { pin(cur >> 1, 0); cur = -1; }
        tn = 0;
        return;
    }
    tn++;
    ph = (int)((tn / (unsigned long)tt_phase_ticks) & 3);
    loc = (int)(tn % (unsigned long)tt_phase_ticks);
    if (ph != cur) {
        if (cur >= 0) pin(cur >> 1, 0);              /* back to the firmware's idle level (low) */
        cur = ph;
        if (!(cur & 1)) pin(cur >> 1, 1);            /* "normal" polarity idles high */
    }
    if (loc % tt_note_ticks == 5)
        send_note(ph >> 1, ph & 1, 1, ph == 0 ? 60 : (ph == 1 ? 62 : (ph == 2 ? 64 : 65)));
    else if (loc % tt_note_ticks == tt_note_ticks / 2 + 5)
        send_note(ph >> 1, ph & 1, 0, ph == 0 ? 60 : (ph == 1 ? 62 : (ph == 2 ? 64 : 65)));
}
