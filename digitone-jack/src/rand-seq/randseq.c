/* rand-seq 4.3: one-press randomizers, the same four controls on two pages, left to right:
 *   SCL  scale for PIT (0 = off, 1..9); press = scale off
 *   UNDO turn LEFT = undo, turn RIGHT = redo, through the last 8 RND / PIT presses (per track); FUNC + press = undo
 *   RND  0..100 % of the pattern length; press = clear the track's trigs and place that many at random. A new trig takes
 *        the note remembered for its step, so regenerating trigs keeps the notes PIT generated.
 *   PIT  0..36 semitones; press = every STEP (trig or not) gets a new note within +/-PIT of the TRIG page ROOT. With a scale the root,
 *        fifth and third come up more often (gravity) and chords stay in key (their scale-step shape follows the root).
 * LEN+ (MIDI tracks, SBNK slot above RND): 0..100 % of the trigs get a random longer length, anywhere up to 4 steps above the original, on a press (at 0 % a press puts every changed length back), always from their
 *        original length; the lengths are the per-step byte at +0xC0 (0xFF = the page LEN), undoable like RND and PIT.
 * Pages: MIDI tracks: MIDI Source page (SYN1 key), bottom row of slots (PB, VEL, MW, BC).
 *        synth tracks: Amplitude page 2/2 (AMP key), top row of four empty slots.
 * Each knob is drawn, and takes events, only while its slots still show the stock box (no window, popup or blank
 * screen on top). 0 = a press does nothing. Values per track, reset at power-on. Digitone mk1 / Keys OS 1.44. */
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
#define EDITOR(t)  ((unsigned char *)(0x41a73294UL + 0x58UL * (t)))
#define SET_STEP_FLAG ((void (*)(void *, int, int, int))0x4003673aUL)   /* (editor, step, mask, on) */
#define DRAWTEXT   ((void (*)(void *, void *, int, int, int, const char *, ...))0x400de120UL)
#define FONT       ((void *)0x418e934cUL)
#define FILLRECT   ((void (*)(void *, int, int, int, int, int))0x400dd506UL)   /* (bmp, x0, y0, x1, y1, colour: 1 set, 0 clear, -1 xor) */
#define SHOW_TICKS 90
#define HIST       8
#define NTR        8                    /* tracks 0-3 synth, 4-7 MIDI */
#define PIT_FLOOR  24                   /* C2: octaves 0 and 1 are never generated */
#define PIT_TILT   60                   /* the top of the range is 1.6 times as likely as the bottom */
#define ROOT_PULL  7                    /* extra weight (% of all the others) for the exact ROOT note: it pops back about 1 in 10 */
#define OCT_PULL   2                    /* ... and for each octave of it that is in range */

signed char rs_rnd[NTR];               /* percent / 2 (0..50) */
signed char rs_pit[NTR];               /* 0..36 */
signed char rs_scale[NTR];             /* 0 = off (chromatic) .. 9 */
signed char rs_lenp[NTR];              /* LEN+: percent / 2 of the trigs that get longer (0..50), MIDI tracks only */
unsigned char rs_lorig[NTR][64], rs_lnew[NTR][64], rs_lflag[NTR][64];   /* per step: the length before LEN+, the one LEN+ wrote, whether LEN+ has touched it */
int rs_counts_per_step = 6;
int rs_counts_scale = 12;
int rs_counts_undo = 12;               /* encoder counts per undo when turning the UNDO knob left */
int rs_undo_gap = 6;                   /* at least this many ticks (30 per second) between two undos from the knob */
static const unsigned short scale_mask[10] = {0xFFF, 0xAB5, 0x5AD, 0x6AD, 0xAD5, 0x6B5, 0x9AD, 0x295, 0x4A9, 0x4E9};
static const char *const scale_name[10] = {"Off", "Major", "Minor", "Dorian", "Lydian", "Mixolydian", "Harm Minor", "Penta Major", "Penta Minor", "Blues"};
static const char *const scale_short[10] = {"OFF", "MAJ", "MIN", "DOR", "LYD", "MIX", "HMI", "PMA", "PMI", "BLU"};
static int acc[5], uacc;
static unsigned char dirty;
static unsigned long show[5], ticks, last_undo, rng = 0x9E3779B9UL;
static const char *undo_msg = "";
static const signed char ptr_tab[41][2] = {{-5,5}, {-5,4}, {-6,4}, {-6,3}, {-7,2}, {-7,1}, {-7,1}, {-7,0}, {-7,-1}, {-7,-2}, {-6,-3}, {-6,-3}, {-6,-4}, {-5,-5}, {-5,-5}, {-4,-6}, {-3,-6}, {-2,-7}, {-2,-7}, {-1,-7}, {0,-7}, {1,-7}, {2,-7}, {2,-7}, {3,-6}, {4,-6}, {5,-5}, {5,-5}, {6,-4}, {6,-3}, {6,-3}, {7,-2}, {7,-1}, {7,0}, {7,1}, {7,1}, {7,2}, {6,3}, {6,4}, {5,4}, {5,5}};

