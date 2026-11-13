        .section .run, "ax"

| unsigned long tt_irq_off(void): mask all interrupts, return the old status register
        .globl  tt_irq_off
tt_irq_off:
        moveq   #0, %d0
        move.w  %sr, %d0
        move.w  #0x2700, %sr
        rts

| void tt_irq_restore(unsigned long sr)
        .globl  tt_irq_restore
tt_irq_restore:
        move.w  6(%sp), %d0
        move.w  %d0, %sr
        rts
