//  This file extracted from the https://github.com/ucsc-vama/firrtl-sig repo with LICENSE.firrtl-sig license
#pragma once

#include <algorithm>
#include <array>
#include <bitset>
#include <cinttypes>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <type_traits>

// Internal RNG
namespace {
std::mt19937_64 rng64(14);
uint64_t        rng_leftover;
uint64_t        rng_bits_left = 0;
}  // namespace

// Forward dec
template <int w_>
class SInt;

template <int w_, typename word_t = typename std::conditional<(w_ <= 8), uint8_t, uint64_t>::type,
          int n_ = (w_ <= 8) ? 1 : (w_ + 64 - 1) / 64>
class UInt {
private:
  constexpr static int cmin(int wa, int wb) { return wa < wb ? wa : wb; }
  constexpr static int cmax(int wa, int wb) { return wa > wb ? wa : wb; }

  constexpr static int cap(int s) { return s % kWordSize; }

public:
  constexpr UInt() : words_{} {
    for (int i = 0; i < n_; ++i) {
      words_[i] = 0;
    }
  }

  template <word_t... Limbs>
  constexpr UInt(const std::integer_sequence<word_t, Limbs...>) : words_{Limbs...} {}

  constexpr UInt(word_t initial) : UInt() {
    constexpr int bits_word = std::is_same<word_t, uint64_t>::value ? 64 : 8;
    if constexpr (w_ < 64) {
      uint64_t top_word_mask = (1l << w_) - 1;
      words_[0]              = initial & top_word_mask;
    } else {
      words_[0] = initial;
    }
  }

  UInt(std::string initial) {
    if (initial.substr(0, 2) != "0x") {
      std::cout << "ERROR: UInt string literal must start with 0x!" << std::endl;
      std::exit(-17);
    }
    initial.erase(0, 2);
    // FUTURE: check that literal isn't too big
    int input_bits = 4 * initial.length();
    int last_start = initial.length();
    for (int word = 0; word < n_; word++) {
      if (word * kWordSize >= input_bits) {
        words_[word] = 0;
      } else {
        int word_start           = std::max(static_cast<int>(initial.length()) - 16 * (word + 1), 0);
        int word_len             = std::min(16, last_start - word_start);
        last_start               = word_start;
        const std::string substr = initial.substr(word_start, word_len);
        words_[word]             = static_cast<uint64_t>(std::stoul(substr, nullptr, 16));
      }
    }
  }

  // NOTE: reads words right to left so literal appears to be concatted
  constexpr UInt(std::array<word_t, n_> raw_input_reversed) {
    for (int i = 0; i < n_; i++) {
      words_[i] = raw_input_reversed[n_ - i - 1];
    }
    mask_top_unused();
  }

  template <int other_w>
  constexpr explicit UInt(const UInt<other_w> &other) {
    static_assert(other_w <= w_, "Can't copy construct from wider UInt");
    for (int word = 0; word < n_; word++) {
      if (word < UInt<other_w>::NW) {
        words_[word] = other.words_[word];
      } else {
        words_[word] = 0;
      }
    }
  }

  template <int other_w>
  constexpr UInt<w_> operator=(const UInt<other_w> &other) {
    if constexpr (other_w < w_) {
      for (int word = 0; word < other_w; word++) {
        if (word < UInt<other_w>::NW) {
          words_[word] = other.words_[word];
        } else {
          words_[word] = 0;
        }
      }
    } else {
      for (int word = 0; word < n_; word++) {
        if (word < UInt<other_w>::NW) {
          words_[word] = other.words_[word];
        } else {
          words_[word] = 0;
        }
      }
    }
    return *this;
  }

  void rand_init() {
    core_rand_init();
    mask_top_unused();
  }

  template <int out_w>
  UInt<cmax(w_, out_w)> pad() const {
    return UInt<cmax(w_, out_w)>(*this);
  }

#if 0
  template<int other_w>
  constexpr auto cat(const UInt<other_w> &other) {
    UInt<w_ + other_w> to_return(other);
    const int offset = other_w % kWordSize;
    for (int i = 0; i < n_; i++) {
      to_return.words_[word_index(other_w) + i] |= static_cast<uint64_t>(words_[i]) <<
                                                     cap(offset);
      if ((offset != 0) && (i + 1 < to_return.NW - word_index(other_w)))
        to_return.words_[word_index(other_w) + i + 1] |= static_cast<uint64_t>(words_[i]) >>
                                                           cap(kWordSize - offset);
    }
    return to_return;
  }
#endif