/* slot k (0 SCL, 1 UNDO, 2 RND, 3 PIT) sits at x = slot_cx[k] on both pages */
struct layout {
    unsigned char first_enc, first_key, cy, label_y, ly0, ly1;      /* the knob row: encoders, pushes, centre row, label rows */
    unsigned char nk;                                               /* knob slots in the row (4, or 0) */
    unsigned char rcx, rcy, ren, rkey, rly, rly0, rly1;             /* the reset switch slot (rcx = 0: none) */
    unsigned char lcx, lcy, lenc, lkey;                             /* the LEN+ knob slot (lcx = 0: none) */
};
static const struct layout lay_midi  = { 5, 50, 48, 2, 53, 63, 4, 111, 21, 4, 49, 29, 32, 38, 85, 21, 3, 48 };   /* MIDI Source: bottom row E..H, switch in PROG */
static const struct layout lay_synth = { 1, 46, 21, 29, 32, 38, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };          /* synth Amplitude 2/2: top row A..D */
static const struct layout lay_sres  = { 0, 0, 0, 0, 0, 0, 0, 59, 48, 6, 51, 2, 53, 63, 0, 0, 0, 0 };          /* synth Amplitude 1/2: the switch in the empty slot F */
static const unsigned char slot_cx[4] = { 33, 59, 85, 111 };
static const struct layout *vis_lay;   /* the layout that was drawn undisturbed in the last frame, else 0 */
int rs_counts_reset = 24;              /* encoder counts of one firm turn right that throw the reset switch */
static int racc;
static unsigned char len_rest;         /* the last LEN+ press at 0 % put lengths back: the popup says so until the knob is turned */
static unsigned char racc_age, rst_on, rst_ticks;

/* ---- note memory: the root generated for each step (+1; 0 = none), kept whether or not a trig is on the step ---- */
unsigned char rs_nroot[NTR][64];

/* ---- timeline of RND / PIT presses: undo and redo swap the stored data with the pattern, only for the steps a press changed ---- */
static unsigned char tl_flag[NTR][HIST][128], tl_aux[NTR][HIST][64], tl_row[NTR][HIST][512], tl_nroot[NTR][HIST][64], tl_mask[NTR][HIST][8];
static unsigned char tl_lo[NTR][HIST][64], tl_ln[NTR][HIST][64], tl_lf[NTR][HIST][64];
static unsigned char tl_kind[NTR][HIST], tl_n[NTR], tl_c[NTR];      /* entries, cursor (0..entries) */
static unsigned char tmp_flag[128], tmp_aux[64], tmp_row[512], tmp_nroot[64], tmp_lo[64], tmp_ln[64], tmp_lf[64];

