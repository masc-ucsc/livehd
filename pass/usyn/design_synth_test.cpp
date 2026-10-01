// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "design_synth.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>

#include "design_prepare.hpp"
#include "flatten.hpp"
#include "gtest/gtest.h"
#include "hhds/attrs/name.hpp"
#include "node_util.hpp"

namespace livehd::usyn {
namespace {
namespace gu = graph_util;

// top(a, b, clk) -> q: state <= child(a, b) [^ a when `extra`]; child = a ^ b.
// The definition, register, first top input and top output spellings are
// parameters.
struct Design_names {
  std::string child = "child", state = "state", input = "a", output = "q";
  bool        extra = false;  // a top-only edit that grows the design
};
struct Built_design {
  std::shared_ptr<hhds::Graph> top, child;
  hhds::Node_class             state;
};
Built_design build_design(hhds::GraphLibrary& library, const Design_names& names = {}) {
  Built_design built;
  auto         cio = library.create_io(names.child);
  cio->add_input("a", 1);
  cio->add_input("b", 2);
  cio->add_output("y", 3);
  for (auto name : {"a", "b", "y"}) {
    cio->set_bits(name, 1);
  }
  auto& child = built.child;
  child       = cio->create_graph();
  auto x      = gu::create_typed_node(*child, Ntype_op::Xor);
  child->get_input_pin("a").connect_sink(x.create_sink_pin(0));
  child->get_input_pin("b").connect_sink(x.create_sink_pin(1));
  auto xp = x.create_driver_pin(0);
  gu::set_ubits(xp, 1);
  xp.connect_sink(child->get_output_pin("y"));
  auto io = library.create_io("top");
  io->add_input(names.input, 1);
  io->add_input("b", 2);
  io->add_input("clk", 3);
  io->add_output(names.output, 4);
  for (const auto& name : {names.input, std::string{"b"}, std::string{"clk"}, names.output}) {
    io->set_bits(name, 1);
  }
  auto& top = built.top;
  top       = io->create_graph();
  auto sub  = gu::create_typed_node(*top, Ntype_op::Sub);
  sub.set_subnode(cio);
  top->get_input_pin(names.input).connect_sink(sub.create_sink_pin(1));
  top->get_input_pin("b").connect_sink(sub.create_sink_pin(2));
  auto y = sub.create_driver_pin(3);
  gu::set_ubits(y, 1);
  auto& state = built.state;
  state       = gu::create_typed_node(*top, Ntype_op::Flop);
  state.attr(hhds::attrs::name).set(names.state);
  auto din = y;
  if (names.extra) {
    auto edit = gu::create_typed_node(*top, Ntype_op::Xor);
    y.connect_sink(edit.create_sink_pin(0));
    top->get_input_pin(names.input).connect_sink(edit.create_sink_pin(1));
    din = edit.create_driver_pin(0);
    gu::set_ubits(din, 1);
  }
  din.connect_sink(gu::setup_sink_by_name(state, "din"));
  top->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(state, "clock_pin"));
  auto q = state.create_driver_pin(0);
  gu::set_ubits(q, 1);
  gu::set_pin_name(q, names.state);
  q.connect_sink(top->get_output_pin(names.output));
  return built;
}

// A lane definition: `registers` one-bit registers r0.. over inputs a..d
// (ports 1..4, clk 5, y 6). Each next state is a fixed pseudo-random
// And/Xor/Not network of the inputs, the registers and earlier gates (`seed`);
// `flip` (a register index, or -1) inverts that next state, a one-assign edit.
// y XORs all registers.
std::shared_ptr<hhds::GraphIO> build_lane(hhds::GraphLibrary& library, const std::string& name, uint32_t seed, uint32_t registers,
                                          int32_t flip) {
  auto                                        io = library.create_io(name);
  static constexpr std::array<const char*, 5> ports{"a", "b", "c", "d", "clk"};
  for (size_t i = 0; i < ports.size(); ++i) {
    io->add_input(ports[i], i + 1);
    io->set_bits(ports[i], 1);
  }
  io->add_output("y", 6);
  io->set_bits("y", 1);
  auto       graph  = io->create_graph();
  uint32_t   random = seed;
  const auto next   = [&](size_t n) {
    random = random * 1664525U + 1013904223U;
    return (random >> 8) % n;
  };
  const auto driven = [](hhds::Node_class node) {
    auto out = node.create_driver_pin(0);
    gu::set_ubits(out, 1);
    return out;
  };
  const auto xor2 = [&](auto x, auto y) {
    auto node = gu::create_typed_node(*graph, Ntype_op::Xor);
    x.connect_sink(node.create_sink_pin(0));
    y.connect_sink(node.create_sink_pin(1));
    return driven(node);
  };
  const auto and2 = [&](auto x, auto y) {
    auto node = gu::create_typed_node(*graph, Ntype_op::And);
    gu::append_sink_operand(node, Ntype_op::And, 0).connect_driver(x);
    gu::append_sink_operand(node, Ntype_op::And, 0).connect_driver(y);
    return driven(node);
  };
  const auto inv = [&](auto x) {
    auto node = gu::create_typed_node(*graph, Ntype_op::Not);
    x.connect_sink(gu::setup_sink_by_name(node, "a"));
    return driven(node);
  };
  std::vector<decltype(graph->get_input_pin("a"))> pool, qs;
  for (size_t i = 0; i < 4; ++i) {
    pool.push_back(graph->get_input_pin(ports[i]));
  }
  std::vector<hhds::Node_class> flops;
  for (uint32_t r = 0; r < registers; ++r) {
    const auto reg  = "r" + std::to_string(r);
    auto       flop = gu::create_typed_node(*graph, Ntype_op::Flop);
    flop.attr(hhds::attrs::name).set(reg);
    graph->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(flop, "clock_pin"));
    auto q = flop.create_driver_pin(0);
    gu::set_ubits(q, 1);
    gu::set_pin_name(q, reg);
    flops.push_back(flop);
    qs.push_back(q);
    pool.push_back(q);
  }
  for (uint32_t r = 0; r < registers; ++r) {
    size_t index = next(pool.size());
    auto   value = pool[index];
    for (uint32_t k = 0; k < 5; ++k) {
      auto other = (index + 1 + next(pool.size() - 1)) % pool.size();  // never the same operand twice
      value      = next(2) ? xor2(value, pool[other]) : and2(value, pool[other]);
      if (next(3) == 0) {
        value = inv(value);
      }
      index = pool.size();
      pool.push_back(value);
    }
    if (flip >= 0 && r == static_cast<uint32_t>(flip)) {
      value = inv(value);
    }
    value.connect_sink(gu::setup_sink_by_name(flops[r], "din"));
  }
  auto y = qs[0];
  for (size_t r = 1; r < qs.size(); ++r) {
    y = xor2(y, qs[r]);
  }
  y.connect_sink(graph->get_output_pin("y"));
  return io;
}
// top(a..d, clk) -> q = lane0.y ^ lane1.y. Both lanes share one seed; lane1
// inverts one assign, so they are two heavy definitions (the FTQ-pair shape).
// `grown` adds a register to lane0 only: an earlier region's size and search
// change while lane1 and top stay untouched. `only` keeps one lane.
std::shared_ptr<hhds::Graph> build_lanes(hhds::GraphLibrary& library, uint32_t registers, bool grown = false,
                                         std::string_view only = {}, uint32_t count = 2) {
  constexpr uint32_t                          seed = 7;
  std::vector<std::shared_ptr<hhds::GraphIO>> lanes;
  if (only != "lane1") {
    lanes.push_back(build_lane(library, "lane0", seed, registers + grown, -1));
  }
  if (only != "lane0") {
    // Lane k inverts the next state of register registers-k: distinct heavy
    // definitions of the same size, each one assign away from lane0.
    for (uint32_t k = 1; k < count; ++k) {
      lanes.push_back(build_lane(library, "lane" + std::to_string(k), seed, registers, static_cast<int32_t>(registers - k)));
    }
  }
  auto                                        io = library.create_io("top");
  static constexpr std::array<const char*, 5> ports{"a", "b", "c", "d", "clk"};
  for (size_t i = 0; i < ports.size(); ++i) {
    io->add_input(ports[i], i + 1);
    io->set_bits(ports[i], 1);
  }
  io->add_output("q", 6);
  io->set_bits("q", 1);
  auto                                           top = io->create_graph();
  std::vector<decltype(top->get_input_pin("a"))> ys;
  for (const auto& lane : lanes) {
    auto sub = gu::create_typed_node(*top, Ntype_op::Sub);
    sub.set_subnode(lane);
    for (size_t i = 0; i < ports.size(); ++i) {
      top->get_input_pin(ports[i]).connect_sink(sub.create_sink_pin(i + 1));
    }
    auto y = sub.create_driver_pin(6);
    gu::set_ubits(y, 1);
    ys.push_back(y);
  }
  auto q = ys[0];
  for (size_t i = 1; i < ys.size(); ++i) {
    auto x = gu::create_typed_node(*top, Ntype_op::Xor);
    q.connect_sink(x.create_sink_pin(0));
    ys[i].connect_sink(x.create_sink_pin(1));
    q = x.create_driver_pin(0);
    gu::set_ubits(q, 1);
  }
  q.connect_sink(top->get_output_pin("q"));
  return top;
}

