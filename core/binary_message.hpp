// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

namespace livehd::binary_message {
// Private same-build, same-host protocol. Transfer numeric arrays in bulk,
// including exact solver coordinates. Never transfer a pointer or a handle.
struct Encoder {
  std::string bytes;
  void        raw(const void* data, size_t n) {
    if (n) {
      bytes.append(static_cast<const char*>(data), n);
    }
  }
};
struct Decoder {
  std::string_view bytes;
  void             raw(void* data, size_t n) {
    if (n > bytes.size()) {
      throw std::runtime_error("short binary message");
    }
    if (n) {
      std::memcpy(data, bytes.data(), n);
    }
    bytes.remove_prefix(n);
  }
};
template <class A, class T>
  requires(std::is_arithmetic_v<T> || std::is_enum_v<T>)
void field(A& a, T& v) {
  a.raw(&v, sizeof(v));
}

template <class A, class T>
void sequence(A& a, T& v) {
  uint64_t n = v.size();
  field(a, n);
  using V = typename T::value_type;
  if constexpr (std::is_same_v<A, Decoder>) {
    constexpr size_t minimum_bytes = std::is_arithmetic_v<V> ? sizeof(V) : 1;
    if (n > a.bytes.size() / minimum_bytes || n > v.max_size()) {
      throw std::runtime_error("invalid binary array length");
    }
    v.resize(n);
  }
  if constexpr (std::is_arithmetic_v<V>) {
    a.raw(v.data(), n * sizeof(V));
  } else {
    for (auto& elem : v) {
      field(a, elem);
    }
  }
}
inline void field(Encoder& a, std::span<const double>& v) {
  uint64_t n = v.size();
  field(a, n);
  a.raw(v.data(), v.size_bytes());
}
template <class A>
void field(A& a, std::string& v) {
  sequence(a, v);
}
template <class A, class T>
void field(A& a, std::optional<T>& value) {
  bool present = value.has_value();
  field(a, present);
  if constexpr (std::is_same_v<A, Decoder>) {
    if (present) {
      value.emplace();
    } else {
      value.reset();
    }
  }
  if (present) {
    field(a, *value);
  }
}
template <class A, class... T>
void fields(A& a, T&... v) {
  (field(a, v), ...);
}
template <class A, class T, size_t N>
void field(A& a, std::array<T, N>& v) {
  for (auto& e : v) {
    field(a, e);
  }
}
template <class A, class T, class U>
void field(A& a, std::pair<T, U>& v) {
  fields(a, v.first, v.second);
}
template <class A, class... T>
void field(A& a, std::tuple<T...>& v) {
  std::apply([&](auto&... e) { fields(a, e...); }, v);
}
template <class A, class T>
void field(A& a, std::vector<T>& v) {
  sequence(a, v);
}
template <class A, class K, class V>
void field(A& a, std::map<K, V>& v) {
  uint64_t n = v.size();
  field(a, n);
  if constexpr (std::is_same_v<A, Decoder>) {
    if (n > a.bytes.size()) {
      throw std::runtime_error("invalid binary map length");
    }
    for (uint64_t i = 0; i < n; ++i) {
      K k{};
      field(a, k);
      field(a, v[k]);
    }
  } else {
    for (auto& [key, value] : v) {
      auto k = key;
      fields(a, k, value);
    }
  }
}
inline void put(Encoder& a, const auto&... values) { (field(a, const_cast<std::remove_cvref_t<decltype(values)>&>(values)), ...); }
}  // namespace livehd::binary_message
