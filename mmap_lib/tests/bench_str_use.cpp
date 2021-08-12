#include <type_traits>
#include <vector>

#include "flat_hash_map.hpp"
#include "fmt/format.h"
#include "iassert.hpp"
#include "lbench.hpp"
#include "lrand.hpp"
#include "mmap_map.hpp"
#include "mmap_str.hpp"

//#define BENCH             1

#ifdef BENCH
#define CTOR_TESTS        0
#define NEEQ_TESTS        0
#define AT_ISI            0
#define STARTS_WITH       0
#else
#define CTOR_TESTS        1
#define NEEQ_TESTS        1
#define AT_ISI            1
#define STARTS_WITH       1
#endif


int cgen_do_sv(const std::string_view s) {
  return s[0];
}

int cgen_do_sv_ptr(const std::string_view &s) {
  return s[0];
}

int cgen_do_str(const mmap_lib::str s) {
  return s.front();
}

int cgen_do_str_ptr(const mmap_lib::str &s) {
  return s.front();
}

#if CTOR_TESTS
void test_ctor(mmap_lib::str ts, const char* rs) {

  std::cout << "str:" << ts.to_s() << "\n";
  std::cout << "std:" << rs << "\n";
}

void mmap_pstr_ctor_tests() {
  std::cout << "Warming Up";

  mmap_lib::str baa("hello");
  mmap_lib::str bab("12-micro-34567890");
  mmap_lib::str bac("flower");
  mmap_lib::str bad("--foobarbazball!");
  std::cout << ". ";
  mmap_lib::str bae("microarchitecture");
  mmap_lib::str baf("12-roach-34567890");
  mmap_lib::str bag("abcdefghijklmnop");
  mmap_lib::str bah("--Joker-34567890");
  mmap_lib::str bai("12-micro-34567890");
  std::cout << ". ";
  mmap_lib::str baj("12-Andy-34567890");
  mmap_lib::str bak("12-Base-34567890");
  mmap_lib::str bal("12-Reed-34567890");
  mmap_lib::str bam("zyxwvutsrqponmlk");
  std::cout << ". ";
  mmap_lib::str ban("12-Gorgon-34567890");
  mmap_lib::str bao("12-drums-34567890");
  mmap_lib::str bap("12-Johnathan-34567890");
  mmap_lib::str baq("zyxwvutsrqponmlk");
  std::cout << ".\n";

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
}
#endif

#if NEEQ_TESTS
template <std::size_t N>
bool test_eq(mmap_lib::str ts, const char (&rs)[N], bool ans, uint8_t &cnt) {
  ++cnt;
  return (ts == mmap_lib::str(rs)) == ans;
}

template <std::size_t N>
bool test_neq(mmap_lib::str ts, const char (&rs)[N], bool ans, uint8_t &cnt) {
  ++cnt;
  return (ts != mmap_lib::str(rs)) == ans;
}

bool test_eq(mmap_lib::str ls, mmap_lib::str rs, bool ans, uint8_t &cnt) {
  ++cnt;
  return (ls == rs) == ans;

}

bool test_neq(mmap_lib::str ls, mmap_lib::str rs, bool ans, uint8_t &cnt) {
  ++cnt;
  return (ls != rs) == ans;
}

bool test_eq(mmap_lib::str ls, std::string_view rs, bool ans, uint8_t &cnt) {
  ++cnt;
  if (ls == mmap_lib::str(rs))
    assert(ls.to_s() == rs);

  return (ls == mmap_lib::str(rs)) == ans;
}

bool test_neq(mmap_lib::str ls, std::string_view rs, bool ans, uint8_t &cnt) {
  ++cnt;
  if (ls != mmap_lib::str(rs))
    assert(ls.to_s() != rs);

  return (ls != mmap_lib::str(rs)) == ans;
}

