#include <type_traits>
#include <vector>

#include "flat_hash_map.hpp"
#include "fmt/format.h"
#include "iassert.hpp"
#include "lbench.hpp"
#include "lrand.hpp"
#include "mmap_map.hpp"
#include "mmap_str.hpp"

#define CTOR_TESTS        0
#define NEEQ_TESTS        0
#define STARTS_WITH_TESTS 0
#define FIND              0
#define BENCH             1

#if 0
//implicitly changes ts to string_view
void test_sview(const char * ts) {
  std::cout << "> Test Case: str(\"" << ts << "\")" << std::endl;
  mmap_lib::str test11(ts);
  std::cout << "  "; test11.print_PoS(); 
  std::cout << "  "; test11.print_e();
  std::cout << "  "; test11.print_StrMap();
  std::cout << "  "; test11.print_StrVec();
  std::cout << std::endl;
}
#endif

void test_ctor(mmap_lib::str ts, const char* rs) {
  std::cout << "> Test Case: str(\"" << rs << "\")" << std::endl;
  std::cout << "  ";
  ts.print_PoS();
  std::cout << "  ";
  ts.print_e();
  std::cout << "  ";
  ts.print_StrMap();
  std::cout << "  ";
  ts.print_StrVec();
  std::cout << std::endl;
}

void mmap_pstr_ctor_tests() {
  std::cout << "================================ " << std::endl;
  std::cout << "Constructor 1 (size < 14) Tests: " << std::endl;
  std::cout << "================================ " << std::endl;

  test_ctor(mmap_lib::str("hello"), "hello");
  test_ctor(mmap_lib::str("cat"), "cat");
  test_ctor(mmap_lib::str("four"), "four");
  test_ctor(mmap_lib::str("feedback"), "feedback");
  test_ctor(mmap_lib::str("natural_fries"), "natural_fries");

  std::cout << "================================ " << std::endl;
  std::cout << "Constructor 2 (size >=14) Tests: " << std::endl;
  std::cout << "================================ " << std::endl;

  test_ctor(mmap_lib::str("0_words_1234567"), "0_words_1234567");
  test_ctor(mmap_lib::str("7_words_6543210"), "7_words_6543210");
  test_ctor(mmap_lib::str("0_sloan_1234567"), "0_sloan_1234567");
  test_ctor(mmap_lib::str("0_tang_1234567"), "0_tang_1234567");
  test_ctor(mmap_lib::str("hisloanbuzzball"), "hisloanbuzzball");
  test_ctor(mmap_lib::str("--this_var_will_bee_very_longbuzzball"), "--this_var_will_bee_very_longbuzzball");
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
  test_ctor(mmap_lib::str(std::string_view("--this_var_will_bee_very_longbuzzball")), "--this_var_will_bee_very_longbuzzball");

  std::cout << "================================== " << std::endl;
  std::cout << "Constructor 3 (const char *) Tests: " << std::endl;
  std::cout << "================================== " << std::endl;

  const char* hello    = "hello";
  const char* cat      = "cat";
  const char* abcd     = "abcd";
  const char* feedback = "feedback";
  const char* n13      = "neutralizatio";
  const char* n14      = "neutralization";
  const char* andy     = "01andy23456789";
  const char* lvar     = "--this_var_will_bee_very_longbuzzball";

  test_ctor(mmap_lib::str(hello), "hello");
  test_ctor(mmap_lib::str(cat), "cat");
  test_ctor(mmap_lib::str(abcd), "abcd");
  test_ctor(mmap_lib::str(feedback), "feedback");
  test_ctor(mmap_lib::str(n13), "neutralizatio");
  test_ctor(mmap_lib::str(n14), "neutralization");
  test_ctor(mmap_lib::str(andy), "01andy23456789");
  test_ctor(mmap_lib::str(lvar), "--this_var_will_bee_very_longbuzzball");
  /*
  std::cout << "> Test 17:
  str(\"lonng_andalsjdfkajsdkfljkalsjdfkljaskldjfklajdkslfjalsdjfllaskdfjklajskdlfjklasjdfljasdklfjklasjdflasjdflkajsdflkjakljdfkljaldjfkjakldsjfjaklsjdfjklajsdfjaklsfasjdklfjklajskdljfkljlaksjdklfjlkajsdklfjkla01words23456789\")
  " << std::endl; mmap_lib::str
  test17("lonng_andalsjdfkajsdkfljkalsjdfkljaskldjfklajdkslfjalsdjfllaskdfjklajskdlfjklasjdfljasdklfjklasjdflasjdflkajsdflkjakljdfkljaldjfkjakldsjfjaklsjdfjklajsdfjaklsfasjdklfjklajskdljfkljlaksjdklfjlkajsdklfjkla01words23456789");
  */
}

