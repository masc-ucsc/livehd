// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Some code based on https://github.com/ucsc-vama/firrtl-sig/blob/master/uint.h

#pragma once

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

// lop: Logic Operation Library
//
// blop: Base Logic Operation
// dlop: Dynamic Logic Operation
// slop: Static Logic Operation
//
// slop needs of an external pass to "BOUND" the maximum number of bits needed to perform the operation. Hence, it does not need
// memory allocation and it is much faster. As a result, the goal of slop is for simulation.
//
// dlop adjusts at run-time the number of bits needed. Ideal for emulation inside the compiler to avoid prepasses with max/min
// sizing.
//
// Both dlop/slop use the blop (base) class for all the operations. They are fairly close, the main difference is the memory
// allocation.
//
// Both dlop/slop can handle unknowns (?). The semantics are NOT compatible with Verilog, but they have more accuracy.
//
// The "unknowns" (extra field) are a runtime (dlop) or compile (slop) option. If disabled, the unknowns ('?') are randomly
// translated to zero or one when importing to lop.
//
// When unknowns are supported, each number is encoded with a "base" and an "extra" blop. The extra is true for each bit with
// unknowns. The corresponding bit in the "base" is set to 1. Then, assert(base == (base|extra)) should hold always.
//
// The lop is anways SIGNED. All the operations are always signed and behave like having unlimited accuracy. This means that for
// numbers that can be encoding in 13 bits, we can use 32 bit signed, and we do not need to drop bits. The logical optimization is
// to have 8 bit, 64bit, and multiples of 64bits for all the numbers. (32bit is reasonable)
//
// It may make sense to move the lop to a separate repo so that other projects could use the library.
//
// jop is the javascript version of this project (should be semantically equivalent in operations and results)
//
// For development and benchmarking, most of the operations (pyrope strings) should be equivalent to Lconst, but the idea is to
// replace lconst for the new lop library.
//
// The slop class should compile with tcc  (https://repo.or.cz/w/tinycc.git) (not C++, but C99) so that a faster compiler for code
// generation can be used.
//

class Blop {
protected:
public:
  //---------------------------------------------------------------------------
  // extend
  //---------------------------------------------------------------------------
  static void extend(int64_t *dest, size_t dest_sz, const int64_t src) {
    dest[0]   = src;
    int64_t v = src < 0 ? -1 : 0;
    for (auto i = 1u; i < dest_sz; ++i) {
      dest[i] = v;
    }
  }

  //--------------------------------------------------------------------------
  // ADD
  //---------------------------------------------------------------------------
  static void add8(int8_t &dest, const int8_t src1, const int8_t src2) { dest = src1 + src2; }
  static void add64(int64_t &dest, const int64_t src1, const int64_t src2) { dest = src1 + src2; }
  static void addn(int64_t *dest, size_t dest_sz, const int64_t *src1, const int64_t *src2) {
    assert(dest_sz >= 1);

    uint64_t carry = __builtin_uaddll_overflow(src1[0], src2[0], reinterpret_cast<unsigned long long *>(dest));
    for (auto i = 1u; i < dest_sz - 1; ++i) {
      unsigned long long tmp;
      carry = __builtin_uaddll_overflow(src1[i], carry, &tmp);
      carry |= __builtin_uaddll_overflow(tmp, src2[i], reinterpret_cast<unsigned long long *>(dest + i));
    }
    assert(carry == 0 || carry == 1);
    dest[dest_sz - 1] = src1[dest_sz - 1] + src2[dest_sz - 1] + carry;
  }

  //---------------------------------------------------------------------------
  // SUB
  //---------------------------------------------------------------------------
  static void sub8(int8_t &dest, const int8_t src1, const int8_t src2) { dest = src1 - src2; }
  static void sub64(int64_t &dest, const int64_t src1, const int64_t src2) { dest = src1 - src2; }

  static void subn(int64_t *dest, size_t dest_sz, const int64_t *src1, const int64_t *src2) {
    assert(dest_sz >= 1);

    uint64_t carry = __builtin_usubll_overflow(src1[0], src2[0], reinterpret_cast<unsigned long long *>(dest));
    for (auto i = 1u; i < dest_sz - 1; ++i) {
      unsigned long long tmp;
      carry = __builtin_usubll_overflow(src1[i], carry, &tmp);
      carry |= __builtin_usubll_overflow(tmp, src2[i], reinterpret_cast<unsigned long long *>(dest + i));
    }
    assert(carry == 0 || carry == 1);
    dest[dest_sz - 1] = src1[dest_sz - 1] - src2[dest_sz - 1] - carry;
  }

