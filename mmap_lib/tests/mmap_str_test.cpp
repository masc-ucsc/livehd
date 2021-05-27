#include "mmap_str.hpp"

#include <stdlib.h>
#include <time.h>

#include <functional>
#include <iostream>
#include <vector>

#include "fmt/format.h"
#include "gtest/gtest.h"

#define RNDN   150  // number of rand strings
#define MaxLen 50   // max len + 1 for rand strings
#define MaxNum 10
#define MinLen 2  // min len for rand strings
#define MinNum 1

// Set RUN to 1 when you want to run the tests
#define RUN 0


/* ---------TEST FIXTURES---------
 * Test Name             Status   Description
 * _________             ______   ___________
 * random_ctor_cmp       pass     == and != tested on random pstr's
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
      str_bank.push_back(ele);  // add string to vector
      // std::cout << ele << std::cout;
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

  void TearDown() override {
    mmap_lib::str<1>::clear_map();
    mmap_lib::str<2>::clear_map();
    mmap_lib::str<3>::clear_map();
  }

  // wrapper for .at() since vectors are private
  std::string s_get(int i) { return str_bank.at(i); }
  std::string n_get(int i) { return num_bank.at(i); }
  std::string nu_get(int i) { return no_underscore.at(i); }
  std::string nd_get(int i) { return no_dash.at(i); }
};

// random mmap_lib::str creation
TEST_F(Mmap_str_test, random_ctor_cmp) {
  for (auto i = 0; i < RNDN; ++i) {
    std::string      c_st = s_get(i), n_st = s_get((i + 1) % RNDN);
    std::string_view c_sv = c_st, n_sv = n_st;
    mmap_lib::str<1>    c_s1(c_st); 
    mmap_lib::str<2>    n_s1(n_st);
    mmap_lib::str<3>    c_s2(c_sv);
    mmap_lib::str<1>    n_s2(n_sv);
    mmap_lib::str<2>    c_s3(c_st.c_str());
    mmap_lib::str<3>    n_s3(n_st.c_str());
    

#if 0
    std::cout << "str curr: " << c_st << std::endl;
    std::cout << "str next: " << n_st << std::endl;
    std::cout << "pstr c_s1: ";
    c_s1.print_string();
    std::cout << "\npstr n_s1: ";
    n_s1.print_string();
    std::cout << std::endl;
#endif

    // Testing ctors
    EXPECT_EQ(c_s3, c_s1);
    EXPECT_EQ(c_s3, c_s2);
    EXPECT_EQ(n_s3, n_s1);
    EXPECT_EQ(n_s3, n_s2);

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
    mmap_lib::str<2> temp(hold);
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
    mmap_lib::str<3> str1(hold1), str2(hold2);

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

TEST_F(Mmap_str_test, starts_ends_with) {
  uint32_t start_sw = 0, end_sw = 0;
  uint32_t start_ew = 0, end_ew = 0;

  // ALWAYS TRUE
  for (auto i = 0; i < RNDN; ++i) {
    std::string   orig = s_get(i);  // std::string creation
    mmap_lib::str<1> temp(orig);       // mmap_lib::str creation

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
    mmap_lib::str<1>    check_sw(stable_sw);  // str
    mmap_lib::str<1>    check_ew(stable_ew);

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
    mmap_lib::str<1> temp(orig);

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
    mmap_lib::str<1>    check_sw(stable_sw);
    mmap_lib::str<1>    check_ew(stable_ew);

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
    mmap_lib::str<2> check(temp);
    mmap_lib::str<2> numch(numt);
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
    mmap_lib::str<1> sone(one); 
    mmap_lib::str<2> stwo(two);
    mmap_lib::str<3> sthree(one);
    mmap_lib::str<1> sfour(two);
    mmap_lib::str<2> sfive(one);
    mmap_lib::str<3> ssix(two);
    std::string      three = one + two, three2 = two + one;
    mmap_lib::str<1> ref(three);
    mmap_lib::str<2> ref2(three2);

#if 0
    std::cout << one << "   " << two << "   " << three << std::endl;
    std::cout << "pstr one is: ";
    sone.print_string();
    std::cout << std::endl;
    std::cout << "pstr two is: ";
    stwo.print_string();
    std::cout << std::endl;
    std::cout << "pstr ref is: ";
    ref.print_string();
    std::cout << std::endl;
#endif

#if 0
    std::cout << two << "   " << one << "   " << three2 << std::endl;
    std::cout << "pstr two is: ";
    stwo.print_string();
    std::cout << std::endl;
    std::cout << "pstr one is: ";
    sone.print_string();
    std::cout << std::endl;
    std::cout << "pstr ref is: ";
    ref2.print_string();
    std::cout << std::endl;
#endif

    sone.append(stwo);
    sfour.append(sthree);
    auto yon = mmap_lib::str<3>::concat(sfive, ssix);
    auto yok = mmap_lib::str<3>::concat(ssix, sfive);

    // mmap_lib::str    test  = mmap_lib::str::concat(sone, stwo);
    // mmap_lib::str    test2 = mmap_lib::str::concat(sv1, stwo);
    // mmap_lib::str    test3 = mmap_lib::str::concat(sone, sv2);
    // mmap_lib::str    test4 = sone.append(sv2);

#if 0
    std::cout << "one.append(two): ";
    sone.print_string();
    std::cout << std::endl;
#endif

#if 0
    std::cout << "two.append(one): ";
    stwo.print_string();
    std::cout << std::endl;
#endif

    EXPECT_EQ(ref, sone);
    EXPECT_EQ(ref2, sfour);
    EXPECT_EQ(ref, yon);
    EXPECT_EQ(ref2, yok);
    /*
     EXPECT_EQ(ref, test);
     EXPECT_EQ(ref, test2);
     EXPECT_EQ(ref, test3);
     EXPECT_EQ(ref, test4);
     EXPECT_EQ(ref, test5);
   */
  }
}

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
    mmap_lib::str<1>    curr_str(curr);
    mmap_lib::str<2>    next_str(next);
    mmap_lib::str<3>    curr_sub(stable_c);
    mmap_lib::str<1>    next_sub(stable_n);

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
    mmap_lib::str<1> curr_sub_ref(stable_c);
    mmap_lib::str<2> next_sub_ref(stable_n);
    mmap_lib::str<3> curr_str(curr);
    mmap_lib::str<1> next_str(next);
    auto curr_sub = curr_str.substr(start, end);
    auto next_sub = next_str.substr(start2, end2);

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
  std::vector<mmap_lib::str<3>> ref_nu, ref_nd, hold_nu, hold_nd;
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
    ref_nu.push_back(mmap_lib::str<3>(nu_get(i)));  // add to ref_
    ref_nd.push_back(mmap_lib::str<3>(nd_get(i)));  // add to ref-
    longg_nu += nu_get(i);                       // add to long
    longg_nd += nd_get(i);
    --iter;
    if (iter == 0) {  // iter == 0 means times to split
      mmap_lib::str<3> tt_nu(longg_nu);
      mmap_lib::str<3> tt_nd(longg_nd);
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

TEST_F(Mmap_str_test, get_str_before_after) {
  for (auto i = 0; i < RNDN; ++i) {
    std::string   temp = s_get(i);
    mmap_lib::str<1> hold(temp);
    char          ch  = temp[rand() % temp.size()];
    size_t        ffo = temp.find_first_of(ch);
    size_t        flo = temp.find_last_of(ch);

#if 0
    std::cout << "Original string is: " << temp << std::endl;
    std::cout << "Chosen char is: " << ch << std::endl;
    std::cout << "ffo = " << ffo << " flo = " << flo << std::endl;
#endif

    auto gsaf = hold.get_str_after_first(ch);
    auto gsbf = hold.get_str_before_first(ch);
    auto gsal = hold.get_str_after_last(ch);
    auto gsbl = hold.get_str_before_last(ch);

#if 0
    std::cout << "pstr gsaf: ";
    gsaf.print_string();
    std::cout << std::endl;
#endif

    std::string gsaf_stable = temp.substr(ffo + 1, temp.size() - (ffo + 1));
    std::string gsbf_stable = temp.substr(0, ffo);
    std::string gsal_stable = temp.substr(flo + 1, temp.size() - (flo + 1));
    std::string gsbl_stable = temp.substr(0, flo);

    mmap_lib::str<1> gsaf_ref(gsaf_stable);
    mmap_lib::str<2> gsbf_ref(gsbf_stable);
    mmap_lib::str<3> gsal_ref(gsal_stable);
    mmap_lib::str<1> gsbl_ref(gsbl_stable);

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


#if RUN
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
#endif