template <std::size_t N>
bool test_eq(mmap_lib::str ts, const char (&rs)[N], bool ans) {
  return (ts == rs) == ans;
}

template <std::size_t N>
bool test_neq(mmap_lib::str ts, const char (&rs)[N], bool ans) {
  return (ts != rs) == ans;
}

bool test_eq(mmap_lib::str ls, mmap_lib::str rs, bool ans) { return (ls == rs) == ans; }

bool test_neq(mmap_lib::str ls, mmap_lib::str rs, bool ans) { return (ls != rs) == ans; }

/*
bool test_eq(mmap_lib::str ls, const char* rs, bool ans) {
  return (ls == rs) == ans;
}

bool test_neq(mmap_lib::str ls, const char* rs, bool ans) {
  return (ls != rs) == ans;
}
*/

bool test_eq(mmap_lib::str ls, std::string_view rs, bool ans) { return (ls == rs) == ans; }

bool test_neq(mmap_lib::str ls, std::string_view rs, bool ans) { return (ls != rs) == ans; }

bool test_starts_with(mmap_lib::str ls, mmap_lib::str rs, bool ans) { return ls.starts_with(rs) == ans; }

bool test_starts_with(mmap_lib::str ls, std::string_view rs, bool ans) { return ls.starts_with(rs) == ans; }

void pstrVchar_eqeq_tests() {
  std::cout << "pstr vs. char Operator == Tests: ";
  mmap_lib::str hello("hello");
  mmap_lib::str hi("hi");
  mmap_lib::str hello_world("hello_!_world");
  mmap_lib::str micro("micro-architecture");
  mmap_lib::str foo("--foo1234567890!!!");

  uint8_t r = 0u, t = 0u;
  r += test_eq(hello, "hello", true);
  ++t;
  r += test_eq(hello, "hi", false);
  ++t;
  r += test_eq(hi, "hi", true);
  ++t;
  r += test_eq(hi, "hello", false);
  ++t;
  r += test_eq(hello_world, "hello_!_world", true);
  ++t;
  r += test_eq(hello_world, "hi", false);
  ++t;
  r += test_eq(hello, "hello_!_word", false);
  ++t;
  r += test_eq(micro, "micro-architecture", true);
  ++t;
  r += test_eq(foo, "micro-architecture", false);
  ++t;
  r += test_eq(foo, "--foo1234567890!!!", true);
  ++t;
  r += test_eq(micro, "micro-architecture", true);
  ++t;
  r += test_eq(micro, "hi", false);
  ++t;
  r += test_eq(hi, "micro-architecture", false);
  ++t;
  printf("passed(%02d/%02d), failed(%02d/%02d)\n", r, t, t - r, t);
}

void pstrVchar_noeq_tests() {
  std::cout << "pstr vs. char Operator != Tests: ";
  mmap_lib::str hello("hello");
  mmap_lib::str hi("hi");
  mmap_lib::str hello_world("hello_!_world");
  mmap_lib::str amethysts("amethysts_emeralds");
  mmap_lib::str balloon("_balloon_223344!!!");

  uint8_t r = 0u, t = 0u;
  r += test_neq(hello, "hi", true);
  ++t;
  r += test_neq(hello, "hello", false);
  ++t;
  r += test_neq(hello_world, "hi", true);
  ++t;
  r += test_neq(hello_world, "hello_!_world", false);
  ++t;
  r += test_neq(amethysts, "_balloon_223344!!!", true);
  ++t;
  r += test_neq(balloon, "_balloon_223344!!!", false);
  ++t;
  r += test_neq(hello, "amethysts_emeralds", true);
  ++t;
  r += test_neq(amethysts, "hi", true);
  ++t;
  printf("passed(%02d/%02d), failed(%02d/%02d)\n", r, t, t - r, t);
}

