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
#define AT_ISI            0
#define STARTS_WITH       0
#define BENCH             1
#define WHITEBOARD        0

#if CTOR_TESTS
template<int m_id>
void test_ctor(mmap_lib::str<m_id> ts, const char* rs) {
  std::cout << "> Test Case: str<" << ts.get_map_id() << ">(\"" << rs << "\")" << std::endl;
  std::cout << "  ";
  ts.print_PoS();
  std::cout << "  ";
  ts.print_e();
  std::cout << "  ";
  ts.print_StrMap();
  std::cout << std::endl;
}

void mmap_pstr_ctor_tests() {
  std::cout << "Warming Up";

  mmap_lib::str<0> baa("hello");
  mmap_lib::str<0> bab("12-micro-34567890");
  mmap_lib::str<0> bac("flower");
  mmap_lib::str<0> bad("--foobarbazball!");
  std::cout << ". ";
  mmap_lib::str<1> bae("microarchitecture");
  mmap_lib::str<1> baf("12-roach-34567890");
  mmap_lib::str<1> bag("abcdefghijklmnop");
  mmap_lib::str<1> bah("--Joker-34567890");
  mmap_lib::str<1> bai("12-micro-34567890");
  std::cout << ". ";
  mmap_lib::str<2> baj("12-Andy-34567890");
  mmap_lib::str<2> bak("12-Base-34567890");
  mmap_lib::str<2> bal("12-Reed-34567890");
  mmap_lib::str<2> bam("zyxwvutsrqponmlk");
  std::cout << ". ";
  mmap_lib::str<3> ban("12-Gorgon-34567890");
  mmap_lib::str<3> bao("12-drums-34567890");
  mmap_lib::str<3> bap("12-Johnathan-34567890");
  mmap_lib::str<3> baq("zyxwvutsrqponmlk");
  std::cout << ".\n";
  
  std::cout << "================================ " << std::endl;
  std::cout << "Constructor 1 (size < 14) Tests: " << std::endl;
  std::cout << "================================ " << std::endl;

  test_ctor(mmap_lib::str<0>("hello"), "hello");
  test_ctor(mmap_lib::str<1>("cat"), "cat");
  test_ctor(mmap_lib::str<2>("four"), "four");
  test_ctor(mmap_lib::str<3>("feedback"), "feedback");
  test_ctor(mmap_lib::str<1>("natural_fries"), "natural_fries");

  std::cout << "================================ " << std::endl;
  std::cout << "Constructor 2 (size >=14) Tests: " << std::endl;
  std::cout << "================================ " << std::endl;

  test_ctor(mmap_lib::str<0>("0_words_1234567"), "0_words_1234567");
  test_ctor(mmap_lib::str<1>("7_words_6543210"), "7_words_6543210");
  test_ctor(mmap_lib::str<2>("0_sloan_1234567"), "0_sloan_1234567");
  test_ctor(mmap_lib::str<3>("0_tang_1234567"), "0_tang_1234567");
  test_ctor(mmap_lib::str<0>("hisloanbuzzball"), "hisloanbuzzball");
  test_ctor(mmap_lib::str<1>("--this_var_will_bee_very_longbuzzball"), "--this_var_will_bee_very_longbuzzball");
  test_ctor(mmap_lib::str<2>("hidifferentbuzzball"), "hidifferentbuzzball");

  std::cout << "================================ " << std::endl;
  std::cout << "Constructor 3 (string_view) Tests: " << std::endl;
  std::cout << "================================ " << std::endl;

  test_ctor(mmap_lib::str<3>(std::string_view("hello")), "hello");
  test_ctor(mmap_lib::str<0>(std::string_view("cat")), "cat");
  test_ctor(mmap_lib::str<1>(std::string_view("abcd")), "abcd");
  test_ctor(mmap_lib::str<2>(std::string_view("feedback")), "feedback");
  test_ctor(mmap_lib::str<3>(std::string_view("neutralizatio")), "neutralizatio");
  test_ctor(mmap_lib::str<0>(std::string_view("neutralization")), "neutralization");
  test_ctor(mmap_lib::str<1>(std::string_view("01andy23456789")), "01andy23456789");
  test_ctor(mmap_lib::str<2>(std::string_view("--this_var_will_bee_very_longbuzzball")), "--this_var_will_bee_very_longbuzzball");

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

  test_ctor(mmap_lib::str<3>(hello), "hello");
  test_ctor(mmap_lib::str<0>(cat), "cat");
  test_ctor(mmap_lib::str<1>(abcd), "abcd");
  test_ctor(mmap_lib::str<2>(feedback), "feedback");
  test_ctor(mmap_lib::str<3>(n13), "neutralizatio");
  test_ctor(mmap_lib::str<0>(n14), "neutralization");
  test_ctor(mmap_lib::str<1>(andy), "01andy23456789");
  test_ctor(mmap_lib::str<2>(lvar), "--this_var_will_bee_very_longbuzzball");
}
#endif

