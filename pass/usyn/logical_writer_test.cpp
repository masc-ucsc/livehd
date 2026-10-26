// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "logical_writer.hpp"

#include <format>
#include <iostream>
#include <map>
#include <set>

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

// A frozen one-phase region from the real selection pipeline:
// r1 <= (a & b) | (c & d), r2 <= en ? a : r2 (binate in en) and r3 <= a, all
// clocked by port clk. Outputs: the three Qs and a ^ b.
Stateful_region one_phase() {
  Lnet                      net;
  const auto                a = net.add_input("a"), b = net.add_input("b"), c = net.add_input("c"), d = net.add_input("d");
  const auto                en = net.add_input("en"), clk = net.add_input("clk");
  synth::Source_state_table table;
  table.logical_boundary = true;
  table.source_graph     = "source";
  const std::array<std::string, 3> names{"r1", "r2", "r3"};
  for (uint32_t i = 0; i < names.size(); ++i) {
    synth::Source_state row;
    row.node            = 10 + i;
    row.name            = names[i];
    row.bits            = 1;
    row.width_known     = true;
    row.stages          = 1;
    row.translated_bits = 1;
    row.role            = synth::State_role::register_candidate;
    row.clock.present   = true;
    row.clock.bits      = 1;
    row.span.file       = "rtl/one.v";
    row.span.start_line = 5 + i;
    table.sources.push_back(row);
    const auto k = net.add_latch(names[i], 'x');
    table.bits.push_back({
        i,
        0,
        0,
        k,
        names[i],
        {net.latch(k).q, false},
        {}
    });
  }
  const std::array<Lid, 3> next{net.add_lut({net.add_lut({a, b}, 8), net.add_lut({c, d}, 8)}, 14),
                                net.add_lut({en, a, net.latch(1).q}, 0xD8),
                                a};
  for (uint32_t i = 0; i < next.size(); ++i) {
    net.set_latch_input(i, next[i]);
    table.bits[i].d = {next[i], false};
    net.add_output(net.latch(i).q, "q" + std::to_string(i));
  }
  net.add_output(net.add_lut({a, b}, Lnet::kXor2), "x");
  for (uint32_t i = 0; i < names.size(); ++i) {
    table.controls.push_back({i, synth::State_control_kind::clock, {static_cast<uint32_t>(net.outputs().size())}});
    net.add_output(clk, "control");
  }
  Logical_options options;
  options.endpoint.clock_phases = 1;
  Budget work{100000000};
  auto   result = synthesize_stateful_region(net, table, synth::State_target::cmos, options, work);
  EXPECT_TRUE(result.region) << result.reason;
  return std::move(*result.region);
}

// Convert `states` with their frozen DominoLatch rails, as pass/domplace does.
Physical_emission convert(const Stateful_region& region, const std::set<uint32_t>& states) {
  Physical_emission emission;
  const auto&       net = *region.frozen;
  for (uint32_t i = 0; i < net.state.size(); ++i) {
    auto& state  = emission.states.emplace_back();
    state.state  = i;
    state.reason = "area";
    if (!states.contains(i)) {
      continue;
    }
    const auto& cell = net.cells.at(*net.state[i].domino_latch);
    state.decision   = Physical_decision::domino;
    state.formula    = cell.formula;
    state.metrics    = cell.metrics;
    state.cell       = static_cast<uint32_t>(emission.cells.size());
    auto& interface  = emission.cells.emplace_back();
    interface.name   = std::format("__lhd_domino_test{}_v0_t0", i);
    for (const auto& rail : cell.rails) {
      EXPECT_EQ(rail.producer.space, Endpoint_ref::Space::native);
      state.rails.push_back({
          rail.input,
          rail.producer.inverted != cell.inputs[rail.input].inverted,
          {rail.producer.index, rail.producer.inverted}
      });
      interface.rails.push_back(std::format("R{}", interface.rails.size()));
    }
  }
  return emission;
}

// Values of a frozen native signal and of an emitted module pin under one
// assignment of the region inputs and current state values.
bool native_value(const Xag& graph, Xsignal signal, const std::vector<bool>& sources) {
  const auto& node  = graph.node(signal.id);
  bool        value = false;
  switch (node.kind) {
    case Xag::Kind::constant: value = false; break;
    case Xag::Kind::source  : value = sources.at(node.source_index); break;
    case Xag::Kind::and_gate:
      value = native_value(graph, node.inputs[0], sources) && native_value(graph, node.inputs[1], sources);
      break;
    case Xag::Kind::xor_gate:
      value = native_value(graph, node.inputs[0], sources) != native_value(graph, node.inputs[1], sources);
      break;
  }
  return value != signal.inverted;
}
bool module_value(hhds::Pin_class pin, const std::map<std::string, bool>& inputs, const std::map<std::string, bool>& state) {
  if (pin.is_const()) {
    return !gu::const_of(pin).is_known_zero();
  }
  if (gu::is_graph_input_pin(pin)) {
    return inputs.at(std::string(gu::pin_name_of(pin)));
  }
  const auto node = pin.get_master_node();
  const auto op   = gu::type_op_of(node);
  if (op == Ntype_op::Flop || op == Ntype_op::Sub) {
    return state.at(std::string(gu::node_name_of(node)));
  }
  EXPECT_TRUE(op == Ntype_op::And || op == Ntype_op::Xor) << Ntype::get_name(op);
  bool value = op == Ntype_op::And;
  for (const auto& operand : gu::inp_sink_drivers(node)) {
    const auto v = module_value(operand.driver, inputs, state);
    value        = op == Ntype_op::And ? value && v : value != v;
  }
  return value;
}

