#include "mmap_str.hpp"

#include <functional>
#include <iostream>
#include <vector>
#include <stdlib.h>
#include <time.h>

#include "fmt/format.h"
#include "gtest/gtest.h"

#define RNDN 250 // number of rand strings
#define MaxLen 30 // max len + 1 for rand strings
#define MaxNum 10 
#define MinLen 2  // min len for rand strings
#define MinNum 1
#define RUN 1

class Mmap_str_test : public ::testing::Test {
  //std::vector<std::vector<std::string>> ast_sorted_verification;
  std::vector<std::string> str_bank;
  std::vector<std::string> num_bank;

public:

  void SetUp() override {
    srand(time(0));
    uint8_t t_len = 0u, decPos = 0u;
    bool isNeg = false, isFloat = false;
    // random string generation
    for (uint8_t i = 0; i < RNDN; ++i) { // # of strings in vectors
      std::string ele;
      t_len = MinLen + (rand() % (MaxLen-MinLen)); // deciding string length (0-31)
      // construct string with ASCII (32-126) -> 95 chars
      for(uint8_t j = 0; j < t_len; ++j) { 
        if (j % 2 == 0) {
          uint8_t hd = rand() % 94;
          while (hd >= 15 && hd <= 24) {
            hd = rand() % 94;
          }
          ele += ('!' + hd);
        } else {
          ele += ('!' + (rand() % 94)); 
        }
      }
      str_bank.push_back(ele); // add string to vector
    }

    for (uint8_t i = 0; i < RNDN; ++i) {
      std::string ele;
      t_len = MinNum + (rand() % (MaxNum-MinNum));
      for (uint8_t j = 0; j < t_len; ++j) { 
        if (j == 0) {
          if ((rand() % 2) == 1) { 
            ele += '-';
            ele += ('1' + (rand() % 9)); 
          } else { 
            ele += ('1' + (rand() % 9)); 
          }
        } else { ele += ('0' + (rand() % 10)); }
      }
      num_bank.push_back(ele);
    }
  }

  // wrapper for .at() since vectors are private
  std::string s_get(int i) { return str_bank.at(i); }
  std::string n_get(int i) { return num_bank.at(i); }
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

  mmap_lib::str empT("");
  mmap_lib::str empT2("");

  std::cout << "testing empty str ctor:\n";
  empT.print_string();
  std::cout << std::endl;
  empT2.print_string();
}

// random mmap_lib::str creation
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

