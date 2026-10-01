//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Tests `cert_dag_order`: the ONE dependency order the certificate is emitted
// in, taken over the combined DAG (ordinary nodes plus the synthetic nodes a
// Memory decomposes into) rather than over the LGraph node vector.
//
// WHY THIS IS A UNIT TEST AND NOT AN RTL ONE.  The case that matters most --
// a read port FORWARDED from the very write port whose data it feeds -- is not
// expressible in any program-ordered source language.  For a read to see a
// write's data the write must come first; for the write's data to see the read
// the read must come first.  Pyrope and SystemVerilog both make it
// unreachable, so the only way to pin the guard is to hand it the certificate
// DAG that shape produces.  That is also what lets the forwarding and
// non-forwarding cases differ in EXACTLY ONE dependency, which is the
// difference `memory_fwd_bit` decides.
//
// The LGraph-level consequence is covered from the other side by
// lhd/tests/mem_async_read_feedback.v and mem_sync_read_feedback.v, which are
// the real-RTL shapes this ordering has to accept.

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "lean_common.hpp"

using livehd::lean_ir::cert_dag_order;
using livehd::lean_ir::CertNodeInfo;
using livehd::lean_ir::CertPool;

namespace {

CertNodeInfo mk(uint32_t id, std::vector<uint32_t> deps, std::string op = "LGraphOp.Op_And") {
  CertNodeInfo i;
  i.nid     = id;
  i.op_expr = std::move(op);
  i.width   = 8;
  i.deps    = std::move(deps);
  return i;
}

void add(CertPool& p, uint32_t id, std::vector<uint32_t> deps, std::string what = {}) {
  p.add(id, "{ nid := " + std::to_string(id) + " }", mk(id, std::move(deps)), std::move(what));
}

// Position of `id` in the result, or -1.
int pos(const std::vector<uint32_t>& order, uint32_t id) {
  for (size_t i = 0; i < order.size(); ++i) {
    if (order[i] == id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

// The ids a memory decomposition uses, named the way the pass names them.
constexpr uint32_t SRC_ARRAY = 9000;  // source: the committed array image
constexpr uint32_t SRC_IN    = 9001;  // source: a primary input
constexpr uint32_t ADDR_R    = 10;    // ordinary node: the read address cone
constexpr uint32_t ADDR_W    = 11;    // ordinary node: the write address cone
constexpr uint32_t EN_W      = 12;    // ordinary node: the write enable cone
constexpr uint32_t READ      = 20;    // synthetic: Op_MemRead for read port 0
constexpr uint32_t DIN_W     = 30;    // ordinary node: the write DATA cone
constexpr uint32_t CHAIN     = 40;    // synthetic: Op_MemWrite, the write chain

// The shape minion_frontend_thread_buffer has, as a certificate DAG:
// the write data is computed FROM the read output.  `forwarded` is the single
// bit `memory_fwd_bit` decides -- whether the read sees the committed array or
// the chain that includes this write.
CertPool feedback_pool(bool forwarded) {
  CertPool p;
  add(p, ADDR_R, {SRC_IN}, "read port 0 address of memory n_7 (mem)");
  add(p, ADDR_W, {SRC_IN}, "write port 0 address of memory n_7 (mem)");
  add(p, EN_W, {SRC_IN}, "write port 0 enable of memory n_7 (mem)");
  add(p, READ, forwarded ? std::vector<uint32_t>{CHAIN, ADDR_R} : std::vector<uint32_t>{SRC_ARRAY, ADDR_R},
      "read port 0 data of memory n_7 (mem)");
  add(p, DIN_W, {READ}, "write port 0 data of memory n_7 (mem)");
  add(p, CHAIN, {SRC_ARRAY, ADDR_W, DIN_W, EN_W}, "write chain step 0 of memory n_7 (mem)");
  return p;
}

const std::set<uint32_t> kSources = {SRC_ARRAY, SRC_IN};

}  // namespace

// A seed that is ALREADY a dependency order must come back unchanged.  This is
// what keeps every memory-free design's emitted text byte-identical: those
// designs are seeded in the LGraph topological order and never move.
TEST(CertDagOrder, IdentityOnAnAlreadyOrderedSeed) {
  CertPool p;
  add(p, 1, {SRC_IN});
  add(p, 2, {1});
  add(p, 3, {1, 2});
  auto r = cert_dag_order(p, kSources);
  ASSERT_TRUE(r.ok) << r.reason;
  EXPECT_EQ(r.order, (std::vector<uint32_t>{1, 2, 3}));
}

// ...and a seed that is NOT ordered is sorted, rather than accepted as-is.
TEST(CertDagOrder, SortsAnOutOfOrderSeed) {
  CertPool p;
  add(p, 3, {1, 2});
  add(p, 2, {1});
  add(p, 1, {SRC_IN});
  auto r = cert_dag_order(p, kSources);
  ASSERT_TRUE(r.ok) << r.reason;
  ASSERT_EQ(r.order.size(), 3u);
  EXPECT_LT(pos(r.order, 1), pos(r.order, 2));
  EXPECT_LT(pos(r.order, 2), pos(r.order, 3));
}

// THE CASE.  A non-forwarding read whose output feeds the write data is
// ORDERABLE: the read depends on the committed array, not on the chain.
TEST(CertDagOrder, NonForwardingReadFeedingItsOwnWriteIsOrdered) {
  auto r = cert_dag_order(feedback_pool(false), kSources);
  ASSERT_TRUE(r.ok) << r.reason;
  EXPECT_LT(pos(r.order, ADDR_R), pos(r.order, READ));
  EXPECT_LT(pos(r.order, READ), pos(r.order, DIN_W));
  EXPECT_LT(pos(r.order, DIN_W), pos(r.order, CHAIN));
}

// ...and the SAME pool with the one forwarding dependency added is a real
// cycle in the model, and must be refused.  The two pools differ in exactly
// the dependency `memory_fwd_bit` selects.
TEST(CertDagOrder, ForwardedReadFeedingItsOwnWriteIsRefused) {
  auto r = cert_dag_order(feedback_pool(true), kSources);
  ASSERT_FALSE(r.ok);
  EXPECT_NE(r.reason.find("CERTIFICATE CYCLE"), std::string::npos) << r.reason;
  // The diagnostic must name the memory and the ports, not just two integers:
  // "cert id 40" alone does not tell anyone which memory to look at.
  EXPECT_NE(r.reason.find("memory n_7 (mem)"), std::string::npos) << r.reason;
  EXPECT_NE(r.reason.find("read port 0 data"), std::string::npos) << r.reason;
}

// A SYNCHRONOUS read's output is a register SOURCE, not a certificate node, so
// the same feedback shape carries no order at all and cannot cycle -- even
// when the write chain is what feeds the register's next value.
TEST(CertDagOrder, SyncReadOutputIsASourceAndBreaksTheLoop) {
  constexpr uint32_t SRC_RDREG = 9002;  // source: the read-data register
  CertPool           p;
  add(p, ADDR_R, {SRC_IN}, "read port 0 address of memory n_7 (mem)");
  add(p, DIN_W, {SRC_RDREG}, "write port 0 data of memory n_7 (mem)");
  add(p, CHAIN, {SRC_ARRAY, DIN_W}, "write chain step 0 of memory n_7 (mem)");
  // the register's NEXT value reads through the chain -- a dependency, but on
  // the register's next node, never on the register source the consumers see.
  add(p, 50, {CHAIN, ADDR_R}, "read port 0 raw read of memory n_7 (mem)");
  add(p, 51, {50}, "read port 0 register next of memory n_7 (mem)");
  auto r = cert_dag_order(p, {SRC_ARRAY, SRC_IN, SRC_RDREG});
  ASSERT_TRUE(r.ok) << r.reason;
  EXPECT_LT(pos(r.order, DIN_W), pos(r.order, CHAIN));
  EXPECT_LT(pos(r.order, CHAIN), pos(r.order, 50));
}

// An ordinary combinational cycle with no memory in it is still a cycle.
TEST(CertDagOrder, PlainCombinationalCycleIsRefused) {
  CertPool p;
  add(p, 1, {2});
  add(p, 2, {1});
  auto r = cert_dag_order(p, kSources);
  ASSERT_FALSE(r.ok);
  EXPECT_NE(r.reason.find("CERTIFICATE CYCLE"), std::string::npos) << r.reason;
}

TEST(CertDagOrder, SelfDependencyIsRefused) {
  CertPool p;
  add(p, 1, {1});
  auto r = cert_dag_order(p, kSources);
  ASSERT_FALSE(r.ok);
  EXPECT_NE(r.reason.find("CERTIFICATE CYCLE"), std::string::npos) << r.reason;
}

// A dependency that is neither a certificate node nor a source would make the
// emitted model reference a value nothing defines.  Refuse rather than emit it.
TEST(CertDagOrder, DanglingDependencyIsRefused) {
  CertPool p;
  add(p, 1, {4242});
  auto r = cert_dag_order(p, kSources);
  ASSERT_FALSE(r.ok);
  EXPECT_NE(r.reason.find("neither a certificate node nor a source"), std::string::npos) << r.reason;
}

// Every node reachable from the seed is emitted exactly once, including one
// reached only as a dependency of a later seed entry.
TEST(CertDagOrder, EmitsEveryNodeExactlyOnce) {
  CertPool p;
  add(p, 1, {SRC_IN});
  add(p, 2, {1});
  add(p, 3, {2});
  add(p, 4, {2, 3});
  auto r = cert_dag_order(p, kSources);
  ASSERT_TRUE(r.ok) << r.reason;
  EXPECT_EQ(r.order, (std::vector<uint32_t>{1, 2, 3, 4}));
}

// ---------------------------------------------------------------------------
// TWO MEMORIES.  A memory's port pins may be driven by ANOTHER memory's read
// output, so the read ids of every memory must exist before any memory's
// dependencies are built, and the order must not depend on which memory the
// graph happens to hand back first.
// ---------------------------------------------------------------------------
namespace {

constexpr uint32_t SRC_ARRAY_B = 9100;
constexpr uint32_t ADDR_RB     = 110;  // memory B's read address cone
constexpr uint32_t READ_B      = 120;  // memory B's read data
constexpr uint32_t ADDR_WB     = 111;
constexpr uint32_t DIN_WB      = 130;
constexpr uint32_t CHAIN_B     = 140;

// B's read feeds A's read ADDRESS, and A's read feeds A's write data.  Neither
// read is forwarded, so this is acyclic -- and A cannot be built before B's
// read id exists.
CertPool two_memories_acyclic() {
  CertPool p;
  p.add(ADDR_RB, "{}", mk(ADDR_RB, {SRC_IN}), "read port 0 address of memory n_9 (memB)");
  p.add(READ_B, "{}", mk(READ_B, {SRC_ARRAY_B, ADDR_RB}), "read port 0 data of memory n_9 (memB)");
  p.add(ADDR_R, "{}", mk(ADDR_R, {READ_B}), "read port 0 address of memory n_7 (memA)");
  p.add(READ, "{}", mk(READ, {SRC_ARRAY, ADDR_R}), "read port 0 data of memory n_7 (memA)");
  p.add(DIN_W, "{}", mk(DIN_W, {READ}), "write port 0 data of memory n_7 (memA)");
  p.add(ADDR_W, "{}", mk(ADDR_W, {SRC_IN}), "write port 0 address of memory n_7 (memA)");
  p.add(EN_W, "{}", mk(EN_W, {SRC_IN}), "write port 0 enable of memory n_7 (memA)");
  p.add(CHAIN, "{}", mk(CHAIN, {SRC_ARRAY, ADDR_W, DIN_W, EN_W}), "write chain step 0 of memory n_7 (memA)");
  return p;
}

// Both reads ARE forwarded, and each memory's write data comes from the other
// memory's read.  That is a genuine cycle spanning two memories.
CertPool two_memories_forwarding_cycle() {
  CertPool p;
  p.add(ADDR_R, "{}", mk(ADDR_R, {SRC_IN}), "read port 0 address of memory n_7 (memA)");
  p.add(ADDR_RB, "{}", mk(ADDR_RB, {SRC_IN}), "read port 0 address of memory n_9 (memB)");
  p.add(ADDR_W, "{}", mk(ADDR_W, {SRC_IN}), "write port 0 address of memory n_7 (memA)");
  p.add(ADDR_WB, "{}", mk(ADDR_WB, {SRC_IN}), "write port 0 address of memory n_9 (memB)");
  p.add(EN_W, "{}", mk(EN_W, {SRC_IN}), "write port 0 enable of memory n_7 (memA)");
  p.add(READ, "{}", mk(READ, {CHAIN, ADDR_R}), "read port 0 data of memory n_7 (memA)");
  p.add(READ_B, "{}", mk(READ_B, {CHAIN_B, ADDR_RB}), "read port 0 data of memory n_9 (memB)");
  p.add(DIN_W, "{}", mk(DIN_W, {READ_B}), "write port 0 data of memory n_7 (memA)");
  p.add(DIN_WB, "{}", mk(DIN_WB, {READ}), "write port 0 data of memory n_9 (memB)");
  p.add(CHAIN, "{}", mk(CHAIN, {SRC_ARRAY, ADDR_W, DIN_W, EN_W}), "write chain step 0 of memory n_7 (memA)");
  p.add(CHAIN_B, "{}", mk(CHAIN_B, {SRC_ARRAY_B, ADDR_WB, DIN_WB}), "write chain step 0 of memory n_9 (memB)");
  return p;
}

const std::set<uint32_t> kSources2 = {SRC_ARRAY, SRC_ARRAY_B, SRC_IN};

}  // namespace

TEST(CertDagOrderTwoMemories, OneMemoryFeedingTheOthersAddressIsOrdered) {
  auto r = cert_dag_order(two_memories_acyclic(), kSources2);
  ASSERT_TRUE(r.ok) << r.reason;
  EXPECT_LT(pos(r.order, READ_B), pos(r.order, ADDR_R));
  EXPECT_LT(pos(r.order, ADDR_R), pos(r.order, READ));
  EXPECT_LT(pos(r.order, READ), pos(r.order, DIN_W));
  EXPECT_LT(pos(r.order, DIN_W), pos(r.order, CHAIN));
}

// ...and the order must not depend on which memory came first.  Same pool,
// reversed seed: still ordered, and still with B's read before A's address.
TEST(CertDagOrderTwoMemories, OrderDoesNotDependOnWhichMemoryIsSeededFirst) {
  auto p = two_memories_acyclic();
  std::reverse(p.seed.begin(), p.seed.end());
  auto r = cert_dag_order(p, kSources2);
  ASSERT_TRUE(r.ok) << r.reason;
  EXPECT_LT(pos(r.order, READ_B), pos(r.order, ADDR_R));
  EXPECT_LT(pos(r.order, ADDR_R), pos(r.order, READ));
  EXPECT_LT(pos(r.order, DIN_W), pos(r.order, CHAIN));
}

TEST(CertDagOrderTwoMemories, ForwardingCycleAcrossTwoMemoriesIsRefused) {
  auto r = cert_dag_order(two_memories_forwarding_cycle(), kSources2);
  ASSERT_FALSE(r.ok);
  EXPECT_NE(r.reason.find("CERTIFICATE CYCLE"), std::string::npos) << r.reason;
  // It must name a memory, so a reader knows where to look.
  EXPECT_TRUE(r.reason.find("memory n_7 (memA)") != std::string::npos
              || r.reason.find("memory n_9 (memB)") != std::string::npos)
      << r.reason;
}

// ---------------------------------------------------------------------------
// A malformed pool is refused, not quietly sorted.  `by_id`, `text` and `seed`
// are three views of one set; an id in only some of them changes the model.
// ---------------------------------------------------------------------------
TEST(CertPoolValidation, ReservingTheSameIdTwiceIsRefused) {
  CertPool p;
  add(p, 1, {SRC_IN});
  add(p, 1, {SRC_IN});  // an id collision in the synthetic space
  auto r = cert_dag_order(p, kSources);
  ASSERT_FALSE(r.ok);
  EXPECT_NE(r.reason.find("reserved twice"), std::string::npos) << r.reason;
}

// The one that would be SILENT: the sort skips a source as a leaf, so a node
// that is also a source never has its operator evaluated.
TEST(CertPoolValidation, ANodeThatIsAlsoASourceIsRefused) {
  CertPool p;
  add(p, 1, {SRC_IN});
  add(p, SRC_ARRAY, {SRC_IN});
  auto r = cert_dag_order(p, kSources);
  ASSERT_FALSE(r.ok);
  EXPECT_NE(r.reason.find("both a certificate node and a source"), std::string::npos) << r.reason;
}

TEST(CertPoolValidation, AReservedButUnfilledIdIsRefused) {
  CertPool p;
  add(p, 1, {SRC_IN});
  p.reserve(2, "read port 0 data of memory n_7 (mem)");  // never filled
  auto r = cert_dag_order(p, kSources);
  ASSERT_FALSE(r.ok);
  EXPECT_NE(r.reason.find("reserved but never filled in"), std::string::npos) << r.reason;
  EXPECT_NE(r.reason.find("memory n_7 (mem)"), std::string::npos) << r.reason;
}

TEST(CertPoolValidation, AFilledButUnreservedIdIsRefused) {
  CertPool p;
  add(p, 1, {SRC_IN});
  p.fill(2, "{}", mk(2, {1}));  // never reserved, so it has no place in the order
  auto r = cert_dag_order(p, kSources);
  ASSERT_FALSE(r.ok);
  EXPECT_NE(r.reason.find("never reserved"), std::string::npos) << r.reason;
}

TEST(CertPoolValidation, AnInfoStoredUnderTheWrongKeyIsRefused) {
  CertPool p;
  p.add(1, "{}", mk(77, {SRC_IN}));
  auto r = cert_dag_order(p, kSources);
  ASSERT_FALSE(r.ok);
  EXPECT_NE(r.reason.find("different key than its own nid"), std::string::npos) << r.reason;
}

// reserve() + fill() is exactly add(), which is what the memory expansion uses
// when it must claim an id before it can build that port's dependencies.
TEST(CertPoolValidation, ReserveThenFillIsWellFormed) {
  CertPool p;
  p.reserve(2, "read port 0 data of memory n_7 (mem)");
  p.reserve(1);
  p.fill(1, "{}", mk(1, {SRC_IN}));
  p.fill(2, "{}", mk(2, {1}));
  EXPECT_TRUE(p.errors(kSources).empty());
  auto r = cert_dag_order(p, kSources);
  ASSERT_TRUE(r.ok) << r.reason;
  EXPECT_LT(pos(r.order, 1), pos(r.order, 2));
}