#if NEEQ_TESTS
template <std::size_t N, int m_id>
bool test_eq(mmap_lib::str<m_id> ts, const char (&rs)[N], bool ans, uint8_t &cnt) {
  ++cnt;
  return (ts == rs) == ans;
}

template <std::size_t N, int m_id>
bool test_neq(mmap_lib::str<m_id> ts, const char (&rs)[N], bool ans, uint8_t &cnt) {
  ++cnt;
  return (ts != rs) == ans;
}

template<int m_id, int m_id2>
bool test_eq(mmap_lib::str<m_id> ls, mmap_lib::str<m_id2> rs, bool ans, uint8_t &cnt) { 
  ++cnt;
  return (ls == rs) == ans; 

}

template<int m_id, int m_id2>
bool test_neq(mmap_lib::str<m_id> ls, mmap_lib::str<m_id2> rs, bool ans, uint8_t &cnt) { 
  ++cnt;
  return (ls != rs) == ans; 
}

template<int m_id>
bool test_eq(mmap_lib::str<m_id> ls, std::string_view rs, bool ans, uint8_t &cnt) { 
  ++cnt;
  return (ls == rs) == ans; 
}

template<int m_id>
bool test_neq(mmap_lib::str<m_id> ls, std::string_view rs, bool ans, uint8_t &cnt) { 
  ++cnt;
  return (ls != rs) == ans; 
}

void pstrVchar_eqeq_tests() {
  std::cout << "pstr vs. char Operator == Tests: ";
  mmap_lib::str<1> hello("hello");
  mmap_lib::str<1> hi("hi");
  mmap_lib::str<1> hello_world("hello_!_world");
  mmap_lib::str<1> micro("micro-architecture");
  mmap_lib::str<1> foo("--foo1234567890!!!");

  uint8_t r = 0u, t = 0u;
  r += test_eq(hello, "hello", true, t);
  r += test_eq(hello, "hi", false, t);
  r += test_eq(hi, "hi", true, t);
  r += test_eq(hi, "hello", false, t);
  r += test_eq(hello_world, "hello_!_world", true, t);
  r += test_eq(hello_world, "hi", false, t);
  r += test_eq(hello, "hello_!_word", false, t);
  r += test_eq(micro, "micro-architecture", true, t);
  r += test_eq(foo, "micro-architecture", false, t);
  r += test_eq(foo, "--foo1234567890!!!", true, t);
  r += test_eq(micro, "micro-architecture", true, t);
  r += test_eq(micro, "hi", false, t);
  r += test_eq(hi, "micro-architecture", false, t);
  printf("passed(%02d/%02d), failed(%02d/%02d)\n", r, t, t - r, t);
}

void pstrVchar_noeq_tests() {
  std::cout << "pstr vs. char Operator != Tests: ";
  mmap_lib::str<2> hello("hello");
  mmap_lib::str<2> hi("hi");
  mmap_lib::str<2> hello_world("hello_!_world");
  mmap_lib::str<2> amethysts("amethysts_emeralds");
  mmap_lib::str<2> balloon("_balloon_223344!!!");

  uint8_t r = 0u, t = 0u;
  r += test_neq(hello, "hi", true, t);
  r += test_neq(hi, "bagel", true, t);
  r += test_neq(hello, "hello", false, t);
  r += test_neq(hello_world, "hi", true, t);
  r += test_neq(hello_world, "hello_!_world", false, t);
  r += test_neq(amethysts, "_balloon_223344!!!", true, t);
  r += test_neq(balloon, "_balloon_223344!!!", false, t);
  r += test_neq(hello, "amethysts_emeralds", true, t);
  r += test_neq(amethysts, "hi", true, t);
  printf("passed(%02d/%02d), failed(%02d/%02d)\n", r, t, t - r, t);
}

