    .arch armv7-a
    .fpu neon
    .arm
    .text
    .global t_neon
t_neon: vadd.i32 q0, q0, q0
        bx lr
    .section .note.GNU-stack,"",%progbits