class DesignSynth : public ::testing::Test {
protected:
  hhds::GraphLibrary           source;
  std::shared_ptr<hhds::Graph> top, child;
  hhds::Node_class             state;
  std::string                  cache_dir;

  void SetUp() override {
    cache_dir = (std::filesystem::temp_directory_path() / "usyn-design-cache-XXXXXX").string();
    ASSERT_NE(mkdtemp(cache_dir.data()), nullptr);
    diag::sink().clear();
    diag::sink().set_human_stderr(false);
    diag::sink().set_jsonl_path("off");
    auto built = build_design(source);
    top        = built.top;
    child      = built.child;
    state      = built.state;
  }
  // Synthesize with the logical cache in `directory`; region rows by module.
  static Design_result cached_run(const std::shared_ptr<hhds::Graph>& design, const std::string& directory, Budget& work) {
    Design_options options;
    options.cache.directory = directory;
    return synthesize_cmos_design(design, options, work);
  }
  static const Design_region* region_of(const Logical_design& design, std::string_view module) {
    for (const auto& region : design.regions) {
      if (region.module_name == module) {
        return &region;
      }
    }
    return nullptr;
  }
  static std::string record_bytes(const std::string& directory, const Design_region& region) {
    std::ifstream file(std::filesystem::path(directory) / (region.cache_key + ".usyn-cache"), std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  }
  static std::vector<std::string> artifacts(const Logical_design& design) {
    std::vector<std::string> bytes;
    for (const auto& region : design.regions) {
      Budget     work{100000000};
      const auto encoded = serialize_artifact(region.module_name, synth::State_target::cmos, region.selected, work);
      EXPECT_EQ(encoded.status, Status::feasible) << encoded.reason;
      bytes.push_back(encoded.bytes);
    }
    return bytes;
  }
  // A warm result must equal a cold one from an empty cache under the same
  // limit: same modules, keys, reports, search credits and credit floors,
  // region structural work, published artifacts and total structural work.
  void expect_cold_equivalent(const Design_result& warm, const Budget& warm_work, const std::shared_ptr<hhds::Graph>& design,
                              const std::string& label) {
    Budget     cold_work{warm_work.consumed + warm_work.remaining};
    const auto cold = cached_run(design, cache_dir + "/cold-" + label, cold_work);
    ASSERT_TRUE(cold.design) << cold.reason;
    ASSERT_EQ(cold.design->regions.size(), warm.design->regions.size()) << label;
    EXPECT_EQ(cold.design->cache.reused, 0U) << label;
    for (size_t i = 0; i < cold.design->regions.size(); ++i) {
      const auto& a = cold.design->regions[i];
      const auto& b = warm.design->regions[i];
      EXPECT_EQ(a.module_name, b.module_name) << label;
      EXPECT_EQ(a.cache_key, b.cache_key) << label << ' ' << a.module_name;
      EXPECT_EQ(a.report, b.report) << label << ' ' << a.module_name;
      EXPECT_EQ(a.search_credits, b.search_credits) << label << ' ' << a.module_name;
      EXPECT_EQ(a.search, b.search) << label << ' ' << a.module_name;
      EXPECT_EQ(a.structural_work, b.structural_work) << label << ' ' << a.module_name;
    }
    EXPECT_EQ(artifacts(*cold.design), artifacts(*warm.design)) << label;
    EXPECT_EQ(cold_work.remaining, warm_work.remaining) << label;
  }
  void TearDown() override {
    if (HasFailure()) {
      for (const auto& d : diag::sink().records()) {
        std::cerr << d.code << ": " << d.message << '\n';
      }
    }
    diag::sink().clear();
    std::filesystem::remove_all(cache_dir);
  }
  void check_function(Logical_design& design, bool negedge) {
    hhds::GraphLibrary flat_library;
    auto               flat = partition::flatten_hierarchy(design.top.get(), &flat_library, "flat");
    ASSERT_TRUE(flat);
    partition::Region_body rb;
    rb.src         = flat.get();
    rb.module_name = "flat";
    std::vector<hhds::Node_class> nodes;
    for (auto node : flat->body().nodes()) {
      nodes.push_back(node);
    }
    rb.nodes = nodes;
    for (auto name : {"a", "b", "clk"}) {
      rb.inputs.push_back({name, flat->get_input_pin(name), 1, false});
    }
    rb.outputs.push_back({"q", flat->get_output_pin("q").get_driver_pin(), 1, false});
    synth::Blast_options options;
    options.logical_state = true;
    auto b                = synth::blast_region(rb, options, {});
    ASSERT_EQ(b.status, synth::Region_blast::Status::blasted);
    ASSERT_EQ(b.lnet.latches().size(), 1U);
    ASSERT_TRUE(b.source_state);
    ASSERT_EQ(b.source_state->bits.size(), 1U);
    const auto& row = b.source_state->sources[b.source_state->bits[0].source];
    EXPECT_EQ(row.neg_clock, negedge);
    for (unsigned assignment = 0; assignment < 16; ++assignment) {
      const auto&       net = b.lnet;
      std::vector<bool> values(net.size());
      for (synth::Lid id = 0; id < net.size(); ++id) {
        if (net.kind(id) == synth::Lnet::Kind::source) {
          if (net.is_latch_source(id)) {
            values[id] = assignment & 8;
          } else {
            const auto origin = b.all_pi_order[net.source_index(id)];
            ASSERT_EQ(origin.kind, synth::Pi_kind::region_input);
            const auto [port, bit] = b.pi_order[origin.index];
            ASSERT_EQ(bit, 0);
            values[id] = (assignment >> port) & 1;
          }
        } else {
          unsigned word = 0;
          for (unsigned f = 0; f < net.fanin_count(id); ++f) {
            word |= unsigned(values[net.fanin(id, f)]) << f;
          }
          values[id] = net.eval(id, word);
        }
      }
      EXPECT_EQ(values[net.latch(0).d], bool((assignment ^ (assignment >> 1)) & 1));
      ASSERT_FALSE(net.outputs().empty());
      EXPECT_EQ(values[net.outputs()[0].node], bool(assignment & 8));
      ASSERT_EQ(b.source_state->controls.size(), 1U);
      const auto clock_output = b.source_state->controls[0].outputs[0];
      EXPECT_EQ(values[net.outputs()[clock_output].node], bool(assignment & 4));
    }
  }
};

TEST_F(DesignSynth, MapsUnlistedChildAndParentPreservingBehaviorAndSource) {
  Budget work{100000000};
  auto   result = synthesize_cmos_design(top, {}, work);
  ASSERT_TRUE(result.design) << result.reason;
  EXPECT_EQ(result.status, Status::feasible);
  EXPECT_NE(result.design->top, top);
  ASSERT_TRUE(result.design->library.find_io("child"));
  EXPECT_GE(result.design->regions.size(), 2U);
  size_t states = 0;
  for (const auto& region : result.design->regions) {
    states += region.selected.state_bits.size();
    for (const auto& bit : region.selected.source.bits) {
      EXPECT_EQ(bit.name, "state");
    }
  }
  EXPECT_EQ(states, 1U);
  check_function(*result.design, false);
  EXPECT_EQ(gu::node_name_of(state), "state");
  EXPECT_EQ(top->get_output_pin("q").get_driver_pin(), state.get_driver_pin(0));
  EXPECT_EQ(source.find_io("child")->get_graph(), child);
}

TEST_F(DesignSynth, CmosPreservesNegativeEdgeWithoutDominoRestriction) {
  gu::create_const(*top, *Dlop::create_integer(0)).connect_sink(gu::setup_sink_by_name(state, "posclk"));
  Budget work{100000000};
  auto   result = synthesize_cmos_design(top, {}, work);
  ASSERT_TRUE(result.design) << result.reason;
  check_function(*result.design, true);
}

TEST_F(DesignSynth, CacheReplaysEveryRegionAndSearchBudgetAndRecoversOneDamagedEntry) {
  Design_options options;
  options.cache.directory = cache_dir;
  Budget cold_work{100000000};
  auto   cold = synthesize_cmos_design(top, options, cold_work);
  ASSERT_TRUE(cold.design) << cold.reason;
  const auto count = cold.design->regions.size();
  ASSERT_GE(count, 2U);
  EXPECT_EQ(cold.design->cache.stored, count);
  EXPECT_EQ(cold.design->cache.misses, count);
  EXPECT_EQ(cold.design->cache.reused, 0U);
  for (unsigned iteration = 0; iteration < 2; ++iteration) {
    if (iteration == 1) {
      std::ofstream(std::filesystem::path(cache_dir) / (cold.design->regions[0].cache_key + ".usyn-cache")) << "truncated";
    }
    Budget work{100000000};
    auto   replay = synthesize_cmos_design(top, options, work);
    ASSERT_TRUE(replay.design) << replay.reason;
    EXPECT_EQ(replay.design->cache.reused, count - iteration);
    EXPECT_EQ(replay.design->cache.invalid, iteration);
    EXPECT_EQ(replay.design->cache.stored, iteration);
    EXPECT_EQ(work.remaining, cold_work.remaining);
    ASSERT_EQ(replay.design->regions.size(), count);
    for (size_t i = 0; i < count; ++i) {
      EXPECT_EQ(replay.design->regions[i].cache_key, cold.design->regions[i].cache_key);
      EXPECT_EQ(replay.design->regions[i].report, cold.design->regions[i].report);
    }
    check_function(*replay.design, false);
  }
  options.cache.directory.clear();
  Budget disabled_work{100000000};
  auto   disabled = synthesize_cmos_design(top, options, disabled_work);
  ASSERT_TRUE(disabled.design) << disabled.reason;
  EXPECT_FALSE(disabled.design->cache.enabled);
  EXPECT_EQ(disabled.design->cache.io_work, 0U);
  EXPECT_EQ(disabled.design->cache.reused, 0U);
  EXPECT_EQ(disabled_work.remaining, cold_work.remaining);
}

TEST_F(DesignSynth, ReconnectsSeparatelyColoredRegions) {
  for (auto node : top->body().nodes()) {
    gu::set_color(node, node == state ? 2 : 1);
  }
  Budget work{100000000};
  auto   result = synthesize_cmos_design(top, {}, work);
  ASSERT_TRUE(result.design) << result.reason;
  EXPECT_GE(result.design->regions.size(), 3U);
  check_function(*result.design, false);
  EXPECT_EQ(gu::color_of(state), 2);
}

TEST_F(DesignSynth, CacheAdmissionFailureStopsPublicationButLocalWorkLimitAllowsRegeneration) {
  Design_options options;
  options.cache.directory = cache_dir;
  Budget cold_work{100000000};
  auto   cold = synthesize_cmos_design(top, options, cold_work);
  ASSERT_TRUE(cold.design) << cold.reason;
  Budget   measure{100000000};
  uint64_t samples           = 0;
  measure.admission_interval = 1;
  measure.admission          = [&] {
    ++samples;
    return true;
  };
  auto warm = synthesize_cmos_design(top, options, measure);
  ASSERT_TRUE(warm.design) << warm.reason;
  ASSERT_GT(samples, 32U);
  // Probe across the design, including cache decoding and its final checks.
  for (const auto stop : {samples / 4, samples / 2, samples - 1}) {
    Budget   work{100000000};
    uint64_t calls          = 0;
    work.admission_interval = 1;
    // A single failed sample is sticky even if subsequent samples would pass.
    work.admission          = [&] { return ++calls != stop; };
    auto refused            = synthesize_cmos_design(top, options, work);
    EXPECT_FALSE(refused.design) << stop;
    EXPECT_EQ(refused.status, Status::search_exhausted);
    EXPECT_TRUE(work.resource_exhausted);
    EXPECT_EQ(calls, stop);
  }
  options.cache.entry_work = 0;
  Budget work{100000000};
  auto   rebuilt = synthesize_cmos_design(top, options, work);
  ASSERT_TRUE(rebuilt.design) << rebuilt.reason;
  EXPECT_EQ(rebuilt.design->cache.reused, 0U);
  EXPECT_EQ(rebuilt.design->cache.refused, rebuilt.design->regions.size());
  EXPECT_FALSE(work.resource_exhausted);
  EXPECT_EQ(work.remaining, cold_work.remaining);
  check_function(*rebuilt.design, false);
}

TEST_F(DesignSynth, ResolvesOpaqueDefinitionsWithoutPullingInUnreachableGraphs) {
  auto opaque = source.create_io("opaque");
  opaque->add_input("a", 1);
  opaque->add_output("y", 2);
  opaque->set_bits("a", 1);
  opaque->set_bits("y", 1);
  auto io = source.create_io("opaque_top");
  io->add_input("a", 1);
  io->add_output("y", 2);
  io->set_bits("a", 1);
  io->set_bits("y", 1);
  auto graph    = io->create_graph();
  auto instance = gu::create_typed_node(*graph, Ntype_op::Sub);
  instance.set_subnode(opaque);
  graph->get_input_pin("a").connect_sink(instance.create_sink_pin(1));
  auto y = instance.create_driver_pin(2);
  gu::set_ubits(y, 1);
  y.connect_sink(graph->get_output_pin("y"));
  Budget work{100000000};
  auto   result = synthesize_cmos_design(graph, {}, work);
  ASSERT_TRUE(result.design) << result.reason;
  EXPECT_FALSE(result.design->library.find_io("top"));
  EXPECT_FALSE(result.design->library.find_io("child"));
  auto copied = result.design->library.find_io("opaque");
  ASSERT_TRUE(copied);
  EXPECT_NE(copied, opaque);
  EXPECT_FALSE(copied->get_graph());
  EXPECT_EQ(copied->get_input_port_id("a"), 1U);
  EXPECT_EQ(copied->get_output_port_id("y"), 2U);
  unsigned instances = 0;
  for (const auto& definition : result.design->top->definitions().graphs()) {
    for (auto node : definition->body().nodes()) {
      if (node.get_subnode_io() == copied) {
        ++instances;
      }
    }
  }
  EXPECT_EQ(instances, 1U);
}

TEST_F(DesignSynth, ProcessAdmissionCancellationDiscardsAllOutput) {
  for (const unsigned stop : {1U, 8U, 32U}) {
    Budget work{100000000};
    work.admission_interval = 1;
    unsigned samples        = 0;
    work.admission          = [&] { return ++samples < stop; };
    auto result             = synthesize_cmos_design(top, {}, work);
    EXPECT_EQ(result.status, Status::search_exhausted) << stop << ": " << result.reason;
    EXPECT_FALSE(result.design);
    EXPECT_TRUE(work.resource_exhausted);
    EXPECT_EQ(samples, stop);
  }
  EXPECT_EQ(top->get_output_pin("q").get_driver_pin(), state.get_driver_pin(0));
}

TEST_F(DesignSynth, FinalPreparationAdmissionRefusalIsExhaustionNotAnInvalidDesign) {
  const std::array          roots{top};
  synth::Preparation_budget preparation;
  uint64_t                  samples = 0, final_sample = 0;
  preparation.admission = [&](std::string_view stage, uint64_t) {
    ++samples;
    if (stage == "complete") {
      final_sample = samples;
    }
    return true;
  };
  auto prepared = synth::prepare_design(roots, false, "pass.usyn", &preparation);
  ASSERT_TRUE(prepared);
  ASSERT_EQ(final_sample, samples);
  ASSERT_GT(samples, 1U);
  Budget work{100000000};
  work.admission_interval = 1;
  uint64_t calls          = 0;
  // One entry check precedes shared preparation. Every preparation checkpoint
  // is sampled with interval 1, including the zero-work bulk-copy checks.
  work.admission          = [&] { return ++calls != final_sample + 1; };
  const auto refused      = synthesize_cmos_design(top, {}, work);
  EXPECT_EQ(refused.status, Status::search_exhausted);
  EXPECT_EQ(refused.reason, "design preparation budget");
  EXPECT_FALSE(refused.design);
  EXPECT_TRUE(work.resource_exhausted);
  EXPECT_EQ(calls, final_sample + 1);
  EXPECT_EQ(top->get_output_pin("q").get_driver_pin(), state.get_driver_pin(0));
  Budget retry{100000000};
  auto   result = synthesize_cmos_design(top, {}, retry);
  ASSERT_TRUE(result.design) << result.reason;
  check_function(*result.design, false);
}

TEST_F(DesignSynth, InternalFlattenRefusalIsBudgetExhaustionAndStopsCallbacks) {
  // Measure the shared traversal up to an internal flatten checkpoint. No
  // region builders have run yet, so this prefix is identical in the driver.
  uint64_t                  samples = 0, spent = 0;
  synth::Preparation_budget preparation;
  preparation.admission = [&](std::string_view, uint64_t work) {
    ++samples;
    spent += work;
    return true;
  };
  const std::array roots{top};
  auto             prepared = synth::prepare_design(roots, false, "pass.usyn", &preparation);
  ASSERT_TRUE(prepared);
  hhds::GraphLibrary scratch;
  bool               stopped = false;
  ASSERT_FALSE(Pass_partition::build_decomposition(prepared->resolve_graphs,
                                                   &scratch,
                                                   "top",
                                                   false,
                                                   {},
                                                   partition::Flatten_mode::on,
                                                   false,
                                                   {},
                                                   1,
                                                   prepared->loops.preserved_defs,
                                                   {},
                                                   [&](std::string_view stage, uint64_t work) {
                                                     EXPECT_FALSE(stopped);
                                                     ++samples;
                                                     spent += work;
                                                     if (stage == "flatten-wire_edges-step") {
                                                       stopped = true;
                                                       return false;
                                                     }
                                                     return true;
                                                   }));
  ASSERT_TRUE(stopped);
  Design_options options;
  options.flatten = partition::Flatten_mode::on;
  Budget work{100000000};
  work.admission_interval = 1;
  uint64_t calls          = 0;
  work.admission          = [&] { return ++calls != samples + 1; };  // driver entry adds one sample
  auto result             = synthesize_cmos_design(top, options, work);
  EXPECT_EQ(result.status, Status::search_exhausted);
  EXPECT_EQ(result.reason, "design partition budget");
  EXPECT_FALSE(result.design);
  EXPECT_TRUE(work.resource_exhausted);
  EXPECT_EQ(calls, samples + 1);

  // The same prefix also obeys deterministic work admission without any
  // process callback. The driver's entry charge exhausts this budget one step
  // before the measured flatten checkpoint can be accepted.
  Budget limited{spent};
  result = synthesize_cmos_design(top, options, limited);
  EXPECT_EQ(result.status, Status::search_exhausted);
  EXPECT_EQ(result.reason, "design partition budget");
  EXPECT_FALSE(result.design);
  EXPECT_TRUE(limited.exhausted);
  EXPECT_FALSE(limited.resource_exhausted);
  EXPECT_EQ(top->get_output_pin("q").get_driver_pin(), state.get_driver_pin(0));
  Budget retry{100000000};
  result = synthesize_cmos_design(top, options, retry);
  ASSERT_TRUE(result.design) << result.reason;
  check_function(*result.design, false);
}

TEST_F(DesignSynth, FinalPartitionRefusalDiscardsCompletedRegionsAndAllowsFreshRetry) {
  for (const auto flatten : {partition::Flatten_mode::off, partition::Flatten_mode::on}) {
    Design_options options;
    options.flatten = flatten;
    Budget   measured{100000000};
    uint64_t samples            = 0;
    measured.admission_interval = 1;
    measured.admission          = [&] {
      ++samples;
      return true;
    };
    auto reference = synthesize_cmos_design(top, options, measured);
    ASSERT_TRUE(reference.design) << reference.reason;
    ASSERT_FALSE(reference.design->regions.empty());
    ASSERT_GT(samples, 1U);
    check_function(*reference.design, false);

    // The final sample is partition completion, after builders have emitted
    // all bodies. Reject only this sample: no partial design may escape.
    Budget   work{100000000};
    uint64_t calls          = 0;
    work.admission_interval = 1;
    work.admission          = [&] { return ++calls != samples; };
    auto refused            = synthesize_cmos_design(top, options, work);
    EXPECT_EQ(refused.status, Status::search_exhausted);
    EXPECT_EQ(refused.reason, "design partition budget");
    EXPECT_FALSE(refused.design);
    EXPECT_TRUE(work.resource_exhausted);
    EXPECT_EQ(calls, samples);
    EXPECT_EQ(top->get_output_pin("q").get_driver_pin(), state.get_driver_pin(0));

    Budget retry{100000000};
    auto   result = synthesize_cmos_design(top, options, retry);
    ASSERT_TRUE(result.design) << result.reason;
    EXPECT_EQ(retry.remaining, measured.remaining);
    check_function(*result.design, false);
  }
}

TEST_F(DesignSynth, RefusalDoesNotPublishPartialDesignOrChangeSource) {
  Design_options options;
  options.max_source_nodes = 1;
  Budget work{100000000};
  auto   result = synthesize_cmos_design(top, options, work);
  EXPECT_EQ(result.status, Status::search_exhausted);
  EXPECT_FALSE(result.design);
  options.max_source_nodes  = 100;
  options.logical.max_nodes = 1;
  Budget later{100000000};
  result = synthesize_cmos_design(top, options, later);
  EXPECT_EQ(result.status, Status::search_exhausted);
  EXPECT_FALSE(result.design);
  EXPECT_EQ(top->get_output_pin("q").get_driver_pin(), state.get_driver_pin(0));
  Budget retry{100000000};
  result = synthesize_cmos_design(top, {}, retry);
  ASSERT_TRUE(result.design) << result.reason;
  check_function(*result.design, false);
}
TEST_F(DesignSynth, EarlierRegionStillHitsAfterALaterSizeEditAndWarmEqualsCold) {
  Budget cold_work{100000000};
  auto   cold = cached_run(top, cache_dir + "/shared", cold_work);
  ASSERT_TRUE(cold.design) << cold.reason;
  const auto* child_cold = region_of(*cold.design, "child");
  ASSERT_NE(child_cold, nullptr);
  // Growing top changes preparation, partition, translation and emission work
  // charged before and around the child's search. The child's search credits,
  // and hence its key and decisions, must not observe them.
  hhds::GraphLibrary edited_library;
  const auto         edited = build_design(edited_library, {.extra = true});
  Budget             work{100000000};
  auto               warm = cached_run(edited.top, cache_dir + "/shared", work);
  ASSERT_TRUE(warm.design) << warm.reason;
  const auto* child_warm = region_of(*warm.design, "child");
  ASSERT_NE(child_warm, nullptr);
  EXPECT_TRUE(child_warm->cache_reused);
  EXPECT_EQ(child_warm->cache_key, child_cold->cache_key);
  EXPECT_EQ(warm.design->cache.reused, 1U);
  EXPECT_EQ(warm.design->cache.misses, warm.design->regions.size() - 1);
  EXPECT_NE(work.remaining, cold_work.remaining);  // the edit did change the structural work
  expect_cold_equivalent(warm, work, edited.top, "edit");
}

TEST_F(DesignSynth, RenamedDefinitionHitsAndPublishesCurrentNames) {
  Budget cold_work{100000000};
  auto   cold = cached_run(top, cache_dir + "/shared", cold_work);
  ASSERT_TRUE(cold.design) << cold.reason;
  hhds::GraphLibrary renamed_library;
  const auto         renamed = build_design(renamed_library, {.child = "lane"});
  Budget             work{100000000};
  auto               warm = cached_run(renamed.top, cache_dir + "/shared", work);
  ASSERT_TRUE(warm.design) << warm.reason;
  // Definition and module names are intermediate: every region is reused, and
  // the output carries the current names, exactly like a cold run.
  EXPECT_EQ(warm.design->cache.reused, warm.design->regions.size());
  EXPECT_EQ(warm.design->cache.invalid, 0U);
  ASSERT_NE(region_of(*warm.design, "lane"), nullptr);
  EXPECT_EQ(region_of(*warm.design, "child"), nullptr);
  EXPECT_EQ(region_of(*warm.design, "lane")->cache_key, region_of(*cold.design, "child")->cache_key);
  EXPECT_EQ(region_of(*warm.design, "lane")->selected.source.source_graph, "lane");
  EXPECT_TRUE(warm.design->library.find_io("lane"));
  EXPECT_FALSE(warm.design->library.find_io("child"));
  EXPECT_EQ(work.remaining, cold_work.remaining);
  expect_cold_equivalent(warm, work, renamed.top, "definition");
}

TEST_F(DesignSynth, RenamedRegisterOrTopLevelIoRegeneratesOnlyItsRegion) {
  Budget cold_work{100000000};
  auto   cold = cached_run(top, cache_dir + "/shared", cold_work);
  ASSERT_TRUE(cold.design) << cold.reason;
  const auto top_key = region_of(*cold.design, "top")->cache_key;
  for (const auto& [label, names] : {
           std::pair{std::string{"register"}, Design_names{.state = "count"}},
           std::pair{   std::string{"input"},    Design_names{.input = "a2"}},
           std::pair{  std::string{"output"},   Design_names{.output = "q2"}}
  }) {
    hhds::GraphLibrary library;
    const auto         renamed = build_design(library, names);
    Budget             work{100000000};
    auto               warm = cached_run(renamed.top, cache_dir + "/shared", work);
    ASSERT_TRUE(warm.design) << warm.reason;
    // Matched names are identity: the region binding them misses, while the
    // child definition's own ports and body are unchanged and still hit.
    const auto* renamed_top = region_of(*warm.design, "top");
    ASSERT_NE(renamed_top, nullptr) << label;
    EXPECT_FALSE(renamed_top->cache_reused) << label;
    EXPECT_NE(renamed_top->cache_key, top_key) << label;
    EXPECT_TRUE(region_of(*warm.design, "child")->cache_reused) << label;
    EXPECT_EQ(warm.design->cache.reused, 1U) << label;
    expect_cold_equivalent(warm, work, renamed.top, label);
  }
}

// Region searches share one search-only remainder: each later region starts
// with the limit minus the search work before it. Two heavy definitions whose
// searches, each given the whole limit, exceed it together (the rejected
// per-region-credit accounting refused this design) complete: the second
// region degrades, keeping its complete incumbent, and a warm replay equals
// the cold run.
TEST_F(DesignSynth, HeavyRegionsShareTheSearchRemainderAndDegradeInsteadOfFailing) {
  constexpr uint64_t limit = 60000000;
  hhds::GraphLibrary library;
  const auto         design = build_lanes(library, 4);
  Budget             work{limit};
  auto               cold = cached_run(design, cache_dir + "/lanes", work);
  ASSERT_TRUE(cold.design) << cold.reason;
  EXPECT_EQ(cold.status, Status::feasible);
  const auto* lane0 = region_of(*cold.design, "lane0");
  const auto* lane1 = region_of(*cold.design, "lane1");
  ASSERT_NE(lane0, nullptr);
  ASSERT_NE(lane1, nullptr);
  // Regions draw on the remainder in order; structural work never reduces it.
  uint64_t searched = 0;
  for (const auto& region : cold.design->regions) {
    EXPECT_EQ(region.search_credits, limit - searched) << region.module_name;
    searched += region.search.work;
  }
  EXPECT_LE(searched, limit);
  EXPECT_LT(work.consumed, limit);
  // The same lane1 alone gets the whole limit as its credits.
  hhds::GraphLibrary alone_library;
  Budget             alone_work{limit};
  const auto         alone = synthesize_cmos_design(build_lanes(alone_library, 4, false, "lane1"), {}, alone_work);
  ASSERT_TRUE(alone.design) << alone.reason;
  const auto* lane1_alone = region_of(*alone.design, "lane1");
  ASSERT_NE(lane1_alone, nullptr);
  EXPECT_EQ(lane1_alone->search_credits, limit);
  EXPECT_GT(work.consumed + lane0->search.work + lane1_alone->search.work, limit);
  // lane1 ran short: a different, exhausted search that still published its
  // complete incumbent under fewer credits than it would otherwise use.
  EXPECT_LT(lane1->search_credits, lane1_alone->search.work);
  EXPECT_TRUE(lane1->report.exhausted);
  EXPECT_NE(lane1->report, lane1_alone->report);
  EXPECT_FALSE(lane1->selected.selected.endpoints.empty());
  EXPECT_FALSE(work.resource_exhausted);

  Budget warm_work{limit};
  auto   warm = cached_run(design, cache_dir + "/lanes", warm_work);
  ASSERT_TRUE(warm.design) << warm.reason;
  EXPECT_EQ(warm.design->cache.reused, warm.design->regions.size());
  EXPECT_EQ(warm.design->cache.replayed_search_work, searched);
  EXPECT_EQ(warm_work.remaining, work.remaining);
  expect_cold_equivalent(warm, warm_work, design, "lanes");
}

// An earlier region whose size and search change changes every later
// region's search credits. An untouched later region still hits: its record
// was not bound to its credits and the new credits stay above its floor.
TEST_F(DesignSynth, EarlierRegionSizeAndSearchChangeKeepsLaterUntouchedRegionsReused) {
  constexpr uint64_t limit = 4000000000;
  hhds::GraphLibrary library;
  Budget             cold_work{limit};
  auto               cold = cached_run(build_lanes(library, 3), cache_dir + "/shared", cold_work);
  ASSERT_TRUE(cold.design) << cold.reason;
  ASSERT_EQ(cold.design->regions.front().module_name, "lane0");  // children first, in definition order
  hhds::GraphLibrary edited_library;
  const auto         edited = build_lanes(edited_library, 3, true);
  Budget             work{limit};
  auto               warm = cached_run(edited, cache_dir + "/shared", work);
  ASSERT_TRUE(warm.design) << warm.reason;
  const auto* lane0 = region_of(*warm.design, "lane0");
  ASSERT_NE(lane0, nullptr);
  EXPECT_FALSE(lane0->cache_reused);
  EXPECT_NE(lane0->search.work, region_of(*cold.design, "lane0")->search.work);
  for (const auto* module : {"lane1", "top"}) {
    const auto* before = region_of(*cold.design, module);
    const auto* after  = region_of(*warm.design, module);
    ASSERT_NE(after, nullptr) << module;
    EXPECT_TRUE(after->cache_reused) << module;
    EXPECT_NE(after->search_credits, before->search_credits) << module;
    EXPECT_FALSE(after->search.bound) << module;
    EXPECT_GE(after->search_credits, after->search.floor) << module;
    EXPECT_EQ(after->search, before->search) << module;
    EXPECT_EQ(after->report, before->report) << module;
  }
  EXPECT_EQ(warm.design->cache.reused, 2U);
  EXPECT_EQ(warm.design->cache.credit_misses, 0U);
  expect_cold_equivalent(warm, work, edited, "grown");
}

// A record replays only under credits that reproduce it: a bound record
// needs its exact credits, an unbound one at least its floor. Anything else is
// a credit miss that re-searches. Its result replaces the record, except that
// an unbound record is never replaced by a bound one (replaces_stored_record):
// the more general record stays, so the original credits hit again.
TEST_F(DesignSynth, CreditMissesReSearchAndKeepTheMoreGeneralRecord) {
  hhds::GraphLibrary library;
  const auto         design = build_lanes(library, 3);
  const auto         kept   = cache_dir + "/kept";
  Budget             big{4000000000};
  auto               first = cached_run(design, kept, big);
  ASSERT_TRUE(first.design) << first.reason;
  const auto* recorded = region_of(*first.design, "lane0");
  ASSERT_NE(recorded, nullptr);
  const auto floor = recorded->search.floor;
  ASSERT_FALSE(recorded->search.bound);
  const auto unbound = record_bytes(kept, *recorded);
  ASSERT_FALSE(unbound.empty());
  const auto lane0 = [&](const std::string& directory, uint64_t limit) {
    Budget work{limit};
    auto   run = cached_run(design, directory, work);
    EXPECT_TRUE(run.design) << limit << ": " << run.reason;
    const auto* region = run.design ? region_of(*run.design, "lane0") : nullptr;
    EXPECT_NE(region, nullptr) << limit;
    return region ? std::optional<Design_region>(*region) : std::nullopt;
  };
  // Just enough for the unbound record.
  EXPECT_TRUE(lane0(kept, floor)->cache_reused);
  // Too little: re-searched, bound to exactly these credits, and not stored.
  {
    Budget work{floor - 1};
    auto   run = cached_run(design, kept, work);
    ASSERT_TRUE(run.design) << run.reason;
    const auto* region = region_of(*run.design, "lane0");
    EXPECT_FALSE(region->cache_reused);
    EXPECT_GE(run.design->cache.credit_misses, 1U);
    EXPECT_GE(run.design->cache.kept, 1U);
    EXPECT_LE(run.design->cache.kept, run.design->cache.credit_misses);
    ASSERT_TRUE(region->search.bound);
    EXPECT_EQ(region->search.credits, floor - 1);
    expect_cold_equivalent(run, work, design, "below-floor");
  }
  EXPECT_EQ(record_bytes(kept, *recorded), unbound);
  EXPECT_TRUE(lane0(kept, 4000000000)->cache_reused);

  // Bound records replace each other; an unbound one replaces a bound one.
  const auto bound = cache_dir + "/bound";
  EXPECT_FALSE(lane0(bound, floor - 1)->cache_reused);
  const auto at_floor = record_bytes(bound, *recorded);
  ASSERT_FALSE(at_floor.empty());
  const auto lower = lane0(bound, floor - 2);
  EXPECT_FALSE(lower->cache_reused);
  ASSERT_TRUE(lower->search.bound);
  EXPECT_NE(record_bytes(bound, *recorded), at_floor);
  EXPECT_FALSE(lane0(bound, 4000000000)->cache_reused);
  EXPECT_EQ(record_bytes(bound, *recorded), unbound);
  EXPECT_FALSE(lane0(bound, floor - 1)->cache_reused);
  EXPECT_EQ(record_bytes(bound, *recorded), unbound);
}

// The structural allowance and the search remainder fail differently. The
// searches never charge the structural allowance: its measured work alone
// suffices (the searches, starved to the same limit, degrade), and any less
// refuses the design wherever it runs out -- preparation, partitioning or a
// region's emission after its search completed -- publishing nothing and
// leaving the source untouched. A later region whose share of the search
// remainder cannot complete a search publishes its identity selection instead:
// its mandatory steps draw on the structural allowance only.
TEST_F(DesignSynth, StructuralRefusalPublishesNothingButAStarvedLaterRegionDegrades) {
  Budget measured{100000000};
  auto   reference = synthesize_cmos_design(top, {}, measured);
  ASSERT_TRUE(reference.design) << reference.reason;
  const auto structural = measured.consumed;
  Budget     exact{structural};
  auto       degraded = synthesize_cmos_design(top, {}, exact);
  ASSERT_TRUE(degraded.design) << degraded.reason;
  EXPECT_EQ(exact.consumed, structural);
  EXPECT_EQ(exact.remaining, 0U);
  check_function(*degraded.design, false);
  std::set<std::string> reasons;
  for (uint64_t limit = structural - 1; limit > 0; limit = limit * 7 / 8) {
    Budget work{limit};
    auto   refused = synthesize_cmos_design(top, {}, work);
    ASSERT_FALSE(refused.design) << limit;
    EXPECT_EQ(refused.status, Status::search_exhausted) << limit;
    EXPECT_FALSE(work.resource_exhausted) << limit;
    if (work.exhausted) {
      reasons.insert(refused.reason.substr(refused.reason.find(": ") == std::string::npos ? 0 : refused.reason.find(": ") + 2));
    }
  }
  EXPECT_TRUE(reasons.contains("design preparation budget"));
  EXPECT_TRUE(reasons.contains("design partition budget"));
  EXPECT_TRUE(
      std::any_of(reasons.begin(), reasons.end(), [](const auto& r) { return r.find("emission budget") != std::string::npos; }));
  EXPECT_EQ(top->get_output_pin("q").get_driver_pin(), state.get_driver_pin(0));

  // Starve lane1: lane0's search leaves too little for lane1's searches, which
  // used to fail the design (its import and freeze drew on its share too).
  hhds::GraphLibrary library;
  const auto         lanes    = build_lanes(library, 8);
  bool               fallback = false;
  for (uint64_t limit = 400000; limit > 20000; limit = limit * 4 / 5) {
    Budget work{limit};
    auto   result = synthesize_cmos_design(lanes, {}, work);
    ASSERT_TRUE(result.design) << limit << ": " << result.reason;
    EXPECT_FALSE(work.exhausted) << limit;
    const auto* lane1 = region_of(*result.design, "lane1");
    ASSERT_NE(lane1, nullptr);
    const auto& endpoints = lane1->selected.selected.endpoints;
    ASSERT_EQ(endpoints.size(), 8U);
    if (lane1->report.identity_fallbacks) {
      fallback = true;
      EXPECT_TRUE(lane1->report.exhausted);
      EXPECT_GE(std::count_if(endpoints.begin(), endpoints.end(), [](const auto& e) { return e.origin == "identity"; }),
                static_cast<std::ptrdiff_t>(lane1->report.identity_fallbacks));
    }
  }
  EXPECT_TRUE(fallback);
}

// Four heavy definitions (one assign apart) share the search remainder: each
// search spends most of what it is given, so the last lane is left with fewer
// credits than its own mandatory steps cost and none of its endpoint searches
// completes. Charging those steps to the search failed such a design
// ("ftq_d__c0: logical synthesis admission exhausted" on four FTQ copies at the
// default work).
// Every region still publishes -- the last one its identity baseline -- and a
// warm run replays both ledgers exactly.
TEST_F(DesignSynth, FourHeavyRegionsPublishTheirMinimalSelectionWhenTheRemainderRunsOut) {
  constexpr uint64_t limit = 1000000;
  hhds::GraphLibrary library;
  const auto         design = build_lanes(library, 6, false, {}, 4);
  Budget             work{limit};
  auto               cold = cached_run(design, cache_dir + "/four", work);
  ASSERT_TRUE(cold.design) << cold.reason;
  EXPECT_EQ(cold.status, Status::feasible);
  EXPECT_FALSE(work.exhausted);
  uint64_t searched = 0, structural = 0;
  for (const auto& region : cold.design->regions) {
    EXPECT_EQ(region.search_credits, limit - searched) << region.module_name;
    searched   += region.search.work;
    structural += region.structural_work;
  }
  EXPECT_LE(searched, limit);
  const auto* first = region_of(*cold.design, "lane0");
  const auto* last  = region_of(*cold.design, "lane3");
  ASSERT_NE(first, nullptr);
  ASSERT_NE(last, nullptr);
  EXPECT_EQ(first->report.identity_fallbacks, 0U);
  EXPECT_LT(last->search_credits, last->structural_work);
  EXPECT_TRUE(last->report.exhausted);
  const auto& endpoints = last->selected.selected.endpoints;
  ASSERT_EQ(endpoints.size(), 6U);
  EXPECT_EQ(last->report.identity_fallbacks, endpoints.size());
  ASSERT_EQ(last->report.initial.size(), endpoints.size());
  for (const auto& search : last->report.initial) {
    EXPECT_TRUE(search.exhausted);
  }
  for (const auto& endpoint : endpoints) {
    EXPECT_EQ(endpoint.origin, "identity");
    ASSERT_EQ(endpoint.cells.size(), 1U);
    EXPECT_TRUE(endpoint.cells[0].latch);
  }
  EXPECT_EQ(last->report.before, last->report.after);

  Budget warm_work{limit};
  auto   warm = cached_run(design, cache_dir + "/four", warm_work);
  ASSERT_TRUE(warm.design) << warm.reason;
  EXPECT_EQ(warm.design->cache.reused, warm.design->regions.size());
  EXPECT_EQ(warm.design->cache.replayed_search_work, searched);
  EXPECT_EQ(warm.design->cache.replayed_structural_work, structural);
  EXPECT_EQ(warm_work.remaining, work.remaining);
  expect_cold_equivalent(warm, warm_work, design, "four");
}

// A hit replays its region's structural work as one spend, which is refused
// exactly when the cold run's mandatory steps would run out: at every limit a
// warm run publishes like a cold one, or refuses in the same region. Cheap
// searches (no residual, feedback or pairs) keep the records' floors below
// the structural total, so some hits do replay into a refusal.
TEST_F(DesignSynth, WarmRunsRefuseOrPublishLikeColdRunsAtEveryLimit) {
  Design_options options;
  options.logical.optimize_residual = false;
  options.logical.feedback          = false;
  options.logical.pair_candidates   = 0;

  const auto run = [&](const std::string& directory, Budget& work) {
    auto with            = options;
    with.cache.directory = directory;
    return synthesize_cmos_design(top, with, work);
  };
  Budget seed{100000000};
  ASSERT_TRUE(run(cache_dir + "/replay", seed).design);
  const auto region_of_reason = [](const std::string& reason) {
    const auto colon = reason.find(": ");
    return colon == std::string::npos ? std::string{} : reason.substr(0, colon);
  };
  uint64_t replay_refusals = 0, hits = 0;
  for (uint64_t limit = seed.consumed + 64; limit > 0; limit = limit * 31 / 32) {
    Budget     cold_work{limit}, warm_work{limit};
    const auto cold = run({}, cold_work);
    const auto warm = run(cache_dir + "/replay", warm_work);
    ASSERT_EQ(bool(cold.design), bool(warm.design)) << limit << ": " << cold.reason << " / " << warm.reason;
    if (cold.design) {
      EXPECT_EQ(cold_work.remaining, warm_work.remaining) << limit;
      hits += warm.design->cache.reused;
      continue;
    }
    EXPECT_EQ(region_of_reason(cold.reason), region_of_reason(warm.reason)) << limit << ": " << cold.reason << " / " << warm.reason;
    replay_refusals += warm.reason.ends_with("region structural budget");
  }
  EXPECT_GT(hits, 0U);
  EXPECT_GT(replay_refusals, 0U);
}

}  // namespace
}  // namespace livehd::usyn