  template <int other_w>
  UInt<w_ + other_w> cat(const UInt<other_w> &other) const {
    UInt<w_ + other_w> to_return(other);
    const int          offset = other_w % kWordSize;
    for (int i = 0; i < n_; i++) {
      to_return.words_[word_index(other_w) + i] |= static_cast<uint64_t>(words_[i]) << cap(offset);
      if ((offset != 0) && (i + 1 < to_return.NW - word_index(other_w))) {
        to_return.words_[word_index(other_w) + i + 1] |= static_cast<uint64_t>(words_[i]) >> cap(kWordSize - offset);
      }
    }
    return to_return;
  }

  template <int other_w, typename other_word_t = typename std::conditional<(other_w <= 8), uint8_t, uint64_t>::type,
            int other_n = (other_w <= 8) ? 1 : (other_w + 64 - 1) / 64>
  constexpr auto operator+(const UInt<other_w> &other) const {
    constexpr auto max_bits = cmax(w_, other_w);
    constexpr auto max_n    = cmax(n_, other_n);

    UInt<max_bits + 1> result;

    if constexpr (w_ > other_w) {
      result = core_add_sub<max_bits + 1, false>(other.template pad<max_bits>());
    } else if constexpr (w_ < other_w) {
      result = other.core_add_sub<max_bits + 1, false>(pad<max_bits>());
    } else {
      result = core_add_sub<w_ + 1, false>(other);
    }
    if constexpr (kWordSize * max_n == max_bits) {
      // propagate to next word in needed
      if (result.words_[max_n - 1] < words_[max_n - 1]) {
        result.words_[word_index(w_ + 1)] = 1;
      }
    }
    return result;
  }

  SInt<w_ + 1> operator+(const SInt<w_> &other) const { return SInt<w_ + 1>(pad<w_ + 1>()).addw(other.template pad<w_ + 1>()); }

  UInt<w_> addw(const UInt<w_> &other) const {
    UInt<w_> result = core_add_sub<w_, false>(other);
    result.mask_top_unused();
    return result;
  }

  SInt<w_> subw(const UInt<w_> &other) const {
    SInt<w_> result(core_add_sub<w_, true>(other.ui));
    result.sign_extend();
    return result;
  }

  SInt<w_ + 1> operator-() const { return SInt<w_>(0) - *this; }

  SInt<w_ + 1> operator-(const UInt<w_> &other) const {
    return SInt<w_ + 1>(pad<w_ + 1>()).subw(SInt<w_ + 1>(other.template pad<w_ + 1>()));
  }

  SInt<w_ + 1> operator-(const SInt<w_> &other) const { return SInt<w_ + 1>(pad<w_ + 1>()).subw(other.template pad<w_ + 1>()); }

#if 1
  template <int other_w, int result_n = ((w_ + other_w) + 64 - 1) / 64>
  constexpr UInt<w_ + other_w> operator*(UInt<other_w> other) const {
    if constexpr ((w_ + other_w) <= 8) {
      uint64_t val = words_[0] * other.words_[0];
      return UInt<w_ + other_w>(val);
    } else {
      UInt<w_ + other_w> result(0);
      using u128 = unsigned __int128;  // Sometimes 16 is enough but just for small consts

      for (auto j = 0; j < other.NW; ++j) {
        uint64_t k = 0;
        for (auto i = 0; i < n_; ++i) {
          u128 t               = static_cast<u128>(words_[i]) * static_cast<u128>(other.words_[j]) + result.words_[i + j] + k;
          result.words_[i + j] = static_cast<uint64_t>(t);
          k                    = t >> std::numeric_limits<uint64_t>::digits;
        }
        if ((j + n_) < result_n) {
          result.words_[j + n_] = k;
        }
      }

      return result;
    }
  }

#else
  constexpr UInt<w_ + w_> operator*(const UInt<w_> &other) const {
    UInt<w_ + w_> result(0);
    uint64_t      carry = 0;
    for (int i = 0; i < n_; i++) {
      carry = 0;
      for (int j = 0; j < n_; j++) {
        uint64_t prod_ll   = lower(words_[i]) * lower(other.words_[j]);
        uint64_t prod_lu   = lower(words_[i]) * upper(other.words_[j]);
        uint64_t prod_ul   = upper(words_[i]) * lower(other.words_[j]);
        uint64_t prod_uu   = upper(words_[i]) * upper(other.words_[j]);
        uint64_t lower_sum = lower(result.words_[i + j]) + lower(carry) + lower(prod_ll);
        uint64_t upper_sum
            = upper(result.words_[i + j]) + upper(carry) + upper(prod_ll) + upper(lower_sum) + lower(prod_lu) + lower(prod_ul);
        result.words_[i + j] = (upper_sum << 32) | lower(lower_sum);
        carry                = upper(upper_sum) + upper(prod_lu) + upper(prod_ul) + prod_uu;
      }
      if ((i + n_) < result.NW) {
        result.words_[i + n_] += carry;
      }
    }
    return result;
  }
#endif

