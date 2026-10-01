// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "xag_lnet.hpp"

#include <bit>

#include "gtest/gtest.h"

namespace livehd::usyn {
TEST(XagLnet, AllThreeInputFunctionsSurviveImport) {
  for (uint32_t bits = 0; bits < 256; ++bits) {
    synth::Lnet net;
    const auto  a = net.add_input("a"), b = net.add_input("b"), c = net.add_input("c");
    net.add_output(net.add_lut({a, b, c}, bits), "out");
    Budget     work{100000};
    const auto imported = import_lnet(net, work);
    ASSERT_EQ(imported.status, Status::feasible) << imported.reason;
    const auto exported = export_lnet(imported, work);
    ASSERT_TRUE(exported.net) << exported.reason;
    ASSERT_EQ(exported.net->inputs().size(), 3U);
    for (uint32_t x = 0; x < 8; ++x) {
      std::vector<bool> values(exported.net->size(), false);
      for (synth::Lid id = 0; id < exported.net->size(); ++id) {
        if (exported.net->kind(id) == synth::Lnet::Kind::source) {
          values[id] = (x >> exported.net->source_index(id)) & 1;
        } else {
          uint32_t input = 0;
          for (uint32_t j = 0; j < exported.net->fanin_count(id); ++j) {
            input |= uint32_t{values[exported.net->fanin(id, j)]} << j;
          }
          values[id] = exported.net->eval(id, input);
        }
      }
      ASSERT_EQ(values[exported.net->outputs()[0].node], ((bits >> x) & 1) != 0) << bits << ':' << x;
    }
    const std::array<Id, 3> leaves{imported.inputs[0].id, imported.inputs[1].id, imported.inputs[2].id};
    // Whole-cone collection removes unused inputs. Evaluate with their source
    // indices so constants and functions of a strict support subset count too.
    const auto              cut = whole_cone(imported.graph, imported.outputs[0].signal, {}, work);
    ASSERT_EQ(cut.status, Status::feasible);
    const auto f = window_function(imported.graph, cut, work);
    ASSERT_EQ(f.status, Status::feasible);
    for (uint32_t x = 0; x < 8; ++x) {
      uint32_t assignment = 0;
      for (uint32_t j = 0; j < cut.leaves.size(); ++j) {
        for (uint32_t k = 0; k < leaves.size(); ++k) {
          if (cut.leaves[j] == leaves[k]) {
            assignment |= ((x >> k) & 1) << j;
          }
        }
      }
      ASSERT_EQ(f.table.get(assignment), ((bits >> x) & 1) != 0) << bits << ':' << x;
    }
  }
}

TEST(XagLnet, StateNamesInitializationAndFeedbackRemainExplicit) {
  synth::Lnet net;
  const auto  a   = net.add_input("a");
  const auto  reg = net.add_latch("hier.counter[3]", '1');
  const auto  q   = net.latch(reg).q;
  net.set_latch_input(reg, net.add_lut({a, q}, synth::Lnet::kXor2));
  net.add_output(q, "q");
  Budget     work{10000};
  const auto imported = import_lnet(net, work);
  ASSERT_EQ(imported.status, Status::feasible) << imported.reason;
  ASSERT_EQ(imported.state.size(), 1U);
  const auto& state = imported.state[0];
  EXPECT_EQ(state.name, "hier.counter[3]");
  EXPECT_EQ(state.init, '1');
  EXPECT_EQ(imported.outputs[0].signal, state.q);
  EXPECT_EQ(imported.graph.node(state.d.id).kind, Xag::Kind::xor_gate);
  const auto cut = whole_cone(imported.graph, state.d, {}, work);
  const auto f   = window_function(imported.graph, cut, work);
  ASSERT_EQ(f.status, Status::feasible);
  EXPECT_EQ(f.table.words[0], 6U);
}

TEST(XagLnet, WideLutAndExplicitAdmissionLimits) {
  synth::Lnet             net;
  std::vector<synth::Lid> inputs;
  for (uint32_t j = 0; j < 8; ++j) {
    inputs.push_back(net.add_input(std::to_string(j)));
  }
  std::array<uint64_t, 4> table{};
  for (uint32_t x = 0; x < 256; ++x) {
    table[x / 64] |= static_cast<uint64_t>((std::popcount(x) & 1) != 0) << (x % 64);
  }
  net.add_output(net.add_lut(inputs, table), "parity");
  Budget     work{10000};
  const auto imported = import_lnet(net, work);
  ASSERT_EQ(imported.status, Status::feasible) << imported.reason;
  const auto cut = whole_cone(imported.graph, imported.outputs[0].signal, {}, work);
  const auto f   = window_function(imported.graph, cut, work);
  ASSERT_EQ(f.status, Status::feasible);
  EXPECT_EQ(f.table.words, (std::vector<uint64_t>{table.begin(), table.end()}));
  Budget none{0};
  EXPECT_EQ(import_lnet(net, none).status, Status::search_exhausted);
  Budget small{10000};
  EXPECT_EQ(import_lnet(net, small, 2).status, Status::search_exhausted);
  synth::Lnet incomplete;
  incomplete.add_latch("missing_d", 'x');
  EXPECT_EQ(import_lnet(incomplete, small).status, Status::invalid);
}
}  // namespace livehd::usyn
