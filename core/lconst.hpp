//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <vector>

#include "absl/types/span.h"
#include "boost/multiprecision/cpp_int.hpp"
#include "graph_sizing.hpp"
#include "iassert.hpp"

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

protected:
  using Number = boost::multiprecision::cpp_int;

  bool explicit_str;

  Bits_t bits;
  Number num;

  std::string_view skip_underscores(std::string_view txt) const;

  Lconst(bool str, Bits_t d, Number n) : explicit_str(str), bits(d), num(n) { assert(d < Bits_max); }

  static Bits_t calc_num_bits(const Number &num) {
    if (num == 0) {
      return 0;
    }
    if (num == -1) {
      return 1;
    }
    if (num > 0) {
      return msb(num) + 2;  // +2 because values are signed (msb==0 is 1 bit)
    }
    return msb(-num - 1) + 2;
  }
  Bits_t calc_num_bits() const { return calc_num_bits(num); }

  const Number &get_num() const { return num; }
  void          adjust(const Lconst &o);

  static std::pair<std::string, std::string> match_binary(const Lconst &l, const Lconst &r);

  [[nodiscard]] static std::string to_string(Number num);
  [[nodiscard]] std::string        to_string() const {  // use to_pyrope, to_verilog not the to_str directly
    I(explicit_str);
    return to_string(num);
  }

public:
  using Container = std::string;

  explicit Lconst(absl::Span<unsigned char> v);
  explicit Lconst(const Number &v);
  Lconst(int64_t v);  // not explicit to allow easy Lconst(x) < 0 operations

  Lconst();

  [[nodiscard]] std::string to_field() const;  // tuple field (a subset of pyrope allowed)
  [[nodiscard]] std::string to_binary() const;
  [[nodiscard]] std::string to_verilog() const;
  [[nodiscard]] std::string to_pyrope() const;

  // TODO for from_verilog ...
  static Lconst from_pyrope(std::string_view txt);
  static Lconst from_binary(std::string_view txt, bool unsigned_result);
  static Lconst from_string(std::string_view txt);
  static Lconst from_ref(std::string_view txt);
  static Lconst invalid() { return Lconst(true, 0, 0); }

  static Lconst unknown(Bits_t nbits);
  static Lconst unknown_positive(Bits_t nbits);
  static Lconst unknown_negative(Bits_t nbits);

  static Lconst             unserialize(std::string_view v);
  [[nodiscard]] std::string serialize() const;

  bool is_invalid() const { return explicit_str && bits == 0; }
  bool is_ref() const { return is_invalid() && num != 0; }

  uint64_t hash() const;

  void                 dump() const;
  [[nodiscard]] size_t get_trailing_zeroes() const;

  [[nodiscard]] static Lconst get_mask_value(Bits_t bits);
  [[nodiscard]] static Lconst get_mask_value(Bits_t h, Bits_t l);
  [[nodiscard]] static Lconst get_neg_mask_value(Bits_t bits);
  [[nodiscard]] Lconst        get_mask_value() const;

  Lconst to_known_rand() const;

  [[nodiscard]] Lconst sext_op(Bits_t bits) const;
  [[nodiscard]] Lconst get_mask_op() const;
  [[nodiscard]] Lconst get_mask_op(const Lconst &pos) const;
  [[nodiscard]] Lconst set_mask_op(const Lconst &pos, const Lconst &val) const;
  [[nodiscard]] Lconst add_op(const Lconst &o) const;
  [[nodiscard]] Lconst mult_op(const Lconst &o) const;
  [[nodiscard]] Lconst div_op(const Lconst &o) const;
  [[nodiscard]] Lconst sub_op(const Lconst &o) const;
  [[nodiscard]] Lconst lsh_op(Bits_t amount) const;
  [[nodiscard]] Lconst rsh_op(Bits_t amount) const;
  [[nodiscard]] Lconst ror_op(const Lconst &o) const;
  [[nodiscard]] Lconst or_op(const Lconst &o) const;
  [[nodiscard]] Lconst and_op(const Lconst &o) const;
  [[nodiscard]] Lconst not_op() const;  // bitwise not
  [[nodiscard]] Lconst neg_op() const;  // change sign
  [[nodiscard]] Lconst concat_op(const Lconst &o) const;

  [[nodiscard]] Lconst eq_op(const Lconst &o) const;

  [[nodiscard]] Lconst adjust_bits(Bits_t amount) const;

  bool has_unknowns() const { return explicit_str && bits < calc_num_bits(num); }
  bool is_negative() const {
    if (!explicit_str) {
      return num < 0;
    }

    if (!has_unknowns()) {
      return false;
    }

    uint8_t msb = static_cast<uint8_t>(num);
    return (msb == '1');
  }
  bool is_positive() const {
    if (!explicit_str) {
      return num >= 0;
    }

    if (!has_unknowns()) {
      return false;
    }

    uint8_t msb = static_cast<uint8_t>(num);
    return (msb == '0');
  }
  bool has_unknown_sign() const { return has_unknowns() && static_cast<uint8_t>(num) == '?'; }
  bool is_fully_unkown() const { return explicit_str && bits == 8 && static_cast<uint8_t>(num) == '?'; }

  [[nodiscard]] bool is_known_false() const { return num == 0; }
  [[nodiscard]] bool is_known_true() const;
  [[nodiscard]] bool is_string() const { return explicit_str && !has_unknowns(); }
  [[nodiscard]] bool is_mask() const { return !explicit_str && ((num + 1) & (num)) == 0; }
  [[nodiscard]] bool is_power2() const { return !explicit_str && ((num - 1) & (num)) == 0; }

  [[nodiscard]] std::vector<std::pair<int, int>> get_mask_range_pairs() const;
  [[nodiscard]] std::pair<int, int>              get_mask_range() const;

  [[nodiscard]] Bits_t get_bits() const { return bits; }  // note: this is returning signed bits of the constant

  [[nodiscard]] bool   bit_test(size_t p) const;
  [[nodiscard]] size_t get_first_bit_set() const;
  [[nodiscard]] size_t get_last_bit_set() const;

  [[nodiscard]] size_t popcount() const;

  [[nodiscard]] bool    is_i() const { return !explicit_str && bits <= 62; }  // 62 to handle sign (int)
  [[nodiscard]] int64_t to_i() const;                                         // must fit in int or exception raised

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

  const Number get_raw_num() const { return num; }  // FOR DEBUG ONLY
};

#include <format>

template <>
struct std::formatter<Lconst> : formatter<string_view> {
  // parse is inherited from formatter<string_view>.
  template <typename FormatContext>
  auto format(Lconst c, FormatContext &ctx) const {
    return formatter<string_view>::format(c.to_pyrope(), ctx);
  }
};
