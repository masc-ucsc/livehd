
#include "symbol_table.hpp"

#include "bundle.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lrand.hpp"

// Dlop no longer defines operator==; same_repr is the structural-identity
// check (including matching unknowns), which is what these tests actually
// want. Wrap it so EXPECT_TRUE prints something readable on mismatch.
#define EXPECT_DLOP_EQ(lhs, rhs) \
  EXPECT_TRUE((lhs).same_repr(*(rhs))) << "  lhs: " << (lhs).to_pyrope() << "\n  rhs: " << (rhs)->to_pyrope()

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

  // Post-bundle_sorted refactor (PR3): ":N:name" is no longer accepted;
  // named entries are stored by their bare name, unnamed entries by their
  // bare decimal index. Decision 1: positional access on named slots
  // (`tup.0` → first-by-position) is no longer supported.
  st.set("foo.bar", Dlop::create_integer(1));
  st.set("foo.xxx", Dlop::create_integer(2));
  st.set("foo.2", Dlop::create_integer(3));
  st.set("foo.99", Dlop::create_integer(4));

  auto bundle = st.get_bundle("foo");
  bundle->dump();

  // Named access — name-only.
  EXPECT_DLOP_EQ(bundle->get_trivial("bar"), Dlop::create_integer(1));
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

// bundle_flat_storage Phase 1 — Canonical_less comparator semantics.
TEST_F(Symbol_table_test, canonical_less_segment_category) {
  Bundle::Canonical_less less;

  // attrs first
  EXPECT_TRUE(less("__bits", "a"));
  EXPECT_FALSE(less("a", "__bits"));

  // named before unnamed
  EXPECT_TRUE(less("a", "0"));
  EXPECT_FALSE(less("0", "a"));

  // attrs first vs unnamed
  EXPECT_TRUE(less("__bits", "0"));
  EXPECT_FALSE(less("0", "__bits"));
}

TEST_F(Symbol_table_test, canonical_less_numeric_within_unnamed) {
  Bundle::Canonical_less less;

  // numeric, not lex: "2" < "10"
  EXPECT_TRUE(less("2", "10"));
  EXPECT_FALSE(less("10", "2"));

  // and at nested depth
  EXPECT_TRUE(less("a.2", "a.10"));
  EXPECT_FALSE(less("a.10", "a.2"));
}

TEST_F(Symbol_table_test, canonical_less_alpha_within_named) {
  Bundle::Canonical_less less;

  EXPECT_TRUE(less("alpha", "beta"));
  EXPECT_FALSE(less("beta", "alpha"));
  EXPECT_TRUE(less("a.foo", "a.zzz"));
  EXPECT_FALSE(less("a.zzz", "a.foo"));
}

TEST_F(Symbol_table_test, canonical_less_prefix_orders_first) {
  Bundle::Canonical_less less;

  // shorter prefix sorts before its extensions at the same level
  EXPECT_TRUE(less("a", "a.b"));
  EXPECT_FALSE(less("a.b", "a"));
}

TEST_F(Symbol_table_test, canonical_less_attrs_within_named_subtree) {
  Bundle::Canonical_less less;

  // a.__bits sorts before a.0, a.foo (attrs first within level)
  EXPECT_TRUE(less("a.__bits", "a.foo"));
  EXPECT_TRUE(less("a.__bits", "a.0"));
  EXPECT_FALSE(less("a.foo", "a.__bits"));
}

TEST_F(Symbol_table_test, canonical_less_strict_weak_ordering) {
  Bundle::Canonical_less less;

  // irreflexive
  EXPECT_FALSE(less("a", "a"));
  EXPECT_FALSE(less("a.b.0", "a.b.0"));

  // antisymmetric on a sample
  for (auto k1 : {"__bits", "a", "a.0", "a.10", "a.2", "a.__bits", "a.b.0", "b", "0", "10", "2"}) {
    for (auto k2 : {"__bits", "a", "a.0", "a.10", "a.2", "a.__bits", "a.b.0", "b", "0", "10", "2"}) {
      if (less(k1, k2)) {
        EXPECT_FALSE(less(k2, k1)) << k1 << " vs " << k2;
      }
    }
  }
}

// bundle_flat_storage Phase 1 — Top_level_entry collapse rule.
TEST_F(Symbol_table_test, top_levels_collapse_and_has_leafs) {
  auto bundle = std::make_shared<Bundle>("tlcollapse");

  // scalar 'a', single-named-sub 'f.foo', multi-unnamed 'x.0' + 'x.1'.
  bundle->set("a",     *Dlop::create_integer(3));
  bundle->set("f.foo", *Dlop::create_integer(44));
  bundle->set("x.0",   *Dlop::create_integer(10));
  bundle->set("x.1",   *Dlop::create_integer(20));

  auto tls = bundle->top_levels();
  ASSERT_EQ(tls.size(), 3u);

  // canonical order: named alphabetical first
  auto it = tls.begin();
  EXPECT_EQ(it->name, "a");
  EXPECT_EQ(it->pos, -1);
  EXPECT_EQ(it->leaf_count, 1u);
  EXPECT_FALSE(it->has_leafs);
  EXPECT_TRUE(it->scalar.same_repr(*Dlop::create_integer(3)));

  ++it;
  EXPECT_EQ(it->name, "f");
  EXPECT_EQ(it->leaf_count, 1u);
  EXPECT_TRUE(it->has_leafs);
  EXPECT_TRUE(it->scalar.same_repr(*Dlop::create_integer(44)));

  ++it;
  EXPECT_EQ(it->name, "x");
  EXPECT_EQ(it->leaf_count, 2u);
  EXPECT_TRUE(it->has_leafs);
  EXPECT_TRUE(it->scalar.is_invalid());
}

TEST_F(Symbol_table_test, top_levels_named_and_unnamed_orthogonal) {
  auto bundle = std::make_shared<Bundle>("orthogonal");

  bundle->set("b", *Dlop::create_integer(3));
  bundle->set("0", *Dlop::create_integer(0));

  auto tls = bundle->top_levels();
  ASSERT_EQ(tls.size(), 2u);

  // canonical order: named ('b') before unnamed ('0')
  auto it = tls.begin();
  EXPECT_EQ(it->name, "b");
  EXPECT_EQ(it->pos, -1);
  EXPECT_TRUE(it->scalar.same_repr(*Dlop::create_integer(3)));

  ++it;
  EXPECT_EQ(it->pos, 0);
  EXPECT_TRUE(it->name.empty());
  EXPECT_TRUE(it->scalar.same_repr(*Dlop::create_integer(0)));

  EXPECT_TRUE(bundle->has_top_named("b"));
  EXPECT_FALSE(bundle->has_top_named("a"));
  EXPECT_TRUE(bundle->has_top_unnamed(0));
  EXPECT_FALSE(bundle->has_top_unnamed(1));
}

TEST_F(Symbol_table_test, entries_view_skips_attrs) {
  auto bundle = std::make_shared<Bundle>("attrs_skip");

  bundle->set("a",         *Dlop::create_integer(3));
  bundle->set("a.__bits",  *Dlop::create_integer(8));
  bundle->set("b",         *Dlop::create_integer(5));

  auto all = bundle->entries();
  auto na  = bundle->non_attr_entries();
  auto at  = bundle->get_attrs();

  EXPECT_EQ(all.size(), 3u);
  EXPECT_EQ(na.size(), 2u);
  EXPECT_EQ(at.size(), 1u);

  auto tls = bundle->top_levels();
  EXPECT_EQ(tls.size(), 2u);  // 'a' and 'b' only
}