void pstrVpstr_eqeq_tests() {
  std::cout << "pstr vs. pstr Operator == Tests: ";
  mmap_lib::str<3> hello("hello");
  mmap_lib::str<3> hi("hi");
  mmap_lib::str<3> hello_world("hello_!_world");
  mmap_lib::str<3> micro("micro-architecture");
  mmap_lib::str<3> foo("--foo1234567890!!!");

  uint8_t r = 0u, t = 0u;
  r += test_eq(hello, hello, true, t);
  r += test_eq(hello, hi, false, t);
  r += test_eq(hi, hi, true, t);
  r += test_eq(hi, hello, false, t);
  r += test_eq(hello_world, hi, false, t);
  r += test_eq(hello, hello_world, false, t);
  r += test_eq(micro, micro, true, t);
  r += test_eq(foo, micro, false, t);
  r += test_eq(foo, foo, true, t);
  r += test_eq(micro, micro, true, t);
  r += test_eq(hi, micro, false, t);
  printf("passed(%02d/%02d), failed(%02d/%02d)\n", r, t, t - r, t);
}

void pstrVpstr_noeq_tests() {
  std::cout << "pstr vs. pstr Operator != Tests: ";
  mmap_lib::str<0> hello("hello");
  mmap_lib::str<0> hi("hi");
  mmap_lib::str<0> hello_world("hello_!_world");
  mmap_lib::str<0> amethysts("amethysts_emeralds");
  mmap_lib::str<0> balloon("_balloon_223344!!!");

  uint8_t r = 0u, t = 0u;
  r += test_neq(hello, hi, true, t);
  r += test_neq(hello, hello, false, t);
  r += test_neq(hello_world, hi, true, t);
  r += test_neq(hello_world, hello_world, false, t);
  r += test_neq(amethysts, balloon, true, t);
  r += test_neq(balloon, balloon, false, t);
  r += test_neq(hello, amethysts, true, t);
  r += test_neq(amethysts, hi, true, t);
  printf("passed(%02d/%02d), failed(%02d/%02d)\n", r, t, t - r, t);
}

void pstrVcstr_eqeq_tests() {
  std::cout << "pstr vs. cstr Operator == Tests: ";
  mmap_lib::str<1> hello("hello");
  mmap_lib::str<1> hi("hi");
  mmap_lib::str<1> hello_world("hello_!_world");
  mmap_lib::str<1> micro("micro-architecture");
  mmap_lib::str<1> foo("--foo1234567890!!!");
  const char*   chello       = "hello";
  const char*   chi          = "hi";
  const char*   chello_world = "hello_!_world";
  const char*   cmicro       = "micro-architecture";
  const char*   cfoo         = "--foo1234567890!!!";

  uint8_t r = 0u, t = 0u;
  r += test_eq(hello, chello, true, t);
  r += test_eq(hello, chi, false, t);
  r += test_eq(hi, chi, true, t);
  r += test_eq(hi, chello, false, t);
  r += test_eq(hello_world, chello_world, true, t);
  r += test_eq(hello_world, chi, false, t);
  r += test_eq(hello, chello_world, false, t);
  r += test_eq(micro, cmicro, true, t);
  r += test_eq(foo, cmicro, false, t);
  r += test_eq(foo, cfoo, true, t);
  r += test_eq(micro, cmicro, true, t);
  r += test_eq(micro, chi, false, t);
  r += test_eq(hi, cmicro, false, t);
  printf("passed(%02d/%02d), failed(%02d/%02d)\n", r, t, t - r, t);
}

void pstrVcstr_noeq_tests() {
  std::cout << "pstr vs. cstr Operator != Tests: ";
  mmap_lib::str<2> hello("hello");
  mmap_lib::str<2> hi("hi");
  mmap_lib::str<2> hello_world("hello_!_world");
  mmap_lib::str<2> amethysts("amethysts_emeralds");
  mmap_lib::str<2> balloon("_balloon_223344!!!");
  const char*   chello       = "hello";
  const char*   chi          = "hi";
  const char*   chello_world = "hello_!_world";
  const char*   camethysts   = "amethysts_emeralds";
  const char*   cballoon     = "_balloon_223344!!!";

  uint8_t r = 0u, t = 0u;
  r += test_neq(hello, chi, true, t);
  r += test_neq(hi, chi, false, t);
  r += test_neq(hi, chello, true, t);
  r += test_neq(hello, chello, false, t);
  r += test_neq(hello_world, chi, true, t);
  r += test_neq(hello_world, chello_world, false, t);
  r += test_neq(hello, chello_world, true, t);
  r += test_neq(amethysts, cballoon, true, t);
  r += test_neq(balloon, cballoon, false, t);
  r += test_neq(hello, camethysts, true, t);
  r += test_neq(amethysts, chi, true, t);
  printf("passed(%02d/%02d), failed(%02d/%02d)\n", r, t, t - r, t);
}

