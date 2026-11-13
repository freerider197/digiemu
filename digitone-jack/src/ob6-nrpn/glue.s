| Replaces, at 0x400f3d72 (8 bytes):
|     jsr  0x400f3c1a (pc-relative)    append(len=3, buf) to the MIDI out buffer
|     move.b %d3,(0,%a2,%d5.l)         mark the value as sent (last-sent cache)
| The firmware pushed (len, buf) before this; the stack is left exactly as stock.
        .equ    APPEND, 0x400f3c1a
        .section .run, "ax"
        .globl  ob6_nrpn_hook
ob6_nrpn_hook:                          | sp: ret, len, buf
        move.l  8(%sp), -(%sp)          | buf
        jsr     ob6_nrpn_xlate
        addq.l  #4, %sp
        moveq   #2, %d1
        cmp.l   %d1, %d0
        beq.s   2f                      | buffer full: skip, keep value unsent
        tst.l   %d0
        bne.s   1f                      | sent as NRPN
        move.l  8(%sp), -(%sp)          | buf
        move.l  8(%sp), -(%sp)          | len
        jsr     APPEND                  | stock CC
        addq.l  #8, %sp
1:      move.b  %d3, (0,%a2,%d5.l)      | the stock instruction this site covers
2:      rts
