/*
 * Copyright 2023 (C) Alexander Vysokovskikh
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define HAVE_RISCV_MEMCPY
#define HAVE_RISCV_MEMSET
#define HAVE_RISCV_MEMMOVE

.section .text, "ax", @progbits
    # we want to reduce the size of the code, so in order
    # to comply with the compressed ISA specification,
    # we will use preferably s0-s1, a0-a5 registers.

.globl _rv64spinlock_lock
.type _rv64spinlock_lock, @function
_rv64spinlock_lock:
    li      t0, 1
    j       ___rv64spinlock_start
___rv64spinlock_lock_start:
    mv      t1, a1 
___rv64spinlock_lock_loop:
    nop
    addi    t1, t1, -1
    bnez    t1, ___rv64spinlock_lock_loop
___rv64spinlock_start:
    amoor.w t1, t0, (a0)
    bnez    t1, ___rv64spinlock_lock_start
    ret

.globl _rv64spinlock_unlock
.type _rv64spinlock_unlock, @function
    amoswap.w   t0, zero, (a0)
    ret

#ifdef HAVE_RISCV_MEMCPY
.globl _rv64memcpy
.type _rv64memcpy, @function
    #
    # void *_memcpy(void *dst, void *src, size_t sz)
    #
    # Copies sz bytes from memory area src to memory area dst.
    # The memory areas must not overlap. Uses load/stores of XLEN.
    # For mutual misaligned buffers does byte-by-byte coping.
_rv64memcpy:
    # save initial dst value
    mv t2, a0

    # threshold for byte-by-byte copying
    li a3, 2 * 8
    bltu a2, a3, .Lmemcpy_bops

    # the src and dst buffers must have the same
    # alignment for load/store operations
    andi a3, a0, 8-1
    andi a4, a1, 8-1
    bne a3, a4, .Lmemcpy_bops
    beqz a3, .Lmemcpy_main

    # handle head misalignments
    addi a3, a3, -8
    add a2, a2, a3
    sub a3, a0, a3
0:  lb a5, 0(a1)
    sb a5, 0(a0)
    addi a1, a1, 1
    addi a0, a0, 1
    blt a0, a3, 0b

    # copy 16/8/4/2/1*8 per one cycle iteration
.Lmemcpy_main:
    # according to convention
    # s0, s1 must be stored by callee
    mv t0, s0
    mv t1, s1

#ifndef __riscv_abi_rve
    li a7, 16*8
    mv a3, a7
    bltu a2, a7, 7f
#else
    j 6f
#endif
1:  ld a4,  8*8(a1)
    ld a5,  9*8(a1)
    ld s0, 10*8(a1)
    ld s1, 11*8(a1)
    sd a4,  8*8(a0)
    sd a5,  9*8(a0)
    sd s0, 10*8(a0)
    sd s1, 11*8(a0)
    ld a4, 12*8(a1)
    ld a5, 13*8(a1)
    ld s0, 14*8(a1)
    ld s1, 15*8(a1)
    sd a4, 12*8(a0)
    sd a5, 13*8(a0)
    sd s0, 14*8(a0)
    sd s1, 15*8(a0)
2:  ld a4,  4*8(a1)
    ld a5,  5*8(a1)
    ld s0,  6*8(a1)
    ld s1,  7*8(a1)
    sd a4,  4*8(a0)
    sd a5,  5*8(a0)
    sd s0,  6*8(a0)
    sd s1,  7*8(a0)
3:  ld a4,  2*8(a1)
    ld a5,  3*8(a1)
    sd a4,  2*8(a0)
    sd a5,  3*8(a0)
4:  ld s0,  1*8(a1)
    sd s0,  1*8(a0)
5:  ld s1,  0*8(a1)
    sd s1,  0*8(a0)
    add a0, a0, a3
    add a1, a1, a3
    sub a2, a2, a3
#ifndef __riscv_abi_rve
6:  bgeu a2, a7, 1b
7:  srli a3, a7, 1
#else
6:  li a3, 16*8
    bgeu a2, a3, 1b
7:  srli a3, a3, 1
#endif
    bgeu a2, a3, 2b
    srli a3, a3, 1
    bgeu a2, a3, 3b
    srli a3, a3, 1
    bgeu a2, a3, 4b
    srli a3, a3, 1
    bgeu a2, a3, 5b

    # restore s0, s1
    mv s1, t1
    mv s0, t0

    # handle tail misalignment
    # byte-by-byte copying
.Lmemcpy_bops:
    beqz a2, 1f
    add a2, a2, a0
0:  lb a4, 0(a1)
    sb a4, 0(a0)
    addi a1, a1, 1
    addi a0, a0, 1
    bltu a0, a2, 0b

    # return initial a0
1:  mv a0, t2
    ret
    .size _rv64memcpy, . - _rv64memcpy
#endif /* HAVE_RISCV_MEMCPY */