  SInt<w_ + w_> operator*(const SInt<w_> &other) const {
    SInt<w_ + w_ + 2> product(SInt<w_ + 1>(pad<w_ + 1>()) * other.template pad<w_ + 1>());
    SInt<w_ + w_>     result = (product.template tail<2>()).asSInt();
    result.sign_extend();
    return result;
  }

  // this / other
  template <int other_w>
  UInt<w_> operator/(const UInt<other_w> &other) const {
    static_assert(w_ <= kWordSize, "Div not supported beyond 64b");
    static_assert(other_w <= kWordSize, "Div not supported beyond 64b");
    return UInt<w_>(as_single_word() / other.as_single_word());
  }

  template <int other_w>
  UInt<w_ + 1> operator/(const SInt<other_w> &other) const {
    return SInt<w_ + 1>(pad<w_ + 1>()) / other;
  }

  // this % other
  template <int other_w>
  UInt<cmin(w_, other_w)> operator%(const UInt<other_w> &other) const {
    static_assert(w_ <= kWordSize, "Mod not supported beyond 64b");
    static_assert(other_w <= kWordSize, "Mod not supported beyond 64b");
    return UInt<cmin(w_, other_w)>(as_single_word() % other.as_single_word());
  }

  template <int other_w>
  UInt<cmin(w_, other_w)> operator%(const SInt<other_w> &other) const {
    return *this % other.asUInt();
  }

  UInt<w_> operator~() const {
    UInt<w_> result;
    for (int i = 0; i < n_; i++) {
      result.words_[i] = ~words_[i];
    }
    result.mask_top_unused();
    return result;
  }

  UInt<w_> operator&(const UInt<w_> &other) const {
    UInt<w_> result;
    for (int i = 0; i < n_; i++) {
      result.words_[i] = words_[i] & other.words_[i];
    }
    return result;
  }

  UInt<w_> operator|(const UInt<w_> &other) const {
    UInt<w_> result;
    for (int i = 0; i < n_; i++) {
      result.words_[i] = words_[i] | other.words_[i];
    }
    return result;
  }

  UInt<w_> operator^(const UInt<w_> &other) const {
    UInt<w_> result;
    for (int i = 0; i < n_; i++) {
      result.words_[i] = words_[i] ^ other.words_[i];
    }
    return result;
  }

  UInt<1> andr() const { return *this == ~UInt<w_>(0); }

  UInt<w_> operator!() const {
    UInt<w_> result;
    for (int i = 0; i < n_; i++) {
      result.words_[i] = ~words_[i];
    }
    return result;
  }

  UInt<1> orr() const { return *this != UInt<w_>(0); }

  UInt<1> xorr() const {
    word_t result = 0;
    for (int i = 0; i < n_; i++) {
      word_t word_parity_scratch = words_[i] ^ (words_[i] >> 1);
      word_parity_scratch ^= (word_parity_scratch >> 2);
      word_parity_scratch ^= (word_parity_scratch >> 4);
      if (WW > 8) {
        word_parity_scratch ^= (word_parity_scratch >> 8);
        word_parity_scratch ^= (word_parity_scratch >> 16);
        word_parity_scratch ^= (word_parity_scratch >> 32);
      }
      result ^= word_parity_scratch;
    }
    return UInt<1>(result & 1);
  }

  template <int hi, int lo>
  UInt<hi - lo + 1> bits() const {
    UInt<hi - lo + 1> result = core_bits<hi, lo>();
    result.mask_top_unused();
    return result;
  }

