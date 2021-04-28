#include "mmap_str.hpp"

#include <stdlib.h>
#include <time.h>

#include <functional>
#include <iostream>
#include <vector>

#include "fmt/format.h"
#include "gtest/gtest.h"

#define RNDN   250  // number of rand strings
#define MaxLen 30   // max len + 1 for rand strings
#define MaxNum 10
#define MinLen 2  // min len for rand strings
#define MinNum 1
#define RUN    1

/* ---------TEST FIXTURES---------
 * Test Name             Status   Description
 * _________             ______   ___________
 * basic_ctor            pass     basic pstr construction
 * random_ctor           pass     randomly generated pstr construction
 * random_comparisons    pass     == and != tested on random pstr's
 * random_at_operator    pass     [] tested on random pstr's
 * isI_operator          pass     is_i() tested on random number pstr's
 * starts_ends_with      pass     starts_with(str), ends_with(str) tested on random pstr's
 * to_s_to_i             pass     to_s(), to_i() tested on random pstr's
 * concat_append         pass     concat(s1, s2), append(str) tested on random pstr's
 * find_rfind            pass     find(str/chr) and rfind(str/chr) tested on random pstr's
 * substr                pass     substr(s, e) tested on random pstr's
 * split                 pass     split(chr) tested on random pstr's
 * get_str_before_after  pass     get_str_before/after_first/last()
 */

class Mmap_str_test : public ::testing::Test {
  // std::vector<std::vector<std::string>> ast_sorted_verification;
  std::vector<std::string> str_bank;
  std::vector<std::string> num_bank;
  std::vector<std::string> no_underscore;
  std::vector<std::string> no_dash;

public:
  void SetUp() override {
    srand(time(0));
    uint8_t t_len = 0u;
    // random string generation
    for (uint8_t i = 0; i < RNDN; ++i) {  // # of strings in vectors
      std::string ele, ele2, ele3, ele4;
      t_len = MinLen + (rand() % (MaxLen - MinLen));  // deciding string length (0-31)
      // construct string with ASCII (32-126) -> 95 chars
      for (uint8_t j = 0; j < t_len; ++j) {
        uint8_t hd = rand() % 94;
        if (j % 2 == 0) {
          while (hd >= 15 && hd <= 24) {
            hd = rand() % 94;
          }
          ele += ('!' + hd);
        } else {
          ele += ('!' + (rand() % 94));
        }

        ele3 += (('!' + hd) == '_') ? ('_' + 1) : ('!' + hd);
        ele4 += (('!' + hd) == '-') ? ('-' + 1) : ('!' + hd);
      }
      str_bank.push_back(ele);        // add string to vector
      no_underscore.push_back(ele3);  // add to no underscore
      no_dash.push_back(ele4);        // add to no dash

      t_len = MinNum + (rand() % (MaxNum - MinNum));
      for (uint8_t j = 0; j < t_len; ++j) {
        if (j == 0) {
          if ((rand() % 2) == 1) {
            ele2 += '-';
            ele2 += ('1' + (rand() % 9));
          } else {
            ele2 += ('1' + (rand() % 9));
          }
        } else {
          ele2 += ('0' + (rand() % 10));
        }
      }
      num_bank.push_back(ele2);
    }
  }

  // wrapper for .at() since vectors are private
  std::string s_get(int i) { return str_bank.at(i); }
  std::string n_get(int i) { return num_bank.at(i); }
  std::string nu_get(int i) { return no_underscore.at(i); }
  std::string nd_get(int i) { return no_dash.at(i); }
};

TEST_F(Mmap_str_test, basic_ctor) {
  mmap_lib::str a1("hello"), a2("hello");
  EXPECT_EQ(a1, a2);

  std::string_view b_sv("schizophrenia");
  std::string      b_st("schizophrenia");
  mmap_lib::str    b1(b_sv), b2(b_st), b3("schizophrenia");
  EXPECT_EQ(b3, b1);
  EXPECT_EQ(b3, b2);

  std::string_view c_sv("neutralization");
  std::string      c_st("neutralization");
  mmap_lib::str    c1(c_sv), c2(c_st), c3("neutralization");
  EXPECT_EQ(c3, c1);
  EXPECT_EQ(c3, c2);

  std::string_view d_sv("--this_var_will_be_very_longbuzzball");
  std::string      d_st("--this_var_will_be_very_longbuzzball");
  mmap_lib::str    d1(d_sv), d2(d_st), d3("--this_var_will_be_very_longbuzzball");
  EXPECT_EQ(d3, d1);
  EXPECT_EQ(d3, d2);

  mmap_lib::str empT("");
  mmap_lib::str empT2("");
}

