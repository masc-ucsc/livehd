
#include "symbol_table.hpp"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lrand.hpp"

// Dlop no longer defines operator==; same_repr is the structural-identity
// check (including matching unknowns), which is what these tests actually
// want. Wrap it so EXPECT_TRUE prints something readable on mismatch.
#define EXPECT_DLOP_EQ(lhs, rhs) EXPECT_TRUE((lhs).same_repr(*(rhs))) \
    << "  lhs: " << (lhs).to_pyrope() << "\n  rhs: " << (rhs)->to_pyrope()

class Symbol_table_test : public ::testing::Test {
protected:
  void SetUp() override {}

  void TearDown() override {}
};

TEST_F(Symbol_table_test, trivial_constants) {
  Symbol_table st;

  st.function_scope("myfunc");

  {
    EXPECT_FALSE(st.has_trivial("foo"));
    EXPECT_FALSE(st.has_bundle("foo"));
    auto ok = st.var("foo");
    EXPECT_TRUE(ok);
    EXPECT_TRUE(st.has_trivial("foo"));
    EXPECT_FALSE(st.has_bundle("foo.bar"));
  }

  {
    auto ok = st.mut("foo", Dlop::create_integer(1));
    EXPECT_TRUE(ok);

    EXPECT_TRUE(st.has_trivial("foo"));
    EXPECT_TRUE(st.has_bundle("foo"));

    EXPECT_DLOP_EQ(st.get_trivial("foo"), Dlop::create_integer(1));
  }

  {
    auto ok = st.mut("foo", Dlop::create_integer(2));
    EXPECT_TRUE(ok);

    EXPECT_DLOP_EQ(st.get_trivial("foo"), Dlop::create_integer(2));
  }

  auto out = st.leave_scope();

  EXPECT_TRUE(out == nullptr);
}

TEST_F(Symbol_table_test, recursion) {
  Symbol_table st;

  //------------------------------------------
  st.function_scope("myfunc");

  EXPECT_FALSE(st.has_trivial("foo"));
  auto ok = st.var("foo");
  EXPECT_TRUE(ok);
  EXPECT_TRUE(st.has_trivial("foo"));

  auto ok1 = st.mut("foo", Dlop::create_integer(1));
  EXPECT_TRUE(ok1);
  EXPECT_DLOP_EQ(st.get_trivial("foo"), Dlop::create_integer(1));

  //------------------------------------------
  st.function_scope("myfunc");
  EXPECT_FALSE(st.has_trivial("foo"));
  auto ok2 = st.var("foo");
  EXPECT_TRUE(ok2);
  EXPECT_TRUE(st.has_trivial("foo"));

  auto ok3 = st.mut("foo", Dlop::create_integer(2));
  EXPECT_TRUE(ok3);
  EXPECT_DLOP_EQ(st.get_trivial("foo"), Dlop::create_integer(2));

  //------------------------------------------
  st.function_scope("other_call");
  EXPECT_FALSE(st.has_trivial("foo"));
  auto ok4 = st.var("foo");
  EXPECT_TRUE(ok4);
  EXPECT_TRUE(st.has_trivial("foo"));

  auto ok5 = st.mut("foo", Dlop::create_integer(3));
  EXPECT_TRUE(ok5);
  EXPECT_DLOP_EQ(st.get_trivial("foo"), Dlop::create_integer(3));

  //------------------------------------------
  st.function_scope("myfunc");
  EXPECT_FALSE(st.has_trivial("foo"));
  auto ok6 = st.var("foo");
  EXPECT_TRUE(ok6);
  EXPECT_TRUE(st.has_trivial("foo"));

  auto ok7 = st.mut("foo", Dlop::create_integer(4));
  EXPECT_TRUE(ok7);
  EXPECT_DLOP_EQ(st.get_trivial("foo"), Dlop::create_integer(4));

  //------------------------------------------
  st.leave_scope();
  EXPECT_DLOP_EQ(st.get_trivial("foo"), Dlop::create_integer(3));

  st.leave_scope();
  EXPECT_DLOP_EQ(st.get_trivial("foo"), Dlop::create_integer(2));

  st.leave_scope();
  EXPECT_DLOP_EQ(st.get_trivial("foo"), Dlop::create_integer(1));
}

TEST_F(Symbol_table_test, ordered_check) {
  Symbol_table st;

  st.function_scope("my function with spaces and very log name");

  // Post-bundle_sorted refactor: ":N:name" producers are transitionally
  // accepted but stored as the bare name. Decision 1: positional access on
  // named slots (`tup.0` → first-by-position) is no longer supported; named
  // entries are name-only, unnamed are decimal-index-only.
  st.set("foo.:0:bar", Dlop::create_integer(1));  // stored as "bar"
  st.set("foo.:1:xxx", Dlop::create_integer(2));  // stored as "xxx"
  st.set("foo.2", Dlop::create_integer(3));
  st.set("foo.99", Dlop::create_integer(4));

  auto bundle = st.get_bundle("foo");
  bundle->dump();

  // Named access — both the canonical "name" and the legacy ":N:name"
  // form (during PR1 transitional acceptance) hit the same stored entry.
  EXPECT_DLOP_EQ(bundle->get_trivial("bar"), Dlop::create_integer(1));
  EXPECT_DLOP_EQ(bundle->get_trivial(":1:xxx"), Dlop::create_integer(2));
  EXPECT_DLOP_EQ(bundle->get_trivial("xxx"), Dlop::create_integer(2));
  // Unnamed access by decimal index — entries stored at their position-key.
  EXPECT_DLOP_EQ(bundle->get_trivial("2"), Dlop::create_integer(3));
  EXPECT_DLOP_EQ(bundle->get_trivial("99"), Dlop::create_integer(4));
  // Decision 1: "0" no longer aliases the first named slot.
  EXPECT_FALSE(bundle->has_trivial("0"));

  // "foo" key has no entries matching the prefix (entries live inside this
  // bundle), so is_ordered("foo") is vacuously true.
  EXPECT_TRUE(bundle->is_ordered("foo"));

  st.set("foo.bar", Dlop::create_integer(4));  // replace stored "bar"
  EXPECT_DLOP_EQ(bundle->get_trivial("bar"), Dlop::create_integer(4));
  // Bundle now mixes named ("bar", "xxx") and unnamed ("2", "99") — no
  // longer a pure positional list, so is_ordered("") is false.
  EXPECT_FALSE(bundle->is_ordered(""));

  st.set("foo.nothere", Dlop::create_integer(4));
  EXPECT_FALSE(bundle->is_ordered(""));

  // is_ordered("nothere") inspects the "nothere" sub-bundle's first-segment
  // pos; a leaf entry has no sub-positions so it's vacuously true.
  EXPECT_TRUE(bundle->is_ordered("nothere"));
  st.set("foo.nothere.2", Dlop::create_integer(4));
  EXPECT_TRUE(bundle->is_ordered("nothere"));
  st.set("foo.nothere.x", Dlop::create_integer(4));
  EXPECT_FALSE(bundle->is_ordered("nothere"));

  st.set("foo.bar.0.xx", Dlop::create_integer(10));
  st.set("foo.bar.1.yy", Dlop::create_integer(11));
  st.set("foo.bar.2.xx", Dlop::create_integer(12));
  st.set("foo.bar.8.zz", Dlop::create_integer(13));
  EXPECT_TRUE(bundle->is_ordered("bar"));
  st.set("foo.bar.NOT.zz", Dlop::create_integer(13));
  EXPECT_FALSE(bundle->is_ordered("bar"));

  st.leave_scope();
}