  template <int hi, int lo>
  constexpr auto mbits() const {
    constexpr UInt<hi - lo + 1> result = mcore_bits<hi, lo>();
    return result;
  }

  template <int hi>
  UInt<1> bit() const {
    constexpr int word_down = word_index(hi);
    constexpr int bits_down = hi % kWordSize;
    UInt<1>       result;
    result.words_[0] = words_[0 + word_down] >> bits_down;
    result.mask_top_unused();
    return result;
  }

  template <int n>
  UInt<n> head() const {
    static_assert(n <= w_, "Head n must be <= width");
    return bits<w_ - 1, w_ - n>();
  }

  template <int n>
  UInt<w_ - n> tail() const {
    static_assert(n < w_, "Tail n must be < width");
    return bits<w_ - n - 1, 0>();
  }

  template <int shamt>
  UInt<w_ + shamt> shl() const {
    return cat(UInt<shamt>(0));
  }

  template <int shamt>
  UInt<w_> shlw() const {
    return shl<shamt>().template tail<shamt>();
  }

  template <int shamt>
  UInt<w_ - shamt> shr() const {
    return bits<w_ - 1, shamt>();
  }

  template <int other_w>
  UInt<w_> operator>>(const UInt<other_w> &other) const {
    UInt<w_> result(0);
    uint64_t dshamt    = other.as_single_word();
    uint64_t word_down = word_index(dshamt);
    uint64_t bits_down = dshamt % kWordSize;
    for (uint64_t i = word_down; i < n_; i++) {
      result.words_[i - word_down] = words_[i] >> bits_down;
      if ((bits_down != 0) && (i < n_ - 1)) {
        result.words_[i - word_down] |= words_[i + 1] << cap(kWordSize - bits_down);
      }
    }
    return result;
  }

  template <int other_w>
  UInt<w_ + (1 << other_w) - 1> operator<<(const UInt<other_w> &other) const {
    UInt<w_ + (1 << other_w) - 1> result(0);
    uint64_t                      dshamt  = other.as_single_word();
    uint64_t                      word_up = word_index(dshamt);
    uint64_t                      bits_up = dshamt % kWordSize;
    for (uint64_t i = 0; i < n_; i++) {
      result.words_[i + word_up] |= words_[i] << bits_up;
      if ((bits_up != 0) && (dshamt + w_ > kWordSize) && (i + word_up + 1 < result.NW)) {
        result.words_[i + word_up + 1] = words_[i] >> cap(kWordSize - bits_up);
      }
    }
    return result;
  }

  template <int other_w>
  UInt<w_> dshlw(const UInt<other_w> &other) const {
    // return operator<<(other).template bits<w_-1,0>();
    UInt<w_> result(0);
    uint64_t dshamt  = other.as_single_word();
    uint64_t word_up = word_index(dshamt);
    uint64_t bits_up = dshamt % kWordSize;
    for (uint64_t i = 0; i + word_up < n_; i++) {
      result.words_[i + word_up] |= words_[i] << bits_up;
      if ((bits_up != 0) && (w_ > kWordSize) && (i + word_up + 1 < n_)) {
        result.words_[i + word_up + 1] = words_[i] >> cap(kWordSize - bits_up);
      }
    }
    result.mask_top_unused();
    return result;
  }

  constexpr UInt<1> operator<=(const UInt<w_> &other) const {
    for (int i = n_ - 1; i >= 0; i--) {
      if (words_[i] < other.words_[i]) {
        return UInt<1>(1);
      }
      if (words_[i] > other.words_[i]) {
        return UInt<1>(0);
      }
    }
    return UInt<1>(1);
  }

  UInt<1> operator>=(const UInt<w_> &other) const {
    for (int i = n_ - 1; i >= 0; i--) {
      if (words_[i] > other.words_[i]) {
        return UInt<1>(1);
      }
      if (words_[i] < other.words_[i]) {
        return UInt<1>(0);
      }
    }
    return UInt<1>(1);
  }

  UInt<1> operator<(const UInt<w_> &other) const { return ~(*this >= other); }

  UInt<1> operator>(const UInt<w_> &other) const { return ~(*this <= other); }

