
#pragma once

#include <stdint.h>

inline void expand_master_root_0(uint32_t *__restrict__ dest, const __uint128_t master, uint32_t start) {
  dest[0] = start + (master >> 0) & 0x1FFFUL;     // 13
  dest[1] = dest[0] + (master >> 13) & 0x1FFFUL;  // 13
  dest[2] = dest[1] + (master >> 26) & 0x1FFFUL;  // 13
  dest[3] = dest[2] + (master >> 39) & 0x1FFFUL;  // 13
  dest[4] = dest[3] + (master >> 52) & 0x1FFFUL;  // 13
  dest[5] = dest[4] + (master >> 65) & 0x1FFFUL;  // 13
  dest[6] = dest[5] + (master >> 78) & 0x1FFFUL;  // 13
  dest[7] = dest[6] + (master >> 91) & 0x1FFFUL;  // 13
}

inline void expand_master_root_1(uint32_t *__restrict__ dest, const __uint128_t master, uint32_t start) {
  dest[0] = start + (master >> 0) & 0x3FFFFUL;     // 18
  dest[1] = dest[0] + (master >> 18) & 0x3FFFFUL;  // 18
  dest[2] = dest[1] + (master >> 36) & 0x3FFFFUL;  // 18
  dest[3] = dest[2] + (master >> 54) & 0x3FFFFUL;  // 18
  dest[4] = dest[3] + (master >> 72) & 0x3FFFFUL;  // 18
  dest[5] = dest[4] + (master >> 90) & 0x03FFFUL;  // 14
}

inline void expand_master_root_2(uint32_t *__restrict__ dest, const __uint128_t master, uint32_t start) {
  dest[0] = start + (master >> 0) & 0xFFFFFFFUL;     // 26
  dest[1] = dest[0] + (master >> 26) & 0xFFFFFFFUL;  // 26
  dest[2] = dest[1] + (master >> 52) & 0xFFFFFFFUL;  // 26
  dest[3] = dest[2] + (master >> 88) & 0xFFFFFFFUL;  // 26
}

inline void expand_master_root_3(uint32_t *__restrict__ dest, const __uint128_t master, uint32_t start) {
  dest[0] = start + (master >> 0) & ((1ULL << 32) - 1);     // 32
  dest[1] = dest[0] + (master >> 33) & ((1ULL << 32) - 1);  // 32
  dest[2] = dest[1] + (master >> 66) & ((1ULL << 20) - 1);  // 32
}

inline void expand_master_0(uint32_t *__restrict__ dest, const __uint128_t master, uint32_t start) {
  dest[0] = start + (master >> 0) & 0x7FFUL;     // 11
  dest[1] = dest[0] + (master >> 11) & 0x7FFUL;  // 11
  dest[2] = dest[1] + (master >> 22) & 0x7FFUL;  // 11
  dest[3] = dest[2] + (master >> 33) & 0x7FFUL;  // 11
  dest[4] = dest[3] + (master >> 44) & 0x7FFUL;  // 11
  dest[5] = dest[4] + (master >> 55) & 0x7FFUL;  // 11
  dest[6] = dest[5] + (master >> 66) & 0x7FFUL;  // 11
  dest[7] = dest[6] + (master >> 77) & 0x7FFUL;  // 11
}

inline void expand_master_1(uint32_t *__restrict__ dest, const __uint128_t master, uint32_t start) {
  dest[0] = start + (master >> 0) & 0x7FFFUL;      // 15
  dest[1] = dest[0] + (master >> 15) & 0x7FFFUL;   // 15
  dest[2] = dest[1] + (master >> 30) & 0x7FFFUL;   // 15
  dest[3] = dest[2] + (master >> 45) & 0x7FFFUL;   // 15
  dest[4] = dest[3] + (master >> 60) & 0x7FFFUL;   // 15
  dest[5] = dest[4] + (master >> 75) & 0x01FFFUL;  // 13
}

inline void expand_master_2(uint32_t *__restrict__ dest, const __uint128_t master, uint32_t start) {
  dest[0] = start + (master >> 0) & 0xFFFFFFFUL;     // 22
  dest[1] = dest[0] + (master >> 22) & 0xFFFFFFFUL;  // 22
  dest[2] = dest[1] + (master >> 44) & 0xFFFFFFFUL;  // 22
  dest[3] = dest[2] + (master >> 66) & 0xFFFFFFFUL;  // 22
}

inline void expand_master_3(uint32_t *__restrict__ dest, const __uint128_t master, uint32_t start) {
  dest[0] = start + (master >> 0) & ((1ULL << 32) - 1);     // 32
  dest[1] = dest[0] + (master >> 32) & ((1ULL << 28) - 1);  // 28
  dest[2] = dest[1] + (master >> 60) & ((1ULL << 28) - 1);  // 28
}
