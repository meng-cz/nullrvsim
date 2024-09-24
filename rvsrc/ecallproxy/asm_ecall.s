
.section .text

.global proxy_220_clone
proxy_220_clone:
    li      t1, 0x100000
    and     t2, a0, t1
    li      t1, 0x1000000
    and     t3, a0, t1
    li      a7, 1220
    ecall
    beqz    a0, child_settid
    beqz    t2, skip_settid
    sw      a0, 0(a2)
skip_settid:
    ebreak
child_settid:
    beqz    t3, skip_settid
    li      a7, 178
    ecall 
    sw      a0, 0(a4)
    li      a0, 0
    ebreak


.global proxy_435_clone3
proxy_435_clone3:
    mv      s0, a0 
    addi    a0, a1, 0
    li      a7, 901
    ecall   
    mv      a2, a1
    addi    a1, s0, 0
    addi    s1, a0, 0
    call    _rv64memcpy
    ld      t0, 0(s1)
    li      t1, 0x100000
    and     t2, t0, t1
    li      t1, 0x1000000
    and     t3, t0, t1
    addi    a0, s1, 0
    mv      a1, a2
    li      a7, 1435
    ecall 
    beqz    a0, clone3_child_settid
    beqz    t2, clone3_skip_settid
    ld      t0, 24(s1)
    sw      a0, 0(t0)
clone3_skip_settid:
    mv      s2, a0
    mv      a0, s1
    li      a7, 902
    ecall 
    mv      a0, s2
    ebreak
clone3_child_settid:
    beqz    t3, clone3_skip_settid
    li      a7, 178
    ecall   
    ld      t0, 16(s1)
    sw      a0, 0(t0)
    mv      a0, s1
    li      a7, 902
    ecall 
    li      a0, 0
    ebreak


.global proxy_501_pagefault
proxy_501_pagefault:
    addi    s11, a1, 0
    li      t0, 4096/128
pgcpy_loop:
    ld      a3, 0*8(a1)
    ld      a4, 1*8(a1)
    ld      a5, 2*8(a1)
    ld      a6, 3*8(a1)
    sd      a3, 0*8(a0)
    sd      a4, 1*8(a0)
    sd      a5, 2*8(a0)
    sd      a6, 3*8(a0)

    ld      s2, 4*8(a1)
    ld      s3, 5*8(a1)
    ld      s4, 6*8(a1)
    ld      s5, 7*8(a1)
    sd      s2, 4*8(a0)
    sd      s3, 5*8(a0)
    sd      s4, 6*8(a0)
    sd      s5, 7*8(a0)

    addi    s0, a0, 64
    addi    s1, a1, 64

    ld      s6, 0*8(s1)
    ld      s7, 1*8(s1)
    ld      s8, 2*8(s1)
    ld      s9, 3*8(s1)
    sd      s6, 0*8(s0)
    sd      s7, 1*8(s0)
    sd      s8, 2*8(s0)
    sd      s9, 3*8(s0)

    ld      t3, 4*8(s1)
    ld      t4, 5*8(s1)
    ld      t5, 6*8(s1)
    ld      t6, 7*8(s1)
    sd      t3, 4*8(s0)
    sd      t4, 5*8(s0)
    sd      t5, 6*8(s0)
    sd      t6, 7*8(s0)

    addi    a0, a0, 128
    addi    a1, a1, 128
    addi    t0, t0, -1
    bnez    t0, pgcpy_loop

    mv      a0, s11
    li      a7, 905
    ecall
    ebreak