  template <int other_w, typename other_word_t = typename std::conditional<(other_w <= 8), uint8_t, uint64_t>::type,
            int other_n = (other_w <= 8) ? 1 : (other_w + 64 - 1) / 64>
  constexpr UInt<1> operator==(const UInt<other_w> &other) const {
    constexpr auto min_words = cmin(n_, other_n);
    for (int i = 0; i < min_words; ++i) {
      if (words_[i] != other.words_[i]) {
        return UInt<1>(0);
      }
    }
    if constexpr (n_ < other_n) {
      for (int i = min_words; i < other_n; ++i) {
        if (other.words_[i]) {
          return UInt<1>(0);
        }
      }
    } else if constexpr (n_ > other_n) {
      for (int i = min_words; i < other_n; ++i) {
        if (words_[i]) {
          return UInt<1>(0);
        }
      }
    }
    return UInt<1>(1);
  }

  constexpr UInt<1> operator!=(const UInt<w_> &other) const { return ~(*this == other); }

  constexpr operator bool() const {
    static_assert(w_ == 1, "conversion to bool only allowed for width 1");
    return static_cast<bool>(words_[0]);
  }

  UInt<w_> asUInt() const { return UInt<w_>(*this); }

  SInt<w_> asSInt() const {
    SInt<w_> result(*this);
    result.sign_extend();
    return result;
  }

  SInt<w_ + 1> cvt() const { return pad<w_ + 1>().asSInt(); }

  // Direct access for ops that only need small signals
  uint64_t as_single_word() const {
    static_assert(w_ <= kWordSize, "UInt too big for single uint64_t");
    return words_[0];
  }

  std::string to_string_binary() const {
    constexpr std::string_view nibble_to_str[] = {"0000",
                                                  "0001",
                                                  "0010",
                                                  "0011",
                                                  "0100",
                                                  "0101",
                                                  "0110",
                                                  "0111",
                                                  "1000",
                                                  "1001",
                                                  "1010",
                                                  "1011",
                                                  "1100",
                                                  "1101",
                                                  "1110",
                                                  "1111"};

    bool multibit = (bits_in_top_word_ / sizeof(uint8_t)) > 1 ? 1 : 0;
    if constexpr (n_ <= 64 && n_ > 8) {
      std::string result;
      auto        v              = words_[0];
      bool        non_zero_found = false;
      for (int i = 64; i > 0; i = i - 4) {
        auto x = v >> i;
        if (!non_zero_found && x == 0) {
          continue;
        }
        non_zero_found = true;
        result.append(nibble_to_str[x & 0xF]);
      }
      return result;
    } else if constexpr (n_ <= 8) {
      bool        top_zeroes = true;
      auto        v          = words_[0];
      auto        v_casted   = static_cast<uint64_t>(words_[0]);
      std::string result     = (multibit ? "b" : "");

      if (!multibit) {
        result.append(nibble_to_str[v & 0xF]);
        result = result.back();
      } else {
        for (int i = bits_in_top_word_; i > 0; i = i - 4) {
          auto x = (v >> (i - 4));
          if ((top_zeroes && (x & 0xF)) || (i == 4)) {
            top_zeroes = false;
          }
          if (!top_zeroes) {
            result.append(nibble_to_str[x & 0xF]);
          }
        }
      }
      return result;
    } else {
      std::stringstream ss;
      if (multibit) {
        ss << "b";
      }  // we want "b" only in multi-bit variables
      // ss << "b";
      uint64_t top_word_mask = bits_in_top_word_ == kWordSize ? -1 : (1ULL << cap(bits_in_top_word_)) - 1;
      auto     v             = (static_cast<uint64_t>(words_[n_ - 1]) & top_word_mask);
      uint64_t mask          = 1ULL << (bits_in_top_word_ - 1);
      while (mask) {
        ss << ((mask & v) ? "1" : "0");
        mask >>= 1;
      }
      for (int word = n_ - 2; word >= 0; word--) {
        auto     v2   = words_[word];
        uint64_t mask = 1ULL << 63;
        while (mask) {
          ss << ((mask & v2) ? "1" : "0");
          mask >>= 1;
        }
      }

      return ss.str();
    }
  }

  std::string to_string_hex() const {
    std::stringstream ss;
    ss << "0x" << std::hex << std::setfill('0');
    int top_nibble_width = (bits_in_top_word_ + 3) / 4;
    ss << std::setw(top_nibble_width);
    uint64_t top_word_mask = bits_in_top_word_ == kWordSize ? -1 : (1ULL << cap(bits_in_top_word_)) - 1;
    ss << (static_cast<uint64_t>(words_[n_ - 1]) & top_word_mask);
    for (int word = n_ - 2; word >= 0; word--) {
      ss << std::hex << std::setfill('0') << std::setw(16) << words_[word];
    }
    return ss.str();
  }

