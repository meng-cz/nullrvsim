
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
    sw      a0, 0(a2)
    li      a0, 0
    ebreak



