/* chord-knobs 2.0: NOT2 / NOT3 on the MIDI track TRIG page, knobs G and H.
 * - No step held: the values are stamped into every new trig (chord notes
 *   in the Digitone's own per-step storage: root + semitone offsets).
 * - Step(s) held: the knobs show the first held step's chord and turning
 *   them edits the chord of every held step.
 * Range -12..+12 semitones, 0 = off. Digitone mk1 / Keys OS 1.44. */
#define ACT_TRACK  (*(volatile unsigned char *)0x41368ce3UL)   /* 0-3 synth, 4-7 MIDI */
#define PAGE       (*(volatile unsigned char *)0x41368ce7UL)   /* 0 = TRIG page */
#define BRAIN      0x41a0a7a0UL                                /* UI brain: screen list at +0x58/+0x5c */
#define SCR_A      (*(volatile unsigned long *)(BRAIN + 0x58))
#define SCR_B      (*(volatile unsigned long *)(BRAIN + 0x5c))
#define MAIN_SCREEN_VT 0x40198694UL   /* method table of the main track-page screen (object = node - 16) */

/* The firmware updates the "current page" byte one frame AFTER the new page is already on screen. The page keys (ids 20..25 =
 * TRIG, SYN1, SYN2, FLTR, AMP, LFO) pass through ev_key first, so remember the page that was just selected until the byte catches up. */
static unsigned char pg_pend, pg_on, pg_age;
static unsigned page_now(void) { return pg_on ? pg_pend : PAGE; }
static void page_key(int id, int fl)
{
    if ((fl & 1) && !(fl & 2) && !(fl & 8) && id >= 20 && id <= 25) { pg_pend = (unsigned char)(id - 20); pg_on = 1; pg_age = 0; }
}
static void page_tick(void)
{
    if (pg_on && (PAGE == pg_pend || ++pg_age > 6)) pg_on = 0;
}
#define HELD_HI    (*(volatile unsigned long *)0x41a5b7f8UL)   /* held steps, bit = step (0-63) */
#define HELD_LO    (*(volatile unsigned long *)0x41a5b7fcUL)
#define EDITOR(t)  ((unsigned char *)(0x41a73294UL + 0x58UL * (t)))   /* per-track editor object */
#define SET_STEP_OFFSET ((void (*)(void *, int, int, int))0x40035858UL) /* (editor, step, value, index) */
#define DRAWTEXT   ((void (*)(void *, void *, int, int, int, const char *, ...))0x400de120UL)
#define FONT       ((void *)0x418e934cUL)
#define FILLRECT ((void (*)(void *, int, int, int, int, int))0x400dd506UL)   /* (bmp, x0, y0, x1, y1, colour: 1 set, 0 clear, -1 xor) */
#define POP_TICKS  90

signed char chord_off[4][2];            /* per MIDI track: NOT2, NOT3 for new trigs (.bss) */
static int acc[2];
int chord_counts_per_step = 12;         /* encoder counts per semitone (was 4: too fast on hardware) */
static unsigned char dirty;
static unsigned char pop_k;
static unsigned long pop_ticks;
static void setpix(unsigned char *px, int x, int y, int on)
{
    unsigned char *b, m;
    if (x < 0 || x > 127 || y < 0 || y > 63)
        return;
    b = &px[x * 8 + 7 - (y >> 3)];
    m = (unsigned char)(1u << (y & 7));
    if (on) *b |= m; else *b &= (unsigned char)~m;
}

/* The stock header popup ("Name=Value", about 3 s): the title bar after the A01 box is a white bar
 * with black text and rounded corners. Clear it, draw the text white, then XOR the bar. */
static void hdr_popup(void *bmp, const char *fmt, int v)
{
    unsigned char *px = *(unsigned char **)((char *)bmp + 0x10);
    FILLRECT(bmp, 17, 54, 127, 63, 0);
    DRAWTEXT(bmp, FONT, 20, 56, 0, fmt, v);
    FILLRECT(bmp, 17, 54, 127, 63, -1);
    if (px) {
        setpix(px, 17, 0, 0); setpix(px, 127, 0, 0);
        setpix(px, 17, 9, 0); setpix(px, 127, 9, 0);
    }
}

