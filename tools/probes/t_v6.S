    .arch armv6k
    .arm
    .text
    .global t_rev, t_ldrex, t_uxtb
t_rev:   rev r0, r0
         bx lr
t_ldrex: ldrex r1, [r0]
         strex r2, r1, [r0]
         bx lr
t_uxtb:  uxtb r0, r0
         bx lr
    .section .note.GNU-stack,"",%progbits
