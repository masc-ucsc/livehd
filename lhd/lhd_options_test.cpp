// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <algorithm>
#include <regex>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "lhd.hpp"
#include "lhd_kernel_internal.hpp"

TEST(LhdOptions, SynthCommandAndPassKeepSeparateNamespaces) {
  EXPECT_EQ(lhd::canonical_set_key("synth.potato", "synth"), "synth.potato");
  EXPECT_EQ(lhd::canonical_set_key("synth.mapper", "pass.synth"), "synth.mapper");
  EXPECT_EQ(lhd::canonical_set_key("recipes", "pass.synth"), "pass.synth.recipes");
  EXPECT_EQ(lhd::canonical_set_key("pass.synth.recipes", "synth"), "pass.synth.recipes");
  EXPECT_FALSE(lhd::retired_set_hint("pass.synth", "out").empty());
  EXPECT_FALSE(lhd::retired_set_hint("pass.synth", "threads").empty());
}

// Keep the full retired-option matrix in one process. The CLI integration
// test separately checks the result envelope and --config plumbing.
TEST(LhdOptions, RetiredLabelsAreHiddenAndRejected) {
  const auto listed = lhd::list_set_options();
  for (const std::string key : {
           "compile.cgen.verbose",
           "formal.lean.normalize",
           "formal.lean.cert_chunk_size",
           "formal.lean.cert_chunk_limit",
           "formal.lean.cert_wf_fallback",
           "pass.semdiff.alg",
           "pass.semdiff.verbose",
           "pass.color.compact",
           "sim.flatten",
           "compile.formal.enabled",
           "pass.abc.out",
           "pass.partition.out",
           "pass.liberty.out",
           "pass.single_edge.out",
           "compile.yosys.frontend",
           "compile.slang.defines",
           "compile.slang.includes",
           "compile.slang.undefines",
           "pass.abc.threads",
           "pass.abc.small_flow",
           "pass.abc.small_ge",
           "pass.abc.small_min_ge",
           "pass.abc.ctrl_flow",
           "pass.abc.ctrl_area_relax",
           "pass.abc.ctrl_time_budget_ms",
           "compile.yosys.abc",
           "compile.yosys.techmap",
           "compile.yosys.elab_top",
           "compile.yosys.rename_top",
           "compile.formal.active",
           "compile.formal.hier_preflight",
           "compile.upass.import_defer",
           "compile.upass.dce",
           "compile.upass.inherit",
           "compile.upass.preserve_param_provenance",
           "compile.upass.ssa_stream",
           "compile.slang.slang_flags",
           "compile.yosys.slang_flags",
           "pass.abc.stats",
           "pass.color.stats",
           "pass.opentimer.stats",
           "pass.semdiff.stats",
           "formal.stats",
           "upass.dce",
           "pass.formal.active",
           "cgen.verbose",
       }) {
    SCOPED_TRACE(key);
    EXPECT_TRUE(std::none_of(listed.begin(), listed.end(), [&](const auto& option) { return option.name == key; }));
    std::vector<std::string> args{"lhd", "compile", "unused.prp", "--set", key + "=1"};
    std::vector<char*>       argv;
    for (auto& arg : args) {
      argv.push_back(arg.data());
    }
    try {
      const auto opts = lhd::parse_args(static_cast<int>(argv.size()), argv.data());
      lhd::check_known_set_passes(opts);
      FAIL() << "retired option was accepted";
    } catch (const lhd::Lhd_error& error) {
      EXPECT_EQ(error.cls, "usage");
      EXPECT_NE(error.msg.find("no longer a public option"), std::string::npos) << error.msg;
      EXPECT_FALSE(error.hint.empty());
    }
  }
}

TEST(LhdOptions, HelpDoesNotAdvertiseRemovedLecNamespace) {
  const std::regex removed{R"((^|[^.a-z_])lec\.flag|--set lec\.|legacy lec\.)"};
  for (const auto& command : std::vector<std::vector<std::string>>{
           {"describe", "lec"},
           {"describe", "formal"},
           {"describe", "formal verify"},
           {"lec", "--help"},
           {"formal", "verify", "--help"},
           {"help", "formal"},
           {"help"}
  }) {
    for (const auto* format : {"pretty", "jsonl"}) {
      auto args = command;
      args.insert(args.begin(), "lhd");
      args.insert(args.end(), {"--diag-fmt", format});
      std::vector<char*> argv;
      for (auto& arg : args) {
        argv.push_back(arg.data());
      }
      SCOPED_TRACE(command.front());
      SCOPED_TRACE(format);
      const auto opts = lhd::parse_args(static_cast<int>(argv.size()), argv.data());
      ASSERT_TRUE(lhd::is_meta_command(opts));
      testing::internal::CaptureStdout();
      testing::internal::CaptureStderr();
      const auto status = lhd::run_meta_command(opts);
      const auto out    = testing::internal::GetCapturedStdout() + testing::internal::GetCapturedStderr();
      EXPECT_EQ(status, 0) << out;
      EXPECT_FALSE(std::regex_search(out, removed)) << out;
    }
  }
}