void pstrVpstr_eqeq_tests() {
  std::cout << "pstr vs. pstr Operator == Tests: ";
  mmap_lib::str hello("hello");
  mmap_lib::str hi("hi");
  mmap_lib::str hello_world("hello_!_world");
  mmap_lib::str micro("micro-architecture");
  mmap_lib::str foo("--foo1234567890!!!");

  uint8_t r = 0u, t = 0u;
  r += test_eq(hello, hello, true);
  ++t;
  r += test_eq(hello, hi, false);
  ++t;
  r += test_eq(hi, hi, true);
  ++t;
  r += test_eq(hi, hello, false);
  ++t;
  r += test_eq(hello_world, hello_world, true);
  ++t;
  r += test_eq(hello_world, hi, false);
  ++t;
  r += test_eq(hello, hello_world, false);
  ++t;
  r += test_eq(micro, micro, true);
  ++t;
  r += test_eq(foo, micro, false);
  ++t;
  r += test_eq(foo, foo, true);
  ++t;
  r += test_eq(micro, micro, true);
  ++t;
  r += test_eq(micro, hi, false);
  ++t;
  r += test_eq(hi, micro, false);
  ++t;
  printf("passed(%02d/%02d), failed(%02d/%02d)\n", r, t, t - r, t);
}

void pstrVpstr_noeq_tests() {
  std::cout << "pstr vs. pstr Operator != Tests: ";
  mmap_lib::str hello("hello");
  mmap_lib::str hi("hi");
  mmap_lib::str hello_world("hello_!_world");
  mmap_lib::str amethysts("amethysts_emeralds");
  mmap_lib::str balloon("_balloon_223344!!!");

  uint8_t r = 0u, t = 0u;
  r += test_neq(hello, hi, true);
  ++t;
  r += test_neq(hello, hello, false);
  ++t;
  r += test_neq(hello_world, hi, true);
  ++t;
  r += test_neq(hello_world, hello_world, false);
  ++t;
  r += test_neq(amethysts, balloon, true);
  ++t;
  r += test_neq(balloon, balloon, false);
  ++t;
  r += test_neq(hello, amethysts, true);
  ++t;
  r += test_neq(amethysts, hi, true);
  ++t;
  printf("passed(%02d/%02d), failed(%02d/%02d)\n", r, t, t - r, t);
}

void pstrVcstr_eqeq_tests() {
  std::cout << "pstr vs. cstr Operator == Tests: ";
  mmap_lib::str hello("hello");
  mmap_lib::str hi("hi");
  mmap_lib::str hello_world("hello_!_world");
  mmap_lib::str micro("micro-architecture");
  mmap_lib::str foo("--foo1234567890!!!");
  const char*   chello       = "hello";
  const char*   chi          = "hi";
  const char*   chello_world = "hello_!_world";
  const char*   cmicro       = "micro-architecture";
  const char*   cfoo         = "--foo1234567890!!!";

  uint8_t r = 0u, t = 0u;
  r += test_eq(hello, chello, true);
  ++t;
  r += test_eq(hello, chi, false);
  ++t;
  r += test_eq(hi, chi, true);
  ++t;
  r += test_eq(hi, chello, false);
  ++t;
  r += test_eq(hello_world, chello_world, true);
  ++t;
  r += test_eq(hello_world, chi, false);
  ++t;
  r += test_eq(hello, chello_world, false);
  ++t;
  r += test_eq(micro, cmicro, true);
  ++t;
  r += test_eq(foo, cmicro, false);
  ++t;
  r += test_eq(foo, cfoo, true);
  ++t;
  r += test_eq(micro, cmicro, true);
  ++t;
  r += test_eq(micro, chi, false);
  ++t;
  r += test_eq(hi, cmicro, false);
  ++t;
  printf("passed(%02d/%02d), failed(%02d/%02d)\n", r, t, t - r, t);
}

