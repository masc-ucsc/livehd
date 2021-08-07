//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <iostream>

#include "mmap_str.hpp"

void call_dump_not_used_but_force_to_preserve_dump() {
  mmap_lib::str str("hello");

  str.dump();
}

void mmap_lib::str::dump() const {
  std::cout << "size:" << size() << " " << "str:" << to_s() << "\n";
}
