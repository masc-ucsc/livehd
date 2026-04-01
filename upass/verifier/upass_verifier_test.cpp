//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <memory>

#include "gtest/gtest.h"
#include "lnast.hpp"
#include "lnast_manager.hpp"
#include "upass_verifier.hpp"

namespace {

// Exposes the protected cursor so tests can position it before calling
// process_* methods directly.
class TestableVerifier : public uPass_verifier {
public:
  using uPass_verifier::uPass_verifier;
  void position(const Lnast_nid& nid) { move_to_nid(nid); }
};

struct VerifierFixture {
  std::shared_ptr<Lnast>                ln;
  std::shared_ptr<upass::Lnast_manager> lm;
  Lnast_nid                             stmts_nid;

  VerifierFixture() {
    ln        = std::make_shared<Lnast>("test");
    ln->set_root(Lnast_node::create_top("top"));
    stmts_nid = ln->add_child(Lnast_nid::root(), Lnast_node::create_stmts("stmts"));
    lm        = std::make_shared<upass::Lnast_manager>(ln);
  }
};

// ── check_binary: well-formed ─────────────────────────────────────────────

TEST(UpassVerifier, WellFormedBinaryNoThrow) {
  VerifierFixture f;
  // plus ref("dst") const(1) const(2)   ← exactly 3 children
  auto op = f.ln->add_child(f.stmts_nid, Lnast_node::create_plus());
  f.ln->add_child(op, Lnast_node::create_ref("dst"));
  f.ln->add_child(op, Lnast_node::create_const(int64_t(1)));
  f.ln->add_child(op, Lnast_node::create_const(int64_t(2)));

  TestableVerifier vf(f.lm);
  vf.position(op);
  EXPECT_NO_THROW(vf.process_plus());
}

// ── check_binary: missing second operand (only 2 children) ───────────────

TEST(UpassVerifier, BinaryMissingSecondOperandThrows) {
  VerifierFixture f;
  // plus ref("dst") const(1)   ← only 2 children — missing second operand
  auto op = f.ln->add_child(f.stmts_nid, Lnast_node::create_plus());
  f.ln->add_child(op, Lnast_node::create_ref("dst"));
  f.ln->add_child(op, Lnast_node::create_const(int64_t(1)));

  TestableVerifier vf(f.lm);
  vf.position(op);
  EXPECT_THROW(vf.process_plus(), std::runtime_error);
}

// ── check_binary: extra operand (4 children) ─────────────────────────────

TEST(UpassVerifier, BinaryExtraOperandThrows) {
  VerifierFixture f;
  // plus ref("dst") const(1) const(2) const(3)   ← 4 children — extra
  auto op = f.ln->add_child(f.stmts_nid, Lnast_node::create_plus());
  f.ln->add_child(op, Lnast_node::create_ref("dst"));
  f.ln->add_child(op, Lnast_node::create_const(int64_t(1)));
  f.ln->add_child(op, Lnast_node::create_const(int64_t(2)));
  f.ln->add_child(op, Lnast_node::create_const(int64_t(3)));

  TestableVerifier vf(f.lm);
  vf.position(op);
  EXPECT_THROW(vf.process_plus(), std::runtime_error);
}

// ── check_binary: wrong dest type (const instead of ref) ─────────────────

TEST(UpassVerifier, BinaryWrongDestTypeThrows) {
  VerifierFixture f;
  // plus const("1") const(2) const(3)   ← dest is const, not ref
  auto op = f.ln->add_child(f.stmts_nid, Lnast_node::create_plus());
  f.ln->add_child(op, Lnast_node::create_const(int64_t(1)));
  f.ln->add_child(op, Lnast_node::create_const(int64_t(2)));
  f.ln->add_child(op, Lnast_node::create_const(int64_t(3)));

  TestableVerifier vf(f.lm);
  vf.position(op);
  EXPECT_THROW(vf.process_plus(), std::runtime_error);
}

// ── check_unary: well-formed ──────────────────────────────────────────────

TEST(UpassVerifier, WellFormedUnaryNoThrow) {
  VerifierFixture f;
  // assign ref("dst") const(5)   ← exactly 2 children
  auto op = f.ln->add_child(f.stmts_nid, Lnast_node::create_assign());
  f.ln->add_child(op, Lnast_node::create_ref("dst"));
  f.ln->add_child(op, Lnast_node::create_const(int64_t(5)));

  TestableVerifier vf(f.lm);
  vf.position(op);
  EXPECT_NO_THROW(vf.process_assign());
}

// ── check_unary: extra child (3 children for assign) ─────────────────────

TEST(UpassVerifier, UnaryExtraChildThrows) {
  VerifierFixture f;
  // assign ref("dst") const(5) const(6)   ← 3 children — extra
  auto op = f.ln->add_child(f.stmts_nid, Lnast_node::create_assign());
  f.ln->add_child(op, Lnast_node::create_ref("dst"));
  f.ln->add_child(op, Lnast_node::create_const(int64_t(5)));
  f.ln->add_child(op, Lnast_node::create_const(int64_t(6)));

  TestableVerifier vf(f.lm);
  vf.position(op);
  EXPECT_THROW(vf.process_assign(), std::runtime_error);
}

// ── check_binary: bit_not with well-formed unary ─────────────────────────

TEST(UpassVerifier, WellFormedBitNotUnaryNoThrow) {
  VerifierFixture f;
  // bit_not ref("dst") const(5)
  auto op = f.ln->add_child(f.stmts_nid, Lnast_node::create_bit_not());
  f.ln->add_child(op, Lnast_node::create_ref("dst"));
  f.ln->add_child(op, Lnast_node::create_const(int64_t(5)));

  TestableVerifier vf(f.lm);
  vf.position(op);
  EXPECT_NO_THROW(vf.process_bit_not());
}

}  // namespace