void pstrVcstr_noeq_tests() {
  std::cout << "pstr vs. cstr Operator != Tests: ";
  mmap_lib::str hello("hello");
  mmap_lib::str hi("hi");
  mmap_lib::str hello_world("hello_!_world");
  mmap_lib::str amethysts("amethysts_emeralds");
  mmap_lib::str balloon("_balloon_223344!!!");
  const char*   chello       = "hello";
  const char*   chi          = "hi";
  const char*   chello_world = "hello_!_world";
  const char*   camethysts   = "amethysts_emeralds";
  const char*   cballoon     = "_balloon_223344!!!";

  uint8_t r = 0u, t = 0u;
  r += test_neq(hello, chi, true);
  ++t;
  r += test_neq(hello, chello, false);
  ++t;
  r += test_neq(hello_world, chi, true);
  ++t;
  r += test_neq(hello_world, chello_world, false);
  ++t;
  r += test_neq(amethysts, cballoon, true);
  ++t;
  r += test_neq(balloon, cballoon, false);
  ++t;
  r += test_neq(hello, camethysts, true);
  ++t;
  r += test_neq(amethysts, chi, true);
  ++t;
  printf("passed(%02d/%02d), failed(%02d/%02d)\n", r, t, t - r, t);
}
void pstr_at_operator() {
  std::cout << "pstr_at_operator Operator [] Tests: ";
  mmap_lib::str hello("hello");
  mmap_lib::str hi("hi");
  mmap_lib::str hello_world("hello_!_world");
  mmap_lib::str micro("micro-architecture");
  mmap_lib::str foo("--foo1234567890!!!");
  uint8_t       p = 0u, f = 0u;
  (hello[2] == 'l') ? p++ : f++;
  (hello[0] == 'h') ? p++ : f++;
  (hello[1] == 'e') ? p++ : f++;
  (hi[0] == 'h') ? p++ : f++;
  (hi[1] == 'i') ? p++ : f++;
  (micro[1] == 'i') ? p++ : f++;
  (micro[5] == '-') ? p++ : f++;
  (micro[10] == 'i') ? p++ : f++;
  (micro[9] == 'h') ? p++ : f++;
  (micro[17] == 'e') ? p++ : f++;
  (micro[3] == 'r') ? p++ : f++;
  (micro[2] == 'c') ? p++ : f++;
  (foo[0] == '-') ? p++ : f++;
  (foo[1] == '-') ? p++ : f++;
  printf("passed(%02d/%02d), failed(%02d/%02d)\n", p, p + f, f, p + f);
}

void pstr_isI() {
  std::cout << "pstr_isI Tests: ";
  mmap_lib::str hello("hello");
  mmap_lib::str eight("888888");
  mmap_lib::str neg_one("-111111");
  mmap_lib::str not_i("123g5");
  mmap_lib::str not_int("-1234f");
  mmap_lib::str zero("-1234f");
  mmap_lib::str num_float("12.34");
  uint8_t       p = 0u, f = 0u;
  (hello.is_i() == false) ? p++ : f++;
  (eight.is_i() == true) ? p++ : f++;
  (neg_one.is_i() == true) ? p++ : f++;
  (not_i.is_i() == false) ? p++ : f++;
  (not_int.is_i() == false) ? p++ : f++;
  (zero.is_i() == false) ? p++ : f++;
  (num_float.is_i() == false) ? p++ : f++;
  printf("passed(%02d/%02d), failed(%02d/%02d)\n", p, p + f, f, p + f);
}

