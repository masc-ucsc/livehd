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

  std::cout << "================================ " << std::endl;
  std::cout << "Constructor 3 (string_view) Tests: " << std::endl;
  std::cout << "================================ " << std::endl;
  std::string_view test_6("hello");
  mmap_lib::str test6(test_6);
  std::cout << "Expecting \"hell\":  ";
  test6.print_PoS(); 
  std::cout << "Expecting \"o\":  ";
  test6.print_e();
  std::cout << "Expecting nothing:  ";
  test6.print_key_val_str();

  std::string_view test_7("cat");
  mmap_lib::str test7(test_7);
  std::cout << "Expecting \"cat\":  ";
  test7.print_PoS(); 
  std::cout << "Expecting nothing:  ";
  test7.print_e();
  std::cout << "Expecting nothing:  ";
  test7.print_key_val_str();

  std::string_view test_8("abcd");
  mmap_lib::str test8(test_8);
  std::cout << "Expecting \"abcd\":  ";
  test8.print_PoS(); 
  std::cout << "Expecting nothing ";
  test8.print_e();
  std::cout << "Expecting nothing:  ";
  test8.print_key_val_str();

  std::string_view test_9("feedback");
  mmap_lib::str test9(test_9);
  std::cout << "Expecting \"feed\":  ";
  test9.print_PoS(); 
  std::cout << "Expecting \"back\":  ";
  test9.print_e();
  std::cout << "Expecting nothing:  ";
  test9.print_key_val_str();

  std::string_view test_10("neutralizatio");
  mmap_lib::str test10(test_10);
  std::cout << "Expecting \"neut\":  ";
  test10.print_PoS(); 
  std::cout << "Expecting \"ralizatio\":  ";
  test10.print_e();
  std::cout << "Expecting nothing:  ";
  test10.print_key_val_str();
}


int main(int argc, char **argv) {

  mmap_pstr("bench_map_use_mmap.data");
  return 0;
}
