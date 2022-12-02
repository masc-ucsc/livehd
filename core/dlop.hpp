//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "blop.hpp"
#include "raw_ptr_pool.hpp"
#include "spool_ptr.hpp"

class Dlop {
protected:
private:
  constexpr static int char_to_bits[256]
      = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 4, 4, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  constexpr static int char_to_val[256]
      = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
         -1, -1, -1, -1, -1, -1, -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

  enum class Type : int16_t {
    Invalid  = -1,
    Integer  = 0,  // Most common
    Boolean  = 1,
    String   = 2,
    Bitwidth = 3
  };
  // Pools with multiple of 64 bytes (cache line)
  static inline thread_local std::vector<raw_ptr_pool *> free_pool;

public:
  static void     free(size_t sz, int64_t *ptr);
  static int64_t *alloc(size_t sz);

  Type    type;
  int16_t size;  // bucket size, bits < size*64

  int64_t *base;   // min or 0/1
  int64_t *extra;  // max or x

  int64_t data[2];  // to avoid pool for most common case (sz==1)

  void shl_base(int64_t src2) { Blop::shln(base, size, base, src2); }
  void shl_extra(int64_t src2) { Blop::shln(extra, size, extra, src2); }

  void mult_base(int64_t src2) { Blop::multn(base, size, base, src2); }

  void extend_base(int64_t src2) { Blop::extend(base, size, src2); }
  void extend_extra(int64_t src2) { Blop::extend(extra, size, src2); }

  void add_base(int64_t src2) {
    int64_t tmp[size];
    Blop::extend(tmp, size, src2);
    Blop::addn(base, size, base, tmp);
  }

  void or_base(int64_t src2) {
    if (size == 1) {
      Blop::or64(*base, *base, src2);
    } else {
      Blop::orn(base, size, base, src2);
    }
  }
  void or_extra(int64_t src2) {
    if (size == 1) {
      Blop::or64(*extra, *extra, src2);
    } else {
      Blop::orn(extra, size, extra, src2);
    }
  }

  bool is_negative() const { return base[size - 1] < 0 || extra[size - 1] < 0; }

  void negate_mut() {
    assert(type == Type::Integer);
    if (size > 1) {
      int64_t tmp[size];
      bzero(tmp, size * sizeof(int64_t));
      Blop::subn(base, size, tmp, base);
    } else {
      Blop::sub64(*base, 0, *base);
    }
  }

  void clear() {
    for (auto i = 0; i < size; ++i) {
      base[i]  = 0;
      extra[i] = 0;
    }
  }

  void reconstruct(Type tp, size_t sz) {
    type = tp;
    size = sz;
    if (sz > 1) {
      base  = alloc(sz);
      extra = alloc(sz);
      clear();
    } else {
      base    = &data[0];
      extra   = &data[1];
      data[0] = 0;
      data[1] = 0;
    }
  }

  friend spool_ptr<Dlop>;
  friend spool_ptr_pool<Dlop>;
  uint32_t shared_count;

public:
  Dlop() : type(Type::Invalid), size(0), base(nullptr), extra(nullptr) {}

  ~Dlop() {
    if (base && size > 1) {
      assert(extra);
      free(size, base);
      free(size, extra);
    }
  }

  bool is_integer() const { return type == Type::Integer; }
  bool is_bool() const { return type == Type::Boolean; }
  bool is_string() const { return type == Type::String; }

  static spool_ptr<Dlop> create_bool(bool val);
  static spool_ptr<Dlop> create_integer(int64_t val);
  static spool_ptr<Dlop> create_string(std::string_view orig_txt);
  static spool_ptr<Dlop> from_binary(std::string_view txt, bool unsigned_result);
  static spool_ptr<Dlop> from_pyrope(std::string_view orig_txt);

  spool_ptr<Dlop> add_op(spool_ptr<Dlop> other);
  spool_ptr<Dlop> add_op(int64_t other);
  void            mut_add(spool_ptr<Dlop> other);
  void            mut_add(int64_t other);

  void dump() const;
};