// testing == and != ops
// mmap_lib::str vs. mmap_lib::str
// mmap_lib::str vs. string_view
// mmap_lib::str vs. std::string
TEST_F(Mmap_str_test, random_comparisons) {
  for (auto i = 0; i < RNDN; ++i) {
    // from the str_bank, get string at current index and next index
    std::string c_st = s_get(i), n_st = s_get((i+1) % RNDN);
    std::string_view c_sv = c_st, n_sv = n_st;
    mmap_lib::str c_s1(c_st), n_s1(n_st), c_s2(c_sv), n_s2(c_sv);
   
    #if 0 
    std::cout << "pstr c_s1: ";
    c_s1.print_string();
    std::cout << "\npstr n_s1: ";
    n_s1.print_string();
    std::cout << std::endl;
    #endif

    EXPECT_TRUE(c_s1 == c_s2);
    EXPECT_TRUE(c_s1 == c_st);
    EXPECT_TRUE(c_s1 == c_sv);
    EXPECT_TRUE(c_s1 == c_st.c_str());

    EXPECT_FALSE(c_s1 != c_s2); 
    EXPECT_FALSE(c_s1 != c_st); 
    EXPECT_FALSE(c_s1 != c_sv);
    EXPECT_FALSE(c_s1 != c_st.c_str());
  
    //tests for next and curr
    if (c_st == n_st) {
      EXPECT_FALSE(c_s1 != n_s1);
      EXPECT_FALSE(c_s1 != n_st);
      EXPECT_FALSE(c_s1 != n_sv);
      EXPECT_FALSE(c_s1 != n_st.c_str());
    
      EXPECT_TRUE(n_s1 == c_s1); 
      EXPECT_TRUE(n_s1 == c_st); 
      EXPECT_TRUE(n_s1 == c_sv);    
      EXPECT_TRUE(n_s1 == c_st.c_str());
    } else { 
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
}

// std::string vs. mmap_lib::str
TEST_F(Mmap_str_test, random_at_operator) {
  for (auto i = 0; i < RNDN; ++i) {
    std::string hold = s_get(i);
    mmap_lib::str temp(hold);
    for (auto j = 0; j < temp.size(); ++j) {
      EXPECT_EQ(hold[j], temp[j]);
    }
  }  
}

// Uses two user-generated arrays to check each other
// one for numbers, one for outcomes (bool)
TEST_F(Mmap_str_test, isI_operator) {
  for (auto i = 0; i < RNDN; ++i) {
    std::string hold1 = n_get(i), hold2 = s_get(i);
    mmap_lib::str str1(hold1), str2(hold2);
   
    #if 0 
    std::cout << "str1: ";
    str1.print_string();
    std::cout << "\nstr2: ";
    str2.print_string();
    std::cout << "\n";
    #endif

    if ((rand() % 100) >= 50) {
      EXPECT_TRUE(str1.is_i());
    } else {
      EXPECT_FALSE(str2.is_i());
    }
  }
}

// 1) pull a string from the random str_bank
// 2) take a sub-string of the string 
//    -> randomly generate start and end indx of sub-string
// 3) turn string and sub-string into mmap_lib::str
// 4) run string.starts_with(sub-string)
// 5) if the randomly generated start indx is 0, 
//    -> then starts_with should return true
//    -> else it is false
TEST_F(Mmap_str_test, starts_ends_with) {
  uint32_t start_sw = 0, end_sw = 0;
  uint32_t start_ew = 0, end_ew = 0;

  // ALWAYS TRUE
  for (auto i = 0; i < RNDN; ++i) {
    std::string orig = s_get(i); // std::string creation
    mmap_lib::str temp(orig);    // mmap_lib::str creation

    // gen start/end indx
    if (temp.size() == 0) { 
      end_sw = 0; 
      start_ew = 0; 
    } else { 
      end_sw = (rand() % temp.size()) + 1; 
      start_ew = rand() % temp.size();
    }

    std::string stable_sw = orig.substr(0, end_sw);
    std::string stable_ew = orig.substr(start_ew);
    std::string_view sv_check_sw = stable_sw; //sv
    std::string_view sv_check_ew = stable_ew;
    mmap_lib::str check_sw(stable_sw); // str
    mmap_lib::str check_ew(stable_ew); 


    #if 0    
    std::cout << "pstr temp: ";
    temp.print_string();
    std::cout << "\npstr check_sw: ";
    check_sw.print_string();
    std::cout << "\npstr check_ew: ";
    check_ew.print_string();
    std::cout << std::endl;
    #endif
    
    EXPECT_TRUE(temp.starts_with(check_sw));
    EXPECT_TRUE(temp.starts_with(sv_check_sw));
    EXPECT_TRUE(temp.starts_with(stable_sw));
    
    EXPECT_TRUE(temp.ends_with(check_ew));
    EXPECT_TRUE(temp.ends_with(sv_check_ew));
    EXPECT_TRUE(temp.ends_with(stable_ew));
  }


  // TRUE AND FALSE
  for (auto i = 0; i < RNDN; ++i) {
    std::string orig = s_get(i);
    mmap_lib::str temp(orig);
    
    if (temp.size() == 0) { 
      start_sw = 0; end_sw = 0;
      start_ew = 0; end_ew = 0;
    } else { 
      start_sw = rand() % temp.size(); 
      end_sw = (rand() % (temp.size()-start_sw)) + 1; 
      start_ew = rand() % temp.size(); 
      end_ew = (rand() % (temp.size()-start_ew)) + 1; 
    }
    
    std::string stable_sw = orig.substr(start_sw, end_sw);
    std::string stable_ew = orig.substr(start_ew, end_ew);
    std::string_view sv_check_sw = stable_sw;
    std::string_view sv_check_ew = stable_ew;
    mmap_lib::str check_sw(stable_sw);
    mmap_lib::str check_ew(stable_ew);
    
    #if 0    
    std::cout << "pstr temp: ";
    temp.print_string();
    std::cout << "\npstr check_sw: ";
    check_sw.print_string();
    std::cout << "\npstr check_ew: ";
    check_ew.print_string();
    std::cout << std::endl;
    #endif

    if (start_sw == 0) {
      EXPECT_TRUE(temp.starts_with(check_sw));
      EXPECT_TRUE(temp.starts_with(sv_check_sw));
      EXPECT_TRUE(temp.starts_with(stable_sw));
    } else {
      if (orig.substr(0, stable_sw.size()) == stable_sw) {
        EXPECT_TRUE(temp.starts_with(check_sw));
        EXPECT_TRUE(temp.starts_with(sv_check_sw));
        EXPECT_TRUE(temp.starts_with(stable_sw));
      } else {
        EXPECT_FALSE(temp.starts_with(check_sw));
        EXPECT_FALSE(temp.starts_with(sv_check_sw));
        EXPECT_FALSE(temp.starts_with(stable_sw));
      }
    }
    
    if (end_ew == temp.size()) {
      EXPECT_TRUE(temp.ends_with(check_ew));
      EXPECT_TRUE(temp.ends_with(sv_check_ew));
      EXPECT_TRUE(temp.ends_with(stable_ew));
    } else {
      if (orig.substr(orig.size() - stable_ew.size()) == stable_ew) {
        EXPECT_TRUE(temp.ends_with(check_ew));
        EXPECT_TRUE(temp.ends_with(sv_check_ew));
        EXPECT_TRUE(temp.ends_with(stable_ew));
      } else {
        EXPECT_FALSE(temp.ends_with(check_ew));
        EXPECT_FALSE(temp.ends_with(sv_check_ew));
        EXPECT_FALSE(temp.ends_with(stable_ew));
      }
    }
  }
}

TEST_F(Mmap_str_test, to_s_to_i) {
  for (auto i = 0; i < RNDN; ++i) {
    std::string temp = s_get(i), numt = n_get(i);
    int numref = stoi(numt);
    mmap_lib::str check(temp);
    mmap_lib::str numch(numt);
    std::string hold = check.to_s();
    int numh = numch.to_i();
    EXPECT_EQ(temp, hold);
    EXPECT_EQ(numref, numh);
  }
}


TEST_F(Mmap_str_test, concat_append) {
  for (auto i = 0; i < RNDN; ++i) {
    std::string one = s_get(i), two = s_get((i+1)%RNDN);
    std::string_view sv1 = one, sv2 = two;
    mmap_lib::str sone(one);
    mmap_lib::str stwo(two);
    std::string three = one + two;
    mmap_lib::str ref(three);
    mmap_lib::str test = mmap_lib::str::concat(sone, stwo);
    mmap_lib::str test2 = mmap_lib::str::concat(sv1, stwo);
    mmap_lib::str test3 = mmap_lib::str::concat(sone, sv2);
    mmap_lib::str test4 = sone.append(stwo);
    mmap_lib::str test5 = sone.append(sv2);
    
    EXPECT_EQ(ref, test);
    EXPECT_EQ(ref, test2);
    EXPECT_EQ(ref, test3);
    EXPECT_EQ(ref, test4);
    EXPECT_EQ(ref, test5);
  }
}



TEST_F(Mmap_str_test, find) {
  for (auto i = 0; i < RNDN; ++i) { 
    std::string curr = s_get(i);
    std::string next = s_get((i+1) % RNDN);
    uint32_t start=0, start2=0, end=0, end2=0;    
    
    
    if (curr.size() == 0) { 
      start = 0; end = 0;
    } else { 
      start = rand() % curr.size(); 
      end = (rand() % (curr.size()-start)) + 1; 
    }
    
    if (next.size() == 0) { 
      start2 = 0; end2 = 0;
    } else { 
      start2 = rand() % next.size(); 
      end2 = (rand() % (next.size()-start2)) + 1; 
    }
    
    
    std::string stable_c = curr.substr(start, end);
    std::string stable_n = next.substr(start2, end2);
    std::string_view c_sv = stable_c;
    std::string_view n_sv = stable_n;
    mmap_lib::str curr_str(curr);
    mmap_lib::str next_str(next);
    mmap_lib::str curr_sub(stable_c);
    mmap_lib::str next_sub(stable_n);

    #if 0
    std::cout << "curr_str: ";
    curr_str.print_string();
    std::cout << "\ncurr_sub: ";
    curr_sub.print_string();
    std::cout << "\nnext_str: ";
    next_str.print_string();
    std::cout << "\nnext_sub: ";
    next_sub.print_string();
    std::cout << "\n";
    #endif

    // find(const str& a)
    EXPECT_EQ(curr_str.find(curr_sub), curr.find(stable_c));
    EXPECT_EQ(next_str.find(next_sub), next.find(stable_n));
  }
}


TEST_F(Mmap_str_test, substr) {
  for (auto i = 0; i < RNDN; ++i) { 
    std::string curr = s_get(i);
    std::string next = s_get((i+1) % RNDN);
    uint32_t start=0, start2=0, end=0, end2=0;    
    
    
    if (curr.size() == 0) { 
      start = 0; end = 0;
    } else { 
      start = rand() % curr.size(); 
      end = (rand() % (curr.size()-start)) + 1; 
    }
    
    if (next.size() == 0) { 
      start2 = 0; end2 = 0;
    } else { 
      start2 = rand() % next.size(); 
      end2 = (rand() % (next.size()-start2)) + 1; 
    }
    
    
    std::string stable_c = curr.substr(start, end);
    std::string stable_n = next.substr(start2, end2);
    mmap_lib::str curr_sub_ref(stable_c);
    mmap_lib::str next_sub_ref(stable_n);
    mmap_lib::str curr_str(curr);
    mmap_lib::str next_str(next);
    mmap_lib::str curr_sub = curr_str.substr(start, end);
    mmap_lib::str next_sub = next_str.substr(start2, end2);


    #if 0
    std::cout << "curr_str: ";
    curr_str.print_string();
    std::cout << "\nstart = " << start << " end = " << end;
    std::cout << "\ncurr_sub: ";
    curr_sub.print_string();
    std::cout << "\ncurr_sub_ref: ";
    curr_sub_ref.print_string();
    std::cout << "\nnext_str: ";
    next_str.print_string();
    std::cout << "\nstart2 = " << start2 << " end2 = " << end2;
    std::cout << "\nnext_sub: ";
    next_sub.print_string();
    std::cout << "\nnext_sub_ref: ";
    next_sub_ref.print_string();
    std::cout << "\n";
    #endif

    // find(const str& a)
    EXPECT_EQ(curr_sub, curr_sub_ref);
    EXPECT_EQ(next_sub, next_sub_ref);  
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