void cross_template_compare_tests() {
  std::cout << "pstr vs. pstr Cross template Compare Tests: ";
  mmap_lib::str<1> a1("i-am-tired-today=(");
  mmap_lib::str<2> a2("i-am-tired-today=(");
  mmap_lib::str<3> a3("i-am-tired-today=(");
  mmap_lib::str<1> b1("i-am-tired-today>>");
  mmap_lib::str<2> b2("i-am-tired-today>>");
  mmap_lib::str<3> b3("i-am-tired-today>>");
  
  uint8_t r = 0u, t = 0u;
  r += test_eq(a1, a2, true, t);
  r += test_eq(a1, a3, true, t);
  r += test_eq(a2, a1, true, t);
  r += test_eq(a2, a3, true, t);
  r += test_eq(a3, a1, true, t);
  r += test_eq(a3, a2, true, t);
  r += test_eq(b1, b2, true, t);
  r += test_eq(b1, b3, true, t);
  r += test_eq(b2, b1, true, t);
  r += test_eq(b2, b3, true, t);
  r += test_eq(b3, b1, true, t);
  r += test_eq(b3, b2, true, t);
  r += test_neq(a1, b1, true, t);
  r += test_neq(a1, b2, true, t);
  r += test_neq(a1, b3, true, t);
  r += test_neq(a2, b1, true, t);
  r += test_neq(a2, b2, true, t);
  r += test_neq(a2, b3, true, t);
  r += test_neq(a3, b1, true, t);
  r += test_neq(a3, b2, true, t);
  r += test_neq(a3, b3, true, t);
  r += test_neq(b1, a1, true, t);
  r += test_neq(b1, a2, true, t);
  r += test_neq(b1, a3, true, t);
  r += test_neq(b2, a1, true, t);
  r += test_neq(b2, a2, true, t);
  r += test_neq(b2, a3, true, t);
  r += test_neq(b3, a1, true, t);
  r += test_neq(b3, a2, true, t);
  r += test_neq(b3, a3, true, t);
  printf("passed(%02d/%02d), failed(%02d/%02d)\n", r, t, t - r, t);
}
#endif

