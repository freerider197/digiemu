| Replaces, at 0x40035b0a (6 bytes), "move.l d2,-(sp); move.l a2,-(sp); jsr (a3)"
| inside the loop that clears a new trig's chord offsets 1..7:
|     for (i = 1; i < 8; i++) setStepOffset(obj a2, step d2, value 0, index i)
| On entry the loop has pushed index and value. For a MIDI track (active track
| 4-7) and index 1/2 with value 0, value becomes NOT2/NOT3 of that track.
| The stack is left as stock (4 args) for the loop's own lea 16(sp),sp.
        .equ    SET_STEP_OFFSET, 0x40035858
        .equ    ACT_TRACK, 0x41368ce3
        .section .run, "ax"
        .globl  chord_wrap
chord_wrap:
        move.l  (%sp)+, chord_ret       | sp -> value, 4(sp) -> index
        tst.l   (%sp)
        bne.s   2f                      | a non-zero value: leave it
        move.l  4(%sp), %d0
        subq.l  #1, %d0                 | index 1/2 -> 0/1
        moveq   #1, %d1
        cmp.l   %d1, %d0
        bhi.s   2f
        moveq   #0, %d1
        move.b  ACT_TRACK, %d1
        subq.l  #4, %d1
        moveq   #3, %d0
        cmp.l   %d0, %d1
        bhi.s   2f                      | not a MIDI track
        move.l  4(%sp), %d0
        subq.l  #1, %d0                 | k again
        add.l   %d1, %d1
        add.l   %d0, %d1                | t*2 + k
        lea     chord_off, %a0
        move.b  (%a0,%d1.l), %d1
        extb.l  %d1
        move.l  %d1, (%sp)              | value = NOT2 / NOT3
2:      move.l  %d2, -(%sp)
        move.l  %a2, -(%sp)
        jsr     SET_STEP_OFFSET
        move.l  chord_ret, -(%sp)
        rts
        .bss
        .align  4
chord_ret:      .space  4
