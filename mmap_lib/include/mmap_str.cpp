//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <iostream>

#include "mmap_str.hpp"

void str_dump(const mmap_lib::str &str) {
  str.dump();
}

void mmap_lib::str::dump() const {
  std::cout << "size:" << size() << " " << "str:" << to_s() << "\n";
}
