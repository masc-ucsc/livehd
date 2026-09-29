// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "abc_lnet.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <string>
#include <vector>

#include "lnet.hpp"

// clang-format off
extern "C" {
#include "base/abc/abc.h"
#include "base/main/main.h"
}
// clang-format on

namespace {
using livehd::abc::Lnet_names;
using livehd::abc::lnet_to_abc;
using livehd::synth::Lid;
using livehd::synth::Lnet;

// ABC's network conversions run inside a frame, as every pass.abc session does.
class AbcLnet : public testing::Test {
protected:
  Abc_Frame_t* frame    = nullptr;
  Abc_Frame_t* previous = nullptr;
  void         SetUp() override {
    frame = Abc_FrameCreate();
    ASSERT_NE(frame, nullptr);
    previous = Abc_FrameEnter(frame);
  }
  void TearDown() override {
    Abc_FrameLeave(previous);
    Abc_FrameDestroy(frame);
  }
};

struct Network {
  Abc_Ntk_t* ntk;
  explicit Network(Abc_Ntk_t* n) : ntk(n) {}
  ~Network() { Abc_NtkDelete(ntk); }
};

// The CO values of a netlist for one CI assignment (CI order: PIs, then
// latch outputs).
std::vector<int> simulate(Abc_Ntk_t* netlist, std::vector<int> ci) {
  Network          logic(Abc_NtkToLogic(netlist));
  int*             values = Abc_NtkVerifySimulatePattern(logic.ntk, ci.data());
  std::vector<int> out(values, values + Abc_NtkCoNum(logic.ntk));
  free(values);
  return out;
}

std::string net_name(Abc_Obj_t* obj) { return Abc_ObjName(obj); }

// RAW replay: the objects come back in the Lnet's creation order, an output
// in the position it was recorded, a latch with its name and init, and the
// function is the Lnet's.
TEST_F(AbcLnet, RawReplayKeepsOrderNamesAndFunction) {
  Lnet       net;
  const auto latch = net.add_latch("q_%r0_0", '1');
  const auto q     = net.latch(latch).q;
  const auto a     = net.add_input("a_b0");
  const auto b     = net.add_input({});
  const auto zero  = net.add_constant(false);
  const auto g     = net.add_lut({a, b}, Lnet::kAnd2);
  net.add_output(g, "__po0_y_b0");
  const auto n = net.add_lut({q}, Lnet::kNot);
  const auto x = net.add_lut({n, a}, Lnet::kXor2);
  const auto o = net.add_lut({x, zero}, Lnet::kOr2);
  net.add_output(o, "__po0_y_b1");
  net.set_latch_input(latch, g);

  Network ntk(static_cast<Abc_Ntk_t*>(lnet_to_abc(net, "region")));
  Abc_NtkFinalizeRead(ntk.ntk);
  ASSERT_TRUE(Abc_NtkCheck(ntk.ntk));
  EXPECT_STREQ(ntk.ntk->pName, "region");
  ASSERT_EQ(Abc_NtkPiNum(ntk.ntk), 2);
  ASSERT_EQ(Abc_NtkPoNum(ntk.ntk), 2);
  ASSERT_EQ(Abc_NtkLatchNum(ntk.ntk), 1);
  EXPECT_EQ(net_name(Abc_ObjFanout0(Abc_NtkPi(ntk.ntk, 0))), "a_b0");
  EXPECT_EQ(net_name(Abc_ObjFanin0(Abc_NtkPo(ntk.ntk, 0))), "__po0_y_b0");
  EXPECT_EQ(net_name(Abc_ObjFanin0(Abc_NtkPo(ntk.ntk, 1))), "__po0_y_b1");
  auto* l = Abc_NtkBox(ntk.ntk, 0);
  EXPECT_TRUE(Abc_LatchIsInit1(l));
  EXPECT_EQ(net_name(Abc_ObjFanout0(Abc_ObjFanout0(l))), "q_%r0_0");
  // The first output was recorded before the nodes after g, so its PO is
  // created before them (not at the end with the second).
  auto* last_or = Abc_ObjFanin0(Abc_ObjFanin0(Abc_NtkPo(ntk.ntk, 1)));  // PO <- alias net <- node
  ASSERT_TRUE(Abc_ObjIsNode(last_or));
  EXPECT_LT(Abc_ObjId(Abc_NtkPo(ntk.ntk, 0)), Abc_ObjId(last_or));

  // CIs: a, b, q. COs: y0 = a&b, y1 = ~q ^ a, then the latch input a&b.
  for (int bits = 0; bits < 8; ++bits) {
    const int  va = bits & 1, vb = (bits >> 1) & 1, vq = (bits >> 2) & 1;
    const auto co = simulate(ntk.ntk, {va, vb, vq});
    ASSERT_EQ(co.size(), 3u);
    EXPECT_EQ(co[0], va & vb) << bits;
    EXPECT_EQ(co[1], (1 - vq) ^ va) << bits;
    EXPECT_EQ(co[2], va & vb) << bits;
  }
}

// A LUT that is not a RAW gate is built from its table.
TEST_F(AbcLnet, GeneralLutsFollowTheirTables) {
  Lnet               net;
  std::array<Lid, 4> in{};
  for (auto& i : in) {
    i = net.add_input({});
  }
  const uint64_t maj3  = 0xE8E8E8E8E8E8E8E8ULL;  // f(x0,x1,x2) = majority
  const uint64_t mux   = 0xCACACACACACACACAULL;  // x2 ? x1 : x0
  const uint64_t xnor4 = 0x9669966996699669ULL;  // parity of x0..x3, complemented
  net.add_output(net.add_lut({in[0], in[1], in[2]}, maj3), "maj");
  net.add_output(net.add_lut({in[0], in[1], in[2]}, mux), "mux");
  net.add_output(net.add_lut({in[0], in[1], in[2], in[3]}, xnor4), "xnor");
  net.add_output(net.add_lut({in[3]}, Lnet::kVar0), "buf");
  net.add_output(net.add_lut({in[1], in[2]}, 0), "zero");

  Network ntk(static_cast<Abc_Ntk_t*>(lnet_to_abc(net, "luts")));
  Abc_NtkFinalizeRead(ntk.ntk);
  ASSERT_TRUE(Abc_NtkCheck(ntk.ntk));
  for (int bits = 0; bits < 16; ++bits) {
    const int  x0 = bits & 1, x1 = (bits >> 1) & 1, x2 = (bits >> 2) & 1, x3 = (bits >> 3) & 1;
    const auto co = simulate(ntk.ntk, {x0, x1, x2, x3});
    ASSERT_EQ(co.size(), 5u);
    EXPECT_EQ(co[0], (x0 + x1 + x2) >= 2 ? 1 : 0) << bits;
    EXPECT_EQ(co[1], x2 ? x1 : x0) << bits;
    EXPECT_EQ(co[2], 1 - (x0 ^ x1 ^ x2 ^ x3)) << bits;
    EXPECT_EQ(co[3], x3) << bits;
    EXPECT_EQ(co[4], 0) << bits;
  }
}

// A proof network: inputs named by their PI object, outputs read the driver
// net directly under their own names.
TEST_F(AbcLnet, ProofNamesInputsByObjectAndOutputsDirectly) {
  Lnet       net;
  const auto a = net.add_input({});
  const auto b = net.add_input({});
  net.add_output(net.add_lut({a, b}, Lnet::kXor2), "f0");

  Network ntk(static_cast<Abc_Ntk_t*>(lnet_to_abc(net, "satopt", Lnet_names::proof)));
  auto*   pi = Abc_NtkPi(ntk.ntk, 1);
  EXPECT_EQ(net_name(Abc_ObjFanout0(pi)), "i" + std::to_string(Abc_ObjId(pi)));
  auto* po = Abc_NtkPo(ntk.ntk, 0);
  EXPECT_EQ(net_name(po), "f0");
  EXPECT_TRUE(Abc_ObjIsNet(Abc_ObjFanin0(po)));
  EXPECT_TRUE(Abc_ObjIsNode(Abc_ObjFanin0(Abc_ObjFanin0(po))));
}

}  // namespace
