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

  std::cout << "================================ " << std::endl;
  std::cout << "Constructor 2 (size >=14) Tests: " << std::endl;
  std::cout << "================================ " << std::endl;
  
  std::cout << "> Test 11: str(\"01words23456789\") " << std::endl;
  mmap_lib::str test11("01words23456789");
  std::cout << "Expecting \"0 \":  ";
  test11.print_PoS(); 
  std::cout << "Expecting \"0123456789\":  ";
  test11.print_e();
  std::cout << "Expecting key:0 val:5  string: words  ";
  test11.print_key_val_str();

  std::cout << "> Test 12: str(\"98words76543210\") " << std::endl;
  mmap_lib::str test12("98words76543210");
  std::cout << "Expecting \"0 \":  ";
  test12.print_PoS(); 
  std::cout << "Expecting \"9876543210\":  ";
  test12.print_e();
  std::cout << "Expecting key:0 val:5  string: words  ";
  test12.print_key_val_str();

  std::cout << "> Test 13: str(\"01sloan23456789\") " << std::endl;
  mmap_lib::str test13("01sloan23456789");
  std::cout << "Expecting \"5 \":  ";
  test13.print_PoS(); 
  std::cout << "Expecting \"0123456789\":  ";
  test13.print_e();
  std::cout << "Expecting key:5 val:5  string: sloan  ";
  test13.print_key_val_str();
  
  std::cout << "> Test 14: str(\"01andy23456789\") " << std::endl;
  mmap_lib::str test14("01andy23456789");
  std::cout << "Expecting \"10 \":  ";
  test14.print_PoS(); 
  std::cout << "Expecting \"0123456789\":  ";
  test14.print_e();
  std::cout << "Expecting key:10 val:4  string: andy  ";
  test14.print_key_val_str();

  std::cout << "> Test 15: str(\"hisloanbuzzball\") " << std::endl;
  mmap_lib::str test15("hisloanbuzzball");
  std::cout << "Expecting \"5 \":  ";
  test15.print_PoS(); 
  std::cout << "Expecting \"hibuzzball\":  ";
  test15.print_e();
  std::cout << "Expecting key:5 val:5  string: sloan  ";
  test15.print_key_val_str();
  
  std::cout << "> Test 16: str(\"--this_var_will_bee_very_longbuzzball\") " << std::endl;
  mmap_lib::str test16("--this_var_will_bee_very_longbuzzball");
  std::cout << "Expecting \"14 \":  ";
  test16.print_PoS(); 
  std::cout << "Expecting \"--buzzball\":  ";
  test16.print_e();
  std::cout << "Expecting key:14 val:27  string: this_var_will_bee_very_long  ";
  test16.print_key_val_str();

  std::cout << "> Test 17: str(\"lonng_andalsjdfkajsdkfljkalsjdfkljaskldjfklajdkslfjalsdjfllaskdfjklajskdlfjklasjdfljasdklfjklasjdflasjdflkajsdflkjakljdfkljaldjfkjakldsjfjaklsjdfjklajsdfjaklsfasjdklfjklajskdljfkljlaksjdklfjlkajsdklfjkla01words23456789\") " << std::endl;
  mmap_lib::str test17("lonng_andalsjdfkajsdkfljkalsjdfkljaskldjfklajdkslfjalsdjfllaskdfjklajskdlfjklasjdfljasdklfjklasjdflasjdflkajsdflkjakljdfkljaldjfkjakldsjfjaklsjdfjklajsdfjaklsfasjdklfjklajskdljfkljlaksjdklfjlkajsdklfjkla01words23456789");
  std::cout << "Expecting \"41 \":  ";
  test17.print_PoS(); 
  std::cout << "Expecting \"lo23456789\":  ";
  test17.print_e();
  std::cout << "Expecting key:41 val:... string: ... ";
  test17.print_key_val_str();

  std::cout << "> Test 18: str(\"hidifferentbuzzball\") " << std::endl;
  mmap_lib::str test18("hidifferentbuzzball");
  std::cout << "Expecting \"249 \":  ";
  test18.print_PoS(); 
  std::cout << "Expecting \"hibuzzball\":  ";
  test18.print_e();
  std::cout << "Expecting key:249 val:9  string: different  ";
  test18.print_key_val_str();
 
  std::cout << "================================================= " << std::endl;
  std::cout << "Constructor 3 (string view with size >=14) Tests: " << std::endl;
  std::cout << "================================================= " << std::endl;

  std::string_view test_19("neutralization");
  mmap_lib::str test19(test_19);
  std::cout << "Expecting \"258\":  ";
  test19.print_PoS(); 
  std::cout << "Expecting \"nelization\":  ";
  test19.print_e();
  std::cout << "Expecting key: 258 value :4 string:utra ";
  test19.print_key_val_str();

  std::string_view test_20("01andy23456789");
  mmap_lib::str test20(test_20);
  std::cout << "Expecting \"10\":  ";
  test20.print_PoS(); 
  std::cout << "Expecting \"0123456789\":  ";
  test20.print_e();
  std::cout << "Expecting key: 10 value :4 string:andy ";
  test20.print_key_val_str();
}


int main(int argc, char **argv) {

  mmap_pstr("bench_map_use_mmap.data");
  return 0;
}
