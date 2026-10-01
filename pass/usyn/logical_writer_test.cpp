// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "logical_writer.hpp"

#include <iostream>
#include <map>

#include "gtest/gtest.h"
#include "hhds/attrs/srcid.hpp"
#include "node_util.hpp"
#include "region_blast.hpp"

namespace livehd::usyn {
namespace {
namespace gu = graph_util;
using synth::Lid;
using synth::Lnet;

class LogicalWriter : public ::testing::Test {
protected:
  void SetUp() override {
    diag::sink().clear();
    diag::sink().set_jsonl_path("off");
    diag::sink().set_human_stderr(false);
  }
  void TearDown() override {
    if (HasFailure()) {
      for (const auto& diagnostic : diag::sink().records()) {
        std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
      }
    }
    diag::sink().clear();
  }

  Stateful_region fixture(bool async, bool negative, bool reset = true, bool dynamic = true) {
    Lnet                      net;
    const auto                a = net.add_input("a"), b = net.add_input("b"), en = net.add_input("en"), clk = net.add_input("clk");
    const auto                r0 = net.add_input("r0"), r1 = net.add_input("r1");
    synth::Source_state_table table;
    table.logical_boundary = true;
    table.source_graph     = "source";
    synth::Source_state row;
    row.node             = 7;
    row.name             = "r";
    row.bits             = 2;
    row.width_known      = true;
    row.stages           = 2;
    row.translated_bits  = 4;
    row.role             = synth::State_role::register_candidate;
    row.neg_clock        = negative;
    row.async_reset      = async && reset;
    row.neg_reset        = true;
    row.power_on         = !reset;
    row.clock.present    = true;
    row.clock.bits       = 1;
    row.reset.present    = reset;
    row.reset.bits       = reset ? 2 : 0;
    row.initial.present  = true;
    row.initial.bits     = 2;
    row.initial.constant = !dynamic;
    row.initial.value    = *Dlop::from_binary("?1", true);
    row.span.file        = "rtl/original.v";
    row.span.start_line  = 23;
    table.sources.push_back(row);
    for (unsigned i = 0; i < 4; ++i) {
      const std::string name = (i < 2 ? "___pipe0_r[" : "r[") + std::to_string(i % 2) + "]";
      const char        init = !reset && !dynamic && i % 2 == 0 ? '1' : 'x';
      const auto        k    = net.add_latch(name, init);
      table.bits.push_back({
          0,
          i / 2,
          i % 2,
          k,
          name,
          {net.latch(k).q, false},
          {}
      });
    }
    const auto mux    = [&](Lid s, Lid t, Lid f) { return net.add_lut({s, t, f}, 0xD8); };
    const auto active = net.add_lut({r0, r1}, 1);
    const Lid  data[] = {net.add_lut({a, b}, Lnet::kXor2), net.add_lut({a, b}, 2), net.latch(0).q, net.latch(1).q};
    for (unsigned i = 0; i < 4; ++i) {
      auto d = mux(en, data[i], net.latch(i).q);
      if (reset && !async) {
        d = mux(active, i % 2 == 0 ? a : b, d);
      }
      net.set_latch_input(i, d);
      table.bits[i].d = {d, false};
      net.add_output(net.latch(i).q, "q" + std::to_string(i));
    }
    // Every two-input function, including both views of complemented nodes.
    for (uint64_t fn = 0; fn < 16; ++fn) {
      net.add_output(net.add_lut({a, b}, fn), "function" + std::to_string(fn));
    }
    const auto control = [&](synth::State_control_kind kind, std::initializer_list<Lid> values) {
      synth::State_control c;
      c.kind = kind;
      for (const auto value : values) {
        c.outputs.push_back(net.outputs().size());
        net.add_output(value, "control");
      }
      table.controls.push_back(std::move(c));
    };
    control(synth::State_control_kind::clock, {net.add_lut({clk, en}, Lnet::kXor2)});
    if (row.async_reset) {
      control(synth::State_control_kind::async_reset, {r0, r1});
    }
    if ((row.async_reset || row.power_on) && dynamic) {
      control(synth::State_control_kind::initial_value, {a, b});
    }
    Budget work{100000000};
    auto   imported = import_semantic_region(net, table, synth::State_target::cmos, work);
    EXPECT_TRUE(imported.region) << imported.reason;
    if (!imported.region) {
      return {};
    }
    Logical_region selected;
    selected.logic = std::move(imported.region->logic);
    return {std::move(selected), std::move(imported.region->source), std::move(imported.region->state_bits)};
  }

