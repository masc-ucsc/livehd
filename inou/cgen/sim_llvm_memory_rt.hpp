// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string_view>

namespace livehd::sim {
// Shared support, identical for every generated LLVM design. Circuit-specific
// memory addressing and forwarding are emitted as LLVM instructions.
inline constexpr std::string_view kLlvmMemorySupport = R"LHD(
#ifndef LHD_SIM_PACKED_MEMORY
#define LHD_SIM_PACKED_MEMORY
#include <cstddef>
#include <type_traits>

#include "checkpoint.hpp"

template <int N, bool Unsigned>
struct __lhd_packed_value {
  using value_type                                          = std::conditional_t<Unsigned, Slop_u<N>, Slop<N>>;
  static constexpr size_t                 packed_word_count = (N + 63) / 64;
  std::array<uint64_t, packed_word_count> words{};
  __lhd_packed_value() = default;
  __lhd_packed_value(const value_type& v) {
    if constexpr (Unsigned) {
      v.copy_packed_words(words.data());
    } else {
      v.copy_packed_words(words.data(), packed_word_count);
    }
    trim();
  }
  void trim() {
    if constexpr (N % 64) {
      words.back() &= (uint64_t{1} << (N % 64)) - 1;
    }
  }
  static uint64_t& random_state() {
    static thread_local uint64_t state = hlop_random_seed();
    return state;
  }
  static __lhd_packed_value random_value() {
    __lhd_packed_value result;
    for (auto& word : result.words) {
      auto z = (random_state() += UINT64_C(0x9e3779b97f4a7c15));
      ++hlop_random_draws();
      z    = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
      z    = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
      word = z ^ (z >> 31);
    }
    result.trim();
    return result;
  }
  value_type value() const {
    if constexpr (Unsigned) {
      return value_type::from_packed_words(words.data());
    } else {
      return value_type::from_packed_words(words.data(), packed_word_count);
    }
  }
                            operator value_type() const { return value(); }
  static __lhd_packed_value from_binary(std::string_view s, bool sign) { return value_type::from_binary(s, sign); }
  bool                      identical(const __lhd_packed_value& b) const { return words == b.words; }
  void                      copy_packed_words(uint64_t* to) const { std::copy(words.begin(), words.end(), to); }
  std::string               to_hex(int digits) const { return value().to_hex(digits); }
  std::string               to_pyrope() const { return value().to_pyrope(); }
  int64_t                   to_i64_low() const { return value().to_i64_low(); }
};
// ADL makes the packed entry visible to the shared memory hash walker.
template <int N, bool U>
uint64_t __lhd_tune_h(const __lhd_packed_value<N, U>& v) {
  return __lhd_tune_value<N>(v);
}
namespace hlop {
template <int N, bool U>
struct Mem_width<__lhd_packed_value<N, U>> {
  static constexpr int value = N;
};
template <int N, bool U>
struct Mem_val<__lhd_packed_value<N, U>> {
  using V = __lhd_packed_value<N, U>;
  using T = typename V::value_type;
  using A = Mem_val<T>;
  static V zero() { return A::zero(); }
  static V ones(int bits) { return A::ones(bits); }
  static V undef(int) { return V::random_value(); }
  static V or_(const V& a, const V& b) { return A::or_(a.value(), b.value()); }
  static V and_(const V& a, const V& b) { return A::and_(a.value(), b.value()); }
  static V not_(const V& a) { return A::not_(a.value()); }
  static V shl_(const V& a, int64_t n) { return A::shl_(a.value(), n); }
  static V sra_(const V& a, int64_t n) { return A::sra_(a.value(), n); }
  static V sext_(const V& a, int n) { return A::sext_(a.value(), n); }
  static V undef_lanes(const V& a, const V& b, int) {
    auto r = V::random_value();
    for (size_t i = 0; i < V::packed_word_count; ++i) {
      r.words[i] = (a.words[i] & ~b.words[i]) | (r.words[i] & b.words[i]);
    }
    return r;
  }
  static bool    bit_test(const V& a, int n) { return A::bit_test(a.value(), n); }
  static bool    truthy(const V& a) { return A::truthy(a.value()); }
  static bool    uncertain(const V&) { return false; }
  static bool    bit_unknown(const V&, int) { return false; }
  static int     nbits(const V& a) { return A::nbits(a.value()); }
  static bool    addr_known(const V& a) { return A::addr_known(a.value()); }
  static int64_t to_i64(const V& a) { return A::to_i64(a.value()); }
};
namespace ckpt {
template <int N, bool U>
struct Hex_codec<__lhd_packed_value<N, U>> {
  using V = __lhd_packed_value<N, U>;
  static std::string format(const V& v) { return Hex_codec<typename V::value_type>::format(v.value()); }
  static V           parse(std::string_view s) { return Hex_codec<typename V::value_type>::parse(s); }
};
}  // namespace ckpt
}  // namespace hlop
template <class Base>
struct __lhd_packed_memory : Base {
  using P                       = typename Base::value_type;
  using V                       = typename P::value_type;
  static constexpr size_t words = P::packed_word_count;
  using Write                   = hlop::Mem_write<P>;
  static_assert(sizeof(P) == words * sizeof(uint64_t));
  static_assert(offsetof(Write, din) == 0);
  static_assert(offsetof(Write, lanes) == words * 8);
  static_assert(offsetof(Write, xlanes) == words * 16);
  static_assert(offsetof(Write, addr) == words * 24);
  static_assert(offsetof(Write, fired) == words * 24 + 8);
  static_assert(sizeof(Write) == words * 24 + 16);
  uint64_t* packed_data() { return reinterpret_cast<uint64_t*>(Base::data_.data()); }
  uint64_t* packed_random() { return &P::random_state(); }
  void*     packed_pending() { return Base::pend_.data(); }
  template <class A>
  V read(int port, const A& address) const {
    return Base::read(port, address).value();
  }
  template <int Port, class A>
  V read(const A& address) const {
    return Base::template read<Port>(address).value();
  }
  auto read_all() const {
    std::array<V, Base::entry_count> a;
    for (size_t i = 0; i < a.size(); ++i) {
      a[i] = Base::data_[i].value();
    }
    return slop_read_all(a);
  }
  template <class T>
  bool apply_update(const T& bus) {
    std::array<V, Base::entry_count> a;
    for (size_t i = 0; i < a.size(); ++i) {
      a[i] = Base::data_[i].value();
    }
    const bool changed = slop_apply_update(a, bus);
    for (size_t i = 0; i < a.size(); ++i) {
      Base::data_[i] = a[i];
    }
    return changed;
  }
};
#endif
)LHD";
}  // namespace livehd::sim
