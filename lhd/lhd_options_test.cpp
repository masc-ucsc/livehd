// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <algorithm>
#include <regex>
#include <string>
#include <tuple>
#include <vector>

#include "gtest/gtest.h"
#include "lhd.hpp"
#include "lhd_kernel_internal.hpp"

// satopt runs only in the compile graph pipeline: on by default for synth and
// lec compiling a Pyrope/Verilog SOURCE, off for every lg:/ln: input and every
// other command; an explicit pass.satopt always wins. Mappers get no satopt
// labels (they map what compile produced).
TEST(LhdOptions, SatoptCommandDefaultsAndExplicitOverrides) {
  for (const auto& [command, subcommand, from_source] : std::vector<std::tuple<std::string, std::string, bool>>{
           {"compile",       "",  true},
           {    "sim",       "",  true},
           { "formal", "verify",  true},
           {    "lec",       "",  true},
           {    "lec",       "", false},
           {  "synth",       "",  true},
           {  "synth",       "", false},
           {   "pass",    "abc", false},
           {   "pass",   "usyn", false}
  }) {
    lhd::Options opts;
    opts.command = command;
    if (!subcommand.empty()) {
      opts.files.push_back(subcommand);
    }
    SCOPED_TRACE(command + " " + subcommand + (from_source ? " source" : " ir"));
    const bool by_default = from_source && (command == "synth" || command == "lec");
    EXPECT_FALSE(lhd::satopt_setting(opts).has_value());
    EXPECT_EQ(lhd::satopt_during_compile(opts, from_source), by_default);
    for (const auto value : {"false", "true"}) {
      opts.sets.emplace_back("pass.satopt", value);
      EXPECT_EQ(lhd::satopt_setting(opts), std::string_view{value} == "true");
      EXPECT_EQ(lhd::satopt_during_compile(opts, from_source), std::string_view{value} == "true");
      if (command == "synth" || command == "pass") {
        Eprp_var::Eprp_dict labels;
        lhd::merge_mapper_sets(opts, subcommand == "usyn" ? "pass.usyn" : "pass.abc", labels);
        EXPECT_FALSE(labels.contains("satopt"));
      }
    }
  }
  const auto listed = lhd::list_set_options();
  const auto option = std::find_if(listed.begin(), listed.end(), [](const auto& o) { return o.name == "pass.satopt"; });
  ASSERT_NE(option, listed.end());
  EXPECT_EQ(option->default_value, "false");
}

TEST(LhdOptions, SynthCommandAndPassKeepSeparateNamespaces) {
  EXPECT_EQ(lhd::canonical_set_key("synth.potato", "synth"), "synth.potato");
  EXPECT_EQ(lhd::canonical_set_key("synth.mapper", "pass.usyn"), "synth.mapper");
  EXPECT_EQ(lhd::canonical_set_key("recipes", "pass.usyn"), "pass.usyn.recipes");
  EXPECT_EQ(lhd::canonical_set_key("pass.usyn.recipes", "synth"), "pass.usyn.recipes");
  // The renamed namespace stays verbatim so it reaches its directed error.
  EXPECT_EQ(lhd::canonical_set_key("pass.synth.support", "synth"), "pass.synth.support");
  EXPECT_FALSE(lhd::retired_set_hint("pass.usyn", "out").empty());
  EXPECT_FALSE(lhd::retired_set_hint("pass.usyn", "threads").empty());
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
           "pass.abc.satopt",
           "pass.usyn.satopt",
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