void pstrVchar_eqeq_tests() {
  std::cout << "pstr vs. char Operator == Tests: ";
  mmap_lib::str hello("hello");
  mmap_lib::str hi("hi");
  mmap_lib::str hello_world("hello_!_world");
  mmap_lib::str micro("micro-architecture");
  mmap_lib::str foo("--foo1234567890!!!");

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
  mmap_lib::str hello("hello");
  mmap_lib::str hi("hi");
  mmap_lib::str hello_world("hello_!_world");
  mmap_lib::str amethysts("amethysts_emeralds");
  mmap_lib::str balloon("_balloon_223344!!!");

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
  mmap_lib::str hello("hello");
  mmap_lib::str hi("hi");
  mmap_lib::str hello_world("hello_!_world");
  mmap_lib::str micro("micro-architecture");
  mmap_lib::str foo("--foo1234567890!!!");

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
  mmap_lib::str hello("hello");
  mmap_lib::str hi("hi");
  mmap_lib::str hello_world("hello_!_world");
  mmap_lib::str amethysts("amethysts_emeralds");
  mmap_lib::str balloon("_balloon_223344!!!");

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
  mmap_lib::str a1("i-am-tired-today=(");
  mmap_lib::str a2("i-am-tired-today=(");
  mmap_lib::str a3("i-am-tired-today=(");
  mmap_lib::str b1("i-am-tired-today>>");
  mmap_lib::str b2("i-am-tired-today>>");
  mmap_lib::str b3("i-am-tired-today>>");

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
  mmap_lib::str hello("hello");
  mmap_lib::str hi("hi");
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
#endif

#if STARTS_WITH
bool test_starts_with(mmap_lib::str ls, mmap_lib::str rs, bool ans, uint8_t &cnt) {
  ++cnt;
  return ls.starts_with(rs) == ans;
}

bool test_starts_with(mmap_lib::str ls, std::string_view rs, bool ans, uint8_t &cnt) {
  ++cnt;
  return ls.starts_with(rs) == ans;
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

  r += test_starts_with(whole, front1, true, t);
  r += test_starts_with(whole, front2, false, t);
  r += test_starts_with(whole, same, true, t);
  r += test_starts_with(whole, one, true, t);
  r += test_starts_with(whole, two, false, t);

  mmap_lib::str whole2("hello_!_world");
  mmap_lib::str front3("hello_!_worl");
  mmap_lib::str almost("hello_!_worl!");
  mmap_lib::str front4("johnny");
  mmap_lib::str same2("hello_!_world");
  mmap_lib::str three("h");
  mmap_lib::str four("he");
  mmap_lib::str empty("");

  r += test_starts_with(whole2, front3, true, t);
  r += test_starts_with(whole2, front4, false, t);
  r += test_starts_with(whole2, almost, false, t);
  r += test_starts_with(whole2, same2, true, t);
  r += test_starts_with(whole2, three, true, t);
  r += test_starts_with(whole2, four, true, t);
  r += test_starts_with(whole2, empty, true, t);
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

#ifdef BENCH
#define STR_SIZE 1e4
void bench_str_cmp() {
  {
    for(auto sz=2;sz<512;sz=sz*2) {
      mmap_lib::str::nuke();

      int conta = 0;
      std::vector<mmap_lib::str> v;
      {
        std::string bench_name = std::string("bench_str_create") + std::to_string(sz);
        Lbench b(bench_name);

        Lrand_range<char>     ch(33, 126);
        //Lrand_range<uint16_t> sz(1, 30);

        for (auto i = 0u; i < STR_SIZE; ++i) {
          auto          s = sz; // sz.any();
#if 0
          mmap_lib::str tmp;
          for (auto j = 0; j < s; ++j) {
            tmp = tmp.append(ch.any());
          }
#else
          std::string str;
          for (auto j = 0; j < s; ++j) {
            str.append(1, ch.any());
          }
          mmap_lib::str tmp(str);
#endif

          v.emplace_back(tmp);
          conta += v.size();
        }
      }

      {
        std::string bench_name = std::string("bench_str_cmp") + std::to_string(sz);
        Lbench b(bench_name);

        for (auto i = 0u; i < STR_SIZE; ++i) {
          for (auto j = 0u; j < STR_SIZE; ++j) {
            if (v[i] == v[j])
              conta++;
          }
        }
      }

      fmt::print("{} conta:{}\n", "mmap_lib::str", conta);
    }
  }

  {
    for(auto sz=2;sz<512;sz=sz*2) {

      int conta = 0;
      std::vector<std::string> v;
      {
        std::string bench_name = std::string("bench_string_create") + std::to_string(sz);
        Lbench b(bench_name);

        Lrand_range<char>     ch(33, 126);
        //Lrand_range<uint16_t> sz(1, 30);

        for (auto i = 0u; i < STR_SIZE; ++i) {
          auto        s = sz; // sz.any();
          std::string str;
          for (auto j = 0; j < s; ++j) {
            str.append(1, ch.any());
          }
          v.emplace_back(str);
          conta += v.size();
        }
      }

      {
        std::string bench_name = std::string("bench_string_cmp") + std::to_string(sz);
        Lbench b(bench_name);

        for (auto i = 0u; i < STR_SIZE; ++i) {
          for (auto j = 0u; j < STR_SIZE; ++j) {
            if (v[i] == v[j])
              conta++;
          }
        }
      }

      fmt::print("{} conta:{}\n", "std::string", conta);
    }
  }
}
#endif

int main(int argc, char** argv) {

  mmap_lib::str::setup();

#ifdef BENCH
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

  return 0;
}


