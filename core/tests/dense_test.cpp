//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <iostream>
#include <vector>

#include <string>

#include <cassert>

#include "dense.hpp"
#include "mmap_allocator.hpp"

class CharPtr {
public:
  CharPtr(const std::string &str) {
    ptr = str.c_str(); // FIXME: Allocate in dense
    std::cout << "1.CharPtr constructor\n";
  }
  CharPtr(const char *ptr_) {
    ptr = ptr_; // FIXME: Allocate in Dense
    std::cout << "2.CharPtr constructor\n";
  }
  const char *get_charptr() const {
    return ptr;
  }

private:
  const char *ptr;
};
std::ostream &operator<<(std::ostream &os, const CharPtr &obj) {
  os << obj.get_charptr();
  return os;
}
Dense<CharPtr> vec("str");

int main(int argc, char **argv) {

  std::string str = "p" + std::to_string(vec.size()) + "x";

  vec.emplace_back(CharPtr("potato"));
  std::cout << "1.last = " << &vec.back() << std::endl;
  vec.emplace_back(CharPtr(str.c_str()));
  std::cout << "2.last = " << &vec.back() << std::endl;
  vec.emplace_back(CharPtr(""));
  std::cout << "3.last = " << &vec.back() << std::endl;
  vec.emplace_back(CharPtr("0123456789"));
  std::cout << "4.last = " << &vec.back() << std::endl;

  std::cout << "size=" << vec.size() << " base=" << vec.data() << std::endl;

  for(const auto &v : vec) {
    std::cout << v;
  }
  std::cout << std::endl;
}