/* The firmware's held-step view inverts the ROOT box. The frame passed to
 * ev_draw is already drawn, so test three pixels of that highlight (outside
 * the ROOT text) and switch NOT2/NOT3 in the very same frame.
 * Bitmap: +4 width 128, +8 height 64, +0x10 pixels: column-major, 8 bytes
 * per column, bit y of the big-endian 64-bit column = row y (0 = top). */
static int pix(const unsigned char *px, int x, int y)
{
    return (px[x * 8 + 7 - (y >> 3)] >> (y & 7)) & 1;
}

/* Is the stock slot box (dotted 17x17 outline, every second pixel lit) still where this slot is? A window, a popup
 * or a blank screen on top of the page breaks it. */
static int slot_stock(const unsigned char *px, int cx, int cy)
{
    int m, o, hit = 0;
    for (m = 0; m < 8; m++) {
        o = -7 + 2 * m;
        hit += pix(px, cx + o, cy - 8) + pix(px, cx + o, cy + 8) + pix(px, cx - 8, cy + o) + pix(px, cx + 8, cy + o);
    }
    return hit >= 28;
}
static unsigned char vis;              /* the page was drawn undisturbed in the last frame */

static int fw_held_view(void *bmp)
{
    const unsigned char *px = *(const unsigned char **)((char *)bmp + 0x10);
    if (*(int *)((char *)bmp + 4) != 128 || *(int *)((char *)bmp + 8) != 64 || !px)
        return 0;
    /* the held view replaces PROB with COND, whose box bottom edge (row 29) is
     * drawn whatever the root: ROOT itself is only inverted when it is locked */
    return pix(px, 106, 29) && pix(px, 111, 29) && pix(px, 116, 29);
}

int chord_x[2] = {85, 111};             /* slots G and H */
int chord_y_val = 14, chord_y_lab = 2;  /* y counts from the bottom row */

static int on_midi_trig_page(void)
{
    unsigned t = ACT_TRACK;
    unsigned long b = SCR_B;
    if (!(t >= 4 && t <= 7 && page_now() == 0))
        return 0;
    if (b < 0x40000010UL || b >= 0x48000000UL)
        return 0;
    return *(volatile unsigned long *)(b - 16) == MAIN_SCREEN_VT;   /* no overlay screen underneath */
}

static int held_any(void) { return (HELD_HI | HELD_LO) != 0; }

static int is_held(int s)
{
    return s < 32 ? (HELD_LO >> s) & 1 : (HELD_HI >> (s - 32)) & 1;
}

/* the track's editor, only if it really is that track */
static unsigned char *editor(int t)
{
    unsigned char *ed = EDITOR(t);
    if (*(volatile unsigned long *)(ed + 0x38) != (unsigned long)t)
        return 0;
    return ed;
}

static unsigned char *live_row(unsigned char *ed, int s)
{
    unsigned char *base = *(unsigned char **)(ed + 0x10);
    return base ? base + 448 + 8 * s : 0;
}

static unsigned char *saved_row(unsigned char *ed, int s)
{
    unsigned char *base = *(unsigned char **)(ed + 0x20);
    return base ? base + 384 + 8 * s : 0;
}

static int first_held_step(void)
{
    int s;
    for (s = 0; s < 64; s++)
        if (is_held(s))
            return s;
    return -1;
}

/* set NOT2 (k=0) / NOT3 (k=1): held steps if any, else the page value.
 * relative: v is a step count added to the current value; else v is absolute */
static void chord_set(int k, int v, int relative)
{
    int t = ACT_TRACK;
    if (held_any()) {
        unsigned char *ed = editor(t), *r;
        int s0 = first_held_step(), s;
        if (!ed || s0 < 0 || !(r = live_row(ed, s0)))
            return;
        if (relative)
            v += (signed char)r[1 + k];
        if (v > 12) v = 12;
        if (v < -12) v = -12;
        for (s = 0; s < 64; s++) {
            unsigned char *lr, *sr;
            if (!is_held(s) || !(lr = live_row(ed, s)))
                continue;                /* root 0xFF = "track root", still a trig */
            SET_STEP_OFFSET(ed, s, v, 1 + k);
            if ((sr = saved_row(ed, s)) != 0)
                sr[1 + k] = (unsigned char)v;
        }
    } else {
        if (relative)
            v += chord_off[t - 4][k];
        if (v > 12) v = 12;
        if (v < -12) v = -12;
        chord_off[t - 4][k] = (signed char)v;
    }
    dirty = 1;
}

