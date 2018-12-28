//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <sys/stat.h>
#include <sys/types.h>

#include <string>
#include <charconv>

#include "char_array.hpp"

void test0(int n) {
  Char_Array<unsigned int> test("char_tst_mmap/test0");
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
  Char_Array<int64_t> test("char_tst_mmap/test1");
  test.clear();
  std::string foo = "a";

  for(int i = 0; i < n; i++) {
    test.create_id(foo, i);
  }
  test.create_id("c", 2);

  assert(test.get_field("a") == n - 1);
}

void test2(int n) {
  Char_Array<uint64_t> test("char_tst_mmap/test2");
  test.clear();
  std::string foo = "a";

  for(int i = 0; i < std::min(n, 32000); i++) { // At most 2**15 character
    foo += "a";
  }
  test.create_id(foo.c_str(), 1);
  test.create_id("b", 2);

  assert(test.get_field(foo.c_str()) == 1);
  assert(test.get_field("b") == 2);
}

void test3(int n) {
  Char_Array<uint32_t> test("char_tst_mmap/test3");
  test.clear();
  std::string foo = "a";

  for(int i = 0; i < n; i++) {
    uint32_t val = 0xdead0000 | (i & 0xFFFF);
    test.create_id(std::to_string(i), val);
  }
  test.create_id("c", 2);

  for(int i = 0; i < n; i++) {
    uint32_t val = 0xdead0000 | (i & 0xFFFF);
    assert(test.get_field(std::to_string(i)) == val);
  }
  assert(test.get_field("c") == 2);
}

void test4(int n) {
  Char_Array<uint32_t> test("char_tst_mmap/test3"); // Read test 3

  for(int i = 0; i < n; i++) {
    uint32_t val = 0xdead0000 | (i & 0xFFFF);
    assert(test.get_field(std::to_string(i)) == val);
  }
  assert(test.get_field("c") == 2);

  assert(test.get_id("c") != 0);
  test.clear();
  assert(test.get_id("c") == 0);
}

void test5(size_t n) {
  struct Test5_data {
    uint32_t potato;
    size_t   id;
    uint64_t banana;
  };
  Char_Array<Test5_data> test("char_tst_mmap/test5"); // Read test 3

  std::vector<Char_Array_ID> idlist;

  for(size_t i = 0; i < n; i++) {
    Test5_data d;
    d.potato = 0xdead0000 | (i & 0xFFFF);
    d.banana = 0xbeef0000 | (i & 0xFFFF);
    d.id     = i;
    auto cid = test.create_id(std::to_string(i), d);
    idlist.push_back(cid);
  }

  for(size_t i = 0; i < n; i++) {
    const Test5_data &d = test.get_field(std::to_string(i));
    assert(d.potato == (0xdead0000 | (i & 0xFFFF)));
    assert(d.banana == (0xbeef0000 | (i & 0xFFFF)));
    assert(d.id == i);

    assert(idlist[i] == test.get_id(std::to_string(i)));
  }

  size_t conta = 0;
  for(auto it = test.begin(); it != test.end(); ++it) {
    assert(idlist.size() > conta);

    assert(idlist[conta] == it.get_id());

    auto str = it.get_name();

#ifndef NDEBUG
    // Ugly code for string_view atoi
    size_t conta2;
    std::from_chars(&str[0], &str[0] + str.size(), conta2);
    assert(conta2 == conta);
#endif

    const auto &d = it.get_field();
    assert(d.potato == (0xdead0000 | (conta & 0xFFFF)));
    assert(d.banana == (0xbeef0000 | (conta & 0xFFFF)));
    assert(d.id == conta);

    conta++;
  }
}

// stress test on the char array
int main(int argc, char **argv) {

  int n = 0;
  if(argc < 2) {
    // 2000 is enough to trigger a couple resizings
    n = 40000;
  } else {
    n = atoi(argv[1]);
  }

  mkdir("char_tst_mmap", 0755);

  test0(n);
  test1(n);
  test2(n);
  test3(n);
  test4(n);
  test5(n);

  return 0;
}
