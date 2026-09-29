// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "usyn_region.hpp"

#include <array>
#include <stdexcept>

#include "gtest/gtest.h"
#include "rapidjson/document.h"

namespace livehd::usyn {
namespace {
using livehd::synth::Lid;
using livehd::synth::Lnet;
using Map = livehd::synth::Region_rewrite::Map;

// The majority/mux region as the blaster records it (RAW): inputs a b c d,
// outputs y = maj(a,b,c) and z = a&d | ~b&d.
Lnet majority_net() {
  Lnet               net;
  std::array<Lid, 4> in{};
  for (uint32_t i = 0; i < 4; ++i) {
    in[i] = net.add_input(std::string(1, static_cast<char>('a' + i)) + "_b0");
  }
  const auto [a, b, c, d] = in;
  const auto and2         = [&](Lid x, Lid y) { return net.add_lut({x, y}, Lnet::kAnd2); };
  const auto or2          = [&](Lid x, Lid y) { return net.add_lut({x, y}, Lnet::kOr2); };
  const auto ab           = and2(a, b);
  const auto ac           = and2(a, c);
  const auto ab_ac        = or2(ab, ac);
  const auto y            = or2(ab_ac, and2(b, c));
  const auto ad           = and2(a, d);
  const auto nb           = net.add_lut({b}, Lnet::kNot);
  const auto z            = or2(ad, and2(nb, d));
  net.add_output(y, "__po0_y_b0");
  net.add_output(z, "__po1_z_b0");
  return net;
}

// The outputs for the source assignment `inputs` (sources in id order).
std::vector<bool> evaluate(const Lnet& net, uint32_t inputs) {
  std::vector<bool> value(net.size());
  uint32_t          source = 0;
  for (Lid i = 0; i < net.size(); ++i) {
    if (net.kind(i) == Lnet::Kind::source) {
      value[i] = ((inputs >> source++) & 1) != 0;
      continue;
    }
    uint32_t x = 0;
    for (uint32_t k = 0; k < net.fanin_count(i); ++k) {
      x |= uint32_t{value[net.fanin(i, k)]} << k;
    }
    value[i] = net.eval(i, x);
  }
  std::vector<bool> out;
  for (auto o : livehd::synth::combinational_outputs(net)) {
    out.push_back(value[o]);
  }
  return out;
}

struct Region {
  livehd::partition::Region_body rb;
  livehd::synth::Driver_options  options;
  livehd::synth::Region_ctx      ctx{rb, options};
  Region() { rb.module_name = "top"; }
};

rapidjson::Document parse(const std::string& report) {
  rapidjson::Document doc;
  doc.Parse(report.data(), report.size());
  EXPECT_FALSE(doc.HasParseError()) << report;
  return doc;
}

TEST(UsynRegion, CoverHandsTheBackendAnEquivalentNetworkOverTheSameBoundary) {
  const auto source = majority_net();
  for (const auto mode : {Search_options::Abc_mode::opt, Search_options::Abc_mode::tmap}) {
    Region         region;
    Search_options search;
    search.abc_mode = mode;
    std::string report;
    const auto  rewrite = rewrite_region(source, region.ctx, search, report);
    auto        doc     = parse(report);
    const bool  opt     = mode == Search_options::Abc_mode::opt;
    EXPECT_STREQ(doc["status"].GetString(), opt ? "abc_opt" : "abc_tmap") << report;
    EXPECT_GT(doc["domino"].GetUint64() + doc["nonunate"].GetUint64(), 0u) << report;
    EXPECT_EQ(rewrite.map, opt ? Map::flow : Map::tmap);
    const auto& logic = rewrite.logic;
    ASSERT_EQ(logic.inputs().size(), 4u);
    EXPECT_EQ(logic.inputs()[3].name, "d_b0");
    ASSERT_EQ(logic.outputs().size(), 2u);
    EXPECT_EQ(logic.outputs()[1].name, "__po1_z_b0");
    for (uint32_t v = 0; v < 16; ++v) {
      EXPECT_EQ(evaluate(logic, v), evaluate(source, v)) << v;
    }
  }
}

TEST(UsynRegion, OnlyAndOverLimitRegionsStayWithTheBackend) {
  for (const bool only : {true, false}) {
    Region         region;
    Search_options search;
    if (only) {
      search.abc_mode = Search_options::Abc_mode::only;
    } else {
      search.max_nodes = 3;
      search.fallback  = true;
    }
    std::string report;
    const auto  rewrite = rewrite_region(majority_net(), region.ctx, search, report);
    auto        doc     = parse(report);
    EXPECT_STREQ(doc["status"].GetString(), only ? "abc_only" : "abc_fallback") << report;
    if (!only) {
      EXPECT_STREQ(doc["reason"].GetString(), "source node limit") << report;
    }
    EXPECT_EQ(rewrite.map, Map::region);
  }
}

TEST(UsynRegion, WithoutFallbackAnUncoverableRegionIsAnError) {
  Region         region;
  Search_options search;
  search.max_nodes = 3;
  std::string report;
  EXPECT_THROW(rewrite_region(majority_net(), region.ctx, search, report), std::runtime_error);
}

TEST(UsynRegion, MemoryRegionsGoToTheBackendBeforeAnyCoverLimit) {
  Region region;
  region.rb.module_name = "top__cgen_memory_1rd_1wr";
  Search_options search;
  search.cover_memories = false;
  search.max_nodes      = 3;  // would be an error for a non-memory region
  std::string report;
  const auto  rewrite = rewrite_region(majority_net(), region.ctx, search, report);
  auto        doc     = parse(report);
  EXPECT_STREQ(doc["status"].GetString(), "abc_only") << report;
  EXPECT_EQ(rewrite.map, Map::region);
}

}  // namespace
}  // namespace livehd::usyn
