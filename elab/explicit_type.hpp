//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

//#include "absl/hash/hash.h"

template <typename T, typename Meaning, T inv_val>
struct Explicit_type {
  //! Default constructor does not initialize the value.
  constexpr Explicit_type() : value(inv_val) {}

  //! Construction from a fundamental value.
  constexpr Explicit_type(T _value) : value(_value) {}

  //! Implicit conversion back to the fundamental data type.
  [[nodiscard]] constexpr inline operator T() const noexcept { return value; }

  //! The actual fundamental value.
  T         value;
  typedef T type;

  bool is_invalid() const { return value == inv_val; }
  void invalidate() { value = inv_val; }

  //bool operator==(const Explicit_type<T,Meaning> &other) const { return value == other.value; }
};

#if 0
#include <iostream>

typedef Explicit_type<int, struct CakeIdStruct> CakeId;
typedef Explicit_type<int, struct PortalIdStruct> PortalId;

void method(PortalId p) {
  std::cout << "p:" << p << std::endl;
}

#if 1
// Comment this to have a compile error
void method(CakeId p) {
  std::cout << "c:" << p << std::endl;
}
#endif

int main() {

  PortalId p = 3;
  CakeId c = 3;

  if (p == c)
    std::cout << "p:" << p << std::endl;

  method(c);
}
#endif
