#include "mmap_str.hpp"

#include <functional>
#include <iostream>
#include <vector>
#include <stdlib.h>
#include <time.h>

#include "fmt/format.h"
#include "gtest/gtest.h"

#define RNDN 10 // number of rand strings
#define MaxLen 32 // max len + 1 for rand strings
#define MinLen 0  // min len for rand strings
#define RUN 1

class Mmap_str_test : public ::testing::Test {
  //std::vector<std::vector<std::string>> ast_sorted_verification;
  std::vector<std::string> str_bank;
  std::vector<std::string> num_bank = {"hello", "888888", "-111111", "123g5", 
                                       "-1234f", "12.34", "0.44", "-123.56", 
                                       "-g12350", "0"};
  std::vector<bool> isi_res =         {false, true, true, false, 
                                       false, false, false, false, 
                                       false, true};

public:
  void SetUp() override {
    srand(time(0));
    uint8_t t_len = 0u, decPos = 0u;
    bool isNeg = false, isFloat = false;
    // random string generation
    for (uint8_t i = 0; i < RNDN; ++i) { // # of strings in vectors
      std::string ele;
      t_len = MinLen + (rand() % MaxLen); // deciding string length (0-31)
                        
      // construct string with ASCII (32-126) -> 95 chars
      for(uint8_t j = 0; j < t_len; ++j) { ele += ('!' + (rand() % 94)); }
      str_bank.push_back(ele); // add string to vector
    }

    //for (auto i = num_bank.begin(); i != num_bank.end(); ++i) {std::cout << *i << "\n";}
  }

  // wrapper for vector.at() since str_bank is private
  std::string s_get(int i) { return str_bank.at(i); }
  std::string n_get(int i) { return num_bank.at(i); }
  bool isi_res_get(int i) { return isi_res.at(i); }
};

// TEST_F are different types of tests
// >> TEST_F(class_name, test_name) {
//      tests in here
//    }

TEST_F(Mmap_str_test, basic_ctor) {
  mmap_lib::str a1("hello"), a2("hello");
  
  EXPECT_EQ(a1, a2);

  std::string_view b_sv("schizophrenia");
  std::string b_st("schizophrenia");
  mmap_lib::str b1(b_sv), b2(b_st), b3("schizophrenia");

  EXPECT_EQ(b3, b1);
  EXPECT_EQ(b3, b2);
  
  std::string_view c_sv("neutralization");
  std::string c_st("neutralization");
  mmap_lib::str c1(c_sv), c2(c_st), c3("neutralization");

  EXPECT_EQ(c3, c1);
  EXPECT_EQ(c3, c2);
  
  std::string_view d_sv("--this_var_will_be_very_longbuzzball");
  std::string d_st("--this_var_will_be_very_longbuzzball");
  mmap_lib::str d1(d_sv), d2(d_st), d3("--this_var_will_be_very_longbuzzball");

  EXPECT_EQ(d3, d1);
  EXPECT_EQ(d3, d2);
}

TEST_F(Mmap_str_test, random_ctor) {
  for (auto i = 0; i < RNDN; ++i) {
    std::string r_st = s_get(i);
    //std::cout << r_st << std::endl;
    std::string_view r_sv = r_st;
    mmap_lib::str r1(r_sv), r2(r_st), r3(r_st.c_str());
    EXPECT_EQ(r3, r1);
    EXPECT_EQ(r3, r2);
  }
}


TEST_F(Mmap_str_test, random_comparisons) {
  for (auto i = 0; i < RNDN; ++i) {
    // from the str_bank, get string at current index and next index
    std::string c_st = s_get(i % RNDN), n_st = s_get((i+1) % RNDN);
    std::string_view c_sv = c_st, n_sv = n_st;
    mmap_lib::str c_s1(c_st), n_s1(n_st), c_s2(c_sv), n_s2(c_sv);
    
    EXPECT_TRUE(c_s1 == c_s2);
    EXPECT_TRUE(c_s1 == c_st);
    EXPECT_TRUE(c_s1 == c_sv);
    EXPECT_TRUE(c_s1 == c_st.c_str());

    EXPECT_FALSE(c_s1 != c_s2); 
    EXPECT_FALSE(c_s1 != c_st); 
    EXPECT_FALSE(c_s1 != c_sv);
    EXPECT_FALSE(c_s1 != c_st.c_str());
  
    //tests for next and curr
    EXPECT_TRUE(c_s1 != n_s1);
    EXPECT_TRUE(c_s1 != n_st);
    EXPECT_TRUE(c_s1 != n_sv);
    EXPECT_TRUE(c_s1 != n_st.c_str());
    
    EXPECT_FALSE(n_s1 == c_s1); 
    EXPECT_FALSE(n_s1 == c_st); 
    EXPECT_FALSE(n_s1 == c_sv);    
    EXPECT_FALSE(n_s1 == c_st.c_str());
  }
}

