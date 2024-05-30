
.section .text

.global proxy_220_clone
proxy_220_clone:
    li      t1, 0x100000
    and     t2, a0, t1
    li      a7, 1220
    ecall
    beqz    a0, skip_settid
    beqz    t2, skip_settid
    sw      a0, 0(a2)
skip_settid:
    ebreak


