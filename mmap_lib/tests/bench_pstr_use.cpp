#include "fmt/format.h"
#include "iassert.hpp"
#include <vector>
#include "lrand.hpp"
#include "lbench.hpp"
#include "flat_hash_map.hpp"
#include <type_traits>
#include "mmap_str.hpp"
#include "mmap_map.hpp"

void test_ctor(mmap_lib::str ts, const char* rs) {
  std::cout << "> Test Case: str(\"" << rs << "\")" << std::endl;
  std::cout << "  "; ts.print_PoS(); 
  std::cout << "  "; ts.print_e();
  std::cout << "  "; ts.print_StrMap();
  std::cout << "  "; ts.print_StrVec();
  std::cout << std::endl;
}

void mmap_pstr_ctor_tests() { 
  std::cout << "================================ " << std::endl;
  std::cout << "Constructor 1 (size < 14) Tests: " << std::endl;
  std::cout << "================================ " << std::endl;
  
  test_ctor(mmap_lib::str("hello"), "hello"); 
  test_ctor(mmap_lib::str("cat"), "cat");
  test_ctor(mmap_lib::str("abcd"), "abcd");
  test_ctor(mmap_lib::str("feedback"), "feedback"); 
  test_ctor(mmap_lib::str("neutralizatio"), "neutralizatio");
  
  std::cout << "================================ " << std::endl;
  std::cout << "Constructor 2 (size >=14) Tests: " << std::endl;
  std::cout << "================================ " << std::endl;

  test_ctor(mmap_lib::str("01words23456789"), "01words23456789");
  test_ctor(mmap_lib::str("98words76543210"), "98words76543210");
  test_ctor(mmap_lib::str("01sloan23456789"), "01sloan23456789");
  test_ctor(mmap_lib::str("01andy23456789"), "01andy23456789");
  test_ctor(mmap_lib::str("hisloanbuzzball"), "hisloanbuzzball");
  test_ctor(mmap_lib::str("--this_var_will_bee_very_longbuzzball")
                         , "--this_var_will_bee_very_longbuzzball");
  test_ctor(mmap_lib::str("hidifferentbuzzball"), "hidifferentbuzzball");

  std::cout << "================================ " << std::endl;
  std::cout << "Constructor 3 (string_view) Tests: " << std::endl;
  std::cout << "================================ " << std::endl;

  test_ctor(mmap_lib::str(std::string_view("hello")), "hello"); 
  test_ctor(mmap_lib::str(std::string_view("cat")), "cat"); 
  test_ctor(mmap_lib::str(std::string_view("abcd")), "abcd"); 
  test_ctor(mmap_lib::str(std::string_view("feedback")), "feedback"); 
  test_ctor(mmap_lib::str(std::string_view("neutralizatio")), "neutralizatio"); 
  test_ctor(mmap_lib::str(std::string_view("neutralization")), "neutralization"); 
  test_ctor(mmap_lib::str(std::string_view("01andy23456789")), "01andy23456789"); 
  test_ctor(mmap_lib::str(std::string_view("--this_var_will_bee_very_longbuzzball"))
                         , "--this_var_will_bee_very_longbuzzball");

  /*
  std::cout << "> Test 17: str(\"lonng_andalsjdfkajsdkfljkalsjdfkljaskldjfklajdkslfjalsdjfllaskdfjklajskdlfjklasjdfljasdklfjklasjdflasjdflkajsdflkjakljdfkljaldjfkjakldsjfjaklsjdfjklajsdfjaklsfasjdklfjklajskdljfkljlaksjdklfjlkajsdklfjkla01words23456789\") " << std::endl;
  mmap_lib::str test17("lonng_andalsjdfkajsdkfljkalsjdfkljaskldjfklajdkslfjalsdjfllaskdfjklajskdlfjklasjdfljasdklfjklasjdflasjdflkajsdflkjakljdfkljaldjfkjakldsjfjaklsjdfjklajsdfjaklsfasjdklfjklajskdljfkljlaksjdklfjlkajsdklfjkla01words23456789");
  */
}

#if 0
template<std::size_t N>
void test_eq(mmap_lib::str ts, const char (&rs)[N], bool ans) {
  return (ts == rs) == ans;
}
#endif

int main(int argc, char **argv) {
  mmap_pstr_ctor_tests();
}
