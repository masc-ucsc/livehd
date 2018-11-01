//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <string>

#include "char_array.hpp"

void test0(int n) {
  Char_Array<unsigned int> test("test0");
  test.clear();
  std::string foo = "a";

  test.create_id("c", 2);
  assert(test.get_field("c") == 2);

  for(int i = 0; i < n; i++) {
    test.create_id(foo.c_str(), 0x98769876);
  }

  assert(test.get_field("c") == 2);
  test.create_id("c", 3);
  assert(test.get_field("c") == 3);

  test.create_id("d", 4);
  assert(test.get_field("d") == 4);

  assert(test.get_field("a") == 0x98769876);
}

void test1(int n) {
  Char_Array<int64_t> test("test1");
  test.clear();
  std::string foo = "a";

  for(int i = 0; i < n; i++) {
    test.create_id(foo, i);
  }
  test.create_id("c", 2);

  assert(test.get_field("a") == n-1);
}

void test2(int n) {
  Char_Array<uint64_t> test("test2");
  test.clear();
  std::string foo = "a";

  for(int i = 0; i < std::min(n,32000); i++) { // At most 2**15 character
    foo += "a";
  }
  test.create_id(foo.c_str(), 1);
  test.create_id("b", 2);

  assert(test.get_field(foo.c_str()) == 1);
  assert(test.get_field("b") == 2);
}

void test3(int n) {
  Char_Array<uint32_t> test("test3");
  test.clear();
  std::string foo = "a";

  for(int i = 0; i < n; i++) {
    uint32_t val = 0xdead0000 | (n&0xFFFF);
    test.create_id(std::to_string(i), val);
  }
  test.create_id("c", 2);

  for(int i = 0; i < n; i++) {
    uint32_t val = 0xdead0000 | (n&0xFFFF);
    assert(test.get_field(std::to_string(i)) == val);
  }
  assert(test.get_field("c") == 2);
}

void test4(int n) {
  Char_Array<uint32_t> test("test3"); // Read test 3

  for(int i = 0; i < n; i++) {
    uint32_t val = 0xdead0000 | (n&0xFFFF);
    assert(test.get_field(std::to_string(i)) == val);
  }
  assert(test.get_field("c") == 2);

  assert(test.get_id("c") != 0);
  test.clear();
  assert(test.get_id("c") == 0);
}

//stress test on the char array
int main(int argc, char** argv) {

  int n = 0;
  if(argc < 2) {
    //2000 is enough to trigger a couple resizings
    n = 40000;
  } else {
    n = atoi(argv[1]);
  }
  test0(n);
  test1(n);
  test2(n);
  test3(n);
  test4(n);

  return 0;
}