  //---------------------------------------------------------------------------
  // SHL
  //---------------------------------------------------------------------------
  static void shl8(int8_t &dest, const int8_t src1, const int8_t src2) {
    assert(src2 >= 0);
    dest = src1 << src2;
    assert((dest >> src2) == src1);  // no precision lost of allocate larger dest
  }
  static void shl64(int64_t &dest, const int64_t src1, const int64_t src2) {
    assert(src2 >= 0);
    dest = src1 << src2;
    assert((dest >> src2) == src1);  // no precision lost of allocate larger dest
  }

  static void shln(int64_t *dest, size_t dest_sz, const int64_t *src1, const int64_t src2) {
    assert(dest_sz >= 1);
    assert(src2 >= 0);

    uint64_t word_up = src2 / 64;
    assert(word_up < dest_sz);
    uint64_t bits_up = src2 & 63;

    if (bits_up == 0) {
      for (int i = dest_sz - word_up - 1; i >= 0; --i) {
        dest[i + word_up] = src1[i];
      }
    } else {
      for (int i = dest_sz - word_up - 1; i > 0; --i) {
        auto tmp          = src1[i];  // need to do copy because self update
        dest[i + word_up] = static_cast<uint64_t>(src1[i - 1]) >> static_cast<uint64_t>(64 - bits_up);
        dest[i + word_up] |= tmp << bits_up;
      }
      dest[word_up] = src1[0] << bits_up;
    }
    for (auto i = 0u; i < word_up; ++i) {
      dest[i] = 0;
    }
  }

  //---------------------------------------------------------------------------
  // SHR
  //---------------------------------------------------------------------------
  static void shr8(int8_t &dest, const int8_t src1, const int8_t src2) {
    assert(src2 >= 0);
    dest = src1 >> src2;
  }
  static void shr64(int64_t &dest, const int64_t src1, const int64_t src2) {
    assert(src2 >= 0);
    dest = src1 >> src2;
  }

  static void shrn(int64_t *dest, size_t dest_sz, const int64_t *src1, const int64_t src2) {
    assert(dest_sz >= 1);
    assert(src2 >= 0);

    uint64_t word_down = src2 / 64;
    uint64_t bits_down = src2 & 63;

    if (bits_down == 0) {
      for (auto i = word_down; i < dest_sz; i++) {
        dest[i - word_down] = src1[i];
      }
    } else {
      for (auto i = word_down; i < dest_sz - 1; i++) {
        auto tmp = static_cast<uint64_t>(src1[i]) >> bits_down;
        tmp |= src1[i + 1] << static_cast<uint64_t>(64 - bits_down);
        dest[i - word_down] = tmp;
      }
      dest[dest_sz - 1 - word_down] = src1[dest_sz - 1] >> bits_down;
    }
  }

  //---------------------------------------------------------------------------
  // OR
  //---------------------------------------------------------------------------
  static void or8(int8_t &dest, const int8_t src1, const int8_t src2) { dest = src1 | src2; }
  static void or64(int64_t &dest, const int64_t src1, const int64_t src2) { dest = src1 | src2; }

  static void orn(int64_t *dest, size_t dest_sz, const int64_t *src1, const int64_t *src2) {
    assert(dest_sz >= 1);
    for (auto i = 0u; i < dest_sz; i++) {
      dest[i] = src1[i] | src2[i];
    }
  }

  static void orn(int64_t *dest, size_t dest_sz, const int64_t *src1, const int64_t src2) {
    assert(dest_sz >= 1);
    dest[0]    = src1[0] | src2;
    uint64_t v = src2 < 0 ? -1 : 0;
    for (auto i = 1u; i < dest_sz; i++) {
      dest[i] = src1[i] | v;
    }
  }

  //---------------------------------------------------------------------------
  // MULT
  //---------------------------------------------------------------------------
  static void mult8(int8_t &dest, const int8_t src1, const int8_t src2) { dest = src1 * src2; }
  static void mult64(int64_t &dest, const int64_t src1, const int64_t src2) { dest = src1 * src2; }