void pstr_starts_with() {
  uint8_t t = 0u, r = 0u;
#if 1
  mmap_lib::str whole("foobar");
  mmap_lib::str front1("foo");
  mmap_lib::str front2("bar");
  mmap_lib::str same("foobar");
  mmap_lib::str one("f");
  mmap_lib::str two("g");

  r += test_starts_with(whole, front1, true);
  ++t;
  r += test_starts_with(whole, front2, false);
  ++t;
  r += test_starts_with(whole, same, true);
  ++t;
  r += test_starts_with(whole, one, true);
  ++t;
  r += test_starts_with(whole, two, false);
  ++t;

  mmap_lib::str whole2("hello_!_world");
  mmap_lib::str front3("hello_!_worl");
  mmap_lib::str almost("hello_!_worl!");
  mmap_lib::str front4("johnny");
  mmap_lib::str same2("hello_!_world");
  mmap_lib::str three("h");
  mmap_lib::str four("he");
  mmap_lib::str empty("");

  r += test_starts_with(whole2, front3, true);
  ++t;
  r += test_starts_with(whole2, front4, false);
  ++t;
  r += test_starts_with(whole2, almost, false);
  ++t;
  r += test_starts_with(whole2, same2, true);
  ++t;
  r += test_starts_with(whole2, three, true);
  ++t;
  r += test_starts_with(whole2, four, true);
  ++t;
  r += test_starts_with(whole2, empty, true);
  ++t;
#endif
  mmap_lib::str whole3("--this_var_will_be_very_long_for_testing_12345");
  mmap_lib::str front5("--this_var");
  mmap_lib::str spec1("-");
  mmap_lib::str spec2("--");
  mmap_lib::str spec3("--t");
  mmap_lib::str spec4("--th");
  mmap_lib::str front6("--this_var_wi");
  mmap_lib::str front7("--this_var_wil");
  mmap_lib::str front8("--this_var_will_be_very_");
  mmap_lib::str same3("--this_var_will_be_very_long_for_testing_12345");
  mmap_lib::str almost2("--this_var_will_be_very_long_for_testing_12346");
  mmap_lib::str five("-");
  mmap_lib::str six("balalalalalalalalala");

  // long vs short
  r += test_starts_with(whole3, front5, true);
  ++t;  // fixed

  // long vs short(13)
  r += test_starts_with(whole3, front6, true);
  ++t;  // fixed

  // long vs long(14)
  r += test_starts_with(whole3, front7, true);
  ++t;  // fixed

  // long vs long
  r += test_starts_with(whole3, front8, true);
  ++t;  // fixed

  // long vs long (same length)
  r += test_starts_with(whole3, same3, true);
  ++t;

  // long vs long (same length)
  r += test_starts_with(whole3, almost2, false);
  ++t;
  r += test_starts_with(whole3, five, true);
  ++t;  // flagged, triggered "should not be here"
  r += test_starts_with(whole3, six, false);
  ++t;

  r += test_starts_with(whole3, spec1, true);
  ++t;  // fixed
  r += test_starts_with(whole3, spec2, true);
  ++t;  // fixed
  r += test_starts_with(whole3, spec3, true);
  ++t;  // fixed
  r += test_starts_with(whole3, spec4, true);
  ++t;  // fixed

#if 0  
  mmap_lib::str whole("foobar");
  std::string_view front1("foo");
  std::string_view front2("bar");
  std::string_view same("foobar");
  std::string_view one("f");
  std::string_view two("g");
  
  r += test_starts_with(whole, front1, true); ++t;
  r += test_starts_with(whole, front2, false); ++t;
  r += test_starts_with(whole, same, true); ++t;
  r += test_starts_with(whole, one, true); ++t;
  r += test_starts_with(whole, two, false); ++t;

  mmap_lib::str whole2("hello_!_world");
  std::string_view front3("hello_!_worl");
  std::string_view almost("hello_!_worl!");
  std::string_view front4("johnny");
  std::string_view same2("hello_!_world");
  std::string_view three("h");
  std::string_view four("he");
  std::string_view empty("");

  r += test_starts_with(whole2, front3, true); ++t;
  r += test_starts_with(whole2, front4, false); ++t;
  r += test_starts_with(whole2, almost, false); ++t;
  r += test_starts_with(whole2, same2, true); ++t;
  r += test_starts_with(whole2, three, true); ++t;
  r += test_starts_with(whole2, four, true); ++t;
  r += test_starts_with(whole2, empty, true); ++t;
  
  mmap_lib::str whole3("--this_var_will_be_very_long_for_testing_12345");
  std::string_view front5("--this_var");
  std::string_view front6("--this_var_wi");
  std::string_view front7("--this_var_wil");
  std::string_view front8("--this_var_will_be_very_");
  std::string_view same3("--this_var_will_be_very_long_for_testing_12345");
  std::string_view almost2("--this_var_will_be_very_long_for_testing_12346");
  std::string_view five("-");
  std::string_view six("balalalalalalalalala");
  
  // long vs short
  r += test_starts_with(whole3, front5, true); ++t; //fixed
  // long vs short(13)
  r += test_starts_with(whole3, front6, true); ++t; //fixed
  // long vs long(14)
  r += test_starts_with(whole3, front7, true); ++t; //fixed
  // long vs long
  r += test_starts_with(whole3, front8, true); ++t; //fixed
  // long vs long (same length)
  r += test_starts_with(whole3, same3, true); ++t;
  // long vs long (same length)
  r += test_starts_with(whole3, almost2, false); ++t;
  r += test_starts_with(whole3, five, true); ++t; //flagged, triggered "should not be here"
  r += test_starts_with(whole3, six, false); ++t;
#endif
  printf("passed(%02d/%02d), failed(%02d/%02d)\n", r, t, t - r, t);
}