static unsigned long xorshift(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void cp(unsigned char *d, const unsigned char *s, int n)    /* volatile stores: never turned into a memcpy call */
{
    volatile unsigned char *vd = d;
    while (n-- > 0) *vd++ = *s++;
}

static int cands(const struct layout **c)
{
    unsigned t = ACT_TRACK, p = page_now();
    if (t >= 4 && t <= 7 && p == 1) { c[0] = &lay_midi; return 1; }               /* MIDI Source page, SYN1 key (SYN2 stays stock) */
    if (t <= 3 && p == 4) { c[0] = &lay_synth; c[1] = &lay_sres; return 2; }      /* Amplitude page; 1/2 and 2/2 are told apart by the slot check */
    return 0;
}

static int layout_ok(const struct layout *v)
{
    const struct layout *c[2];
    int n = cands(c), i;
    for (i = 0; i < n; i++)
        if (c[i] == v) return 1;
    return 0;
}

static int main_screen(void)
{
    unsigned long b = SCR_B;
    if (b < 0x40000010UL || b >= 0x48000000UL)
        return 0;
    return *(volatile unsigned long *)(b - 16) == MAIN_SCREEN_VT;
}

static unsigned char *editor(int t)
{
    unsigned char *ed = EDITOR(t);
    if (*(volatile unsigned long *)(ed + 0x38) != (unsigned long)t)
        return 0;
    return ed;
}

/* ---- pixels: column-major, 8 bytes per column, bit y = row y (0 = top) ---- */
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

/* the stock knob: radius-8 circle and a pointer; ly0..ly1 = the pixel rows of the stock label under it */
static void knob(unsigned char *px, int cx, int cy, int pos, int ly0, int ly1)
{
    static const signed char ring[][2] = {
        {-2,-8},{-1,-8},{0,-8},{1,-8},{2,-8},{-4,-7},{-3,-7},{3,-7},{4,-7},{-5,-6},{5,-6},
        {-6,-5},{6,-5},{-7,-4},{7,-4},{-7,-3},{7,-3},{-8,-2},{8,-2},{-8,-1},{8,-1},{-8,0},{8,0},
        {-8,1},{8,1},{-8,2},{8,2},{-7,3},{7,3},{-7,4},{7,4},{-6,5},{6,5},{-5,6},{5,6},
        {-4,7},{-3,7},{3,7},{4,7},{-2,8},{-1,8},{0,8},{1,8},{2,8}};
    int i, x, y;
    for (x = cx - 9; x <= cx + 9; x++)              /* the empty / disabled slot box */
        for (y = cy - 9; y <= cy + 9; y++)
            setpix(px, x, y, 0);
    for (x = cx - 12; x <= cx + 12; x++)            /* the stock label, if any */
        for (y = ly0; y <= ly1; y++)
            setpix(px, x, y, 0);
    for (i = 0; i < (int)(sizeof ring / sizeof ring[0]); i++)
        setpix(px, cx + ring[i][0], cy + ring[i][1], 1);
    if (pos < 0) pos = 0;
    if (pos > 40) pos = 40;
    line(px, cx, cy, cx + ptr_tab[pos][0], cy + ptr_tab[pos][1]);
}

/* The stock header popup ("Name=Value", about 3 s) */
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

/* ---- scales ---- */
static int in_scale(int q, int root, unsigned short mk)
{
    return (mk >> (((q - root) % 12 + 12) % 12)) & 1;
}

static int wgt(int q, int root, unsigned short mk)                 /* gravity: root, fifth, third pull most */
{
    int deg = ((q - root) % 12 + 12) % 12;
    if (mk == 0xFFF) return 1;                                     /* scale off: all notes equal */
    if (deg == 0) return 22;
    if (deg == 7) return 17;
    if (deg == 3 || deg == 4) return 15;
    return 8;
}

static int snap_note(int n, int root, unsigned short mk, int avoid)   /* nearest scale note, a tie goes down, never 'avoid' */
{
    int d, c;
    for (d = 0; d < 12; d++) {
        c = n - d;
        if (c >= 0 && c <= 127 && c != avoid && in_scale(c, root, mk)) return c;
        c = n + d;
        if (d && c >= 0 && c <= 127 && c != avoid && in_scale(c, root, mk)) return c;
    }
    return n;
}

/* The scale as a lattice of notes: index 0 = the page ROOT, +1 = the next scale note up, ... A chord is kept as
 * scale-step distances from its root, so repeated moves never erode it. */
static int scale_degs(unsigned short mk, int *degs)
{
    int n = 0, d;
    for (d = 0; d < 12; d++)
        if ((mk >> d) & 1) degs[n++] = d;
    return n;
}

static int lat_idx(int note, int R, const int *degs, int nd)       /* note must be in the scale */
{
    int rel = note - R, q = rel >= 0 ? rel / 12 : -((-rel + 11) / 12), pc = rel - 12 * q, p;
    for (p = 0; p < nd; p++)
        if (degs[p] == pc) break;
    return q * nd + p;
}

static int lat_note(int idx, int R, const int *degs, int nd)
{
    int q = idx >= 0 ? idx / nd : -((-idx + nd - 1) / nd), p = idx - q * nd;
    return R + 12 * q + degs[p];
}

/* The chord of a note row as scale-step distances from its root (snapped once to the scale); returns how many notes. */
static int chord_shape(const unsigned char *row, int R, unsigned short mk, const int *degs, int nd, int *dk, int *used)
{
    int n0 = row[0] > 127 ? R : row[0], s0 = snap_note(n0, R, mk, -1), i0 = lat_idx(s0, R, degs, nd), k, o, nk, sk, nu = 0;
    for (k = 1; k < 8; k++) {
        o = (signed char)row[k];
        if (o == 0) continue;
        nk = n0 + o;
        if (nk < 0) nk = 0;
        if (nk > 127) nk = 127;
        sk = snap_note(nk, R, mk, s0);
        dk[nu] = lat_idx(sk, R, degs, nd) - i0;
        used[nu++] = k;
    }
    return nu;
}

/* Move a freshly placed trig's note row to 'target' (the note remembered for its step). The chord stamped by the
 * firmware follows in scale steps when a scale is on and the target is in it; otherwise its semitone offsets stay.
 * If the chord would leave 0..127 the row is left as the firmware stamped it. */
static void apply_note(unsigned char *row, int target, int R, unsigned short mk, int sc, const int *degs, int nd)
{
    int dk[8], used[8], nu, k, j, o, nn, lo = 0, hi = 0;
    if (target < 0 || target > 127)
        return;
    if (sc && in_scale(target, R, mk)) {
        nu = chord_shape(row, R, mk, degs, nd, dk, used);
        for (j = 0; j < nu; j++) {
            nn = lat_note(lat_idx(target, R, degs, nd) + dk[j], R, degs, nd);
            if (nn < 0 || nn > 127) return;
        }
        for (j = 0; j < nu; j++)
            row[used[j]] = (unsigned char)(lat_note(lat_idx(target, R, degs, nd) + dk[j], R, degs, nd) - target);
    } else {
        for (k = 1; k < 8; k++) {
            o = (signed char)row[k];
            if (o < lo) lo = o;
            if (o > hi) hi = o;
        }
        if (target + lo < 0 || target + hi > 127)
            return;
    }
    row[0] = (unsigned char)target;
}

/* ---- timeline ---- */
static void swp(unsigned char *a, unsigned char *b, int n)
{
    volatile unsigned char *va = a, *vb = b;
    unsigned char x;
    while (n-- > 0) { x = *va; *va++ = *vb; *vb++ = x; }
}

static void snap_take(unsigned char *live, int t)
{
    cp(tmp_flag, live, 128);
    cp(tmp_aux, live + 0xC0, 64);
    cp(tmp_row, live + 448, 512);
    cp(tmp_nroot, rs_nroot[t], 64);
    cp(tmp_lo, rs_lorig[t], 64);
    cp(tmp_ln, rs_lnew[t], 64);
    cp(tmp_lf, rs_lflag[t], 64);
}

static void snap_push(int t, unsigned char *live, int kind)
{
    unsigned char m[8];
    int s, j, i, idx, any = 0, ch;
    for (j = 0; j < 8; j++) m[j] = 0;
    for (s = 0; s < 64; s++) {
        ch = tmp_flag[2 * s] != live[2 * s] || tmp_flag[2 * s + 1] != live[2 * s + 1] || tmp_aux[s] != live[0xC0 + s]
             || tmp_nroot[s] != rs_nroot[t][s] || tmp_lo[s] != rs_lorig[t][s] || tmp_ln[s] != rs_lnew[t][s] || tmp_lf[s] != rs_lflag[t][s];
        for (j = 0; j < 8; j++)
            if (tmp_row[8 * s + j] != live[448 + 8 * s + j]) ch = 1;
        if (ch) { m[s >> 3] |= (unsigned char)(1u << (s & 7)); any = 1; }
    }
    if (!any)
        return;
    if (tl_c[t] < tl_n[t])
        tl_n[t] = tl_c[t];                                          /* a new press drops whatever could still be redone */
    if (tl_n[t] >= HIST) {                                          /* full: forget the oldest */
        for (i = 1; i < HIST; i++) {
            cp(tl_flag[t][i - 1], tl_flag[t][i], 128);
            cp(tl_aux[t][i - 1], tl_aux[t][i], 64);
            cp(tl_row[t][i - 1], tl_row[t][i], 512);
            cp(tl_nroot[t][i - 1], tl_nroot[t][i], 64);
            cp(tl_lo[t][i - 1], tl_lo[t][i], 64);
            cp(tl_ln[t][i - 1], tl_ln[t][i], 64);
            cp(tl_lf[t][i - 1], tl_lf[t][i], 64);
            cp(tl_mask[t][i - 1], tl_mask[t][i], 8);
            tl_kind[t][i - 1] = tl_kind[t][i];
        }
        tl_n[t] = HIST - 1;
    }
    idx = tl_n[t];
    cp(tl_flag[t][idx], tmp_flag, 128);
    cp(tl_aux[t][idx], tmp_aux, 64);
    cp(tl_row[t][idx], tmp_row, 512);
    cp(tl_nroot[t][idx], tmp_nroot, 64);
    cp(tl_lo[t][idx], tmp_lo, 64);
    cp(tl_ln[t][idx], tmp_ln, 64);
    cp(tl_lf[t][idx], tmp_lf, 64);
    cp(tl_mask[t][idx], m, 8);
    tl_kind[t][idx] = (unsigned char)kind;
    tl_n[t]++;
    tl_c[t] = tl_n[t];
}

static int tl_step(int t, int dir)                 /* dir -1 = undo, +1 = redo; returns the kind (1 RND, 2 PIT) or 0 if there is nothing to do */
{
    unsigned char *ed = editor(t), *live, *saved;
    int idx, s;
    if (!ed || !(live = *(unsigned char **)(ed + 0x10)) || !(saved = *(unsigned char **)(ed + 0x20)))
        return 0;
    if (dir < 0) { if (!tl_c[t]) return 0; idx = tl_c[t] - 1; }
    else         { if (tl_c[t] >= tl_n[t]) return 0; idx = tl_c[t]; }
    for (s = 0; s < 64; s++)
        if (tl_mask[t][idx][s >> 3] & (1u << (s & 7))) {
            swp(live + 2 * s, tl_flag[t][idx] + 2 * s, 2);
            swp(live + 0xC0 + s, tl_aux[t][idx] + s, 1);
            swp(live + 448 + 8 * s, tl_row[t][idx] + 8 * s, 8);
            swp(&rs_nroot[t][s], tl_nroot[t][idx] + s, 1);
            swp(&rs_lorig[t][s], tl_lo[t][idx] + s, 1);
            swp(&rs_lnew[t][s], tl_ln[t][idx] + s, 1);
            swp(&rs_lflag[t][s], tl_lf[t][idx] + s, 1);
            cp(saved + 2 * s, live + 2 * s, 2);                    /* saved copy = live copy */
            cp(saved + 0xC0 + s, live + 0xC0 + s, 1);
            cp(saved + 384 + 8 * s, live + 448 + 8 * s, 8);
        }
    tl_c[t] = (unsigned char)(dir < 0 ? idx : idx + 1);
    return tl_kind[t][idx];
}

/* ---- the two actions ---- */
static void do_scatter(int t)
{
    unsigned char *ed = editor(t), *live, *saved, idx[64];
    int len, n, i, j, s, r0, R, pct = rs_rnd[t] * 2, sc = rs_scale[t], nd, degs[12];
    unsigned short mk;
    if (!ed || pct == 0 || !(live = *(unsigned char **)(ed + 0x10)) || !(saved = *(unsigned char **)(ed + 0x20)))
        return;
    len = live[966];
    if (len < 1) len = 1;
    if (len > 64) len = 64;
    R = live[960];
    if (R > 127) R = 60;
    if (sc < 0 || sc > 9) sc = 0;
    mk = scale_mask[sc];
    nd = scale_degs(mk, degs);
    n = (len * pct + 50) / 100;
    rng ^= ticks * 2654435761UL;
    snap_take(live, t);
    for (s = 0; s < len; s++)
        if (*(volatile unsigned short *)(live + 2 * s) & 1) {
            r0 = live[448 + 8 * s];
            rs_nroot[t][s] = (unsigned char)((r0 > 127 ? R : r0) + 1);   /* the trig's note is kept for its step */
            SET_STEP_FLAG(ed, s, 1, 0);                                  /* clear, as the stock remove does */
            SET_STEP_FLAG(ed, s, 2, 0);
        }
    for (i = 0; i < len; i++) idx[i] = (unsigned char)i;
    for (i = 0; i < n; i++) {
        j = i + (int)(xorshift() % (unsigned long)(len - i));
        s = idx[j]; idx[j] = idx[i]; idx[i] = (unsigned char)s;
        SET_STEP_FLAG(ed, s, 1, 1);                                      /* place, as the stock trig key does */
        if (rs_nroot[t][s])                                              /* ... and give it the note remembered for this step */
            apply_note(live + 448 + 8 * s, rs_nroot[t][s] - 1, R, mk, sc, degs, nd);
    }
    for (s = 0; s < len; s++) {                                          /* saved copy = live copy, whatever the job queue does */
        cp(saved + 2 * s, live + 2 * s, 2);
        cp(saved + 0xC0 + s, live + 0xC0 + s, 1);
        cp(saved + 384 + 8 * s, live + 448 + 8 * s, 8);
    }
    snap_push(t, live, 1);
}

static long tilt_w(int q, int R, unsigned short mk, int vmin, int span)   /* gravity, leaning upward */
{
    return (long)wgt(q, R, mk) * (100 + PIT_TILT * (q - vmin) / span);
}

static void do_pitch(int t)
{
    unsigned char *ed = editor(t), *live, *saved;
    int len, s, k, q, r = rs_pit[t], R, v, vmin, vmax, nd, sc = rs_scale[t], ok, has, lo, hi, span, root_ok, oct_n;
    int degs[12], dk[8], used[8], nu, nn;
    long total, pick, w, wR, wO;
    unsigned short mk;
    if (!ed || r == 0 || !(live = *(unsigned char **)(ed + 0x10)) || !(saved = *(unsigned char **)(ed + 0x20)))
        return;
    len = live[966];
    if (len < 1) len = 1;
    if (len > 64) len = 64;
    R = live[960];                                                 /* TRIG page ROOT = the scale's root */
    if (R > 127) R = 60;
    if (sc < 0 || sc > 9) sc = 0;
    mk = scale_mask[sc];
    nd = scale_degs(mk, degs);
    rng ^= ticks * 2654435761UL;
    snap_take(live, t);
    for (s = 0; s < len; s++) {                                    /* every step gets a note, with a trig on it or not */
        unsigned char *row = live + 448 + 8 * s;
        has = *(volatile unsigned short *)(live + 2 * s) & 1;
        vmin = R - r; if (vmin < 0) vmin = 0;
        vmax = R + r; if (vmax > 127) vmax = 127;
        nu = 0; lo = 0; hi = 0;
        if (has) {
            if (sc) {                                              /* chord shape in scale steps, snapped once to the scale */
                nu = chord_shape(row, R, mk, degs, nd, dk, used);
            } else {                                               /* scale off: offsets stay, every chord note must stay in 0..127 */
                for (k = 1; k < 8; k++) {
                    int o = (signed char)row[k];
                    if (o < lo) lo = o;
                    if (o > hi) hi = o;
                }
                if (vmin < -lo) vmin = -lo;
                if (vmax > 127 - hi) vmax = 127 - hi;
            }
        }
        if (vmin < PIT_FLOOR) vmin = PIT_FLOOR;                    /* nothing below C2 ... unless the whole range is lower */
        if (vmin > vmax) vmin = vmax;
        span = vmax - vmin; if (span < 1) span = 1;
        total = 0; root_ok = 0; oct_n = 0;
        for (q = vmin; q <= vmax; q++) {                           /* candidates: scale notes whose whole chord stays in 0..127 */
            if (!in_scale(q, R, mk)) continue;
            ok = 1;
            for (k = 0; k < nu && ok; k++) {
                nn = lat_note(lat_idx(q, R, degs, nd) + dk[k], R, degs, nd);
                if (nn < 0 || nn > 127) ok = 0;
            }
            if (ok) {
                total += tilt_w(q, R, mk, vmin, span);
                if (q == R) root_ok = 1;
                else if (q == R + 12 || q == R - 12) oct_n++;
            }
        }
        wR = root_ok ? total * ROOT_PULL / 100 : 0;                /* the ROOT itself pops back now and then */
        wO = total * OCT_PULL / 100;
        total += wR + wO * oct_n;
        if (total == 0) {
            v = R < vmin ? vmin : (R > vmax ? vmax : R);
        } else {                                                   /* a weighted pick (gravity) */
            pick = (long)(xorshift() % (unsigned long)total);
            v = vmin;
            for (q = vmin; q <= vmax; q++) {
                if (!in_scale(q, R, mk)) continue;
                ok = 1;
                for (k = 0; k < nu && ok; k++) {
                    nn = lat_note(lat_idx(q, R, degs, nd) + dk[k], R, degs, nd);
                    if (nn < 0 || nn > 127) ok = 0;
                }
                if (!ok) continue;
                w = tilt_w(q, R, mk, vmin, span);
                if (q == R) w += wR;
                else if (q == R + 12 || q == R - 12) w += wO;
                pick -= w;
                if (pick < 0) { v = q; break; }
            }
        }
        rs_nroot[t][s] = (unsigned char)(v + 1);                   /* remembered for the step */
        if (has) {
            row[0] = (unsigned char)v;
            saved[384 + 8 * s] = (unsigned char)v;
            if (sc && total)                                       /* the chord follows, step for step */
                for (k = 0; k < nu; k++) {
                    nn = lat_note(lat_idx(v, R, degs, nd) + dk[k], R, degs, nd);
                    row[used[k]] = (unsigned char)(nn - v);
                    saved[384 + 8 * s + used[k]] = row[used[k]];
                }
        }
    }
    snap_push(t, live, 2);
}

/* ---- the reset switch: the stock LFO.T toggle (17x9 pixels: off = small lever at the left on a black background, on = filled), thrown right it resets, then springs back left ---- */
static const unsigned long tg_off[9] = {0x07FFC, 0x08002, 0x13801, 0x17C01, 0x17C01, 0x17C01, 0x13801, 0x08002, 0x07FFC};
static const unsigned long tg_on[9]  = {0x07FFC, 0x08002, 0x13FB9, 0x17F7D, 0x17F7D, 0x17F7D, 0x13FB9, 0x08002, 0x07FFC};

/* ---- LEN+: some trigs get longer (never shorter) by 1..4 steps ---- */
/* The LEN value (0..127) is an index into a table of lengths; in 1/16 of a step: 0..14 -> 2..16 (14 = exactly 1 step), then the
 * resolution gets coarser: 1/16, 1/8, 1/4, 1/2, 1, 2 and 4 steps per value, up to 126 = 128 steps; 127 = INF. */
static int len16(int v)
{
    if (v <= 14)  return v + 2;
    if (v <= 30)  return 16 + (v - 14);
    if (v <= 46)  return 32 + 2 * (v - 30);
    if (v <= 62)  return 64 + 4 * (v - 46);
    if (v <= 78)  return 128 + 8 * (v - 62);
    if (v <= 94)  return 256 + 16 * (v - 78);
    if (v <= 110) return 512 + 32 * (v - 94);
    return 1024 + 64 * (v - 110);
}

static int do_lenplus(int t)                          /* returns how many lengths were put back (the 0 % case), else 1 */
{
    unsigned char *ed = editor(t), *live, *saved, idx[64], ext[64];
    int len, n, nt = 0, i, j, s, v, b, base, dflt, tg, cnt = 0, pct = rs_lenp[t] * 2;
    if (!ed || t < 4 || !(live = *(unsigned char **)(ed + 0x10)) || !(saved = *(unsigned char **)(ed + 0x20)))
        return 0;
    if (pct == 0) {                                                /* 0 %: every length LEN+ changed goes back to what it was before */
        snap_take(live, t);
        for (s = 0; s < 64; s++)
            if (rs_lflag[t][s]) {
                if (live[0xC0 + s] == rs_lnew[t][s] && rs_lorig[t][s] != rs_lnew[t][s]) {   /* still ours: put it back; a hand edit stays */
                    live[0xC0 + s] = rs_lorig[t][s];
                    saved[0xC0 + s] = rs_lorig[t][s];
                    cnt++;
                }
                rs_lflag[t][s] = 0;
            }
        snap_push(t, live, 3);
        return cnt;
    }
    len = live[966];
    if (len < 1) len = 1;
    if (len > 64) len = 64;
    dflt = live[962];                                              /* the page LEN, used by unlocked trigs (0xFF) */
    rng ^= ticks * 2654435761UL;
    snap_take(live, t);
    for (s = 0; s < 64; s++) ext[s] = 0;
    for (s = 0; s < len; s++)
        if (*(volatile unsigned short *)(live + 2 * s) & 1) {
            if (!(rs_lflag[t][s] && live[0xC0 + s] == rs_lnew[t][s])) {   /* not ours any more (edited, re-placed): what is there is the original */
                rs_lorig[t][s] = live[0xC0 + s];
                rs_lflag[t][s] = 1;
            }
            idx[nt++] = (unsigned char)s;
        }
    n = (nt * pct + 50) / 100;
    for (i = 0; i < n; i++) {                                      /* exactly that share of the trigs, chosen at random */
        j = i + (int)(xorshift() % (unsigned long)(nt - i));
        s = idx[j]; idx[j] = idx[i]; idx[i] = (unsigned char)s;
        ext[s] = 1;
    }
    for (s = 0; s < len; s++)
        if (*(volatile unsigned short *)(live + 2 * s) & 1) {
            base = rs_lorig[t][s];                                 /* always from the original, so presses never pile up */
            v = base;
            if (ext[s]) {
                b = base == 0xFF ? dflt : base;
                if (b <= 125) {                                    /* a random length anywhere in (original, original + 4 steps] */
                    tg = len16(b) + 1 + (int)(xorshift() % 64UL);   /* 64 = 4 steps in 1/16 steps */
                    v = b;
                    while (v < 126 && len16(v + 1) <= tg) v++;      /* the longest table length not above the target */
                    if (v == b) v = b + 1;                           /* it has to be longer: the next one up (never more than 4 steps further) */
                }
            }
            live[0xC0 + s] = (unsigned char)v;
            saved[0xC0 + s] = (unsigned char)v;
            rs_lnew[t][s] = (unsigned char)v;
        }
    snap_push(t, live, 3);
    return 1;
}

/* ---- reset: start the randomizer again, as after a boot, on this track ---- */
static void do_reset(int t)
{
    unsigned char *ed = editor(t), *live, *saved;
    int s;
    rs_rnd[t] = 0; rs_pit[t] = 0;                                   /* the scale you picked stays */
    for (s = 0; s < 64; s++) { rs_nroot[t][s] = 0; rs_lflag[t][s] = 0; rs_lorig[t][s] = 0; rs_lnew[t][s] = 0; }
    rs_lenp[t] = 0;
    tl_n[t] = 0; tl_c[t] = 0;
    for (s = 0; s < 4; s++) acc[s] = 0;
    uacc = 0;
    if (ed && (live = *(unsigned char **)(ed + 0x10)) && (saved = *(unsigned char **)(ed + 0x20))) {
        for (s = 0; s < 64; s++)
            if (*(volatile unsigned short *)(live + 2 * s) & 1) {   /* every trig goes, as the stock remove does it */
                SET_STEP_FLAG(ed, s, 1, 0);
                SET_STEP_FLAG(ed, s, 2, 0);
            }
        for (s = 0; s < 64; s++) {
            cp(saved + 2 * s, live + 2 * s, 2);
            cp(saved + 0xC0 + s, live + 0xC0 + s, 1);
            cp(saved + 384 + 8 * s, live + 448 + 8 * s, 8);
        }
    }
}

static void draw_switch(unsigned char *px, void *bmp, const struct layout *L, int on)
{
    const unsigned long *a = on ? tg_on : tg_off;
    int x, y, r, c;
    for (x = L->rcx - 9; x <= L->rcx + 9; x++)
        for (y = L->rcy - 9; y <= L->rcy + 9; y++)
            setpix(px, x, y, 0);
    for (x = L->rcx - 12; x <= L->rcx + 12; x++)
        for (y = L->rly0; y <= L->rly1; y++)
            setpix(px, x, y, 0);
    for (r = 0; r < 9; r++)
        for (c = 0; c < 17; c++)
            if ((a[r] >> (16 - c)) & 1)
                setpix(px, L->rcx - 8 + c, L->rcy - 4 + r, 1);
    DRAWTEXT(bmp, FONT, L->rcx, L->rly, 0x22, "RST");
}

/* ---- events ---- */
static void do_tl(int t, int dir)
{
    int kd = tl_step(t, dir);
    if (!kd)           undo_msg = dir < 0 ? "Undo: none left" : "Redo: none left";
    else if (dir < 0)  undo_msg = kd == 1 ? "Undo: Random Hits" : (kd == 3 ? "Undo: Random Len" : "Undo: Random Pitch");
    else               undo_msg = kd == 1 ? "Redo: Random Hits" : (kd == 3 ? "Redo: Random Len" : "Redo: Random Pitch");
    show[1] = SHOW_TICKS;
    dirty = 1;
}

int rs_enc(void *brain, void *ev)
{
    int e = *(int *)((char *)ev + 12), d = *(int *)((char *)ev + 16), k, t = ACT_TRACK, steps = 0, v, cps, dir;
    const struct layout *L = vis_lay;
    if (!L || !layout_ok(L))
        return 0;
    if (L->rcx && e == L->ren) {                     /* the reset switch: only a firm turn right throws it, left does nothing */
        racc_age = 0;
        if (d < 0) { racc = 0; return 1; }
        if (!rst_ticks) {
            racc += d;
            if (racc >= rs_counts_reset) {
                racc = 0;
                do_reset(t);
                rst_on = 1; rst_ticks = 8;           /* thrown; it springs back after 0.27 s, like a spring-loaded switch */
                undo_msg = "Randomizer reset"; show[1] = SHOW_TICKS; dirty = 1;
            }
        }
        return 1;
    }
    if (L->lcx && e == L->lenc) {                    /* the LEN+ knob: turn = how many trigs get longer (percent, steps of 2) */
        acc[4] += d;
        steps = 0;
        while (acc[4] >= rs_counts_per_step) { acc[4] -= rs_counts_per_step; steps++; }
        while (acc[4] <= -rs_counts_per_step) { acc[4] += rs_counts_per_step; steps--; }
        if (steps) {
            v = rs_lenp[t] + steps; if (v < 0) v = 0; if (v > 50) v = 50;
            rs_lenp[t] = (signed char)v;
            dirty = 1;
        }
        len_rest = 0;
        show[4] = SHOW_TICKS;
        return 1;
    }
    k = e - L->first_enc;
    if (!L->nk || k < 0 || k > 3)
        return 0;
    if (k == 1) {                                    /* UNDO knob: turn left = undo, turn right = redo */
        if ((d > 0 && uacc < 0) || (d < 0 && uacc > 0)) uacc = 0;      /* a change of direction starts over: jitter does nothing */
        uacc += d;
        if (uacc < -2 * rs_counts_undo) uacc = -2 * rs_counts_undo;
        if (uacc > 2 * rs_counts_undo) uacc = 2 * rs_counts_undo;
        if (uacc <= -rs_counts_undo || uacc >= rs_counts_undo) {
            dir = uacc < 0 ? -1 : 1;
            if (ticks - last_undo >= (unsigned long)rs_undo_gap) {
                uacc -= dir * rs_counts_undo;
                last_undo = ticks;
                do_tl(t, dir);
                if (dir < 0 ? !tl_c[t] : tl_c[t] >= tl_n[t]) uacc = 0;   /* nothing further that way */
            } else
                uacc = dir * rs_counts_undo;         /* too soon after the last step: wait */
        }
        return 1;
    }
    cps = k == 0 ? rs_counts_scale : rs_counts_per_step;
    acc[k] += d;
    while (acc[k] >= cps) { acc[k] -= cps; steps++; }
    while (acc[k] <= -cps) { acc[k] += cps; steps--; }
    if (steps) {
        if (k == 0)      { v = rs_scale[t] + steps; if (v < 0) v = 0; if (v > 9) v = 9;   rs_scale[t] = (signed char)v; }
        else if (k == 2) { v = rs_rnd[t] + steps;   if (v < 0) v = 0; if (v > 50) v = 50; rs_rnd[t] = (signed char)v; }
        else             { v = rs_pit[t] + steps;   if (v < 0) v = 0; if (v > 36) v = 36; rs_pit[t] = (signed char)v; }
        dirty = 1;
    }
    show[k] = SHOW_TICKS;
    return 1;
}

int rs_key(void *brain, void *ev)
{
    int id = *(int *)((char *)ev + 12), fl = *(int *)((char *)ev + 16), k, t = ACT_TRACK, v;
    const struct layout *L = vis_lay;
    page_key(id, fl);
    if (!L || !layout_ok(L))
        return 0;
    if (L->rcx && id == L->rkey) {                   /* a press on the reset switch only shows how it works */
        if ((fl & 1) && !(fl & 2) && !(fl & 8)) { undo_msg = "Turn right = Reset"; show[1] = SHOW_TICKS; dirty = 1; }
        return 1;
    }
    if (L->lcx && id == L->lkey) {                   /* press LEN+: lengthen that share of the trigs */
        if (fl & 8) return 1;                        /* a key held down repeats: only the first press counts */
        if ((fl & 1) && !(fl & 2)) { v = do_lenplus(t); len_rest = (unsigned char)(rs_lenp[t] == 0 && v > 0); show[4] = SHOW_TICKS; dirty = 1; }
        return 1;
    }
    k = id - L->first_key;
    if (!L->nk || k < 0 || k > 3)
        return 0;
    if (fl & 8) return 1;                            /* a key held down repeats: only the first press counts */
    if (fl & 1) {                                    /* a press (bit 2 = FUNC held; repeats and releases are just swallowed) */
        if (k == 1) {
            if (fl & 2) do_tl(t, -1);
            else { undo_msg = "Turn = Undo / Redo"; show[1] = SHOW_TICKS; }
        } else if (!(fl & 2)) {
            if (k == 2)      do_scatter(t);
            else if (k == 3) do_pitch(t);
            else             rs_scale[t] = 0;        /* press SCL: scale off */
            show[k] = SHOW_TICKS;
        }
        dirty = 1;
    }
    return 1;
}

void rs_tick(void *ctrl)
{
    int k;
    page_tick();
    ticks++;
    for (k = 0; k < 5; k++)
        if (show[k] && --show[k] == 0)
            dirty = 1;
    if (rst_ticks && --rst_ticks == 0) { rst_on = 0; dirty = 1; }       /* the spring */
    if (racc && ++racc_age > 15) { racc = 0; racc_age = 0; }             /* a half-hearted turn fades */
    if (dirty) {
        dirty = 0;
        *((unsigned char *)ctrl + 0x20) = 1;
    }
}

void rs_draw(void *bmp, void *ctrl)
{
    unsigned char *px;
    const struct layout *c[2], *L = 0;
    int n = cands(c), i, ok, t = ACT_TRACK, k, v, pos, cx, pk;
    vis_lay = 0;
    if (!n || !main_screen())
        return;
    px = *(unsigned char **)((char *)bmp + 0x10);
    if (!px || *(int *)((char *)bmp + 4) != 128 || *(int *)((char *)bmp + 8) != 64)
        return;
    for (i = 0; i < n && !L; i++) {                  /* the first layout whose slots all still show the stock box */
        ok = 1;
        for (k = 0; k < c[i]->nk; k++)
            if (!slot_stock(px, slot_cx[k], c[i]->cy)) ok = 0;
        if (c[i]->rcx && !slot_stock(px, c[i]->rcx, c[i]->rcy)) ok = 0;
        if (c[i]->lcx && !slot_stock(px, c[i]->lcx, c[i]->lcy)) ok = 0;
        if (ok) L = c[i];
    }
    if (!L)
        return;                                      /* a window, a popup, another page or a blank screen: draw nothing */
    vis_lay = L;
    for (k = 0; k < L->nk; k++) {
        cx = slot_cx[k];
        v = k == 0 ? rs_scale[t] : (k == 1 ? tl_c[t] : (k == 2 ? rs_rnd[t] * 2 : rs_pit[t]));
        pos = k == 0 ? (v * 40 + 4) / 9 : (k == 1 ? v * 40 / HIST : (k == 2 ? (v * 40 + 50) / 100 : (v * 40 + 18) / 36));
        knob(px, cx, L->cy, pos, L->ly0, L->ly1);
        if (show[k] && k != 1) {
            if (k == 0)      DRAWTEXT(bmp, FONT, cx, L->label_y, 0x22, "%s", scale_short[v]);
            else if (k == 3) DRAWTEXT(bmp, FONT, cx, L->label_y, 0x22, "%d", v);
            else             DRAWTEXT(bmp, FONT, cx, L->label_y, 0x22, "%d%%", v);
        } else
            DRAWTEXT(bmp, FONT, cx, L->label_y, 0x22, k == 0 ? "SCL" : (k == 1 ? "UNDO" : (k == 2 ? "RND" : "PIT")));
    }
    if (L->rcx)
        draw_switch(px, bmp, L, rst_on);
    if (L->lcx) {                                    /* LEN+ in the SBNK slot, above RND */
        v = rs_lenp[t] * 2;
        knob(px, L->lcx, L->lcy, (v * 40 + 50) / 100, L->rly0, L->rly1);
        if (show[4]) DRAWTEXT(bmp, FONT, L->lcx, L->rly, 0x22, "%d%%", v);
        else         DRAWTEXT(bmp, FONT, L->lcx, L->rly, 0x22, "LEN+");
    }
    if (show[0] || show[1] || show[2] || show[3] || show[4]) {
        pk = 0;
        for (k = 1; k < 5; k++)
            if (show[k] > show[pk]) pk = k;
        if (pk == 4)      hdr_popup(bmp, len_rest ? "Len=0%%: restored" : "Random Len=%d%%", rs_lenp[t] * 2);
        else if (pk == 1) hdr_popup(bmp, "%s", (int)undo_msg);
        else if (pk == 0) hdr_popup(bmp, "Scale=%s", (int)scale_name[rs_scale[t]]);
        else if (pk == 3) hdr_popup(bmp, "Random Pitch=%d", rs_pit[t]);
        else              hdr_popup(bmp, "Random Hits=%d%%", rs_rnd[t] * 2);
    }
}
