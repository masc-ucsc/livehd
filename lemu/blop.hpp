// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Some code based on https://github.com/ucsc-vama/firrtl-sig/blob/master/uint.h

#pragma once

#include <cstdint>
#include <cassert>

// Base-inline lop use by dlop and slop

class Blop {
protected:
public:
  //---------------------------------------------------------------------------
  // ADD
  //---------------------------------------------------------------------------
  static void add8(int8_t &dest, const int8_t src1, const int8_t src2) {
    dest = src1 + src2;
  }
  static void add64(int64_t &dest, const int64_t src1, const int64_t src2) {
    dest = src1 + src2;
  }
  static void addn(int64_t *dest, int dest_sz, const int64_t *src1, const int64_t *src2) {
    assert(dest_sz>1);

    uint64_t carry = __builtin_uaddll_overflow(src1[0], src2[0], reinterpret_cast<unsigned long long*>(dest));
    for(auto i=1;i<dest_sz-1;++i) {
      unsigned long long tmp;
      carry =  __builtin_uaddll_overflow(src1[i], carry, &tmp);
      carry |= __builtin_uaddll_overflow(tmp, src2[i], reinterpret_cast<unsigned long long*>(dest + i));
    }
    assert(carry == 0 || carry == 1);
    dest[dest_sz-1] = src1[dest_sz-1] + src2[dest_sz-1] + carry;
  }

  //---------------------------------------------------------------------------
  // SUB
  //---------------------------------------------------------------------------
  static void sub8(int8_t &dest, const int8_t src1, const int8_t src2) {
    dest = src1 - src2;
  }
  static void sub64(int64_t &dest, const int64_t src1, const int64_t src2) {
    dest = src1 - src2;
  }

  static void subn(int64_t *dest, int dest_sz, const int64_t *src1, const int64_t *src2) {
    assert(dest_sz>1);

    uint64_t carry = __builtin_usubll_overflow(src1[0], src2[0], reinterpret_cast<unsigned long long*>(dest));
    for(auto i=1;i<dest_sz-1;++i) {
      unsigned long long tmp;
      carry =  __builtin_usubll_overflow(src1[i], carry, &tmp);
      carry |= __builtin_usubll_overflow(tmp, src2[i], reinterpret_cast<unsigned long long*>(dest + i));
    }
    assert(carry == 0 || carry == 1);
    dest[dest_sz-1] = src1[dest_sz-1] - src2[dest_sz-1] - carry;
  }

  //---------------------------------------------------------------------------
  // SHL
  //---------------------------------------------------------------------------
  static void shl8(int8_t &dest, const int8_t src1, const int8_t src2) {
    assert(src2>=0);
    dest = src1 << src2;
    assert((dest>>src2) == src1); // no precision lost of allocate larger dest
  }
  static void shl64(int64_t &dest, const int64_t src1, const int64_t src2) {
    assert(src2>=0);
    dest = src1 << src2;
    assert((dest>>src2) == src1); // no precision lost of allocate larger dest
  }

  static void shln(int64_t *dest, int dest_sz, const int64_t *src1, const int64_t src2) {
    assert(dest_sz>1);
    assert(src2>=0);

    uint64_t word_up = src2 / 64;
    assert(word_up<dest_sz);
    uint64_t bits_up = src2 & 63;

    if (bits_up==0) {
      for (uint64_t i=0; i < dest_sz; i++) {
        dest[i + word_up] = src1[i];
      }
    }else{
      for (uint64_t i=0; i <= word_up ; i++) {
        dest[i] = 0;
      }
      for (uint64_t i=0; i < dest_sz-1; i++) {
        dest[i + word_up    ] |= src1[i] << bits_up;
        dest[i + word_up + 1]  = static_cast<uint64_t>(src1[i]) >> static_cast<uint64_t>(64 - bits_up);
      }
      dest[dest_sz - 1 + word_up] |= src1[dest_sz-1] << bits_up;
    }
  }

  //---------------------------------------------------------------------------
  // SHR
  //---------------------------------------------------------------------------
  static void shr8(int8_t &dest, const int8_t src1, const int8_t src2) {
    assert(src2>=0);
    dest = src1 >> src2;
  }
  static void shr64(int64_t &dest, const int64_t src1, const int64_t src2) {
    assert(src2>=0);
    dest = src1 >> src2;
  }

  static void shrn(int64_t *dest, int dest_sz, const int64_t *src1, const int64_t src2) {
    assert(dest_sz>1);
    assert(src2>=0);

    uint64_t word_down = src2 / 64;
    uint64_t bits_down = src2 & 63;

    if (bits_down==0) {
      for (uint64_t i = word_down; i < dest_sz; i++) {
        dest[i - word_down] = src1[i];
      }
    }else{
      for (uint64_t i = word_down; i < dest_sz-1; i++) {
        auto tmp = static_cast<uint64_t>(src1[i]) >> bits_down;
        tmp |= src1[i + 1] << static_cast<uint64_t>(64 - bits_down);
        dest[i - word_down] = tmp;
      }
      dest[dest_sz - 1 - word_down] = src1[dest_sz-1] >> bits_down;
    }
  }
};
