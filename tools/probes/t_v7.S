    .arch armv7-a
    .arm
    .text
    .global t_movw, t_movt
t_movw:  movw r0, #1
         bx lr
t_movt:  movt r0, #1
         bx lr
    .section .note.GNU-stack,"",%progbits