  std::string to_string() const {
    if constexpr ((w_ % 4) == 0) {
      return to_string_hex();
    } else {
      return to_string_binary();
    }
  }

  std::string to_verilog() const {
    std::stringstream ss;

    ss << w_ << "'h" << std::hex << std::setfill('0');
    int top_nibble_width = (bits_in_top_word_ + 3) / 4;
    ss << std::setw(top_nibble_width);
    uint64_t top_word_mask = bits_in_top_word_ == kWordSize ? -1 : (1l << cap(bits_in_top_word_)) - 1;
    ss << (static_cast<uint64_t>(words_[n_ - 1]) & top_word_mask);
    for (int word = n_ - 2; word >= 0; word--) {
      ss << std::hex << std::setfill('0') << std::setw(16) << words_[word];
    }

    return ss.str();
  }

  constexpr size_t bit_length() const {
    size_t L = n_;
    while (L > 0 && words_[L - 1] == 0) {
      --L;
    }

    if (L == 0) {
      return 0;
    }

    size_t bitlen = L * std::numeric_limits<word_t>::digits;
    auto   msb    = words_[L - 1];
    while (bitlen > 0 && (msb & (static_cast<word_t>(1) << (std::numeric_limits<word_t>::digits - 1))) == 0) {
      msb <<= 1;
      --bitlen;
    }

    return bitlen;
  }

protected:
  template <int other_w>
  friend class uint_wrapper_t;

  void raw_copy_in(const uint64_t *src) {
    for (int word = 0; word < n_; word++) {
      words_[word] = *src++;
    }
  }

  void raw_copy_out(uint64_t *dst) const {
    for (int word = 0; word < n_; word++) {
      *dst++ = words_[word];
    }
  }

private:
  // Internal state
  std::array<word_t, n_> words_;

  // Access array word type
  typedef word_t WT;
  // Access array length
  constexpr static int NW = n_;
  // Access array word type bit width
  constexpr static int WW = std::is_same<word_t, uint64_t>::value ? 64 : 8;

  constexpr static int bits_in_top_word_ = w_ % WW == 0 ? WW : w_ % WW;

  // Friend Access
  template <int other_w, typename other_word_t, int other_n>
  friend class UInt;

  template <int other_w>
  friend class SInt;

  template <int w>
  friend std::ostream &operator<<(std::ostream &os, const UInt<w> &ui);

  // Bit Addressing
  constexpr static int kWordSize = 64;

  constexpr int static word_index(int bit_index) { return bit_index / kWordSize; }

  static constexpr uint64_t upper(uint64_t i) { return i >> 32; }
  static constexpr uint64_t lower(uint64_t i) { return i & 0x00000000ffffffffUL; }

  // Clean up high bits
  constexpr void mask_top_unused() {
    if (bits_in_top_word_ != WW) {
      words_[n_ - 1] = words_[n_ - 1] & ((1ULL << cap(bits_in_top_word_)) - 1ULL);
    }
  }

  // Reused math operators
  template <int out_w, bool subtract>
  constexpr UInt<out_w> core_add_sub(const UInt<w_> &other) const {
    UInt<out_w> result;
    uint64_t    carry = subtract;
    for (int i = 0; i < n_; i++) {
      uint64_t operand = subtract ? ~other.words_[i] : other.words_[i];
      result.words_[i] = words_[i] + operand + carry;
      carry            = result.words_[i] < operand ? 1 : 0;
    }
    return result;
  }

  __attribute__((noinline)) void core_rand_init() {
    // trusting mask_top_unused() will be called afterwards
    if (w_ < 64) {
      if (w_ > rng_bits_left) {
        rng_leftover  = rng64();
        rng_bits_left = 64;
      }
      words_[0]    = rng_leftover;
      rng_leftover = rng_leftover >> cap(w_);
      rng_bits_left -= w_;
    } else {
      for (int word = 0; word < n_; word++) {
        words_[word] = rng64();
      }
    }
  }