int chord_enc(void *brain, void *ev)
{
    int e = *(int *)((char *)ev + 12), d = *(int *)((char *)ev + 16), k, steps = 0;
    if (!(on_midi_trig_page() && vis) || (e != 7 && e != 8))
        return 0;
    k = e - 7;                           /* G -> NOT2, H -> NOT3 */
    pop_k = (unsigned char)k; pop_ticks = POP_TICKS; dirty = 1;
    acc[k] += d;
    while (acc[k] >= chord_counts_per_step) { acc[k] -= chord_counts_per_step; steps++; }
    while (acc[k] <= -chord_counts_per_step) { acc[k] += chord_counts_per_step; steps--; }
    if (steps)
        chord_set(k, steps, 1);
    return 1;
}

/* pressing knob G / H resets NOT2 / NOT3 to off (held steps, or the page value) */
int chord_key(void *brain, void *ev)
{
    int id = *(int *)((char *)ev + 12), fl = *(int *)((char *)ev + 16);
    page_key(id, fl);
    if ((id != 52 && id != 53) || !(on_midi_trig_page() && vis))
        return 0;
    if (fl == 1) {                       /* the press itself; repeats and release are just swallowed */
        chord_set(id - 52, 0, 0);
        pop_k = (unsigned char)(id - 52); pop_ticks = POP_TICKS; dirty = 1;
    }
    return 1;
}

void chord_tick(void *ctrl)
{
    page_tick();
    if (pop_ticks && --pop_ticks == 0)
        dirty = 1;                       /* header back to the pattern name */
    if (dirty) {
        dirty = 0;
        *((unsigned char *)ctrl + 0x20) = 1;
    }
}

void chord_draw(void *bmp, void *ctrl)
{
    int k, t, v[2], fl, hv;
    unsigned char *px;
    vis = 0;
    if (!on_midi_trig_page())
        return;
    px = *(unsigned char **)((char *)bmp + 0x10);
    if (!px || *(int *)((char *)bmp + 4) != 128 || *(int *)((char *)bmp + 8) != 64)
        return;
    if (!slot_stock(px, chord_x[0], 48) || !slot_stock(px, chord_x[1], 48))
        return;                        /* a window, a popup or a blank screen covers the page */
    vis = 1;
    t = ACT_TRACK;
    hv = held_any() && fw_held_view(bmp);
    if (hv) {   /* switch together with ROOT/VEL/LEN */
        unsigned char *ed = editor(t), *r = 0;
        int s0 = first_held_step();
        if (ed && s0 >= 0)
            r = live_row(ed, s0);
        if (!r)
            return;
        v[0] = (signed char)r[1];
        v[1] = (signed char)r[2];
    } else {
        v[0] = chord_off[t - 4][0];
        v[1] = chord_off[t - 4][1];
    }
    fl = hv ? 0x2a : 0x22;          /* 0x08 = inverted: highlighted like a locked trig value */
    for (k = 0; k < 2; k++) {
        if (v[k])
            DRAWTEXT(bmp, FONT, chord_x[k], chord_y_val, fl, "%+d", v[k]);
        else
            DRAWTEXT(bmp, FONT, chord_x[k], chord_y_val, fl, "--");
        DRAWTEXT(bmp, FONT, chord_x[k], chord_y_lab, 0x22, k ? "NOT3" : "NOT2");
    }
    if (pop_ticks) {
        int pv = v[pop_k];
        if (pv) hdr_popup(bmp, pop_k ? "Chord Note 3=%+d" : "Chord Note 2=%+d", pv);
        else    hdr_popup(bmp, pop_k ? "Chord Note 3=Off" : "Chord Note 2=Off", 0);
    }
}