TEST_F(LogicalWriter, PhysicalModuleReplacesConvertedFlopsWithCellInstances) {
  const auto region = one_phase();
  ASSERT_TRUE(region.frozen);
  const auto& net      = *region.frozen;
  const auto  physical = convert(region, {0, 1});
  Budget      work{10000000};
  auto        plain = write_logical_module(region, "plain", work);
  auto        mixed = write_physical_module(region, physical, "mixed", work);
  ASSERT_TRUE(plain.module) << plain.reason;
  ASSERT_TRUE(mixed.module) << mixed.reason;
  auto& module = *mixed.module;
  // Ports are those of the CMOS reference; the rails stay internal.
  EXPECT_EQ(module.inputs, plain.module->inputs);
  EXPECT_EQ(module.outputs, plain.module->outputs);
  ASSERT_EQ(module.state.size(), 3U);
  uint32_t flops = 0;
  for (auto node : module.graph->body().nodes()) {
    flops += gu::type_op_of(node) == Ntype_op::Flop;
  }
  EXPECT_EQ(flops, 1U);  // only the retained r3
  for (uint32_t i = 0; i < 3; ++i) {
    const auto node = module.state[i];
    EXPECT_EQ(gu::node_name_of(node), net.state[i].name);
    EXPECT_TRUE(node.attr(hhds::attrs::srcid).has());
    if (i == 2) {
      EXPECT_EQ(gu::type_op_of(node), Ntype_op::Flop);
      continue;
    }
    ASSERT_EQ(gu::type_op_of(node), Ntype_op::Sub);
    const auto  decl = node.get_subnode_io();
    const auto& cell = physical.cells[physical.states[i].cell];
    ASSERT_TRUE(decl);
    EXPECT_EQ(decl->get_name(), cell.name);
    EXPECT_FALSE(decl->has_graph());
    EXPECT_TRUE(decl->is_loop_break(cell.output));
    EXPECT_EQ(module.library.find_io(cell.name), decl);
    // CLK is the state's clock control: the clk port.
    EXPECT_EQ(gu::pin_name_of(node.get_sink_pin(decl->get_input_port_id(cell.clock)).get_driver_pin()), "__usyn_i5");
  }
  // Every rail carries its frozen producer, a complemented one through an
  // explicit inverter, and every output keeps its function, under every
  // assignment of the six inputs and three states.
  bool complemented = false;
  for (const auto& rail : physical.states[1].rails) {
    complemented |= rail.producer.inverted;
  }
  EXPECT_TRUE(complemented);  // !en
  for (uint32_t x = 0; x < 512; ++x) {
    std::vector<bool>           sources(net.native.input_names().size());
    std::map<std::string, bool> inputs, state;
    for (size_t i = 0; i < net.inputs.size(); ++i) {
      const bool v                                            = (x >> i) & 1;
      sources[net.native.node(net.inputs[i].id).source_index] = v;
      inputs[module.inputs[i]]                                = v;
    }
    for (size_t i = 0; i < net.state.size(); ++i) {
      const bool v                                             = (x >> (6 + i)) & 1;
      sources[net.native.node(net.state[i].q.id).source_index] = v;
      state[net.state[i].name]                                 = v;
    }
    for (uint32_t i : {0U, 1U}) {
      const auto  node = module.state[i];
      const auto& cell = physical.cells[physical.states[i].cell];
      const auto  decl = node.get_subnode_io();
      for (size_t k = 0; k < cell.rails.size(); ++k) {
        const auto driver = node.get_sink_pin(decl->get_input_port_id(cell.rails[k])).get_driver_pin();
        ASSERT_EQ(module_value(driver, inputs, state), native_value(net.native, physical.states[i].rails[k].producer, sources))
            << net.state[i].name << " R" << k << " x=" << x;
      }
    }
    for (size_t o = 0; o < module.outputs.size(); ++o) {
      ASSERT_EQ(module_value(module.graph->get_output_pin(module.outputs[o]).get_driver_pin(), inputs, state),
                native_value(net.native, net.outputs[o].signal, sources))
          << module.outputs[o];
    }
    ASSERT_EQ(module_value(gu::get_driver_of_sink_name(module.state[2], "din"), inputs, state),
              native_value(net.native, net.state[2].reference_d, sources));
  }
}

TEST_F(LogicalWriter, PhysicalModuleRefusesMismatchedEmissions) {
  const auto region   = one_phase();
  auto       physical = convert(region, {0});
  Budget     work{10000000};
  auto       shorter = physical;
  shorter.states.pop_back();
  EXPECT_FALSE(write_physical_module(region, shorter, "m", work).module);
  auto wrong = physical;
  wrong.cells[0].rails.pop_back();
  EXPECT_FALSE(write_physical_module(region, wrong, "m", work).module);
  auto outside                        = physical;
  outside.states[0].rails[0].producer = {static_cast<Id>(region.frozen->native.size() + 5), false};
  EXPECT_FALSE(write_physical_module(region, outside, "m", work).module);
  // The cell captures on the positive edge only.
  auto negative                        = region;
  negative.source.sources[0].neg_clock = true;
  EXPECT_TRUE(write_logical_module(negative, "m", work).module);
  EXPECT_FALSE(write_physical_module(negative, physical, "m", work).module);
}

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
