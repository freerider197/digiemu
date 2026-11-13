/* lfo-vel 1.2
 * 1) MIDI track LFO destination "Aftertouch" (shown VEL / SRC:Velocity)
 *    modulates note velocity: slot amounts aimed at AT (22) are added.
 * 2) HUM: per MIDI track velocity humanize, knob E on the MIDI TRIG page,
 *    0..40: each trig gets a random -HUM..+HUM added (same for all notes of
 *    the trig's chord). Press knob E to reset to 0.
 * Digitone mk1 / Keys OS 1.44. */
#define MOD_BASE   0x419eb1fcUL
#define DEST_AT    22
#define ACT_TRACK  (*(volatile unsigned char *)0x41368ce3UL)
#define PAGE       (*(volatile unsigned char *)0x41368ce7UL)
#define BRAIN      0x41a0a7a0UL
#define SCR_B      (*(volatile unsigned long *)(BRAIN + 0x5c))
#define MAIN_SCREEN_VT 0x40198694UL

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
#define DRAWTEXT   ((void (*)(void *, void *, int, int, int, const char *, ...))0x400de120UL)
#define FONT       ((void *)0x418e934cUL)
#define FILLRECT ((void (*)(void *, int, int, int, int, int))0x400dd506UL)   /* (bmp, x0, y0, x1, y1, colour: 1 set, 0 clear, -1 xor) */

#define HUM_MAX    60

signed char lfovel_hum[4];              /* per MIDI track, 0 = off (.bss) */
int lfovel_counts_per_step = 6;
static int hacc;
static unsigned char hdirty;
static unsigned long rng = 0x2545F491UL;

static unsigned long show_value;        /* ticks left to show the value instead of HUM */
#define SHOW_TICKS 90                   /* 3 s at 30 ticks/s */
static const signed char hum_ptr[41][2] = {{-5,5}, {-5,4}, {-6,4}, {-6,3}, {-7,2}, {-7,1}, {-7,1}, {-7,0}, {-7,-1}, {-7,-2}, {-6,-3}, {-6,-3}, {-6,-4}, {-5,-5}, {-5,-5}, {-4,-6}, {-3,-6}, {-2,-7}, {-2,-7}, {-1,-7}, {0,-7}, {1,-7}, {2,-7}, {2,-7}, {3,-6}, {4,-6}, {5,-5}, {5,-5}, {6,-4}, {6,-3}, {6,-3}, {7,-2}, {7,-1}, {7,0}, {7,1}, {7,1}, {7,2}, {6,3}, {6,4}, {5,4}, {5,5}};

/* screen bitmap (ev_draw): +4 width 128, +8 height 64, +0x10 pixels, column-major,
 * 8 bytes per column, bit y of the big-endian 64-bit column = row y (0 = top) */
static void setpix(unsigned char *px, int x, int y, int on)
{
    unsigned char *b, m;
    if (x < 0 || x > 127 || y < 0 || y > 63)
        return;
    b = &px[x * 8 + 7 - (y >> 3)];
    m = (unsigned char)(1u << (y & 7));
    if (on) *b |= m; else *b &= (unsigned char)~m;
}

