//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// The analysis soundness fuzz (milestone G): the analyses of rules/analyses.decl, as implemented in
// runtime/analyses.cpp, against their own :spec on random e-graphs with concrete leaf values.

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "gtest/gtest.h"
#include "rulegen/rule_test_support.hpp"
#include "rulegen/rules.hpp"
#include "runtime/analyses.hpp"

namespace {

uint64_t env_or(const char* name, uint64_t def) {
  const char* v = std::getenv(name);
  return v == nullptr ? def : std::strtoull(v, nullptr, 10);
}

std::string read_decl() {
  for (const char* path : {"pass/aegraph/rules/analyses.decl", "rules/analyses.decl", "_main/pass/aegraph/rules/analyses.decl"}) {
    std::ifstream in(path);
    if (in) {
      std::stringstream ss;
      ss << in.rdbuf();
      return ss.str();
    }
  }
  return {};
}

TEST(AegAnalyses, DeclarationMatchesRuntimeAndIsSound) {
  const auto text = read_decl();
  ASSERT_FALSE(text.empty()) << "rules/analyses.decl is not in the test's data";
  livehd::aegraph::rulegen::Diags diags;
  const auto analyses = livehd::aegraph::rulegen::parse_analyses(text, "analyses.decl", diags);
  ASSERT_TRUE(diags.empty()) << (diags.empty() ? "" : diags.front());
  ASSERT_GE(analyses.list.size(), 10u);

  size_t     checked = 0;
  const auto seed    = env_or("LHD_AEG_FUZZ_SEED", 20260601);
  const auto graphs  = env_or("LHD_AEG_ANALYSES_GRAPHS", 60);
  const auto why     = livehd::aegraph::rulegen::testing::analyses_sound(analyses, seed, graphs, 6, &checked);
  EXPECT_TRUE(why.empty()) << why;
  // The fuzz has to bite: it verified many positive claims, not just agreed to disagree.
  EXPECT_GT(checked, 5000u) << "too few positive analysis claims were exercised";
  std::printf("[ analyses ] %zu positive claims verified over %llu graphs (seed %llu)\n", checked, static_cast<unsigned long long>(graphs),
              static_cast<unsigned long long>(seed));
}

TEST(AegAnalyses, StorageWidthRetainsPaddingAcrossEquivalentShapes) {
  using namespace livehd::aegraph;
  Egraph     eg;
  const auto leaf = eg.add_leaf();
  rt::seed_leaf(eg, leaf, 1, true);
  const auto    zero    = eg.add_const(*Dlop::create_integer(0));
  const auto    one     = eg.add_const(*Dlop::create_integer(1));
  const auto    four    = eg.add_const(*Dlop::create_integer(4));
  const Operand lanes[] = {
      {0, zero},
      {1, four},
      {2, leaf},
      {3,  one}
  };
  bool       created = false;
  const auto padded  = eg.intern(Ntype_op::Concat, {}, lanes, created);
  EXPECT_EQ(rt::unsigned_bits(eg, padded), 1);
  EXPECT_EQ(rt::storage_unsigned_bits(eg, padded), 5);

  const Operand window[] = {
      {0, padded},
      {2,   zero},
      {3,   four}
  };
  const auto read = eg.intern(Ntype_op::Get_mask, {}, window, created);
  EXPECT_EQ(rt::unsigned_bits(eg, read), 1);
  EXPECT_EQ(rt::storage_unsigned_bits(eg, read), 4);

  // Equal integer values need not have equal physical shapes. Extraction can choose either member.
  const auto alternatives = eg.add_union(leaf, padded);
  EXPECT_EQ(rt::unsigned_bits(eg, alternatives), 1);
  EXPECT_EQ(rt::storage_unsigned_bits(eg, alternatives), 5);
  EXPECT_EQ(rt::storage_unsigned_bits(eg, eg.add_leaf()), -1);
}

}  // namespace