  synth::Region_blast readback(Logical_module& module) {
    partition::Region_body rb;
    rb.src         = module.graph.get();
    rb.module_name = "emitted";
    std::vector<hhds::Node_class> nodes;
    for (auto node : module.graph->body().nodes()) {
      if (node != module.graph->get_input_node() && node != module.graph->get_output_node()
          && node != module.graph->get_constant_node()) {
        nodes.push_back(node);
      }
    }
    rb.nodes = nodes;
    for (const auto& name : module.inputs) {
      rb.inputs.push_back({name, module.graph->get_input_pin(name), 1, false});
    }
    for (const auto& name : module.outputs) {
      rb.outputs.push_back({name, module.graph->get_output_pin(name).get_driver_pin(), 1, false});
    }
    synth::Blast_options options;
    options.logical_state = true;
    return synth::blast_region(rb, options, {});
  }
};

TEST_F(LogicalWriter, NativeGraphPreservesFunctionsStateControlsAndSourceNames) {
  for (bool async : {false, true}) {
    for (bool negative : {false, true}) {
      SCOPED_TRACE(std::to_string(async) + "/" + std::to_string(negative));
      auto                  region = fixture(async, negative);
      // Run the real endpoint/residual pipeline before graph emission.
      Budget                selection{100000000};
      std::vector<uint32_t> eligible = negative ? std::vector<uint32_t>{} : std::vector<uint32_t>{0, 1, 2, 3};
      auto                  selected = synthesize_logical_region(region.selected.logic, eligible, {}, selection);
      ASSERT_TRUE(selected.region) << selected.reason;
      region.selected = std::move(*selected.region);
      auto frozen     = freeze_endpoint_netlist(region.selected, {}, selection, std::array{0U, 0U, 0U, 0U});
      ASSERT_TRUE(frozen.netlist) << frozen.reason;
      region.frozen = std::move(frozen.netlist);
      Budget     work{10000000};
      const auto saved = serialize_artifact("emitted", synth::State_target::cmos, region, work);
      ASSERT_EQ(saved.status, Status::feasible) << saved.reason;
      const auto restored = deserialize_artifact(saved.bytes, synth::State_target::cmos, work);
      ASSERT_TRUE(restored.region) << restored.reason;
      Logical_module_result result;
      ASSERT_NO_THROW(result = write_logical_module(*restored.region, work));
      ASSERT_TRUE(result.module) << result.reason;
      auto& module = *result.module;
      ASSERT_EQ(module.state.size(), 4U);
      for (size_t i = 0; i < module.state.size(); ++i) {
        const auto node = module.state[i];
        EXPECT_EQ(gu::wire_name(node.get_driver_pin(0)), region.selected.logic.state[i].name);
        EXPECT_TRUE(gu::get_driver_of_sink_name(node, "enable").is_invalid());
        EXPECT_EQ(!gu::get_driver_of_sink_name(node, "reset_pin").is_invalid(), async);
      }
      synth::Region_blast rebuilt;
      ASSERT_NO_THROW(rebuilt = readback(module));
      ASSERT_EQ(rebuilt.status, synth::Region_blast::Status::blasted);
      ASSERT_TRUE(rebuilt.source_state);
      const auto& table = *rebuilt.source_state;
      ASSERT_EQ(table.sources.size(), 4U);
      for (const auto& row : table.sources) {
        EXPECT_EQ(row.neg_clock, negative);
        EXPECT_EQ(row.async_reset, async);
        EXPECT_EQ(row.span.file, "rtl/original.v");
        EXPECT_EQ(row.span.start_line, 23U);
      }
      std::map<std::string, unsigned> state_index;
      for (unsigned i = 0; i < region.selected.logic.state.size(); ++i) {
        state_index.emplace(region.selected.logic.state[i].name, i);
      }
      const auto&           net = rebuilt.lnet;
      std::vector<unsigned> state_order(net.latches().size());
      for (const auto& bit : table.bits) {
        state_order[bit.latch] = state_index.at(table.sources[bit.source].name);
      }
      for (unsigned x = 0; x < 1024; ++x) {
        std::vector<bool> values(net.size());
        for (Lid id = 0; id < net.size(); ++id) {
          if (net.kind(id) == Lnet::Kind::source) {
            if (net.is_latch_source(id)) {
              values[id] = (x >> (6 + state_order[net.source_index(id)])) & 1;
            } else {
              const auto origin = rebuilt.pi_order[net.source_index(id)].first;
              values[id]        = (x >> origin) & 1;
            }
          } else {
            unsigned assignment = 0;
            for (unsigned i = 0; i < net.fanin_count(id); ++i) {
              assignment |= unsigned(values[net.fanin(id, i)]) << i;
            }
            values[id] = net.eval(id, assignment);
          }
        }
        const bool a = x & 1, b = x & 2, en = x & 4;
        for (unsigned q = 0; q < 4; ++q) {
          ASSERT_EQ(values[net.outputs()[q].node], bool((x >> (6 + q)) & 1));
        }
        for (size_t k = 0; k < net.latches().size(); ++k) {
          const auto&    latch    = net.latch(k);
          const unsigned index    = state_order[k];
          const bool     data     = index == 0 ? a != b : index == 1 ? a && !b : (x >> (6 + index - 2)) & 1;
          bool           expected = en ? data : (x >> (6 + index)) & 1;
          if (!async && !(x & 48)) {
            expected = index % 2 == 0 ? a : b;
          }
          ASSERT_EQ(values[latch.d], expected) << x << ":" << latch.name;
        }
        for (unsigned fn = 0; fn < 16; ++fn) {
          ASSERT_EQ(values[net.outputs()[4 + fn].node], bool((fn >> (x & 3)) & 1)) << fn << ":" << x;
        }
        for (const auto& c : table.controls) {
          const auto index = state_index.at(table.sources[c.source].name);
          for (unsigned bit = 0; bit < c.outputs.size(); ++bit) {
            const bool expected = c.kind == synth::State_control_kind::clock         ? bool(x & 8) != en
                                  : c.kind == synth::State_control_kind::async_reset ? bool((x >> (4 + bit)) & 1)
                                  : index % 2 == 0                                   ? a
                                                                                     : b;
            ASSERT_EQ(values[net.outputs()[c.outputs[bit]].node], expected) << x;
          }
        }
      }
    }
  }
}

TEST_F(LogicalWriter, InvalidFrozenArtifactCannotFallBackToExpandedGraph) {
  auto   region = fixture(false, false);
  Budget work{100000000};
  auto   selected = synthesize_logical_region(region.selected.logic, std::array{0U, 1U, 2U, 3U}, {}, work);
  ASSERT_TRUE(selected.region) << selected.reason;
  region.selected = std::move(*selected.region);
  auto frozen     = freeze_endpoint_netlist(region.selected, {}, work);
  ASSERT_TRUE(frozen.netlist) << frozen.reason;
  region.frozen = std::move(frozen.netlist);
  ASSERT_FALSE(region.frozen->cells.empty());
  region.frozen->cells.back().outputs[1] = "lost_complement";
  const auto result                      = write_logical_module(region, "invalid_frozen", work);
  EXPECT_EQ(result.status, Status::invalid);
  EXPECT_FALSE(result.module);
}

TEST_F(LogicalWriter, PowerOnValuesRetainUnknownBitsAndDynamicInputs) {
  for (bool dynamic : {false, true}) {
    auto   region = fixture(false, false, false, dynamic);
    Budget work{1000000};
    auto   result = write_logical_module(region, "initialized", work);
    ASSERT_TRUE(result.module) << result.reason;
    for (size_t i = 0; i < result.module->state.size(); ++i) {
      const auto init = gu::get_driver_of_sink_name(result.module->state[i], "initial");
      ASSERT_FALSE(init.is_invalid());
      EXPECT_EQ(init.is_const(), !dynamic);
      if (dynamic) {
        EXPECT_EQ(init, result.module->graph->get_input_pin(result.module->inputs[i % 2]));
      }
      if (!dynamic) {
        EXPECT_EQ(gu::const_of(init).unknown_bit_test(0), bool(i % 2));
        if (i % 2 == 0) {
          EXPECT_TRUE(gu::const_of(init).bit_test(0));
        }
      }
    }
    auto roundtrip = readback(*result.module);
    ASSERT_EQ(roundtrip.status, synth::Region_blast::Status::blasted);
    ASSERT_TRUE(roundtrip.source_state);
    for (const auto& row : roundtrip.source_state->sources) {
      EXPECT_TRUE(row.power_on);
    }
  }
}

TEST_F(LogicalWriter, ConstantClockAndAsyncValuesKeepTheirPolarityAndUnknownBits) {
  auto  region       = fixture(true, true, true, false);
  auto& row          = region.source.sources[0];
  row.clock.constant = true;
  row.clock.value    = *Dlop::create_integer(0);
  region.source.controls.erase(region.source.controls.begin());
  Budget work{1000000};
  auto   result = write_logical_module(region, "constant_clock", work);
  ASSERT_TRUE(result.module) << result.reason;
  for (size_t i = 0; i < result.module->state.size(); ++i) {
    auto       node = result.module->state[i];
    const auto clk  = gu::get_driver_of_sink_name(node, "clock_pin");
    ASSERT_TRUE(clk.is_const());
    EXPECT_TRUE(gu::const_of(clk).is_known_false());
    const auto init = gu::get_driver_of_sink_name(node, "initial");
    ASSERT_TRUE(init.is_const());
    EXPECT_EQ(gu::const_of(init).unknown_bit_test(0), bool(i % 2));
    EXPECT_TRUE(gu::const_of(gu::get_driver_of_sink_name(node, "posclk")).is_known_false());
    EXPECT_TRUE(gu::const_of(gu::get_driver_of_sink_name(node, "negreset")).bit_test(0));
    EXPECT_EQ(gu::bits_of(gu::get_driver_of_sink_name(node, "reset_pin")), 2);
  }
}

TEST_F(LogicalWriter, PortNamesCannotCollideWithStateAndEmptyModulesNeedNoSentinel) {
  auto region                         = fixture(false, false);
  region.selected.logic.state[0].name = "__usyn_i0";
  region.selected.logic.state[1].name = "__usyn_i0_";
  region.selected.logic.state[2].name = "__usyn_o0";
  Budget work{1000000};
  auto   result = write_logical_module(region, "collisions", work);
  ASSERT_TRUE(result.module) << result.reason;
  EXPECT_EQ(result.module->inputs[0], "__usyn_i0__");
  EXPECT_EQ(result.module->outputs[0], "__usyn_o0_");
  EXPECT_EQ(gu::wire_name(result.module->state[0].get_driver_pin(0)), "__usyn_i0");

  Stateful_region empty;
  empty.source.logical_boundary = true;
  empty.selected.logic.status   = Status::feasible;
  auto no_ports                 = write_logical_module(empty, "empty", work);
  ASSERT_TRUE(no_ports.module) << no_ports.reason;
  EXPECT_TRUE(no_ports.module->inputs.empty());
  EXPECT_TRUE(no_ports.module->outputs.empty());
  EXPECT_TRUE(no_ports.module->state.empty());
  empty.selected.logic.outputs.push_back({"constant", empty.selected.logic.graph.constant(true)});
  auto output_only = write_logical_module(empty, "output_only", work);
  ASSERT_TRUE(output_only.module) << output_only.reason;
  auto driver = output_only.module->graph->get_output_pin(output_only.module->outputs[0]).get_driver_pin();
  ASSERT_TRUE(driver.is_const());
  EXPECT_TRUE(gu::const_of(driver).bit_test(0));
}

TEST_F(LogicalWriter, RefusalsPublishNoPartialGraph) {
  auto region = fixture(true, false);
  for (unsigned broken = 0; broken < 5; ++broken) {
    auto bad = region;
    if (broken == 0) {
      bad.source.logical_boundary = false;
    }
    if (broken == 1) {
      bad.source.controls.pop_back();
    }
    if (broken == 2) {
      bad.state_bits[1] = bad.state_bits[0];
    }
    if (broken == 3) {
      bad.source.sources[0].stages = 3;
    }
    if (broken == 4) {
      bad.source.sources[0].clock_edge_known = false;
    }
    Budget work{1000000};
    auto   result = write_logical_module(bad, "bad", work);
    EXPECT_FALSE(result.module) << broken;
    EXPECT_FALSE(result.reason.empty());
  }
  Budget small{1}, enough{1000000};
  EXPECT_EQ(write_logical_module(region, "small", small).status, Status::search_exhausted);
  EXPECT_EQ(write_logical_module(region, "small", enough, 16).status, Status::search_exhausted);
  Budget   cancelled{1000000};
  unsigned checks              = 0;
  cancelled.admission_interval = 1;
  cancelled.admission          = [&] { return ++checks < 100; };
  auto result                  = write_logical_module(region, "cancelled", cancelled);
  EXPECT_FALSE(result.module);
  EXPECT_EQ(result.status, Status::search_exhausted);

  // Exhaust just before the final connection, after the private graph has
  // been allocated. No partially connected module escapes that late refusal.
  Budget measured{1000000};
  auto   complete = write_logical_module(region, "measured", measured);
  ASSERT_TRUE(complete.module);
  Budget late{1000000 - measured.remaining - 1};
  auto   partial = write_logical_module(region, "late", late);
  EXPECT_FALSE(partial.module);
  EXPECT_EQ(partial.status, Status::search_exhausted);
}

}  // namespace
}  // namespace livehd::usyn