// random mmap_lib::str creation
TEST_F(Mmap_str_test, random_ctor) {
  for (auto i = 0; i < RNDN; ++i) {
    std::string r_st = s_get(i);
    // std::cout << r_st << std::endl;
    std::string_view r_sv = r_st;
    mmap_lib::str    r1(r_sv), r2(r_st), r3(r_st.c_str());
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
    std::string      c_st = s_get(i), n_st = s_get((i + 1) % RNDN);
    std::string_view c_sv = c_st, n_sv = n_st;
    mmap_lib::str    c_s1(c_st), n_s1(n_st), c_s2(c_sv), n_s2(c_sv);

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

    // tests for next and curr
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
    std::string   hold = s_get(i);
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
    std::string   hold1 = n_get(i), hold2 = s_get(i);
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
    std::string   orig = s_get(i);  // std::string creation
    mmap_lib::str temp(orig);       // mmap_lib::str creation

    // gen start/end indx
    if (temp.size() == 0) {
      end_sw   = 0;
      start_ew = 0;
    } else {
      end_sw   = (rand() % temp.size()) + 1;
      start_ew = rand() % temp.size();
    }

    std::string      stable_sw   = orig.substr(0, end_sw);
    std::string      stable_ew   = orig.substr(start_ew);
    std::string_view sv_check_sw = stable_sw;  // sv
    std::string_view sv_check_ew = stable_ew;
    mmap_lib::str    check_sw(stable_sw);  // str
    mmap_lib::str    check_ew(stable_ew);

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
    std::string   orig = s_get(i);
    mmap_lib::str temp(orig);

    if (temp.size() == 0) {
      start_sw = 0;
      end_sw   = 0;
      start_ew = 0;
      end_ew   = 0;
    } else {
      start_sw = rand() % temp.size();
      end_sw   = (rand() % (temp.size() - start_sw)) + 1;
      start_ew = rand() % temp.size();
      end_ew   = (rand() % (temp.size() - start_ew)) + 1;
    }

    std::string      stable_sw   = orig.substr(start_sw, end_sw);
    std::string      stable_ew   = orig.substr(start_ew, end_ew);
    std::string_view sv_check_sw = stable_sw;
    std::string_view sv_check_ew = stable_ew;
    mmap_lib::str    check_sw(stable_sw);
    mmap_lib::str    check_ew(stable_ew);

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
    std::string   temp = s_get(i), numt = n_get(i);
    int           numref = stoi(numt);
    mmap_lib::str check(temp);
    mmap_lib::str numch(numt);
    std::string   hold = check.to_s();
    int           numh = numch.to_i();
    EXPECT_EQ(temp, hold);
    EXPECT_EQ(numref, numh);
  }
}

TEST_F(Mmap_str_test, concat_append) {
  for (auto i = 0; i < RNDN; ++i) {
    std::string      one = s_get(i), two = s_get((i + 1) % RNDN);
    std::string_view sv1 = one, sv2 = two;
    mmap_lib::str    sone(one);
    mmap_lib::str    stwo(two);
    std::string      three = one + two;
    mmap_lib::str    ref(three);
    mmap_lib::str    test  = mmap_lib::str::concat(sone, stwo);
    mmap_lib::str    test2 = mmap_lib::str::concat(sv1, stwo);
    mmap_lib::str    test3 = mmap_lib::str::concat(sone, sv2);
    mmap_lib::str    test4 = sone.append(stwo);
    mmap_lib::str    test5 = sone.append(sv2);

    EXPECT_EQ(ref, test);
    EXPECT_EQ(ref, test2);
    EXPECT_EQ(ref, test3);
    EXPECT_EQ(ref, test4);
    EXPECT_EQ(ref, test5);
  }
}

#if 1
TEST_F(Mmap_str_test, find_rfind) {
  for (auto i = 0; i < RNDN; ++i) {
    std::string curr  = s_get(i);
    std::string next  = s_get((i + 1) % RNDN);
    uint32_t    start = 0, start2 = 0, end = 0, end2 = 0;
    char        chcurr, chnext;

    if (curr.size() == 0) {
      start = 0;
      end   = 0;
    } else {
      start  = rand() % curr.size();
      end    = (rand() % (curr.size() - start)) + 1;
      chcurr = curr[rand() % curr.size()];
    }

    if (next.size() == 0) {
      start2 = 0;
      end2   = 0;
    } else {
      start2 = rand() % next.size();
      end2   = (rand() % (next.size() - start2)) + 1;
      chnext = next[rand() % next.size()];
    }

    std::string      stable_c = curr.substr(start, end);
    std::string      stable_n = next.substr(start2, end2);
    std::string_view c_sv     = stable_c;
    std::string_view n_sv     = stable_n;
    mmap_lib::str    curr_str(curr);
    mmap_lib::str    next_str(next);
    mmap_lib::str    curr_sub(stable_c);
    mmap_lib::str    next_sub(stable_n);

#if 0
    std::cout << "curr_str: ";
    curr_str.print_string();
    std::cout << "\ncurr_sub: ";
    curr_sub.print_string();
    std::cout << "\nChosen char: " << chcurr;
    std::cout << "\nnext_str: ";
    next_str.print_string();
    std::cout << "\nnext_sub: ";
    next_sub.print_string();
    std::cout << "\nChosen char: " << chnext;
    std::cout << "\n";
#endif

    // find(const str& a)
    EXPECT_EQ(curr_str.find(curr_sub), curr.find(stable_c));
    EXPECT_EQ(next_str.find(next_sub), next.find(stable_n));
    EXPECT_EQ(curr_str.rfind(curr_sub), curr.rfind(stable_c));
    EXPECT_EQ(next_str.rfind(next_sub), next.rfind(stable_n));
    EXPECT_EQ(curr_str.find(chcurr), curr.find_first_of(chcurr));
    EXPECT_EQ(next_str.find(chnext), next.find_first_of(chnext));
    EXPECT_EQ(curr_str.rfind(chcurr), curr.find_last_of(chcurr));
    EXPECT_EQ(next_str.rfind(chnext), next.find_last_of(chnext));
  }
}
#endif

