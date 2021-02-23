#include "fmt/format.h"
#include "iassert.hpp"
#include <vector>
#include "lrand.hpp"
#include "lbench.hpp"
#include "flat_hash_map.hpp"
#include <type_traits>
#include "mmap_str.hpp"
#include "mmap_map.hpp"


void mmap_pstr(std::string_view name) {
  //mmap_lib::str foo("yohelloooooo12345678");
  //std::string_view str_view("abcdef");
  //mmap_lib::str boo(str_view);
  std::cout << "================================ " << std::endl;
  std::cout << "Constructor 1 (size < 14) Tests: " << std::endl;
  std::cout << "================================ " << std::endl;
  std::cout << "> Test 1: str(\"hello\") " << std::endl;
  //char * foo = "hello";
  mmap_lib::str test1("hello");
  std::cout << "Expecting \"hell\":  ";
  test1.print_PoS(); 
  std::cout << "Expecting \"o\":  ";
  test1.print_e();
  std::cout << "Expecting nothing:  ";
  test1.print_key_val_str();
  
  
  std::cout << "> Test 2: str(\"cat\") " << std::endl;
  mmap_lib::str test2("cat");
  std::cout << "Expecting \"cat\":  ";
  test2.print_PoS(); 
  std::cout << "Expecting nothing:  ";
  test2.print_e();
  std::cout << "Expecting nothing:  ";
  test2.print_key_val_str();
 
  std::cout << "> Test 3: str(\"abcd\") " << std::endl;
  mmap_lib::str test3("abcd");
  std::cout << "Expecting \"abcd\":  ";
  test3.print_PoS(); 
  std::cout << "Expecting nothing:  ";
  test3.print_e();
  std::cout << "Expecting nothing:  ";
  test3.print_key_val_str();
  
  std::cout << "> Test 4: str(\"feedback\") " << std::endl;
  mmap_lib::str test4("feedback");
  std::cout << "Expecting \"feed\":  ";
  test4.print_PoS(); 
  std::cout << "Expecting \"back\":  ";
  test4.print_e();
  std::cout << "Expecting nothing:  ";
  test4.print_key_val_str();
  
  std::cout << "> Test 5: str(\"neutralizatio\") " << std::endl;
  mmap_lib::str test5("neutralizatio");
  std::cout << "Expecting \"neut\":  ";
  test5.print_PoS(); 
  std::cout << "Expecting \"ralizatio\":  ";
  test5.print_e();
  std::cout << "Expecting nothing:  ";
  test5.print_key_val_str();  
}


int main(int argc, char **argv) {

  mmap_pstr("bench_map_use_mmap.data");
  return 0;
}
