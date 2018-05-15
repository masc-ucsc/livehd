
#include "core/char_array.hpp"


void test1(int n) {
  Char_Array<int> test1("lgdb", "test1");
  test1.clear();
  std::string foo = "a";

  for(int i = 0; i < n; i++) {
    foo += "a";
  }
  test1.create_id(foo.c_str(), 1);
  test1.create_id("b", 2);
}

void test2(int n) {
  Char_Array<int> test1("lgdb", "test2");
  test1.clear();
  std::string foo = "a";

  for(int i = 0; i < n; i++) {
    test1.create_id(foo.c_str(), 0x98769876);
    foo += "aa";
  }
  test1.create_id("c", 2);
}

//stress test on the char array
int main(int argc, char** argv) {

  int n = 0;
  if(argc < 2) {
    //2000 is enough to trigger a couple resizings
    n = 2000;
  } else {
    n = atoi(argv[1]);
  }
  fmt::print("running test1 with {} iterations\n", n);
  test1(n);
  fmt::print("running test2 with {} iterations\n", n);
  test2(n);

  return 0;
}

