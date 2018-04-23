
#include "core/char_array.hpp"


void test0() {
  Char_Array<int> test1("lgdb", "test0");
  test1.clear();
  std::string foo = "a";

  for(int i = 0; i < 32*1024*1024; i++) {
    foo += "a";
  }
  test1.create_id(foo.c_str(), 1);
  test1.create_id("b", 2);
}

void test1() {
  Char_Array<int> test1("lgdb", "test1");
  test1.clear();
  std::string foo = "a";

  for(int i = 0; i < 8188; i++) {
    test1.create_id(foo.c_str(), 0x98769876);
    foo += "aa";
  }
  test1.create_id("c", 2);
}


//stress test on the char array
int main() {

  test1();
  return 0;
}