static int pix(const unsigned char *px, int x, int y)
{
    if (x < 0 || x > 127 || y < 0 || y > 63)
        return 0;
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

static void line(unsigned char *px, int x0, int y0, int x1, int y1)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    for (;;) {
        setpix(px, x0, y0, 1);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* the stock knob: radius-8 circle (as LEN) and a pointer; centre cx, cy */
static void knob(unsigned char *px, int cx, int cy, int v)
{
    static const signed char ring[][2] = {
        {-2,-8},{-1,-8},{0,-8},{1,-8},{2,-8},{-4,-7},{-3,-7},{3,-7},{4,-7},{-5,-6},{5,-6},
        {-6,-5},{6,-5},{-7,-4},{7,-4},{-7,-3},{7,-3},{-8,-2},{8,-2},{-8,-1},{8,-1},{-8,0},{8,0},
        {-8,1},{8,1},{-8,2},{8,2},{-7,3},{7,3},{-7,4},{7,4},{-6,5},{6,5},{-5,6},{5,6},
        {-4,7},{-3,7},{3,7},{4,7},{-2,8},{-1,8},{0,8},{1,8},{2,8}};
    int i, x, y;
    for (x = cx - 9; x <= cx + 9; x++)              /* clear the dotted box */
        for (y = cy - 9; y <= cy + 9; y++)
            setpix(px, x, y, 0);
    for (i = 0; i < (int)(sizeof ring / sizeof ring[0]); i++)
        setpix(px, cx + ring[i][0], cy + ring[i][1], 1);
    if (v < 0) v = 0;
    if (v > 40) v = 40;
    line(px, cx, cy, cx + hum_ptr[v][0], cy + hum_ptr[v][1]);
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

static unsigned long xorshift(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

/* vel: the trig's velocity, track: MIDI track 0-3, index: note of the chord (0 = first) */
#define HUM_GAP 15                       /* with HUM on, two trigs in a row are at least this far apart in velocity */
static int last_vel[4], chord_vel[4];

int lfovel_velocity(int vel, int track, int index)
{
    int k, h, v0, v = 0, tries, r;
    if (track < 0 || track > 3)
        return vel;
    for (k = 0; k < 2; k++) {
        volatile long *slot = (volatile long *)(MOD_BASE + 80UL * track + 24 + 40 * k);
        if (slot[0] == DEST_AT)
            vel += (int)(slot[1] >> 8);
    }
    if (vel < 1) vel = 1;                   /* the LFO part clamps as before */
    if (vel > 127) vel = 127;
    h = lfovel_hum[track];
    if (h <= 0)
        return vel;
    if (h > HUM_MAX) h = HUM_MAX;
    if (index != 0)                         /* the other notes of a chord share the trig's velocity */
        return chord_vel[track] ? chord_vel[track] : vel;
    v0 = vel;
    for (tries = 0; tries < 4; tries++) {   /* a triangle within -h..+h, drawn again if too close to the last trig */
        unsigned long w = (unsigned long)(2 * h + 1);
        int a = (int)(xorshift() % w), b = (int)(xorshift() % w);
        r = (a + b + 1) / 2 - h;
        v = v0 + r;
        if (v > 127) v = 254 - v;           /* reflect at the limits instead of piling up on 127 / 1 */
        if (v < 1) v = 2 - v;
        if (v < 1) v = 1;
        if (v > 127) v = 127;
        if (!last_vel[track] || (v > last_vel[track] ? v - last_vel[track] : last_vel[track] - v) >= HUM_GAP)
            break;
    }
    if (last_vel[track] && (v > last_vel[track] ? v - last_vel[track] : last_vel[track] - v) < HUM_GAP) {
        int up = last_vel[track] + HUM_GAP, dn = last_vel[track] - HUM_GAP;       /* still too close: step 15 away, toward the trig's own velocity */
        if (up > 127) v = dn;
        else if (dn < 1) v = up;
        else v = ((up > v0 ? up - v0 : v0 - up) <= (dn > v0 ? dn - v0 : v0 - dn)) ? up : dn;
    }
    last_vel[track] = v;
    chord_vel[track] = v;
    return v;
}

static int on_midi_trig_page(void)
{
    unsigned t = ACT_TRACK;
    unsigned long b = SCR_B;
    if (!(t >= 4 && t <= 7 && page_now() == 0))
        return 0;
    if (b < 0x40000010UL || b >= 0x48000000UL)
        return 0;
    return *(volatile unsigned long *)(b - 16) == MAIN_SCREEN_VT;
}

int lfovel_enc(void *brain, void *ev)
{
    int e = *(int *)((char *)ev + 12), d = *(int *)((char *)ev + 16), t, v, steps = 0;
    if (e != 5 || !(on_midi_trig_page() && vis))
        return 0;
    hacc += d;
    while (hacc >= lfovel_counts_per_step) { hacc -= lfovel_counts_per_step; steps++; }
    while (hacc <= -lfovel_counts_per_step) { hacc += lfovel_counts_per_step; steps--; }
    if (steps) {
        t = ACT_TRACK - 4;
        v = lfovel_hum[t] + steps;
        if (v < 0) v = 0;
        if (v > HUM_MAX) v = HUM_MAX;
        lfovel_hum[t] = (signed char)v;
        hdirty = 1;
    }
    show_value = SHOW_TICKS;
    return 1;
}

int lfovel_key(void *brain, void *ev)
{
    int id = *(int *)((char *)ev + 12), fl = *(int *)((char *)ev + 16);
    page_key(id, fl);
    if (id != 50 || !(on_midi_trig_page() && vis))      /* 50 = knob E pressed */
        return 0;
    if (fl == 1) {
        lfovel_hum[ACT_TRACK - 4] = 0;
        show_value = SHOW_TICKS;
        hdirty = 1;
    }
    return 1;
}

void lfovel_tick(void *ctrl)
{
    page_tick();
    if (show_value && --show_value == 0)
        hdirty = 1;                      /* back to the HUM label */
    if (hdirty) {
        hdirty = 0;
        *((unsigned char *)ctrl + 0x20) = 1;
    }
}

void lfovel_draw(void *bmp, void *ctrl)
{
    int v;
    unsigned char *px;
    vis = 0;
    if (!on_midi_trig_page())
        return;
    px = *(unsigned char **)((char *)bmp + 0x10);
    if (!px || *(int *)((char *)bmp + 4) != 128 || *(int *)((char *)bmp + 8) != 64 || !slot_stock(px, 33, 48))
        return;                        /* a window, a popup or a blank screen covers the page */
    vis = 1;
    v = lfovel_hum[ACT_TRACK - 4];
    knob(px, 33, 48, (v * 40 + HUM_MAX / 2) / HUM_MAX);   /* pointer spans the full sweep whatever HUM_MAX is */
    if (show_value)
        DRAWTEXT(bmp, FONT, 33, 2, 0x22, "%d", v);
    else
        DRAWTEXT(bmp, FONT, 33, 2, 0x22, "HUM");
    if (show_value)
        hdr_popup(bmp, "Velocity Humanize=%d", v);
}