TEST_F(Mmap_str_test, random_at_operator) {
  for (auto i = 0; i < RNDN; ++i) {
    std::string hold = s_get(i);
    mmap_lib::str temp(hold);
    for (auto j = 0; j < temp.size(); ++j) {
      EXPECT_EQ(hold[j], temp[j]);
    }
  }  
}

TEST_F(Mmap_str_test, isI_operator) {
  for (auto i = 0; i < RNDN; ++i) {
    mmap_lib::str temp(n_get(i)); 
    EXPECT_EQ(temp.is_i(), isi_res_get(i));
  }
}

TEST_F(Mmap_str_test, starts_with) {
  // 1) pull a string from the random str_bank
  // 2) take a sub-string of the string 
  //    -> randomly generate start and end indx of sub-string
  // 3) turn string and sub-string into mmap_lib::str
  // 4) run string.starts_with(sub-string)
  // 5) if the randomly generated start indx is 0, 
  //    -> then starts_with should return true
  //    -> else it is false
  uint32_t start = 0, end = 0; 
  for (auto i = 0; i < RNDN; ++i) {
    std::string orig = s_get(i);
    mmap_lib::str temp(orig);
    if (temp.size() == 0) { end = 0; }
    else { end = rand() % temp.size() + 1; }
    mmap_lib::str check(orig.substr(0, end));
    EXPECT_TRUE(temp.starts_with(check));
    
  }

  for (auto i = 0; i < RNDN; ++i) {
    std::string orig = s_get(i);
    mmap_lib::str temp(orig);
    if (temp.size() == 0) { start = 0; }
    else { start = rand() % temp.size(); }
    if (temp.size() == 0) { end = 0; }
    else { end = rand() % temp.size() + 1; }
    mmap_lib::str check(orig.substr(start, end));
    if (start == 0) {
      EXPECT_TRUE(temp.starts_with(check));
    } else {
      EXPECT_FALSE(temp.starts_with(check));
    }
  }

}



#if 0
TEST_F(Mmap_str_test, const_expr_trival_cmp) {

  mmap_lib::str long_a("hello_hello_hello_hello_hello_hello"); // not right, but it should compile
  constexpr mmap_lib::str a("hello");

  auto b = do_check("hello");
  EXPECT_TRUE(b);

  std::string_view a_sv{"hello"};

  EXPECT_TRUE(do_check(a_sv));

  EXPECT_TRUE(do_check_string("hello"));
  EXPECT_TRUE(do_check_sv("hello"));

  /*
  fmt::print("a[0]:{} size:{}\n",a[0], a.size());
  fmt::print("a[1]:{} size:{}\n",a[1], a.size());
  fmt::print("a[2]:{} size:{}\n",a[2], a.size());
  fmt::print("a[3]:{} size:{}\n",a[3], a.size());
  fmt::print("a[4]:{} size:{}\n",a[4], a.size());
  */

  assert(a[0] == 'h');   // compile time check
  assert(a[1] == 'e');   // compile time check
  assert(a[2] == 'l');   // compile time check
  assert(a[3] == 'l');   // compile time check
  assert(a[4] == 'o');   // compile time check
  assert(a.size() == 5); // compile time check
  // TODO: static_assert(a==a_sv);       // compile time check
  assert(a=="hello");    // compile time check
  //static_assert(a!="helxo");    // compile time check
  //static_assert(a!="hell");     // compile time check
  //static_assert(a!="hellox");   // compile time check
}
#endif

#if RUN
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
#endif
