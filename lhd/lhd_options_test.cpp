// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include "core/metadata.hpp"
#include "diag.hpp"
#include "gtest/gtest.h"
#include "lhd.hpp"
#include "lhd_kernel_internal.hpp"
#include "node_util.hpp"
#include "pass.hpp"
#include "synth_policy.hpp"

TEST(LhdOptions, ConsumersRejectUnresolvedDriverWidths) {
  (void)lhd::list_set_options();
  hhds::GraphLibrary library;
  auto               io = library.create_io("unsized_consumer_input");
  io->add_output("o", 1);
  auto graph = io->create_graph();
  auto cell  = livehd::graph_util::create_typed_node(*graph, Ntype_op::Or);
  livehd::graph_util::create_const(*graph, *Dlop::create_integer(5)).connect_sink(cell.create_sink_pin(0));
  cell.create_driver_pin(0).connect_sink(graph->get_output_pin("o"));
  Eprp_var var;
  var.add(graph);
  auto& sink = livehd::diag::sink();
  sink.set_human_stderr(false);
  for (const auto method : {"pass.abc", "pass.usyn", "pass.lec", "pass.formal", "inou.cgen.verilog", "inou.cgen.sim"}) {
    sink.clear();
    lhd::Options opts;
    opts.workdir = "width_guard_logs";
    lhd::Result result;
    try {
      lhd::run_step(method, var, {}, opts, result);
      FAIL() << method << " consumed an unsized cell";
    } catch (const lhd::Lhd_error& error) {
      EXPECT_EQ(error.cls, "internal") << method;
      EXPECT_NE(error.msg.find("unresolved width"), std::string::npos) << method;
    }
    ASSERT_FALSE(sink.records().empty());
    EXPECT_EQ(sink.records().back().code, "graph-width-unresolved") << method;
  }
  sink.clear();
}