void bench_str_cmp() { 
  {
    Lbench b("bench_str_cmp");

    Lrand_range<char>     ch(33, 126);
    Lrand_range<uint16_t> sz(1, 80);

    std::vector<mmap_lib::str> v;
   
    for (auto i = 0u; i < 1e4; ++i) {
      auto          s = sz.any();
      mmap_lib::str tmp;
      for (auto j = 0; j < s; ++j) {
        tmp.append(ch.any());
      }
      v.emplace_back(tmp);
    }

   
    int conta = 0;
    for (auto i = 0u; i < 1e4; ++i) {
      for (auto j = 0u; j < 1e4; ++j) {
        if (v[i] == v[j])
          conta++;
      }
    }

    fmt::print("bench_str_cmp conta:{}\n", conta);
    
  }

  {
    Lbench b("bench_string_cmp");

    Lrand_range<char>     ch(33, 126);
    Lrand_range<uint16_t> sz(1, 80);

    std::vector<mmap_lib::str> v;
    for (auto i = 0u; i < 1e4; ++i) {
      auto        s = sz.any();
      std::string str;
      for (auto j = 0; j < s; ++j) {
        str.append(1, ch.any());
      }
      v.emplace_back(str);
    }

    int conta = 0;
    for (auto i = 0u; i < 1e4; ++i) {
      for (auto j = 0u; j < 1e4; ++j) {
        if (v[i] == v[j])
          conta++;
      }
    }

    fmt::print("bench_string_cmp conta:{}\n", conta);
  }
}

int main(int argc, char** argv) {
#if BENCH
  bench_str_cmp();
#endif

#if CTOR_TESTS
  mmap_pstr_ctor_tests();
#endif

#if NEEQ_TESTS
  std::cout << "==========================" << std::endl;
  pstrVchar_eqeq_tests();
  pstrVchar_noeq_tests();
  pstrVpstr_eqeq_tests();
  pstrVpstr_noeq_tests();
  pstrVcstr_eqeq_tests();
  pstrVcstr_noeq_tests();
  pstr_at_operator();
  pstr_isI();
  std::cout << "==========================" << std::endl;
#endif

#if FIND
  mmap_lib::str one("abhimnopq");
  mmap_lib::str two("hi");
  int           result = one.rfind(two);
  printf("The result is %d \n", result);
#endif

#if STARTS_WITH_TESTS
  pstr_starts_with();
#endif

  /*
   mmap_lib::str ts("hello");
   const char *t2 = "hello";
   const char (&t3)[6] = "hello";
   std::string t5 = "hello";
   std::string_view t4("hello");

   if (test_eq(ts, t5.c_str(), true)) { printf("match\n"); }
   else { printf("miss\n"); }
   */
  return 0;
}
