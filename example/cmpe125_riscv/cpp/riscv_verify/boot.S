/*
 * RV128I test
 * 
 * Copyright (c) 2016 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
/* warning; this file is executed with RV32I, RV64I and RV128I ISA but
   compiled as RV64I */
#define HTIF_TOHOST   0x40008000

.globl _start
.global putstring
.global putnum_dec
.global putnum_dec_fp

        
_start:
        /* set mstatus to a known state and allow FPU usage if available */
        li a0, 1 << 13
        csrw mstatus, a0 
        
        la a0, banner_str
        jal putstring

        jal main

        j exit

putstring:
        li a2, HTIF_TOHOST
        li a3, (1 << 24) | (1 << 16)
1:      
        lbu a1, (a0)
        beqz a1, 2f
/* Note: we use 32 bit accesses to work in all base isa modes */
        sw a1, 0(a2)
        sw a3, 4(a2)
/* wait until the char is handled */
3:      
        lw a1, 0(a2)
        lw a4, 4(a2)
        or a1, a1, a4
        bnez a1, 3b
        addi a0, a0, 1
        j 1b
2:
        ret


putnum_dec:
        la a1, putnum_buf_end
        li a2, 10
1:
        addi a1, a1, -1
        remu a3, a0, a2
        addi a3, a3, '0'
        sb a3, (a1)
        divu a0, a0, a2
        bnez a0, 1b
        mv a0, a1
        j putstring

# write the number in a0 in base 10 with a decimal point
putnum_dec_fp:
        la a1, putnum_buf_end
        li a2, 10
1:
        remu a3, a0, a2
        addi a3, a3, '0'
        addi a1, a1, -1
        divu a0, a0, a2
        beqz a0, 2f
        sb a3, (a1)
        j 1b
2:      
        li a0, '.'
        sb a0, (a1)
        addi a1, a1, -1
        sb a3, (a1)
        mv a0, a1
        j putstring

exit:
        li a2, HTIF_TOHOST
        li a1, 1
        sw a1, 0(a2)
        sw zero, 4(a2)
1:
        wfi
        j 1b

        .section ".rodata"
        
banner_str:
        .asciz "Booting the RISCV baremetal:\n"
lf_str:
        .asciz "\n"
        .section ".data"
        
putnum_buf:
        .rept 64
        .byte 0
        .endr
putnum_buf_end:
        
