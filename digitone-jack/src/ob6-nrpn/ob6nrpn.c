/* ob6-nrpn: on MIDI channel 14, every CC a Digitone MIDI track sends is
 * turned into the OB-6 NRPN from table.h (99/98 address, 6/38 value),
 * scaled from the knob's 0-127 to the parameter's own range.
 * Pass-through and unused numbers go out unchanged.
 * Digitone mk1 / Keys OS 1.44. Called from glue.s at the CC emit point. */
#include "table.h"

#define CH      13                                   /* MIDI channel 14 */
#define APPEND  ((void (*)(int, const unsigned char *))0x400f3c1aUL)
#define OUTLEN  (*(volatile unsigned char *)0x419b18edUL)  /* bytes queued */

/* The OB-6 applies each data-entry half (CC 6 / CC 38) as it arrives, combined
 * with the other half it last received. When both halves change (127 <-> 128)
 * the in-between value can spike to the top (forum.sequential.com topic 3405).
 * Track the halves we last sent and pick the order that makes the in-between
 * value exact, or else the lower one (a brief dip instead of a full spike). */
static unsigned char g_msb = 0xFF, g_lsb = 0xFF;      /* 0xFF: not known yet */


static unsigned scale(unsigned v, unsigned lo, unsigned hi, int centered)
{
    if (centered) {
        unsigned c = (lo + hi) / 2;
        if (v <= 64)
            return lo + (v * (c - lo) + 32) / 64;
        return c + ((v - 64) * (hi - c) + 31) / 63;
    }
    return lo + (v * (hi - lo) + 63) / 127;
}

/* 0: not ours, send the CC as stock; 1: sent as NRPN;
 * 2: output buffer too full, send nothing and do not mark it as sent */
int ob6_nrpn_xlate(const unsigned char *m)
{
    unsigned char st = m[0], cc = m[1], v = m[2];
    unsigned char out[12];
    unsigned val;
    int n = 0;

    if (st != (0xB0 | CH) || cc > 127 || v > 127)
        return 0;
    if (T_MSB[cc] == 0xFF)
        return 0;
    if (OUTLEN > 240)
        return 2;

    val = scale(v, T_LO[cc], T_HI[cc], T_FL[cc] & 1);
    {
        unsigned char vm = (val >> 7) & 0x7F, vl = val & 0x7F;
        int lsb_first;
        if (g_msb == 0xFF || g_lsb == 0xFF)
            lsb_first = 0;                              /* unknown: MSB, LSB */
        else if (g_msb == vm)
            lsb_first = 1;                              /* in-between = target */
        else if (g_lsb == vl)
            lsb_first = 0;                              /* in-between = target */
        else                                            /* pick the lower one */
            lsb_first = ((unsigned)g_msb * 128 + vl) < ((unsigned)vm * 128 + g_lsb);
        out[0] = st;  out[1] = 99;  out[2] = T_MSB[cc];
        out[3] = st;  out[4] = 98;  out[5] = T_LSB[cc];
        if (lsb_first) {
            out[6] = st;  out[7] = 38;  out[8] = vl;
            out[9] = st;  out[10] = 6;  out[11] = vm;
        } else {
            out[6] = st;  out[7] = 6;   out[8] = vm;
            out[9] = st;  out[10] = 38; out[11] = vl;
        }
        n = 12;
        g_msb = vm;
        g_lsb = vl;
    }
    APPEND(n, out);
    return 1;
}