TEST(LhdOptions, AutomaticPlacementDensityStaysBelowHardCapAfterRestartSerialization) {
  EXPECT_DOUBLE_EQ(lhd::placement_search_density(.6, .8), .65);
  for (double cap : {.8, .999, 1.0}) {
    for (double utilization : {.7, .95, .99}) {
      const auto target   = lhd::placement_search_density(utilization, cap);
      // The kernel feeds this text back through --set for each core-search step.
      const auto restored = std::stod(std::format("{:.17g}", target));
      EXPECT_DOUBLE_EQ(restored, target);
      EXPECT_GT(restored, 0);
      EXPECT_LT(restored, cap);
      EXPECT_LE(restored, utilization + .05);
      if (utilization + .05 >= cap) {
        EXPECT_DOUBLE_EQ(std::nextafter(restored, cap), cap);
      }
    }
  }
  for (double invalid : {0.0, -1.0, 1.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
    EXPECT_THROW(lhd::placement_search_density(.99, invalid), lhd::Lhd_error);
    EXPECT_THROW(lhd::placement_search_density(invalid, 1), lhd::Lhd_error);
  }
  EXPECT_THROW(lhd::placement_search_density(.99, std::numeric_limits<double>::denorm_min()), lhd::Lhd_error);
}

// satopt is opt-in for every command. An explicit pass.satopt always wins;
// mappers get no satopt labels (they map what compile produced).
TEST(LhdOptions, NativeUsynMappingCapabilitiesAndSeparateOptions) {
  lhd::Options opts;
  EXPECT_TRUE(lhd::mapper_maps_cells(opts, "pass.usyn"));
  opts.sets.emplace_back("pass.usyn.tmap", "none");
  EXPECT_FALSE(lhd::mapper_maps_cells(opts, "pass.usyn"));
  EXPECT_TRUE(lhd::mapper_maps_cells(opts, "pass.abc"));
  opts.sets.emplace_back("pass.abc.flow", "resyn2");
  Eprp_var::Eprp_dict labels;
  lhd::merge_mapper_sets(opts, "pass.usyn", labels);
  EXPECT_FALSE(labels.contains("flow"));
  EXPECT_EQ(labels.at("tmap"), "none");
  opts.sets.emplace_back("pass.usyn.tmap", "opt");
  EXPECT_THROW(lhd::mapper_maps_cells(opts, "pass.usyn"), lhd::Lhd_error);
}

TEST(LhdOptions, SatoptCommandDefaultsAndExplicitOverrides) {
  for (const auto& [command, subcommand] : std::vector<std::pair<std::string, std::string>>{
           {"compile",       ""},
           {    "sim",       ""},
           { "formal", "verify"},
           {    "lec",       ""},
           {  "synth",       ""},
           {   "pass",    "abc"},
           {   "pass",   "usyn"}
  }) {
    lhd::Options opts;
    opts.command = command;
    if (!subcommand.empty()) {
      opts.files.push_back(subcommand);
    }
    SCOPED_TRACE(command + " " + subcommand);
    EXPECT_FALSE(lhd::satopt_setting(opts).has_value());
    EXPECT_FALSE(lhd::satopt_during_compile(opts));
    for (const auto value : {"false", "true"}) {
      opts.sets.emplace_back("pass.satopt", value);
      EXPECT_EQ(lhd::satopt_setting(opts), std::string_view{value} == "true");
      EXPECT_EQ(lhd::satopt_during_compile(opts), std::string_view{value} == "true");
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
  // Retired synth.* keys (the hint, not the generic unknown-flag error).
  for (const auto* flag : {"place.threads", "place.wire_rc", "place.tech_rc", "opentimer", "place.reset_fanout"}) {
    EXPECT_FALSE(lhd::retired_set_hint("synth", flag).empty()) << flag;
  }
  EXPECT_FALSE(lhd::retired_set_hint("pass.abc", "threads").empty());
  EXPECT_FALSE(lhd::retired_set_hint("pass.abc", "qor").empty());
  EXPECT_FALSE(lhd::retired_set_hint("pass.abc", "memory_max_bits").empty());
  EXPECT_FALSE(lhd::retired_set_hint("pass.usyn", "memory_max_bits").empty());
  EXPECT_FALSE(lhd::retired_set_hint("synth", "memory.min_bits").empty());
  EXPECT_FALSE(lhd::retired_set_hint("pass.abc", "memory").empty());
  EXPECT_FALSE(lhd::retired_set_hint("pass.usyn", "memory").empty());
  for (const auto* flag : {"place.resize.gp", "place.resize.gp_margin", "place.resize.gp_trials"}) {
    EXPECT_FALSE(lhd::retired_set_hint("synth", flag).empty()) << flag;
  }
}

// Keep the full retired-option matrix in one process. The CLI integration
// test separately checks the result envelope and --config plumbing.
TEST(LhdOptions, RetiredLabelsAreHiddenAndRejected) {
  const auto listed = lhd::list_set_options();
  for (const std::string key : {
           "compile.cgen.verbose",
           "formal.lean.normalize",
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

// These controls are implemented by the refactored chunked-WF emitter.
TEST(LhdOptions, LeanChunkControlsAreListedAndAccepted) {
  const auto listed = lhd::list_set_options();
  for (const auto& [key, value] : std::vector<std::pair<std::string, std::string>>{
           { "formal.lean.cert_chunk_size",    "2"},
           {"formal.lean.cert_chunk_limit",    "1"},
           {"formal.lean.cert_wf_fallback", "fail"}
  }) {
    SCOPED_TRACE(key);
    EXPECT_TRUE(std::any_of(listed.begin(), listed.end(), [&](const auto& option) { return option.name == key; }));
    std::vector<std::string> args{"lhd", "compile", "unused.prp", "--set", key + "=" + value};
    std::vector<char*>       argv;
    for (auto& arg : args) {
      argv.push_back(arg.data());
    }
    const auto opts = lhd::parse_args(static_cast<int>(argv.size()), argv.data());
    EXPECT_NO_THROW(lhd::check_known_set_passes(opts));
  }
}

TEST(LhdOptions, LongOptionsAcceptEqualsSpelling) {
  const auto parse = [](std::vector<std::string> args) {
    std::vector<char*> argv;
    for (auto& arg : args) {
      argv.push_back(arg.data());
    }
    return lhd::parse_args(static_cast<int>(argv.size()), argv.data());
  };
  const auto spaced = parse({"lhd", "list", "options", "--diag-fmt", "pretty", "--top", "alu"});
  const auto joined = parse({"lhd", "list", "options", "--diag-fmt=pretty", "--top=alu"});
  EXPECT_EQ(joined.diag_fmt, lhd::Diag_fmt::pretty);
  EXPECT_EQ(joined.diag_fmt, spaced.diag_fmt);
  EXPECT_EQ(joined.top, "alu");
  EXPECT_EQ(joined.command, "list");

  // Only the first `=` splits: the value keeps its own `=`.
  const auto set = parse({"lhd", "compile", "unused.prp", "--set=synth.adder=cla"});
  ASSERT_FALSE(set.sets.empty());
  EXPECT_EQ(set.sets.back().second, "cla");

  EXPECT_EQ(parse({"lhd", "tool", "tree", "--hier=2"}).tool_hier, 2);
  EXPECT_EQ(parse({"lhd", "compile", "unused.prp", "--top="}).top, "");

  // A boolean flag rejects an inline value rather than ignoring it.
  EXPECT_THROW(parse({"lhd", "list", "options", "--verbose=yes"}), lhd::Lhd_error);
  EXPECT_THROW(parse({"lhd", "list", "options", "--diag-fmt=bogus"}), lhd::Lhd_error);
  // After `--` tokens stay raw.
  const auto raw = parse({"lhd", "compile", "unused.v", "--", "--top=x"});
  ASSERT_EQ(raw.raw_args.size(), 1u);
  EXPECT_EQ(raw.raw_args[0], "--top=x");
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

// Every source attribute `::[synth.K=V]` has a same-named `synth.K` option, and
// `lhd describe synth.K` says it is also an attribute; options that exist only as
// attributes are registered. The registry is also what validate() accepts.
TEST(LhdOptions, SynthAttributesAreOptions) {
  const auto listed = lhd::list_set_options();
  const auto find   = [&](const std::string& name) {
    return std::find_if(listed.begin(), listed.end(), [&](const auto& o) { return o.name == name; });
  };
  for (const auto& attr : livehd::synth_attr::kAttributes) {
    SCOPED_TRACE(std::string(attr.key));
    const auto it = find("synth." + std::string(attr.key));
    ASSERT_NE(it, listed.end()) << "attribute synth." << attr.key << " has no synth." << attr.key << " option";
    EXPECT_NE(it->help.find("source attribute"), std::string::npos) << it->help;
    EXPECT_NO_THROW(livehd::synth_attr::validate(attr.key, attr.sample));
  }
  EXPECT_THROW(livehd::synth_attr::validate("not_an_attribute", "1"), std::invalid_argument);
  // The reverse: a source-only option must be a registered attribute.
  for (const auto& opt : lhd::kSynthSetOptions) {
    if (opt.kind != lhd::Synth_set_option::Kind::source_only) {
      continue;
    }
    const auto& attrs = livehd::synth_attr::kAttributes;
    EXPECT_NE(std::find_if(std::begin(attrs), std::end(attrs), [&](const auto& a) { return a.key == opt.name; }), std::end(attrs))
        << "source-only option synth." << opt.name << " is not a registered attribute";
  }
  // --set of a source-only attribute is a usage error that names the attribute form.
  lhd::Options opts;
  opts.command = "synth";
  opts.sets.emplace_back("synth.color", "x");
  EXPECT_THROW(lhd::check_known_set_passes(opts), lhd::Lhd_error);
}

// synth.delay_frac turns a fraction of the SDC clock period into the ABC budget, so
// one setting serves designs with different clocks. An explicit synth.delay wins;
// an explicit fraction with a missing or unresolvable period is a usage error.
TEST(LhdOptions, SdcGeneratedClocksAreNotCreditedToTheirSource) {
  const auto path = ::testing::TempDir() + "/generated.sdc";
  std::ofstream(path) << "create_clock -name a -period 10 [get_ports clk]\n"
                      << "create_generated_clock -name g -source [get_ports clk] -divide_by 2 [get_pins x/Q]\n"
                      << "create_generated_clock -name h -master_clock a -source [get_ports {clk}] -divide_by 2 [get_ports out]\n";
  const auto clocks = lhd::sdc_clocks(path);
  ASSERT_EQ(clocks.size(), 3);
  EXPECT_EQ(clocks[0].port, "clk");
  EXPECT_TRUE(clocks[0].port_resolved);
  EXPECT_EQ(clocks[1].name, "g");
  EXPECT_FALSE(clocks[1].port_resolved);  // defined on a pin, derived from the port
  EXPECT_EQ(clocks[2].name, "h");
  EXPECT_EQ(clocks[2].port, "out");
  EXPECT_TRUE(clocks[2].port_resolved);
}

TEST(LhdOptions, SynthDelayFractionScalesTheSdcClockPeriod) {
  const auto dir = ::testing::TempDir();
  {
    std::ofstream(dir + "/frac.sdc") << "current_design top\nset clk_period 900\nset clk_name clk\n"
                                     << "create_clock -name $clk_name -period $clk_period [get_ports clk]\n"
                                     << "create_clock -name slow -period 2000 [get_ports clk2]\n";
    std::ofstream(dir + "/frac_ps.lib") << "library (x) {\n  time_unit : \"1ps\";\n}\n";
    std::ofstream(dir + "/frac_ns.lib") << "library (x) {\n  time_unit : \"1ns\";\n}\n";
    std::ofstream(dir + "/frac_ns.sdc") << "create_clock -name clk -period 0.9 [get_ports clk]\n";
    std::ofstream(dir + "/frac_bad.sdc") << "create_clock -name clk -period $no_such_period [get_ports clk]\n";
    // The spelling of the benchmark files: the port and the period through variables, a computed waveform.
    std::ofstream(dir + "/frac_tcl.sdc") << "set clk_name core\nset clk_period [expr {400 + 500}]\nset clk_port [get_ports $clk_name]\n"
                                         << "create_clock -name $clk_name -period $clk_period -waveform [list 0 [expr $clk_period / 2]] $clk_port\n";
  }
  const auto merged = [&](std::vector<std::pair<std::string, std::string>> sets) {
    lhd::Options opts;
    for (auto& [k, v] : sets) {
      opts.sets.emplace_back(std::move(k), std::move(v));
    }
    Eprp_var::Eprp_dict labels;
    lhd::merge_mapper_sets(opts, "pass.abc", labels);
    return labels;
  };
  EXPECT_FALSE(merged({}).contains("delay"));
  EXPECT_EQ(merged({
                       {    "synth.sdc",    dir + "/frac.sdc"},
                       {"synth.liberty", dir + "/frac_ps.lib"}
  })
                .at("delay"),
            "360.000");
  EXPECT_EQ(merged({
                       {     "synth.sdc", dir + "/frac.sdc"},
                       {"synth.delay",             "222"}
  })
                .at("delay"),
            "222");
  {
    // pass.usyn reads the same synth.delay spelling as pass.abc.
    lhd::Options opts;
    opts.sets = {
        {     "synth.sdc",    dir + "/frac.sdc"},
        { "synth.liberty", dir + "/frac_ps.lib"},
        {"synth.delay",                "222"}
    };
    Eprp_var::Eprp_dict labels;
    lhd::merge_mapper_sets(opts, "pass.usyn", labels);
    EXPECT_EQ(labels.at("delay"), "222");
  }
  EXPECT_EQ(merged({
                       {       "synth.sdc",    dir + "/frac.sdc"},
                       {   "synth.liberty", dir + "/frac_ps.lib"},
                       {"synth.delay_frac",                "0.4"}
  })
                .at("delay"),
            "360.000");
  EXPECT_EQ(merged({
                       {       "synth.sdc",   dir + "/frac_tcl.sdc"},
                       {   "synth.liberty",    dir + "/frac_ps.lib"},
                       {"synth.delay_frac",                   "0.5"}
  })
                .at("delay"),
            "450.000");
  {
    const auto clocks = lhd::sdc_clocks(dir + "/frac_tcl.sdc");
    ASSERT_EQ(clocks.size(), 1);
    EXPECT_EQ(clocks[0].name, "core");
    EXPECT_EQ(clocks[0].port, "core");
    EXPECT_TRUE(clocks[0].port_resolved);
    ASSERT_TRUE(clocks[0].waveform);
    EXPECT_DOUBLE_EQ((*clocks[0].waveform)[1], 450);
  }
  // A period is the number of the file, in double: the domino schedule puts 0.9 ns on a 1e-6 ps grid.
  EXPECT_EQ(lhd::sdc_clocks(dir + "/frac_ns.sdc").at(0).period.value(), 0.9);
  EXPECT_EQ(lhd::sdc_min_clock_period_ps(dir + "/frac_ns.sdc", dir + "/frac_ns.lib").value(), 0.9 * 1e3);
  // No synth.liberty: the unit comes from the $LHD_TECH_DIR default library the mapper actually reads, not from
  // an empty path (which meant ps and gave `&nf -D 0.360` for a 0.9 ns clock).
  {
    const auto tech = dir + "/tech_ns";
    std::filesystem::create_directories(tech);
    std::ofstream(tech + "/" + std::string{lhd::kSynthDefaultLiberty}) << "library (x) {\n  time_unit : \"1ns\";\n}\n";
    const char* old_tech = std::getenv("LHD_TECH_DIR");
    const std::string saved = old_tech ? old_tech : "";
    setenv("LHD_TECH_DIR", tech.c_str(), 1);
    const auto delay = merged({
        {"synth.sdc", dir + "/frac_ns.sdc"}
    }).at("delay");
    if (old_tech) {
      setenv("LHD_TECH_DIR", saved.c_str(), 1);
    } else {
      unsetenv("LHD_TECH_DIR");
    }
    EXPECT_EQ(delay, "360.000");
  }
  // The SDC unit is the Liberty unit: 0.9 ns is 900 ps.
  EXPECT_EQ(merged({
                       {       "synth.sdc", dir + "/frac_ns.sdc"},
                       {   "synth.liberty", dir + "/frac_ns.lib"},
                       {"synth.delay_frac",                "0.5"}
  })
                .at("delay"),
            "450.000");
  EXPECT_EQ(merged({
                       {       "synth.sdc",    dir + "/frac.sdc"},
                       {   "synth.liberty", dir + "/frac_ps.lib"},
                       {"synth.delay_frac",                "0.4"},
                       {     "synth.delay",                "123"}
  })
                .at("delay"),
            "123");
  EXPECT_THROW(merged({
                   {"synth.delay_frac", "0.4"}
  }),
               lhd::Lhd_error);
  EXPECT_THROW(merged({
                   {       "synth.sdc", dir + "/frac_bad.sdc"},
                   {"synth.delay_frac",                 "0.4"}
  }),
               lhd::Lhd_error);
  EXPECT_THROW(merged({
                   {       "synth.sdc", dir + "/frac.sdc"},
                   {"synth.delay_frac",              "-1"}
  }),
               lhd::Lhd_error);
}

// A placed netlist goes to CTS: no mapper clock fanout tree unless the user asks for one.
TEST(LhdOptions, SynthPlaceTurnsOffTheMapperClockTree) {
  const auto merged = [](std::vector<std::pair<std::string, std::string>> sets, std::string_view method = "pass.abc") {
    lhd::Options opts;
    for (auto& [k, v] : sets) {
      opts.sets.emplace_back(std::move(k), std::move(v));
    }
    Eprp_var::Eprp_dict labels;
    lhd::merge_mapper_sets(opts, method, labels);
    return labels;
  };
  EXPECT_FALSE(merged({}).contains("icg_fanout"));
  EXPECT_FALSE(merged({{"synth.place", "false"}}).contains("icg_fanout"));
  EXPECT_EQ(merged({{"synth.place", "true"}}).at("icg_fanout"), "0");
  EXPECT_EQ(merged({{"synth.place", "true"}, {"pass.abc.icg_fanout", "32"}}).at("icg_fanout"), "32");
  EXPECT_FALSE(merged({{"synth.place", "true"}}, "pass.usyn").contains("icg_fanout"));
}

// A placed netlist cannot carry a native memory into a physical flow: `auto` folds memories to flops there, unless
// generated SRAM macros are on or the user asks for native memories.
TEST(LhdOptions, SynthPlaceFoldsAutoMemoriesWithoutSram) {
  const auto merged = [](std::vector<std::pair<std::string, std::string>> sets) {
    lhd::Options opts;
    for (auto& [k, v] : sets) {
      opts.sets.emplace_back(std::move(k), std::move(v));
    }
    Eprp_var::Eprp_dict labels;
    lhd::merge_mapper_sets(opts, "pass.abc", labels);
    return labels;
  };
  EXPECT_EQ(merged({}).at("memory"), "auto");
  EXPECT_EQ(merged({{"synth.place", "true"}}).at("memory"), "true");
  EXPECT_EQ(merged({{"synth.place", "true"}, {"synth.memory", "auto"}}).at("memory"), "true");
  EXPECT_EQ(merged({{"synth.place", "true"}, {"synth.memory", "macro"}}).at("memory"), "false");
  EXPECT_EQ(merged({{"synth.place", "true"}, {"synth.memory", "sram"}}).at("memory"), "sram");
  // synth.memory.max_bits: one threshold, auto = 1024 (2048 under sram), an explicit value in both modes.
  EXPECT_EQ(merged({}).at("memory_max_bits"), "1024");
  EXPECT_EQ(merged({{"synth.memory", "sram"}}).at("memory_max_bits"), "2048");
  EXPECT_EQ(merged({{"synth.memory.max_bits", "4096"}}).at("memory_max_bits"), "4096");
  EXPECT_EQ(merged({{"synth.memory", "sram"}, {"synth.memory.max_bits", "0"}}).at("memory_max_bits"), "0");
  EXPECT_THROW(merged({{"synth.memory.max_bits", "lots"}}), lhd::Lhd_error);
}

// The compile schedule's two fold slots both run the e-graph engine under the compile.cprop set name.
TEST(LhdOptions, FoldSlotsRunTheEgraphEngine) {
  lhd::Options             opts;
  std::vector<std::string> methods;
  for (const auto& [set_name, method] : lhd::compile_graph_passes(opts)) {
    if (method != "pass.bitwidth" && method != "pass.enableopt") {
      EXPECT_EQ(set_name, "compile.cprop");
      methods.push_back(method);
    }
  }
  EXPECT_EQ(methods, (std::vector<std::string>{"pass.aegraph", "pass.aegraph"}));
  EXPECT_EQ(lhd::cprop_engine_descriptor(opts), "aegraph:both:cost=synth") << "the spelling compile caches were keyed with";
}

// compile.aegraph.cost: the default is `auto`, which follows the command (sim under `lhd sim`, synth elsewhere, so a bare
// `lhd compile` gets synth); an explicit value overrides it; the RESOLVED profile is what the compile-cache descriptor names, so an
// engine run under the sim profile never replays a synth-profile graph.
TEST(LhdOptions, AegraphCostProfileIsResolvedAndNamedInTheDescriptor) {
  lhd::Options opts;
  opts.command = "sim";
  EXPECT_EQ(lhd::resolve_aegraph_cost(opts), "sim") << "unset: automatic, sim under `lhd sim`";
  EXPECT_EQ(lhd::cprop_engine_descriptor(opts), "aegraph:both:cost=sim");
  opts.command = "compile";
  EXPECT_EQ(lhd::resolve_aegraph_cost(opts), "synth") << "unset: a bare compile gets synth";
  EXPECT_EQ(lhd::cprop_engine_descriptor(opts), "aegraph:both:cost=synth");
  for (const char* command : {"synth", "lec", "formal"}) {
    opts.command = command;
    EXPECT_EQ(lhd::resolve_aegraph_cost(opts), "synth") << command;
  }
  opts.command = "sim";
  opts.sets.emplace_back("compile.aegraph.cost", "synth");
  EXPECT_EQ(lhd::resolve_aegraph_cost(opts), "synth") << "an explicit value overrides the automatic choice under `lhd sim`";
  opts.sets.emplace_back("compile.aegraph.cost", "auto");
  EXPECT_EQ(lhd::resolve_aegraph_cost(opts), "sim") << "last --set wins";
  opts.command = "compile";
  opts.sets.emplace_back("compile.aegraph.cost", "sim");
  EXPECT_EQ(lhd::cprop_engine_descriptor(opts), "aegraph:both:cost=sim") << "last --set wins, any command";
}

// pass.aegraph's after_bitwidth label goes to the slot that runs after pass.bitwidth only.
TEST(LhdOptions, AfterBitwidthReachesOnlyThePostBitwidthSlot) {
  lhd::Options opts;
  const auto   passes = lhd::compile_graph_passes(opts);
  const auto   after  = lhd::follows_bitwidth(passes);
  ASSERT_EQ(after.size(), passes.size());
  std::string out;
  for (size_t i = 0; i < passes.size(); ++i) {
    if (passes[i].second == "pass.aegraph") {
      out += after[i] ? "A" : "a";
    }
  }
  EXPECT_EQ(out, "aA");
}

TEST(LhdOptions, FoldOptionsAreValidatedEagerly) {
  const auto check = [](const std::string& key, const std::string& value) {
    std::vector<std::string> args{"lhd", "compile", "unused.prp", "--set", key + "=" + value};
    std::vector<char*>       argv;
    for (auto& arg : args) {
      argv.push_back(arg.data());
    }
    const auto opts = lhd::parse_args(static_cast<int>(argv.size()), argv.data());
    lhd::check_known_set_passes(opts);
  };
  for (const auto* ok : {"synth", "sim", "auto"}) {
    EXPECT_NO_THROW(check("compile.aegraph.cost", ok)) << ok;
  }
  for (const auto* bad : {"fast", "none", "Sim", ""}) {
    EXPECT_THROW(check("compile.aegraph.cost", bad), lhd::Lhd_error) << bad;
  }
  // The classic engine and its selectors are gone: a --set of one says so.
  for (const auto& [key, value] : std::vector<std::pair<std::string, std::string>>{{"compile.cprop.engine", "classic"},
                                                                                  {"compile.cprop.engine", "aegraph"},
                                                                                  {"compile.cprop.engine_slots", "both"},
                                                                                  {"formal.lec.preflight_engine", "aegraph"}}) {
    try {
      check(key, value);
      FAIL() << key << " accepted";
    } catch (const lhd::Lhd_error& error) {
      EXPECT_NE(error.msg.find("was removed"), std::string::npos) << key << ": " << error.msg;
    }
  }
  // Low-lane narrowing and the loop hoist always run (before and after pass.bitwidth): their switches are gone
  // under either spelling. The rule runtime is compile.aegraph.<flag>.
  EXPECT_NO_THROW(check("compile.aegraph.rules_disable", "not_not"));
  for (const auto* key : {"compile.cprop.low_lanes", "compile.cprop.loop_hoist", "compile.aegraph.low_lanes",
                          "compile.aegraph.loop_hoist"}) {
    try {
      check(key, "false");
      FAIL() << key << " accepted";
    } catch (const lhd::Lhd_error& error) {
      EXPECT_NE(error.msg.find("was removed"), std::string::npos) << key << ": " << error.msg;
    }
  }
  try {
    check("compile.cprop.rules_disable", "not_not");
    FAIL() << "rules_disable accepted under compile.cprop";
  } catch (const lhd::Lhd_error& error) {
    EXPECT_NE(error.hint.find("compile.aegraph.rules_disable"), std::string::npos) << error.hint;
  }
  // after_bitwidth is kernel bookkeeping: not settable by a user.
  EXPECT_THROW(check("compile.aegraph.after_bitwidth", "true"), lhd::Lhd_error);
  const auto listed = lhd::list_set_options();
  const auto has    = [&](std::string_view name) {
    return std::any_of(listed.begin(), listed.end(), [&](const auto& option) { return option.name == name; });
  };
  EXPECT_FALSE(has("compile.cprop.low_lanes"));
  EXPECT_FALSE(has("compile.cprop.loop_hoist"));
  EXPECT_TRUE(has("compile.aegraph.rules_disable"));
  EXPECT_FALSE(has("compile.cprop.engine"));
  EXPECT_FALSE(has("compile.cprop.engine_slots"));
  EXPECT_FALSE(has("compile.cprop.rules_disable")) << "listed once, under compile.aegraph";
  EXPECT_FALSE(has("compile.aegraph.low_lanes"));
  EXPECT_FALSE(has("compile.aegraph.after_bitwidth"));
  EXPECT_FALSE(has("formal.lec.preflight_engine"));
}

// The classic engine's method is gone.
TEST(LhdOptions, PassCpropIsGone) {
  (void)lhd::list_set_options();  // registers every pass method
  EXPECT_EQ(Pass::eprp.get_method("pass.cprop"), nullptr);
  EXPECT_NE(Pass::eprp.get_method("pass.aegraph"), nullptr);
}

TEST(LhdOptions, RecommendedSharedDefaults) {
  const auto listed = lhd::list_set_options();
  for (const auto& [key, value] : std::vector<std::pair<std::string, std::string>>{
           {                "compile.specialize_modules",   "true"},
           {                          "synth.area_relax",    "100"},
           {                            "synth.boundary",  "false"},
           {                       "synth.max_fanout",     "16"},
           {                          "synth.delay_frac",    "0.4"},
           {                           "synth.partition", "coarse"},
           {         "synth.place.pcrs.reset_pipeline",   "true"},
           {                         "synth.place.pcrs",   "true"},
           {                       "synth.place.growth",   "0.05"},
           {         "synth.place.growth.sizing_share",    "0.3"},
           {         "synth.place.repair.iterations",    "3"},
           {            "synth.place.domino_rail_fanout",     "32"},
           {                        "synth.place.resize",   "true"},
           {                "synth.place.buffer_cleanup",  "false"},
           {                          "synth.place.halo",   "auto"},
           {                 "synth.place.fp.pin_adjust",   "true"},
           {              "synth.place.macro.pin_adjust",   "true"},
           {               "synth.place.macro.edge_snap",    "off"},
           {              "synth.place.macro.dead_space",    "off"},
           {           "synth.place.macro.refine_rounds",      "0"},
           {                  "synth.place.macro.anchor",  "false"},
           {   "synth.place.macro.anchor.start_overflow",    "0.3"},
           {            "synth.place.macro.anchor.swaps",  "false"},
           {            "synth.place.final.timing_start",    "200"},
           {         "synth.place.final.timing_schedule",  "fixed"},
           {                   "synth.place.route.model",   "rsmt"},
           {                  "synth.place.rc_pessimism",      "1"},
           {  "synth.place.legalize.critical_pair_limit",    "256"},
           {     "synth.place.legalize.local_pair_limit",      "0"},
           { "synth.place.legalize.joint_minimize_total",   "true"},
           {"synth.place.legalize.remainder_wire_weight",      "4"},
           {                         "synth.state_sweep",   "true"},
           {                          "synth.flat_sweep",  "false"},
           {                     "synth.merge_registers",   "true"},
           {                    "synth.place.io_resynth",      "5"}
  }) {
    const auto option = std::find_if(listed.begin(), listed.end(), [&](const auto& o) { return o.name == key; });
    ASSERT_NE(option, listed.end()) << key;
    EXPECT_EQ(option->default_value, value) << key;
  }
}

// Q19: an automatic value is spelled auto, never a numeric sentinel.
TEST(LhdOptions, AutomaticValuesAreSpelledAuto) {
  const auto listed = lhd::list_set_options();
  for (const auto* key : {"synth.place.explore.memory_mb", "synth.place.explore.session_memory_mb", "synth.place.explore.timeout.t1",
                          "synth.place.fp.color_minimum", "synth.place.macro.filler_fraction", "synth.place.local_density_target",
                          "synth.place.region.boundarypins.pitch_um", "synth.place.preroute.hold_margin", "synth.block_size"}) {
    const auto option = std::find_if(listed.begin(), listed.end(), [&](const auto& o) { return o.name == key; });
    ASSERT_NE(option, listed.end()) << key;
    EXPECT_EQ(option->default_value, "auto") << key;
  }
}

TEST(LhdOptions, WarpspaceIsRetired) {
  for (const auto* flag : {"place.fp.warpspace", "place.macro.warpspace.updates", "place.legalize.warpspace.bins"}) {
    EXPECT_FALSE(lhd::retired_set_hint("synth", flag).empty()) << flag;
  }
}

// legroute is opt-in and its option table is the minimal core's (todo/livehd/2r-route.html, step C): the options of
// the removed SAT stages are unknown keys.
TEST(LhdOptions, LegrouteOptionsAreTheMinimalCoreAndOptIn) {
  const auto   listed = lhd::list_set_options();
  lhd::Options opts;
  opts.command = "synth";
  const std::vector<std::pair<std::string, std::string>> expected{
      {                        "synth.place.legroute",  "false"},
      {            "synth.place.legroute.plan.rounds",      "2"},
      {          "synth.place.legroute.area_restarts",      "1"},
      {        "synth.place.legroute.plan.iterations",    "100"},
      {    "synth.place.legroute.plan.min_iterations",      "0"},
      {   "synth.place.legroute.plan.timing_interval",      "0"},
      {       "synth.place.legroute.plan.route_model", "legacy"},
      {    "synth.place.legroute.plan.route_interval",     "50"},
      {    "synth.place.legroute.plan.route_net_pops",   "2000"},
      {"synth.place.legroute.plan.max_degradation_ps",    "off"},
      {       "synth.place.legroute.plan.local_tiles",      "0"},
      {          "synth.place.legroute.plan.net_pops", "100000"},
      {       "synth.place.legroute.plan.max_inflate",    "1.5"},
      {         "synth.place.legroute.capacity_share",      "1"},
      {          "synth.place.legroute.global_margin",      "8"},
      {          "synth.place.legroute.global_rounds",     "16"},
      {         "synth.place.legroute.global_pattern",  "false"},
      {    "synth.place.legroute.global_astar_weight",      "1"},
  };
  size_t count = 0;
  for (const auto& option : listed) {
    count += option.name.starts_with("synth.place.legroute");
  }
  EXPECT_EQ(count, expected.size());
  for (const auto& [key, value] : expected) {
    SCOPED_TRACE(key);
    const auto found = std::find_if(listed.begin(), listed.end(), [&](const auto& option) { return option.name == key; });
    ASSERT_NE(found, listed.end());
    EXPECT_EQ(found->default_value, value);
    opts.sets = {
        {key, value}
    };
    EXPECT_NO_THROW(lhd::check_known_set_passes(opts));
  }
  for (const auto* key : {"synth.place.legroute", "synth.place.legroute.global_pattern"}) {
    SCOPED_TRACE(key);
    for (const auto* bad : {"-1", "1.5", "invalid"}) {
      opts.sets = {
          {key, bad}
      };
      EXPECT_THROW(lhd::check_known_set_passes(opts), lhd::Lhd_error);
    }
    opts.sets = {
        {key, "true"}
    };
    EXPECT_NO_THROW(lhd::check_known_set_passes(opts));
  }
  for (const auto* key : {"synth.place.legroute.plan.rounds", "synth.place.legroute.global_rounds"}) {
    SCOPED_TRACE(key);
    for (const auto* bad : {"1.5", "invalid"}) {
      opts.sets = {
          {key, bad}
      };
      EXPECT_THROW(lhd::check_known_set_passes(opts), lhd::Lhd_error);
    }
  }
  for (const auto* removed : {"synth.place.legroute.plan",
                              "synth.place.legroute.final",
                              "synth.place.legroute.legal_boxes",
                              "synth.place.legroute.sat_ms",
                              "synth.place.legroute.partition_cells",
                              "synth.place.legroute.critical_cellboxes",
                              "synth.place.legroute.probe",
                              "synth.place.legroute.box_reserve",
                              "synth.place.legroute.reserved_tracks",
                              "synth.place.legroute.fit_density",
                              "synth.place.legroute.global_share",
                              "synth.place.legroute.density_ceiling",
                              "synth.place.legroute.strict"}) {
    SCOPED_TRACE(removed);
    EXPECT_TRUE(std::none_of(listed.begin(), listed.end(), [&](const auto& option) { return option.name == removed; }));
    opts.sets = {
        {removed, "0"}
    };
    EXPECT_THROW(lhd::check_known_set_passes(opts), lhd::Lhd_error);
  }
}

// Macro-placement refinements and area sizing remain opt-in. Sub-options keep their values for explicitly enabled features.
TEST(LhdOptions, MacroPlacementRefinementsRemainOptIn) {
  const auto listed = lhd::list_set_options();
  for (const auto& [key, value] : std::vector<std::pair<std::string, std::string>>{
           {                    "synth.place.macro.channel",        "auto"},
           {                        "synth.place.macro.pdn",        "auto"},
           {                 "synth.place.macro.pdn_origin",        "core"},
           {                               "synth.place.fp",        "auto"},
           {                    "synth.place.fp.auto_share",         "0.2"},
           {               "synth.place.fp.auto_min_copies",           "2"},
           {           "synth.place.fp.auto_min_copy_share",        "0.02"},
           {               "synth.place.fp.auto_candidates",        "true"},
           {                "synth.place.fp.auto_tolerance",        "0.05"},
           {                  "synth.place.macro.edge_snap",         "off"},
           {                 "synth.place.macro.dead_space",         "off"},
           {              "synth.place.macro.refine_rounds",           "0"},
           {          "synth.place.macro.refine_iterations",          "30"},
           {                     "synth.place.macro.anchor",       "false"},
           {              "synth.place.macro.anchor.select", "edge+random"},
           {         "synth.place.macro.anchor.edge_weight",           "4"},
           {       "synth.place.macro.anchor.random_weight",           "1"},
           {     "synth.place.macro.anchor.random_fraction",        "0.25"},
           {          "synth.place.macro.anchor.edge_chain",        "true"},
           {           "synth.place.macro.anchor.edge_gate",       "false"},
           {                "synth.place.macro.anchor.gate",           "1"},
           {       "synth.place.macro.anchor.spring_length",         "0.1"},
           {   "synth.place.macro.anchor.target_hysteresis",         "0.5"},
           {      "synth.place.macro.anchor.start_overflow",         "0.3"},
           {       "synth.place.macro.anchor.final_targets",        "true"},
           {               "synth.place.macro.anchor.swaps",       "false"},
           {           "synth.place.macro.anchor.swap_jump",        "true"},
           {    "synth.place.macro.anchor.refresh_interval",          "30"},
           {                "synth.place.macro.anchor.ramp",           "3"},
           {              "synth.place.macro.anchor.settle",          "20"},
           {       "synth.place.macro.anchor.max_refreshes",          "40"},
           {          "synth.place.macro.anchor.milestones",    "0.15,0.1"},
           {"synth.place.macro.anchor.dead_space_refreshes",           "2"},
           {                             "synth.place.area",       "fixed"},
           {                             "synth.place.halo",        "auto"},
  }) {
    const auto option = std::find_if(listed.begin(), listed.end(), [&](const auto& o) { return o.name == key; });
    ASSERT_NE(option, listed.end()) << key;
    EXPECT_EQ(option->default_value, value) << key;
  }
  // Explicit off values and the opt-in combination both remain accepted.
  lhd::Options opts;
  opts.command = "synth";
  opts.sets    = {
      {    "synth.place.macro.edge_snap",   "off"},
      {   "synth.place.macro.dead_space",   "off"},
      {"synth.place.macro.refine_rounds",     "0"},
      {       "synth.place.macro.anchor", "false"},
  };
  EXPECT_NO_THROW(lhd::check_known_set_passes(opts));
  opts.sets = {
      {    "synth.place.macro.edge_snap", "auto"},
      {   "synth.place.macro.dead_space", "auto"},
      {"synth.place.macro.refine_rounds",    "1"},
      {       "synth.place.macro.anchor", "true"},
  };
  EXPECT_NO_THROW(lhd::check_known_set_passes(opts));
}

TEST(LhdOptions, MacroAnchorRefreshesAcceptTheDocumentedAlwaysSentinel) {
  lhd::Options opts;
  opts.command = "synth";
  for (const auto* text : {"-1", "0", "2", "1000000"}) {
    SCOPED_TRACE(text);
    opts.sets = {
        {"synth.place.macro.anchor.dead_space_refreshes", text}
    };
    EXPECT_NO_THROW(lhd::check_known_set_passes(opts));
    EXPECT_EQ(lhd::parse_macro_anchor_dead_space_refreshes(text), std::stoi(text));
  }
  for (const auto* text : {"-2", "1000001", "2147483648", "", "1.5", "2junk"}) {
    SCOPED_TRACE(text);
    EXPECT_THROW(lhd::parse_macro_anchor_dead_space_refreshes(text), lhd::Lhd_error);
  }
}

TEST(LhdOptions, MacroAnchorMilestonesRequireCompleteFiniteTokens) {
  EXPECT_EQ(lhd::parse_macro_anchor_milestones("0.15,0.1"), (std::vector<double>{.15, .1}));
  EXPECT_EQ(lhd::parse_macro_anchor_milestones("1e-1,0"), (std::vector<double>{.1, 0}));
  EXPECT_TRUE(lhd::parse_macro_anchor_milestones("").empty());
  for (const auto* text : {"0.15junk,0.1", "0.15,0.1junk", "0.15,,0.1", ",0.1", "0.15,", "nan", "inf", "1e999"}) {
    SCOPED_TRACE(text);
    EXPECT_THROW(lhd::parse_macro_anchor_milestones(text), lhd::Lhd_error);
  }
}

// The pre-3g placement partition spellings are retired: each one is refused with a hint naming its new key, and
// none of them is listed.
TEST(LhdOptions, RetiredPartitionSpellingsAreRefused) {
  const auto listed = lhd::list_set_options();
  for (const auto& r : lhd::kSynthSetRenames) {
    SCOPED_TRACE(std::string(r.old_name));
    EXPECT_FALSE(std::any_of(listed.begin(), listed.end(), [&](const auto& o) { return o.name == "synth." + std::string(r.old_name); }));
    EXPECT_EQ(lhd::retired_set_hint("synth", r.old_name), r.hint);
  }
  EXPECT_FALSE(lhd::retired_set_hint("synth", "place.mesh_variants").empty());
}

TEST(LhdOptions, PositionalMetadataIsNotSourceAndMergesByClass) {
  const auto file = testing::TempDir() + "/input-metadata.json";
  {
    std::ofstream out(file);
    out << R"([{"class":"foreign","id":"x"}])";
  }
  std::vector<std::string> args{"lhd", "synth", file, "top.v", file, "--top", "top"};
  std::vector<char*>       argv;
  for (auto& a : args) {
    argv.push_back(a.data());
  }
  auto opts = lhd::parse_args(argv.size(), argv.data());
  lhd::load_metadata(opts);
  EXPECT_EQ(opts.files, std::vector<std::string>{"top.v"});
  EXPECT_EQ(opts.language, "verilog");
  ASSERT_TRUE(opts.metadata);
  EXPECT_EQ(opts.metadata->select("foreign", "top").size(), 1);
  auto combined = opts;
  combined.files.clear();
  combined.metadata_files.clear();
  combined.metadata = std::make_shared<const livehd::metadata::Bundle>(R"([{"id":"x","class":"foreign"}])");
  auto split        = opts;
  split.files.clear();
  EXPECT_EQ(lhd::compute_run_id(combined), lhd::compute_run_id(split));
  combined.metadata = std::make_shared<const livehd::metadata::Bundle>(R"([{"class":"hint","x":929.1233919228121}])");
  split.metadata = std::make_shared<const livehd::metadata::Bundle>(R"([{"class":"hint","x":929.123391922812}])");
  EXPECT_NE(lhd::compute_run_id(combined), lhd::compute_run_id(split));

  for (const auto* key : {"synth.metadata", "synth.tune.file", "sim.tune.file", "synth.place.row_access"}) {
    args = {"lhd", "synth", "top.v", "--set", std::string(key) + "=" + file};
    argv.clear();
    for (auto& a : args) {
      argv.push_back(a.data());
    }
    auto removed = lhd::parse_args(argv.size(), argv.data());
    EXPECT_THROW(lhd::load_metadata(removed), lhd::Lhd_error);
  }
  const auto listed = lhd::list_set_options();
  for (const auto* method : {"pass.abc", "pass.usyn"}) {
    for (const auto* flag : {"tune_file", "tune_metadata", "tune_design"}) {
      const auto key = std::string(method) + "." + flag;
      EXPECT_TRUE(std::none_of(listed.begin(), listed.end(), [&](const auto& option) { return option.name == key; }));
      lhd::Options internal;
      internal.command = "synth";
      internal.sets.emplace_back(key, "x");
      EXPECT_THROW(lhd::check_known_set_passes(internal), lhd::Lhd_error);
    }
  }
  std::remove(file.c_str());
}

TEST(LhdOptions, DebugAcceptsBareRepeatedAndExplicitSelectors) {
  const auto parse = [](std::vector<std::string> args) {
    std::vector<char*> argv;
    for (auto& arg : args) {
      argv.push_back(arg.data());
    }
    return lhd::parse_args(static_cast<int>(argv.size()), argv.data());
  };
  EXPECT_TRUE(parse({"lhd", "compile", "input.v"}).debug.empty());
  EXPECT_EQ(parse({"lhd", "compile", "input.v", "-d"}).debug, "all");
  const auto before_option = parse({"lhd", "compile", "input.v", "-d", "--stats"});
  EXPECT_EQ(before_option.debug, "all");
  EXPECT_EQ(parse({"lhd", "compile", "input.v", "--debug"}).debug, "all");
  EXPECT_EQ(parse({"lhd", "compile", "input.v", "--debug="}).debug, "all");
  EXPECT_EQ(parse({"lhd", "compile", "input.v", "--debug=aegraph-rule"}).debug, "aegraph-rule");
  EXPECT_EQ(parse({"lhd", "compile", "input.v", "-d", "aegraph-rule", "-d", "pass.abc"}).debug,
            "aegraph-rule,pass.abc");
  const auto positional = parse({"lhd", "compile", "-d", "aegraph-rule", "input.v"});
  EXPECT_EQ(positional.debug, "aegraph-rule");
  ASSERT_EQ(positional.files.size(), 1u);
  EXPECT_EQ(positional.files.front(), "input.v");
}
