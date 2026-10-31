    .arch armv6
    .fpu vfp
    .arm
    .text
    .global t_vfp_single, t_vfp_double
t_vfp_single: vmov s0, r0
              bx lr
t_vfp_double: vadd.f64 d0, d0, d0
              bx lr
    .section .note.GNU-stack,"",%progbits
