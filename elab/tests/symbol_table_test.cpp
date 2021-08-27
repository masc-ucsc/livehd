
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "lbench.hpp"
#include "lrand.hpp"
#include "symbol_table.hpp"

class Symbol_table_test : public ::testing::Test {
protected:
  void SetUp() override {
    mmap_lib::str::setup(); // needed for overflowing strings
  }

  void TearDown() override {
  }
};


TEST_F(Symbol_table_test, trivial_constants) {

  Symbol_table st;

  st.funcion_scope("myfunc");

  {
    EXPECT_FALSE(st.has_trivial("foo"));
    EXPECT_FALSE(st.has_bundle("foo"));
    auto ok = st.var("foo");
    EXPECT_TRUE(ok);
    EXPECT_TRUE(st.has_trivial("foo"));
    EXPECT_FALSE(st.has_bundle("foo.bar"));
  }

  {

    auto ok = st.mut("foo", Lconst(1));
    EXPECT_TRUE(ok);

    EXPECT_TRUE(st.has_trivial("foo"));
    EXPECT_TRUE(st.has_bundle("foo"));

    EXPECT_EQ(st.get_trivial("foo"),Lconst(1));
  }

  {
    auto ok = st.mut("foo", Lconst(2));
    EXPECT_TRUE(ok);

    EXPECT_EQ(st.get_trivial("foo"),Lconst(2));
  }

  auto out = st.leave_scope();

  EXPECT_TRUE(out == nullptr);
}

TEST_F(Symbol_table_test, recursion) {

  Symbol_table st;

  //------------------------------------------
  st.funcion_scope("myfunc");

  EXPECT_FALSE(st.has_trivial("foo"));
  auto ok = st.var("foo");
  EXPECT_TRUE(ok);
  EXPECT_TRUE(st.has_trivial("foo"));

  auto ok1 = st.mut("foo", Lconst(1));
  EXPECT_TRUE(ok1);
  EXPECT_EQ(st.get_trivial("foo"),Lconst(1));

  //------------------------------------------
  st.funcion_scope("myfunc");
  EXPECT_FALSE(st.has_trivial("foo"));
  auto ok2 = st.var("foo");
  EXPECT_TRUE(ok2);
  EXPECT_TRUE(st.has_trivial("foo"));

  auto ok3 = st.mut("foo", Lconst(2));
  EXPECT_TRUE(ok3);
  EXPECT_EQ(st.get_trivial("foo"),Lconst(2));

  //------------------------------------------
  st.funcion_scope("other_call");
  EXPECT_FALSE(st.has_trivial("foo"));
  auto ok4 = st.var("foo");
  EXPECT_TRUE(ok4);
  EXPECT_TRUE(st.has_trivial("foo"));

  auto ok5 = st.mut("foo", Lconst(3));
  EXPECT_TRUE(ok5);
  EXPECT_EQ(st.get_trivial("foo"),Lconst(3));

  //------------------------------------------
  st.funcion_scope("myfunc");
  EXPECT_FALSE(st.has_trivial("foo"));
  auto ok6 = st.var("foo");
  EXPECT_TRUE(ok6);
  EXPECT_TRUE(st.has_trivial("foo"));

  auto ok7 = st.mut("foo", Lconst(4));
  EXPECT_TRUE(ok7);
  EXPECT_EQ(st.get_trivial("foo"),Lconst(4));

  //------------------------------------------
  st.leave_scope();
  EXPECT_EQ(st.get_trivial("foo"),Lconst(3));

  st.leave_scope();
  EXPECT_EQ(st.get_trivial("foo"),Lconst(2));

  st.leave_scope();
  EXPECT_EQ(st.get_trivial("foo"),Lconst(1));

}
