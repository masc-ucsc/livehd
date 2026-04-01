//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <memory>

#include "gtest/gtest.h"
#include "lnast.hpp"
#include "lnast_manager.hpp"
#include "upass_assert.hpp"

namespace {

// Exposes the protected cursor so tests can position it before calling
// process_func_call() directly.
class TestableAssert : public uPass_assert {
public:
  using uPass_assert::uPass_assert;
  void position(const Lnast_nid& nid) { move_to_nid(nid); }
};

struct AssertFixture {
  std::shared_ptr<Lnast>                ln;
  std::shared_ptr<upass::Lnast_manager> lm;
  Lnast_nid                             stmts_nid;

  AssertFixture() {
    ln        = std::make_shared<Lnast>("test");
    ln->set_root(Lnast_node::create_top("top"));
    stmts_nid = ln->add_child(Lnast_nid::root(), Lnast_node::create_stmts("stmts"));
    lm        = std::make_shared<upass::Lnast_manager>(ln);
  }

  // Build: func_call ref("_ret") ref("cassert") <arg>
  // arg_is_const: adds const(arg_value), else adds ref(arg_value)
  Lnast_nid add_cassert(int64_t arg_value) {
    auto fc = ln->add_child(stmts_nid, Lnast_node::create_func_call());
    ln->add_child(fc, Lnast_node::create_ref("_ret"));
    ln->add_child(fc, Lnast_node::create_ref("cassert"));
    ln->add_child(fc, Lnast_node::create_const(arg_value));
    return fc;
  }

  // Build cassert with a ref argument (non-const, unknown to the symbol table)
  Lnast_nid add_cassert_ref(std::string_view arg_name) {
    auto fc = ln->add_child(stmts_nid, Lnast_node::create_func_call());
    ln->add_child(fc, Lnast_node::create_ref("_ret"));
    ln->add_child(fc, Lnast_node::create_ref("cassert"));
    ln->add_child(fc, Lnast_node::create_ref(arg_name));
    return fc;
  }
};

// ── cassert(0): statically false ─────────────────────────────────────────

TEST(UpassAssert, CassertFalseThrows) {
  AssertFixture f;
  auto fc = f.add_cassert(0);

  TestableAssert ap(f.lm);
  ap.position(fc);
  EXPECT_THROW(ap.process_func_call(), std::runtime_error);
}

// ── cassert(1): statically true ──────────────────────────────────────────

TEST(UpassAssert, CassertTrueNoThrow) {
  AssertFixture f;
  auto fc = f.add_cassert(1);

  TestableAssert ap(f.lm);
  ap.position(fc);
  EXPECT_NO_THROW(ap.process_func_call());
}

// ── cassert(42): any non-zero constant is true ────────────────────────────

TEST(UpassAssert, CassertNonZeroTrueNoThrow) {
  AssertFixture f;
  auto fc = f.add_cassert(42);

  TestableAssert ap(f.lm);
  ap.position(fc);
  EXPECT_NO_THROW(ap.process_func_call());
}

// ── cassert(x): unknown variable — cannot decide statically ──────────────
// The assert pass must not throw for unknown refs since it can't prove
// the condition false (invalid Lconst has num==0, but is flagged invalid).

TEST(UpassAssert, CassertUnknownRefNoThrow) {
  AssertFixture f;
  auto fc = f.add_cassert_ref("some_unknown_var");

  TestableAssert ap(f.lm);
  ap.position(fc);
  EXPECT_NO_THROW(ap.process_func_call());
}

// ── non-cassert function call: ignored ───────────────────────────────────

TEST(UpassAssert, NonCassertCallIgnored) {
  AssertFixture f;
  auto fc = f.ln->add_child(f.stmts_nid, Lnast_node::create_func_call());
  f.ln->add_child(fc, Lnast_node::create_ref("_ret"));
  f.ln->add_child(fc, Lnast_node::create_ref("some_other_func"));
  f.ln->add_child(fc, Lnast_node::create_const(int64_t(0)));  // arg=0 but not cassert

  TestableAssert ap(f.lm);
  ap.position(fc);
  EXPECT_NO_THROW(ap.process_func_call());  // function name doesn't match "cassert"
}

}  // namespace
