//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <vector>

#include "absl/types/span.h"
#include "boost/multiprecision/cpp_int.hpp"
#include "iassert.hpp"

using Bits_t               = uint32_t;  // bits type (future use)
constexpr int    Bits_bits = 17;
constexpr Bits_t Bits_max  = ((1ULL << Bits_bits) - 1);

class Lconst {
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
  constexpr static int char_to_shift_mode[256] = {
      /* 00 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      /* 10 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      /* 20 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      /* 30 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      /* 40 */ 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3,
      /* 50 */ 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0,
      /* 60 */ 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3,
      /* 70 */ 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0,
      /* 80 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      /* 90 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      /* A0 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      /* B0 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      /* C0 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      /* D0 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      /* E0 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      /* F0 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

  static Bits_t read_bits(std::string_view txt);

protected:
  using Number = boost::multiprecision::cpp_int;

  bool explicit_str;

  Bits_t bits;
  Number num;

  std::string_view skip_underscores(std::string_view txt) const;

  Lconst(bool str, Bits_t d, Number n) : explicit_str(str), bits(d), num(n) {}

  static Bits_t calc_num_bits(const Number &num) {
    if (num == 0 || num == -1)
      return 1;
    if (num > 0)
      return msb(num) + 2;  // +2 because values are signed (msb==0 is 1 bit)
    return msb(-num - 1) + 2;
  }
  Bits_t             calc_num_bits() const { return calc_num_bits(num); }
  static std::string to_string(Number num);

  const Number &get_num() const { return num; }
  Lconst        adjust(const Number &res_num, const Lconst &o) const;

public:
  using Container = std::vector<unsigned char>;

  Lconst(absl::Span<unsigned char> v);
  Lconst(const Container &v);
  Lconst(std::string_view txt);
  Lconst(Number v);
  Lconst(int64_t v);
  // Lconst(int64_t v, Bits_t bits);
  Lconst();

  static Lconst string(std::string_view txt);

  Container serialize() const;
  uint64_t  hash() const;

  void dump() const;

  [[nodiscard]] static Lconst get_mask_value(Bits_t bits);
  [[nodiscard]] Lconst        get_mask_value() const;

  [[nodiscard]] Lconst get_mask_op() const;
  [[nodiscard]] Lconst get_mask_op(const Lconst &pos) const;
  [[nodiscard]] Lconst set_mask_op(const Lconst &pos, const Lconst &val) const;
  [[nodiscard]] Lconst add_op(const Lconst &o) const;
  [[nodiscard]] Lconst mult_op(const Lconst &o) const;
  [[nodiscard]] Lconst div_op(const Lconst &o) const;
  [[nodiscard]] Lconst sub_op(const Lconst &o) const;
  [[nodiscard]] Lconst lsh_op(Bits_t amount) const;
  [[nodiscard]] Lconst rsh_op(Bits_t amount) const;
  [[nodiscard]] Lconst or_op(const Lconst &o) const;
  [[nodiscard]] Lconst and_op(const Lconst &o) const;
  [[nodiscard]] Lconst not_op() const;
  [[nodiscard]] Lconst concat_op(const Lconst &o) const;

  [[nodiscard]] int eq_op(const Lconst &o) const;

  [[nodiscard]] Lconst adjust_bits(Bits_t amount) const;

  bool has_unknowns() const { return explicit_str && bits < calc_num_bits(num); }
  bool is_negative() const { return num < 0; }
  bool is_string() const { return explicit_str && (bits & 0x7) == 0 && bits >= calc_num_bits(num); }
  bool is_mask() const { return ((num + 1) & (num)) == 0; }
  bool is_power2() const { return ((num - 1) & (num)) == 0; }

  Bits_t get_bits() const { return bits; }  // note: this is returning signed bits of the constant

  bool    is_i() const { return !explicit_str && bits <= 62; }  // 62 to handle sign (int)
  int64_t to_i() const;                                         // must fit in int or exception raised

  std::string to_yosys(bool do_unsign = false) const;
  std::string to_verilog() const;
  std::string to_string() const;
  std::string to_string_no_xz() const;
  std::string to_pyrope() const;
  std::string to_firrtl() const;

  // Operator list
  [[nodiscard]] const Lconst operator+(const Lconst &other) const { return add_op(other); }
  [[nodiscard]] const Lconst operator+(int64_t other) const { return add_op(Lconst(other)); }

  [[nodiscard]] const Lconst operator-(const Lconst &other) const { return sub_op(other); }
  [[nodiscard]] const Lconst operator-(int64_t other) const { return sub_op(Lconst(other)); }

  [[nodiscard]] const Lconst operator<<(const Lconst &other) const { return lsh_op(other.to_i()); }
  [[nodiscard]] const Lconst operator<<(Bits_t other) const { return lsh_op(other); }

  [[nodiscard]] const Lconst operator|(const Lconst &other) const { return or_op(other); }
  [[nodiscard]] const Lconst operator|(int64_t other) const { return or_op(Lconst(other)); }

#if 0
  bool equals_op(const Lconst &other) const {
    // similar to ==, but ignore explicit bits
    auto b = std::max(bits,other.bits);
    return get_num(b) == other.get_num(b);
  }
#endif

  bool operator==(const Lconst &other) const { return get_num() == other.get_num() && bits == other.bits; }
  bool operator!=(const Lconst &other) const { return get_num() != other.get_num() || bits != other.bits; }

  bool operator==(int other) const { return get_num() == other && !is_string(); }
  bool operator!=(int other) const { return get_num() != other || is_string(); }

  bool operator<(const Lconst &other) const { return num < other.num; }
  bool operator<=(const Lconst &other) const { return num <= other.num; }
  bool operator>(const Lconst &other) const { return num > other.num; }
  bool operator>=(const Lconst &other) const { return num >= other.num; }

  Number get_raw_num() const { return num; }  // for debugging mostly
};