#if AT_ISI
void pstr_at_operator() {
  std::cout << "pstr_at_operator Operator [] Tests: ";
  mmap_lib::str<1> hello("hello");
  mmap_lib::str<2> hi("hi");
  mmap_lib::str<1> micro("micro-architecture");
  mmap_lib::str<2> foo("--foo1234567890!!!");
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
  mmap_lib::str<1> hello("hello");
  mmap_lib::str<2> eight("888888");
  mmap_lib::str<3> neg_one("-111111");
  mmap_lib::str<1> not_i("123g5");
  mmap_lib::str<2> not_int("-1234f");
  mmap_lib::str<3> zero("-1234f");
  mmap_lib::str<1> num_float("12.34");
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
#endif

#if STARTS_WITH
template<int m_id, int m_id2>
bool test_starts_with(mmap_lib::str<m_id> ls, mmap_lib::str<m_id2> rs, bool ans, uint8_t &cnt) { 
  ++cnt;
  return ls.starts_with(rs) == ans; 
}

template<int m_id>
bool test_starts_with(mmap_lib::str<m_id> ls, std::string_view rs, bool ans, uint8_t &cnt) { 
  ++cnt;
  return ls.starts_with(rs) == ans; 
}

void pstr_starts_with() {
  uint8_t t = 0u, r = 0u;
#if 1
  mmap_lib::str<1> whole("foobar");
  mmap_lib::str<1> front1("foo");
  mmap_lib::str<1> front2("bar");
  mmap_lib::str<1> same("foobar");
  mmap_lib::str<1> one("f");
  mmap_lib::str<1> two("g");

  r += test_starts_with(whole, front1, true, t);
  r += test_starts_with(whole, front2, false, t);
  r += test_starts_with(whole, same, true, t);
  r += test_starts_with(whole, one, true, t);
  r += test_starts_with(whole, two, false, t);

  mmap_lib::str<2> whole2("hello_!_world");
  mmap_lib::str<2> front3("hello_!_worl");
  mmap_lib::str<2> almost("hello_!_worl!");
  mmap_lib::str<2> front4("johnny");
  mmap_lib::str<2> same2("hello_!_world");
  mmap_lib::str<2> three("h");
  mmap_lib::str<2> four("he");
  mmap_lib::str<2> empty("");

  r += test_starts_with(whole2, front3, true, t);
  r += test_starts_with(whole2, front4, false, t);
  r += test_starts_with(whole2, almost, false, t);
  r += test_starts_with(whole2, same2, true, t);
  r += test_starts_with(whole2, three, true, t);
  r += test_starts_with(whole2, four, true, t);
  r += test_starts_with(whole2, empty, true, t);
#endif
  
  mmap_lib::str<3> whole3("--this_var_will_be_very_long_for_testing_12345");
  mmap_lib::str<3> front5("--this_var");
  mmap_lib::str<3> spec1("-");
  mmap_lib::str<3> spec2("--");
  mmap_lib::str<3> spec3("--t");
  mmap_lib::str<3> spec4("--th");
  mmap_lib::str<3> front6("--this_var_wi");
  mmap_lib::str<3> front7("--this_var_wil");
  mmap_lib::str<3> front8("--this_var_will_be_very_");
  mmap_lib::str<3> same3("--this_var_will_be_very_long_for_testing_12345");
  mmap_lib::str<3> almost2("--this_var_will_be_very_long_for_testing_12346");
  mmap_lib::str<3> five("-");
  mmap_lib::str<3> six("balalalalalalalalala");

  // long vs short
  r += test_starts_with(whole3, front5, true, t);
  r += test_starts_with(whole3, front6, true, t);
  r += test_starts_with(whole3, front7, true, t);
  r += test_starts_with(whole3, front8, true, t);
  r += test_starts_with(whole3, same3, true, t);
  r += test_starts_with(whole3, almost2, false, t);
  r += test_starts_with(whole3, five, true, t);
  r += test_starts_with(whole3, six, false, t);
  r += test_starts_with(whole3, spec1, true, t);
  r += test_starts_with(whole3, spec2, true, t);
  r += test_starts_with(whole3, spec3, true, t);
  r += test_starts_with(whole3, spec4, true, t);

  printf("passed(%02d/%02d), failed(%02d/%02d)\n", r, t, t - r, t);
}
#endif

#if BENCH
#define STR_SIZE 1e4
void bench_str_cmp() {
  {
    Lbench b("bench_str_cmp");

    Lrand_range<char>     ch(33, 126);
    Lrand_range<uint16_t> sz(1, 30);

    std::vector<mmap_lib::str<2>> v;
    for (auto i = 0u; i < STR_SIZE; ++i) {
      auto          s = sz.any();
      mmap_lib::str<2> tmp;
      for (auto j = 0; j < s; ++j) {
        tmp.append(ch.any());
      }
      v.emplace_back(tmp);
    }

    int conta = 0;
    for (auto i = 0u; i < STR_SIZE; ++i) {
      for (auto j = 0u; j < STR_SIZE; ++j) {
        if (v[i] == v[j])
          conta++;
      }
    }

    fmt::print("bench_str_cmp conta:{}\n", conta);
    mmap_lib::str<2>::clear_map();
  }

  {
    Lbench b("bench_string_cmp");

    Lrand_range<char>     ch(33, 126);
    Lrand_range<uint16_t> sz(1, 30);

    std::vector<std::string> v;
    for (auto i = 0u; i < STR_SIZE; ++i) {
      auto        s = sz.any();
      std::string str;
      for (auto j = 0; j < s; ++j) {
        str.append(1, ch.any());
      }
      v.emplace_back(str);
    }

    int conta = 0;
    for (auto i = 0u; i < STR_SIZE; ++i) {
      for (auto j = 0u; j < STR_SIZE; ++j) {
        if (v[i] == v[j])
          conta++;
      }
    }

    fmt::print("bench_string_cmp conta:{}\n", conta);
  }
}
#endif

#if WHITEBOARD
void whtbrd() {
  std::cout << "hello\n";
}
#endif


int main(int argc, char** argv) {
  printf("Hello World!\n");

  mmap_lib::str<1>::clear_map();
  mmap_lib::str<2>::clear_map();
  mmap_lib::str<3>::clear_map();

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
  cross_template_compare_tests();
  std::cout << "==========================" << std::endl;
#endif
  
#if AT_ISI
  pstr_at_operator();
  pstr_isI();
#endif

#if STARTS_WITH
  pstr_starts_with();
#endif

#if BENCH
  bench_str_cmp();
#endif

#if WHITEBOARD
  whtbrd();
#endif

  return 0;
}