  template <int hi, int lo>
  UInt<hi - lo + 1> core_bits() const {
    static_assert(hi < w_, "Bit extract hi bigger than width");
    static_assert(hi >= lo, "Bit extract lo > hi");
    static_assert(lo >= 0, "Bit extract lo is negative");
    UInt<hi - lo + 1> result;
    constexpr int     word_down = word_index(lo);
    constexpr int     bits_down = lo % kWordSize;
    for (int i = 0; i < result.NW; i++) {
      result.words_[i] = words_[i + word_down] >> bits_down;
      if ((bits_down != 0) && (i + word_down + 1 < n_)) {
        result.words_[i] |= words_[i + word_down + 1] << cap(kWordSize - bits_down);
      }
    }
    return result;
  }

  template <int hi, int lo>
  constexpr auto mcore_bits() const {
    static_assert(hi < w_, "Bit extract hi bigger than width");
    static_assert(hi >= lo, "Bit extract lo > hi");
    static_assert(lo >= 0, "Bit extract lo is negative");
    UInt<hi - lo + 1> result;
    constexpr int     word_down = word_index(lo);
    constexpr int     bits_down = lo % kWordSize;
    for (int i = 0; i < result.NW; i++) {
      result.words_[i] = words_[i + word_down] >> bits_down;
      if ((bits_down != 0) && (i + word_down + 1 < n_)) {
        result.words_[i] |= words_[i + word_down + 1] << cap(kWordSize - bits_down);
      }
    }
    return result;
  }

  void print_to_stream(std::ostream &os) const {
    os << "0x" << std::hex << std::setfill('0');
    int top_nibble_width = (bits_in_top_word_ + 3) / 4;
    os << std::setw(top_nibble_width);
    uint64_t top_word_mask = bits_in_top_word_ == kWordSize ? -1 : (1l << cap(bits_in_top_word_)) - 1;
    os << (static_cast<uint64_t>(words_[n_ - 1]) & top_word_mask);
    for (int word = n_ - 2; word >= 0; word--) {
      os << std::hex << std::setfill('0') << std::setw(16) << words_[word];
    }
    os << std::dec;
  }
};

template <int w>
std::ostream &operator<<(std::ostream &os, const UInt<w> &ui) {
  ui.print_to_stream(os);
  os << "<U" << w << ">";
  return os;
}

constexpr size_t ctlog2(uint8_t n) { return ((n < 2) ? 1 : 1 + ctlog2(n / 2)); }

template <char... Chars>
constexpr auto operator"" _uint() {
  constexpr int                 N = sizeof...(Chars);
  constexpr std::array<char, N> seq{Chars...};

  uint8_t scale_val = 0;

  std::size_t start_pos = 0;
  if constexpr (seq.size() < 3) {
    scale_val = 10;
  } else if constexpr (seq[0] == '0' && (seq[1] == 'd' || seq[1] == 'D')) {
    start_pos = 2;
    scale_val = 10;
  } else if constexpr (seq[0] == '0' && (seq[1] == 'x' || seq[1] == 'X')) {
    start_pos = 2;
    scale_val = 16;
  } else if constexpr (seq[0] == '0' && (seq[1] == 'b' || seq[1] == 'B')) {
    start_pos = 2;
    scale_val = 2;
  } else if constexpr (seq[0] == '0') {
    start_pos = 1;
    scale_val = 8;
  } else {
    scale_val = 10;
  }

  constexpr std::size_t max_bits
      = (seq.size() > 1 && seq[0] == '0' && (seq[1] == 'b' || seq[1] == 'b'))
            ? (seq.size() - 2)
            : ((seq.size() > 1 && seq[0] == '0' && (seq[1] == 'x' || seq[1] == 'X'))
                   ? (seq.size() - 2) * 4
                   : ((seq.size() > 0 && seq[0] == '0') ? (seq.size() - 1) * 3 : (1 + ctlog2(2 * seq.size()))));

  UInt<max_bits> value;
  UInt<max_bits> scale(scale_val);

  for (std::size_t i = start_pos; i < seq.size(); ++i) {
    uint64_t v = 0;
    if (seq[i] >= 'A' && seq[i] <= 'Z') {
      v = 10 + seq[i] - 'A';
    } else if (seq[i] >= 'a' && seq[i] <= 'z') {
      v = 10 + seq[i] - 'a';
    } else if (seq[i] >= '0' && seq[i] <= '9') {
      v = seq[i] - '0';
    } else {
      continue;
    }
    value = value * scale;
    value = value + UInt<max_bits>(v);
  }
  return value;
}