TEST_F(Mmap_str_test, substr) {
  for (auto i = 0; i < RNDN; ++i) {
    std::string curr  = s_get(i);
    std::string next  = s_get((i + 1) % RNDN);
    uint32_t    start = 0, start2 = 0, end = 0, end2 = 0;

    if (curr.size() == 0) {
      start = 0;
      end   = 0;
    } else {
      start = rand() % curr.size();
      end   = (rand() % (curr.size() - start)) + 1;
    }

    if (next.size() == 0) {
      start2 = 0;
      end2   = 0;
    } else {
      start2 = rand() % next.size();
      end2   = (rand() % (next.size() - start2)) + 1;
    }

    std::string   stable_c = curr.substr(start, end);
    std::string   stable_n = next.substr(start2, end2);
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

TEST_F(Mmap_str_test, split) {
  std::vector<mmap_lib::str> ref_nu, ref_nd, hold_nu, hold_nd;
  std::string                longg_nu, longg_nd;

  uint8_t iter = 0, sum = 0;
  for (auto i = 0; i < RNDN; ++i) {
    while (iter == 0) {          // while iter is 0 at beginning, make sure its not
      iter = 1 + (rand() % 10);  // between 1 and 10
      // adjusting iter so we don't exceed RNDN
      if ((sum + iter) > RNDN) {
        iter = iter - ((sum + iter) - RNDN);
      }
      sum += iter;  // add to sum to compare
    }
    ref_nu.push_back(mmap_lib::str(nu_get(i)));  // add to ref_
    ref_nd.push_back(mmap_lib::str(nd_get(i)));  // add to ref-
    longg_nu += nu_get(i);                       // add to long
    longg_nd += nd_get(i);
    --iter;
    if (iter == 0) {  // iter == 0 means times to split
      mmap_lib::str tt_nu(longg_nu);
      mmap_lib::str tt_nd(longg_nd);
      hold_nu = tt_nu.split('_');  // splitting by underscore
      hold_nd = tt_nd.split('-');  // splitting by dash
      EXPECT_EQ(ref_nu, hold_nu);  // checking
      EXPECT_EQ(ref_nd, hold_nd);
      if (sum == RNDN) {
        break;
      }  // done with vec means break
      else {
        ref_nu.clear();
        hold_nu.clear();
        longg_nu.clear();
        ref_nd.clear();
        hold_nd.clear();
        longg_nd.clear();
      }  // else clear data
    } else {
      longg_nu += '_';
      longg_nd += '-';  // adding token
    }
  }
}

#if 1
TEST_F(Mmap_str_test, get_str_before_after) {
  for (auto i = 0; i < RNDN; ++i) {
    std::string   temp = s_get(i);
    mmap_lib::str hold(temp);
    char          ch  = temp[rand() % temp.size()];
    size_t        ffo = temp.find_first_of(ch);
    size_t        flo = temp.find_last_of(ch);

#if 0
    std::cout << "Original string is: " << temp << std::endl;
    std::cout << "Chosen char is: " << ch << std::endl;
    std::cout << "ffo = " << ffo << " flo = " << flo << std::endl;
#endif

    mmap_lib::str gsaf = hold.get_str_after_first(ch);
    mmap_lib::str gsbf = hold.get_str_before_first(ch);
    mmap_lib::str gsal = hold.get_str_after_last(ch);
    mmap_lib::str gsbl = hold.get_str_before_last(ch);

#if 0
    std::cout << "pstr gsaf: ";
    gsaf.print_string();
    std::cout << std::endl;
#endif

    std::string gsaf_stable = temp.substr(ffo + 1, temp.size() - (ffo + 1));
    std::string gsbf_stable = temp.substr(0, ffo);
    std::string gsal_stable = temp.substr(flo + 1, temp.size() - (flo + 1));
    std::string gsbl_stable = temp.substr(0, flo);

    mmap_lib::str gsaf_ref(gsaf_stable);
    mmap_lib::str gsbf_ref(gsbf_stable);
    mmap_lib::str gsal_ref(gsal_stable);
    mmap_lib::str gsbl_ref(gsbl_stable);

#if 0
    std::cout << "std gsaf_stable: " << gsaf_stable << std::endl;
    std::cout << "pstr gsaf_ref: ";
    gsaf_ref.print_string();
    std::cout << std::endl;
#endif

    EXPECT_EQ(gsaf, gsaf_ref);
    EXPECT_EQ(gsbf, gsbf_ref);
    EXPECT_EQ(gsal, gsal_ref);
    EXPECT_EQ(gsbl, gsbl_ref);
  }
}
#endif

#if RUN
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
#endif
