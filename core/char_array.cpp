//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "char_array.hpp"

#if 0
// Sample use without persistence
int main() {

  //Char_Array<> arr("foo");
  Char_Array<uint64_t> arr("db","bar");
  Char_Array<uint64_t> arr2("db","foo");

  int a1 = arr.create_id("bar" , 100);
  int a2 = arr.create_id("nodo", 200);

  arr2.create_id("this_is_one");
  arr2.create_id("this_is_two");

  fmt::print("a1:{}\n",a1);
  fmt::print("a2:{}\n",a2);
  fmt::print("a1:{} is {}\n",a1,arr.get_char(a1));
  fmt::print("a2:{} is {}\n",a2,arr.get_char(a2));
  fmt::print("a1:{} f is {}\n",a1,arr.get_field(a1));
  fmt::print("a2:{} f is {}\n",a2,arr.get_field(a2));

  arr.dump();
  arr2.dump();

  for(const auto str:arr) {
    uint64_t f = arr.get_field(str);
    fmt::print("s:{} f:{}\n",str,f);
  }
}
#endif
