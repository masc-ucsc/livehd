// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "design_synth.hpp"
#include "flatten.hpp"
#include "gtest/gtest.h"
#include "hhds/attrs/name.hpp"
#include "node_util.hpp"
#include "tmap.hpp"
namespace livehd::abc {
namespace {
namespace gu = graph_util;
class AbcTmap : public ::testing::Test {
protected:
  hhds::GraphLibrary           library;
  std::shared_ptr<hhds::Graph> top;
  hhds::Node_class             flop;
  std::string                  directory;
  void                         SetUp() override {
    directory = (std::filesystem::temp_directory_path() / "abc-tmap-XXXXXX").string();
    ASSERT_NE(mkdtemp(directory.data()), nullptr);
    diag::sink().clear();
    diag::sink().set_human_stderr(false);
    diag::sink().set_jsonl_path("off");
    auto io = library.create_io("top");
    io->add_input("a", 1);
    io->add_input("b", 2);
    io->add_input("clk", 3);
    io->add_output("q", 4);
    for (auto name : {"a", "b", "clk", "q"}) {
      io->set_bits(name, 1);
      io->set_unsign(name, true);
    }
    top    = io->create_graph();
    auto x = gu::create_typed_node(*top, Ntype_op::Xor);
    top->get_input_pin("a").connect_sink(x.create_sink_pin(0));
    top->get_input_pin("b").connect_sink(x.create_sink_pin(1));
    auto data = x.create_driver_pin(0);
    gu::set_ubits(data, 1);
    flop = gu::create_typed_node(*top, Ntype_op::Flop);
    flop.attr(hhds::attrs::name).set("state");
    data.connect_sink(gu::setup_sink_by_name(flop, "din"));
    top->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(flop, "clock_pin"));
    auto q = flop.create_driver_pin(0);
    gu::set_ubits(q, 1);
    gu::set_pin_name(q, "state");
    q.connect_sink(top->get_output_pin("q"));
  }
  void TearDown() override {
    if (HasFailure()) {
      for (const auto& d : diag::sink().records()) {
        std::cerr << d.code << ": " << d.message << '\n';
      }
    }
    diag::sink().clear();
    std::filesystem::remove_all(directory);
  }
};

TEST_F(AbcTmap, MapsNativeUsynResultAndPreservesItsStateFunction) {
  ASSERT_TRUE(synth::has_tmap_provider("abc"));
  usyn::Budget work{100000000};
  auto         logical = usyn::synthesize_cmos_design(top, {}, work);
  ASSERT_TRUE(logical.design) << logical.reason;
  std::vector<std::string> stages;
  synth::Tmap_options      options;
  options.library         = "inou/prp/tests/abc/test.lib";
  options.cache_directory = directory + "/mapping";
  options.admission       = [&](std::string_view stage) {
    stages.emplace_back(stage);
    return true;
  };
  auto mapped = synth::technology_map("abc", logical.design->top, options);
  ASSERT_EQ(mapped.status, synth::Tmap_status::mapped) << mapped.reason;
  ASSERT_TRUE(mapped.design);
  ASSERT_FALSE(mapped.design->regions.empty());
  for (auto command : {"strash", "&get -n", "&nf", "&put -o"}) {
    EXPECT_EQ(std::count(stages.begin(), stages.end(), command), 2) << command;
  }
  for (const auto& row : mapped.design->regions) {
    EXPECT_EQ(row.ware_trials, 0);
    EXPECT_TRUE(row.candidate.empty());
  }
  EXPECT_TRUE(mapped.design->cache_enabled);
  EXPECT_EQ(mapped.design->cache_store_failed, 0U);
  stages.clear();
  auto warm = synth::technology_map("abc", logical.design->top, options);
  ASSERT_TRUE(warm.design) << warm.reason;
  EXPECT_EQ(std::count(stages.begin(), stages.end(), "&nf"), 0);
  EXPECT_EQ(warm.design->cache_invalid, 0U);
  ASSERT_EQ(warm.design->regions.size(), mapped.design->regions.size());
  for (size_t i = 0; i < warm.design->regions.size(); ++i) {
    const auto& row = warm.design->regions[i];
    EXPECT_STREQ(row.cache, "hit");
    EXPECT_EQ(row.gates, mapped.design->regions[i].gates);
    EXPECT_EQ(row.area, mapped.design->regions[i].area);
  }
  mapped = std::move(warm);  // independently evaluate the restored netlist below
  hhds::GraphLibrary flat_library;
  auto               flat = partition::flatten_hierarchy(mapped.design->top.get(), &flat_library, "flat");
  ASSERT_TRUE(flat);
  hhds::Node_class state;
  unsigned         registers = 0;
  for (auto node : flat->body().nodes()) {
    if (gu::type_op_of(node) == Ntype_op::Sub && node.get_subnode_io()->get_name() == "DFFx1") {
      state = node;
      ++registers;
    }
  }
  ASSERT_EQ(registers, 1U);
  EXPECT_NE(gu::node_name_of(state).find("state"), std::string_view::npos);
  const auto sink_driver = [](hhds::Node_class node, std::string_view name) {
    auto pin = node.get_sink_pin(node.get_subnode_io()->get_input_port_id(name));
    return pin.get_driver_pin();
  };
  for (unsigned assignment = 0; assignment < 16; ++assignment) {
    std::function<bool(hhds::Pin_class)> eval = [&](hhds::Pin_class pin) -> bool {
      if (pin.is_const()) {
        return gu::const_of(pin).bit_test(0);
      }
      if (pin == flat->get_input_pin("a")) {
        return assignment & 1;
      }
      if (pin == flat->get_input_pin("b")) {
        return assignment & 2;
      }
      if (pin == flat->get_input_pin("clk")) {
        return assignment & 4;
      }
      auto node = pin.get_master_node();
      if (node == state) {
        return assignment & 8;
      }
      if (gu::type_op_of(node) != Ntype_op::Sub) {
        ADD_FAILURE() << "unexpected unmapped operator " << int(gu::type_op_of(node));
        return false;
      }
      const auto cell = node.get_subnode_io()->get_name();
      bool       a    = eval(sink_driver(node, "A"));
      if (cell == "INVx1") {
        return !a;
      }
      if (cell == "BUFx1") {
        return a;
      }
      bool b = eval(sink_driver(node, "B"));
      if (cell == "XOR2x1") {
        return a != b;
      }
      if (cell == "NAND2x1") {
        return !(a && b);
      }
      if (cell == "NOR2x1") {
        return !(a || b);
      }
      ADD_FAILURE() << "unexpected cell " << cell;
      return false;
    };
    EXPECT_EQ(eval(sink_driver(state, "D")), bool((assignment ^ (assignment >> 1)) & 1));
    EXPECT_EQ(eval(sink_driver(state, "CLK")), bool(assignment & 4));
    EXPECT_EQ(eval(flat->get_output_pin("q").get_driver_pin()), bool(assignment & 8));
  }
  // Both the source design and the authoritative logical artifact remain native.
  EXPECT_EQ(top->get_output_pin("q").get_driver_pin(), flop.get_driver_pin(0));
  size_t logical_state = 0;
  for (const auto& region : logical.design->regions) {
    logical_state += region.selected.state_bits.size();
  }
  EXPECT_EQ(logical_state, 1U);
}

TEST_F(AbcTmap, MissingLibraryAndResourceRefusalPublishNoMappedDesign) {
  auto result = synth::technology_map("abc", top, {.library = "/nonexistent/usyn-test.lib"});
  EXPECT_EQ(result.status, synth::Tmap_status::invalid);
  EXPECT_FALSE(result.design);
  synth::Tmap_options options;
  options.library   = "inou/prp/tests/abc/test.lib";
  bool refused      = false;
  options.admission = [&](std::string_view stage) {
    if (stage == "&nf") {
      refused = true;
      return false;
    }
    return true;
  };
  result = synth::technology_map("abc", top, options);
  EXPECT_EQ(result.status, synth::Tmap_status::refused) << result.reason;
  EXPECT_TRUE(refused);
  EXPECT_FALSE(result.design);
  EXPECT_EQ(top->get_output_pin("q").get_driver_pin(), flop.get_driver_pin(0));
}

TEST_F(AbcTmap, CacheInvalidationCorruptionAndDisabledReuse) {
  namespace fs = std::filesystem;
  synth::Tmap_options options;
  options.library = directory + "/cells.lib";
  fs::copy_file("inou/prp/tests/abc/test.lib", options.library);
  options.cache_directory = directory + "/mapping";
  unsigned commands       = 0;
  options.admission       = [&](std::string_view stage) {
    commands += stage.starts_with("&nf");
    return true;
  };
  const auto map = [&](bool hit, bool invalid) {
    commands    = 0;
    auto result = synth::technology_map("abc", top, options);
    EXPECT_EQ(result.status, synth::Tmap_status::mapped) << result.reason;
    if (!result.design) {
      return;
    }
    EXPECT_EQ(commands == 0, hit);
    EXPECT_EQ(result.design->cache_invalid, invalid);
    EXPECT_EQ(result.design->cache_store_failed, 0U);
    for (const auto& row : result.design->regions) {
      EXPECT_EQ(std::string_view(row.cache) == "hit", hit);
    }
  };
  map(false, false);
  map(true, false);
  const auto current = [&] {
    std::ifstream in(fs::path(options.cache_directory) / "current");
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  };
  auto first = current();
  ASSERT_FALSE(first.empty());
  options.delay_ps = 1000;
  map(false, false);
  EXPECT_NE(current(), first);
  map(true, false);
  first = current();
  std::ofstream(options.library, std::ios::app) << "\n/* changed library content */\n";
  map(false, false);
  EXPECT_NE(current(), first);
  map(true, false);

  bool       damaged  = false;
  const auto snapshot = fs::path(options.cache_directory) / current() / "mapped";
  for (const auto& entry : fs::recursive_directory_iterator(snapshot)) {
    if (!entry.is_regular_file() || entry.path().filename() == "library.txt" || entry.path().filename() == "abc_cache.json"
        || entry.file_size() == 0) {
      continue;
    }
    std::fstream file(entry.path(), std::ios::in | std::ios::out | std::ios::binary);
    char         byte;
    file.read(&byte, 1);
    byte ^= 1;
    file.seekp(0);
    file.write(&byte, 1);
    damaged = true;
    break;
  }
  ASSERT_TRUE(damaged);
  map(false, true);  // no malformed HHDS body reaches the graph loader
  map(true, false);
  first            = current();
  const auto cache = options.cache_directory;
  options.cache_directory.clear();
  map(false, false);
  options.cache_directory = cache;
  EXPECT_EQ(current(), first);
  map(true, false);
}

TEST_F(AbcTmap, CacheAdmissionAndIoFailurePreservePriorSnapshot) {
  namespace fs = std::filesystem;
  synth::Tmap_options options;
  options.library         = "inou/prp/tests/abc/test.lib";
  options.cache_directory = directory + "/mapping";
  auto cold               = synth::technology_map("abc", top, options);
  ASSERT_TRUE(cold.design) << cold.reason;
  const auto current = [&] {
    std::ifstream in(fs::path(options.cache_directory) / "current");
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  };
  const auto first  = current();
  options.admission = [](std::string_view stage) { return stage != "mapping-cache"; };
  auto refused      = synth::technology_map("abc", top, options);
  EXPECT_EQ(refused.status, synth::Tmap_status::refused);
  EXPECT_FALSE(refused.design);
  EXPECT_EQ(current(), first);
  bool mapped       = false;
  options.delay_ps  = 1000;
  options.admission = [&](std::string_view stage) {
    mapped |= stage.starts_with("&nf");
    return !(mapped && stage == "mapping-cache");
  };
  refused = synth::technology_map("abc", top, options);
  EXPECT_TRUE(mapped);
  EXPECT_EQ(refused.status, synth::Tmap_status::refused);
  EXPECT_FALSE(refused.design);
  EXPECT_EQ(current(), first);
  options.admission       = {};
  options.cache_directory = directory + "/regular-file";
  std::ofstream(options.cache_directory) << "not a directory";
  auto fresh = synth::technology_map("abc", top, options);
  ASSERT_TRUE(fresh.design) << fresh.reason;
  EXPECT_EQ(fresh.design->cache_store_failed, 1U);
}

TEST_F(AbcTmap, EmbeddedSynthesisFlowCannotOverrideTmapAndLateRefusalCanRetry) {
  top->get_input_node()
      .attr(attrs::coloring_info)
      .set(R"({"region_opts":{"0":{"flow":"deliberately_invalid_synthesis_command"}}})");
  synth::Tmap_options options;
  options.library   = "inou/prp/tests/abc/test.lib";
  unsigned puts     = 0;
  options.admission = [&](std::string_view stage) { return stage != "&put -o" || ++puts < 2; };
  auto result       = synth::technology_map("abc", top, options);
  EXPECT_EQ(result.status, synth::Tmap_status::refused) << result.reason;
  EXPECT_EQ(puts, 2U);
  EXPECT_FALSE(result.design);
  EXPECT_FALSE(diag::sink().has_halting_errors());
  options.admission = {};
  result            = synth::technology_map("abc", top, options);
  ASSERT_EQ(result.status, synth::Tmap_status::mapped) << result.reason;
  ASSERT_TRUE(result.design);
  EXPECT_EQ(top->get_output_pin("q").get_driver_pin(), flop.get_driver_pin(0));
}
}  // namespace
}  // namespace livehd::abc
