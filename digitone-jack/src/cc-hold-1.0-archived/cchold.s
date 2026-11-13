| cc-hold: keep the MIDI tracks' "synced" flags across STOP, so the next
| PLAY does not force-resend every enabled CC (and BANK/PROG/PB/AT/MW/BC)
| to the external gear. Digitone mk1 / Keys, OS 1.44.
|
| Stock behaviour: the STOP handler calls the pattern re-init (0x40094f14),
| which calls 0x400f396a: store the kit pointer, then clear the four
| per-MIDI-track flags at 0x419c5b14..0x419c5b20. A cleared flag makes the
| next trig on that track take the full-resync path, which force-sends all
| enabled parameters.
|
| This mod: the STOP handler's call goes through cch_stop, which raises
| cch_suppress around the stock re-init; the re-init's call to 0x400f396a
| goes through cch_kitset, which, while suppressed, only stores the kit
| pointer. Every other caller (pattern change, project load) is unchanged.

        .equ    PATTERN_REINIT, 0x40094f14
        .equ    KIT_SET,        0x400f396a
        .equ    KIT_PTR,        0x419c5b10

        .section .run, "ax"
        .globl  cch_stop
        .globl  cch_kitset

| Replaces, at 0x40095410:  jsr PATTERN_REINIT (pc-relative) ; lea 28(%sp),%sp
cch_stop:
        move.l  (%sp)+, cch_ret         | our return address off the stack, so
                                        | the stock arguments sit where expected
        moveq   #1, %d1
        move.l  %d1, cch_suppress
        jsr     PATTERN_REINIT
        clr.l   cch_suppress
        lea     28(%sp), %sp            | the stock instruction this site covers
        move.l  cch_ret, -(%sp)
        rts

| Replaces, at 0x40094f92:  jsr KIT_SET
cch_kitset:
        tst.l   cch_suppress
        bne.s   1f
        jmp     KIT_SET                 | not from STOP: stock, untouched
1:      lea     4(%sp), %a0             | from STOP: store the kit pointer only,
        move.l  (%a0), KIT_PTR          | exactly as stock, without the clears
        rts

        .bss
        .align  4
cch_suppress:   .space  4
cch_ret:        .space  4
