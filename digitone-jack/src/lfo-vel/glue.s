        .equ    CC_SENDER, 0x400f3c4a
        .section .run, "ax"

| Replaces, at 0x400f4846, "move.b 35(a2),71(sp)" (note-on velocity byte).
| a2 = trig event, d2 = MIDI track 0-3, d3 = note index in the chord. The caller's 71(sp) is 75(sp) here.
        .globl  lfovel_note
lfovel_note:
        moveq   #0, %d0
        move.b  35(%a2), %d0
        move.l  %d3, -(%sp)            | index of the note in the trig's chord
        move.l  %d2, -(%sp)            | track
        move.l  %d0, -(%sp)            | velocity
        jsr     lfovel_velocity
        lea     12(%sp), %sp
        move.b  %d0, 75(%sp)
        rts

| Replaces, at 0x400f4792 (8 bytes), "movea.l d4,a0; jsr (a0); lea 20(sp),sp"
| in the modulation loop: send the modulated value, except for AT (22),
| which now drives velocity instead.
| Stack on entry: ret, track, ch, dest, value, 0.
        .globl  lfovel_mod
lfovel_mod:
        move.l  (%sp)+, lfovel_ret
        move.l  8(%sp), %d0            | dest
        moveq   #22, %d1
        cmp.l   %d1, %d0
        beq.s   1f
        jsr     CC_SENDER
1:      lea     20(%sp), %sp           | the stock instruction this site covers
        move.l  lfovel_ret, -(%sp)
        rts

        .bss
        .align  4
lfovel_ret:     .space  4