#ifdef HAVE_RISCV_MEMSET
.globl _rv64memset
.type _rv64memset, @function
    #
    # void *_rv64memset(void *dst, int ch, size_t sz)
    #
    # Function fills the first sz bytes of the memory
    # area pointed to by dst with the constant byte ch.
    # Uses stores operations of XLEN (register) size.
_rv64memset:
    # quit if sz is zero
    beqz a2, 9f

    # will return a0 untouched, further a5 = dst
    mv a5, a0

    # threshold for byte-by-byte operations
    li a3, 2 * 8
    bltu a2, a3, .Lmemset_bops

    # is dst aligned to register size
    andi a3, a5, 8-1
    beqz a3, .Lmemset_main

    # handle head misalignment
    addi a3, a3, -8
    add a2, a2, a3
    sub a3, a5, a3
0:  sb a1, 0(a5)
    addi a5, a5, 1
    blt a5, a3, 0b

.Lmemset_main:
    # zero set byte
    beqz a1, 1f

    # propagate set value to whole register
    zext.b a1, a1
    slli a3, a1, 8
    or a1, a1, a3
    slli a3, a1, 16
    or a1, a1, a3
#if __riscv_xlen == 64
    slli a3, a1, 32
    or a1, a1, a3
#endif

1:  li a4, 32*8
    mv a3, a4
    bltu a2, a4, 7f

    # stores 32/16/8/4/2/1*8 per one cycle iteration
0:  sd a1, 16*8(a5)
    sd a1, 17*8(a5)
    sd a1, 18*8(a5)
    sd a1, 19*8(a5)
    sd a1, 20*8(a5)
    sd a1, 21*8(a5)
    sd a1, 22*8(a5)
    sd a1, 23*8(a5)
    sd a1, 24*8(a5)
    sd a1, 25*8(a5)
    sd a1, 26*8(a5)
    sd a1, 27*8(a5)
    sd a1, 28*8(a5)
    sd a1, 29*8(a5)
    sd a1, 30*8(a5)
    sd a1, 31*8(a5)
1:  sd a1,  8*8(a5)
    sd a1,  9*8(a5)
    sd a1, 10*8(a5)
    sd a1, 11*8(a5)
    sd a1, 12*8(a5)
    sd a1, 13*8(a5)
    sd a1, 14*8(a5)
    sd a1, 15*8(a5)
2:  sd a1,  4*8(a5)
    sd a1,  5*8(a5)
    sd a1,  6*8(a5)
    sd a1,  7*8(a5)
3:  sd a1,  2*8(a5)
    sd a1,  3*8(a5)
4:  sd a1,  1*8(a5)
5:  sd a1,  0*8(a5)
    add a5, a5, a3
    sub a2, a2, a3
6:  bgeu a2, a4, 0b
    beqz a2, 9f
7:  srli a3, a4, 1
    bgeu a2, a3, 1b
    srli a3, a3, 1
    bgeu a2, a3, 2b
    srli a3, a3, 1
    bgeu a2, a3, 3b
    srli a3, a3, 1
    bgeu a2, a3, 4b
    srli a3, a3, 1
    bgeu a2, a3, 5b

    # handle tail misalignment
.Lmemset_bops:
    add a2, a2, a5
0:  sb a1, 0(a5)
    addi a5, a5, 1
    bltu a5, a2, 0b
9:  ret
    .size _rv64memset, . - _rv64memset
#endif /* HAVE_RISCV_MEMSET */

#ifdef HAVE_RISCV_MEMMOVE
.globl _rv64memmove
.type _rv64memmove, @function
    #
    # void *_rv64memmove(void *dst, void *src, size_t sz)
    #
    # Function copies sz bytes from memory area src to memory area dst.
    # The memory areas may overlap. Copies using 8/4/2/1 bytes load/stores
_rv64memmove:
    # save a0, s1
    mv t2, a0
    mv t1, s1

    # threshold for byte operations
    li a5, 2 * 8

    # find out mutual buffer alignment
#if __riscv_xlen == 64
    andi a3, a0, 7
    andi a4, a1, 7
    li s1, 8
    beq a3, a4, 1f
    andi a3, a3, 3
    andi a4, a4, 3
#else
    andi a3, a0, 3
    andi a4, a1, 3
#endif
    li s1, 4
    beq a3, a4, 1f
#ifdef HAVE_RISCV_MEMMOVE_HALF_WORDS
    andi a3, a3, 1
    andi a4, a4, 1
    li s1, 2
    beq a3, a4, 1f
