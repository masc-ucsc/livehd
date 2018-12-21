#pragma once

template <typename T, typename Meaning>
struct Explicit_type
{
  //! Default constructor does not initialize the value.
  Explicit_type()
  { }

  //! Construction from a fundamental value.
  Explicit_type(T value)
    : value(value)
  { }

  //! Implicit conversion back to the fundamental data type.
  inline operator T () const { return value; }

  //! The actual fundamental value.
  T value;
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