  static void multn(int64_t *dest, size_t dest_sz, const int64_t *src1, size_t src1_sz, const int64_t *src2, size_t src2_sz) {
    for (size_t i = 0; i < dest_sz; i++) {
      dest[i] = 0;
    }

    int64_t flip_carry[dest_sz];

    int64_t *temp_src1 = const_cast<int64_t *>(src1);

    bool s1Negative = false;
    bool s2Negative = false;

    for (size_t i = 0; i < dest_sz; i++) {
      if (i == 0) {
        flip_carry[i] = 1;
      } else {
        flip_carry[i] = 0;
      }
    }
    assert(dest_sz >= 1 && sizeof(src1) > 1 && sizeof(src2) > 1);

    if (src1[src1_sz - 1] < 0) {
      s1Negative = true;
      subn(temp_src1, src1_sz, temp_src1, flip_carry);
      for (size_t i = 0; i < src1_sz; i++) {
        temp_src1[i] = ~temp_src1[i];
      }
    }

    {
      int64_t *temp_src2 = const_cast<int64_t *>(src2);
      if (src2[src2_sz - 1] < 0) {
        s2Negative = true;
        subn(temp_src2, src2_sz, temp_src2, flip_carry);
        for (size_t i = 0; i < src2_sz; i++) {
          temp_src2[i] = ~temp_src2[i];
        }
      }
    }

    int64_t exponent = 63;
    for (size_t j = 0; j < src2_sz; j++) {
      uint64_t temp_src2 = (uint64_t)src2[j];
      while (temp_src2 > 0) {
        uint64_t exp = std::pow(2, exponent);
        if (temp_src2 >= exp) {
          int64_t temp[dest_sz];
          for (size_t k = 0; k < dest_sz; k++) {
            temp[k] = 0;
          }
          temp_src2 -= exp;
          // shift left base on exponent
          shln(temp, dest_sz, temp_src1, exponent);
          //-------------add shifted bits to lastindex of temp-----------
          uint64_t src1_msb = temp_src1[src1_sz - 1];
          for (auto i = 0; i < 64; i++) {
            if (src1_msb != 0) {
              uint temp_bit = src1_msb % 2;
              src1_msb      = src1_msb / 2;
              if (i >= (64 - exponent)) {
                // -----------------------------------------------not finished-----------------------------------------------

                temp[src1_sz + j + 1] += temp_bit * (std::pow(2, i));
                // -----------------------------------------------not finished-----------------------------------------------
              }
            } else {
              break;
            }
          }
          addn(dest, dest_sz, temp, dest);
        }
        exponent--;
      }
    }

    // check sign
    if (src2 != 0) {
      if (s1Negative + s2Negative == 1) {
        // positive to two's complement negative
        for (size_t i = 0; i < dest_sz; i++) {
          dest[i] = ~dest[i];
        }
        addn(dest, dest_sz, dest, flip_carry);
      }
    }
  }

  static void multn(int64_t *dest, size_t dest_sz, const int64_t *src1, const int64_t src2) {
    for (size_t i = 0; i < dest_sz; i++) {
      dest[i] = 0;
    }

    int64_t *temp_src1 = const_cast<int64_t *>(src1);
    int64_t  temp_src2 = src2;
    int64_t  flip_carry[dest_sz];
    for (size_t i = 0; i < dest_sz; i++) {
      if (i == 0) {
        flip_carry[i] = 1;
      } else {
        flip_carry[i] = 0;
      }
    }
    assert(dest_sz >= 1 && sizeof(src1) > 1);
    bool s1Negative, s2Negative = false;
    if (src2 < 0) {
      s2Negative = true;
      temp_src2  = ~(src2 - 1);
    }
    if (src1[dest_sz - 2] < 0) {
      s1Negative = true;
      subn(temp_src1, dest_sz - 1, temp_src1, flip_carry);
      for (size_t i = 0; i < dest_sz - 1; i++) {
        temp_src1[i] = ~temp_src1[i];
      }
    }

    // split src2
    int64_t exponent = 63;
    while (temp_src2 > 0) {
      auto exp = std::pow(2, exponent);
      if (temp_src2 >= exp) {
        int64_t temp[dest_sz];
        temp_src2 -= exp;
        // shift left base on exponent
        shln(temp, dest_sz, temp_src1, exponent);

        //-------------add shifted bits to lastindex of temp-----------
        temp[dest_sz - 1] = 0;
        uint64_t src1_msb = temp_src1[dest_sz - 2];
        for (auto i = 0; i < 64; i++) {
          if (src1_msb != 0) {
            uint temp_bit = src1_msb % 2;
            src1_msb      = src1_msb / 2;
            if (i >= (64 - exponent)) {
              temp[dest_sz - 1] += temp_bit * (std::pow(2, i));
            }
          } else {
            break;
          }
        }
        addn(dest, dest_sz, temp, dest);
      }
      exponent--;
    }

    // check sign
    if (src2 != 0) {
      if (s1Negative + s2Negative == 1) {
        // positive to two's complement negative
        for (size_t i = 0; i < dest_sz; i++) {
          dest[i] = ~dest[i];
        }
        addn(dest, dest_sz, dest, flip_carry);
      }
    }
  }
};
