// See LICENSE for license details.

#ifndef _RISCV_COMMON_H
#define _RISCV_COMMON_H

#define   riscvdis_likely(x) __builtin_expect(x, 1)
#define riscvdis_unlikely(x) __builtin_expect(x, 0)

#endif