#endif
    li s1, 0

    # copy from the end if dst > src
1:  bltu a1, a0, .Lmemmove_r

    # byte copy if sz is less than threshold
    bltu a2, a5, .Lmemmove_1b

    # byte copy if src and dst are mutual unaligned
    beqz s1, .Lmemmove_1b

    # at this point:
    # s1 = 8/4/2
    # a4 = head misaligned bytes

    beqz a4, 1f

    # handle head misalignment by byte copying
    sub a4, s1, a4
    sub a2, a2, a4
    add a4, a4, a0
0:  lb a3, 0(a1)
    sb a3, 0(a0)
    addi a1, a1, 1
    addi a0, a0, 1
    bltu a0, a4, 0b

    # calculate last address and tail misaligned bytes number
1:  addi a4, s1, -1
    xori a4, a4, -1
    and a4, a2, a4
    sub a2, a2, a4
    add a4, a4, a0

    # use 8/4/2 byte load/store instructions if buffers are
    # mutually aligned on 8/4/2 byte boundary respectively.
#if __riscv_xlen == 64
    li a5, 8
    bne s1, a5, 1f
0:  ld a3, 0(a1)
    sd a3, 0(a0)
    add a1, a1, s1
    add a0, a0, s1
    bltu a0, a4, 0b
    j .Lmemmove_1b
#endif
1:  li a5, 4
    bne s1, a5, 1f
0:  lw a3, 0(a1)
    sw a3, 0(a0)
    add a1, a1, s1
    add a0, a0, s1
    bltu a0, a4, 0b
#ifdef HAVE_RISCV_MEMMOVE_HALF_WORDS
    j .Lmemmove_1b
1:  li a5, 2
    bne s1, a5, 1f
0:  lh a3, 0(a1)
    sh a3, 0(a0)
    add a1, a1, s1
    add a0, a0, s1
    bltu a0, a4, 0b
#endif

    # byte copy
.Lmemmove_1b:
1:  beqz a2, .Lmemmove_end
    add a2, a2, a0
0:  lb a3, 0(a1)
    sb a3, 0(a0)
    addi a1, a1, 1
    addi a0, a0, 1
    bltu a0, a2, 0b

.Lmemmove_end:
    # restore saved registers
    mv s1, t1
    mv a0, t2
    ret

.Lmemmove_r:
    # start from the end: src += sz, dst += sz
    add a0, a0, a2
    add a1, a1, a2

    # here: a5 = threshold for byte-by-byte copying
    bltu a2, a5, .Lmemmove_r1b

    # byte copy if src and dst are mutual unaligned
    beqz s1, .Lmemmove_r1b

    # fix head misaligned bytes
    add a4, a4, a2
    addi a3, s1, -1
    and a4, a4, a3

    # s1 = 8/4/2
    # a4 = head misaligned bytes
    beqz a4, 1f

    # handle head misalignment
    sub a2, a2, a4
    sub a5, a0, a4
0:  addi a1, a1, -1
    addi a0, a0, -1
    lb a3, 0(a1)
    sb a3, 0(a0)
    bgtu a0, a5, 0b

    # calculate last address and tail misaligned bytes number
1:  addi a4, s1, -1
    xori a4, a4, -1
    and a4, a2, a4
    sub a2, a2, a4
    sub a4, a0, a4

    # use 8/4/2 byte load/store instructions if buffers are
    # mutually aligned on 8/4/2 byte boundary respectively.
#if __riscv_xlen == 64
    li a5, 8
    bne s1, a5, 1f
0:  sub a1, a1, s1
    sub a0, a0, s1
    ld a3, 0(a1)
    sd a3, 0(a0)
    bltu a4, a0, 0b
    j .Lmemmove_r1b
#endif
1:  li a5, 4
    bne s1, a5, 1f
0:  sub a1, a1, s1
    sub a0, a0, s1
    lw a3, 0(a1)
    sw a3, 0(a0)
    bltu a4, a0, 0b
#ifdef HAVE_RISCV_MEMMOVE_HALF_WORDS
    j .Lmemmove_r1b
1:  li a5, 2
    bne s1, a5, 1f
0:  sub a1, a1, s1
    sub a0, a0, s1
    lh a3, 0(a1)
    sh a3, 0(a0)
    bltu a4, a0, 0b
#endif

    # byte copy
.Lmemmove_r1b:
1:  beqz a2, .Lmemmove_end
    sub a5, a0, a2
0:  addi a1, a1, -1
    addi a0, a0, -1
    lb a3, 0(a1)
    sb a3, 0(a0)
    bgtu a0, a5, 0b
    j .Lmemmove_end
    .size _rv64memmove, . - _rv64memmove
#endif /* HAVE_RISCV_MEMMOVE */
