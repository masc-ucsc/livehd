// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <algorithm>
#include <fstream>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include "core/metadata.hpp"
#include "gtest/gtest.h"
#include "lhd.hpp"
#include "lhd_kernel_internal.hpp"
#include "pass.hpp"
#include "synth_policy.hpp"

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
    std::ofstream(dir + "/frac_bad.sdc") << "create_clock -name clk -period [expr {1 + 2}] [get_ports clk]\n";
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
                       {"pass.abc.delay",             "222"}
  })
                .at("delay"),
            "222");
  {
    lhd::Options opts;
    opts.sets = {
        {     "synth.sdc",    dir + "/frac.sdc"},
        { "synth.liberty", dir + "/frac_ps.lib"},
        {"pass.abc.delay",                "222"}
    };
    Eprp_var::Eprp_dict labels;
    lhd::merge_mapper_sets(opts, "pass.usyn", labels);
    EXPECT_EQ(labels.at("delay"), "360.000");
  }
  EXPECT_EQ(merged({
                       {       "synth.sdc",    dir + "/frac.sdc"},
                       {   "synth.liberty", dir + "/frac_ps.lib"},
                       {"synth.delay_frac",                "0.4"}
  })
                .at("delay"),
            "360.000");
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

// compile.cprop.engine: the kernel dispatches the cprop slots. At least one
// engine always runs (there is no "none"), classic stays the default, and
// aegraph+classic runs the e-graph engine then classic in each slot.
TEST(LhdOptions, CpropEngineDispatch) {
  const auto methods = [](const std::vector<std::pair<std::string, std::string>>& sets) {
    lhd::Options opts;
    for (const auto& [k, v] : sets) {
      opts.sets.emplace_back(k, v);
    }
    std::vector<std::string> out;
    for (const auto& [set_name, method] : lhd::compile_graph_passes(opts)) {
      if (method != "pass.bitwidth" && method != "pass.enableopt") {
        EXPECT_EQ(set_name, "compile.cprop") << "every engine shares the compile.cprop set";
        out.push_back(method);
      }
    }
    return out;
  };
  using V = std::vector<std::string>;
  EXPECT_EQ(methods({}), (V{"pass.aegraph", "pass.aegraph"})) << "the e-graph engine is the default";
  EXPECT_EQ(methods({
                {"compile.cprop.engine", "classic"}
  }),
            (V{"pass.cprop", "pass.cprop"}));
  EXPECT_EQ(methods({
                {"compile.cprop.engine", "aegraph"}
  }),
            (V{"pass.aegraph", "pass.aegraph"}));
  EXPECT_EQ(methods({
                {"compile.cprop.engine", "aegraph+classic"}
  }),
            (V{"pass.aegraph", "pass.cprop", "pass.aegraph", "pass.cprop"}));
  EXPECT_EQ(methods({
                {      "compile.cprop.engine", "aegraph"},
                {"compile.cprop.engine_slots",   "first"}
  }),
            (V{"pass.aegraph", "pass.cprop"}));
  EXPECT_EQ(methods({
                {      "compile.cprop.engine", "aegraph"},
                {"compile.cprop.engine_slots",  "second"}
  }),
            (V{"pass.cprop", "pass.aegraph"}));
  EXPECT_EQ(methods({
                {      "compile.cprop.engine", "aegraph+classic"},
                {"compile.cprop.engine_slots",          "second"}
  }),
            (V{"pass.cprop", "pass.aegraph", "pass.cprop"}));
  // The last --set wins, like every other pass flag.
  EXPECT_EQ(methods({
                {"compile.cprop.engine", "aegraph"},
                {"compile.cprop.engine", "classic"}
  }),
            (V{"pass.cprop", "pass.cprop"}));

  lhd::Options opts;
  EXPECT_EQ(lhd::cprop_engine_descriptor(opts), "aegraph:both:cost=synth") << "aegraph is the default engine";
  opts.sets.emplace_back("compile.cprop.engine", "classic");
  EXPECT_EQ(lhd::cprop_engine_descriptor(opts), "classic");
  opts.sets.emplace_back("compile.cprop.engine", "aegraph+classic");
  opts.sets.emplace_back("compile.cprop.engine_slots", "first");
  EXPECT_EQ(lhd::cprop_engine_descriptor(opts), "aegraph+classic:first:cost=synth");
}

// compile.aegraph.cost: the default is `auto`, which follows the command (sim under `lhd sim`, synth elsewhere, so a bare
// `lhd compile` gets synth); an explicit value overrides it; the RESOLVED profile is what the compile-cache descriptor names, so an
// engine run under the sim profile never replays a synth-profile graph.
TEST(LhdOptions, AegraphCostProfileIsResolvedAndNamedInTheDescriptor) {
  lhd::Options opts;
  opts.command = "sim";
  opts.sets.emplace_back("compile.cprop.engine", "aegraph");
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
  lhd::Options classic;
  classic.sets.emplace_back("compile.cprop.engine", "classic");
  classic.sets.emplace_back("compile.aegraph.cost", "sim");
  EXPECT_EQ(lhd::cprop_engine_descriptor(classic), "classic") << "the profile only matters when an aegraph step runs";
}

// pass.aegraph's after_bitwidth label goes to the slot that runs after
// pass.bitwidth only: the first slot (and any pass.cprop) never gets it.
TEST(LhdOptions, AfterBitwidthReachesOnlyThePostBitwidthSlot) {
  const auto slots = [](const std::string& engine) {
    lhd::Options opts;
    opts.sets.emplace_back("compile.cprop.engine", engine);
    const auto passes = lhd::compile_graph_passes(opts);
    const auto after  = lhd::follows_bitwidth(passes);
    EXPECT_EQ(after.size(), passes.size());
    std::string out;
    for (size_t i = 0; i < passes.size(); ++i) {
      if (passes[i].second == "pass.aegraph") {
        out += after[i] ? "A" : "a";
      }
    }
    return out;
  };
  EXPECT_EQ(slots("aegraph"), "aA");
  EXPECT_EQ(slots("aegraph+classic"), "aA");
  EXPECT_EQ(slots("classic"), "");
}

TEST(LhdOptions, CpropEngineValuesAreValidatedEagerly) {
  const auto check = [](const std::string& key, const std::string& value) {
    std::vector<std::string> args{"lhd", "compile", "unused.prp", "--set", key + "=" + value};
    std::vector<char*>       argv;
    for (auto& arg : args) {
      argv.push_back(arg.data());
    }
    const auto opts = lhd::parse_args(static_cast<int>(argv.size()), argv.data());
    lhd::check_known_set_passes(opts);
  };
  for (const auto* ok : {"classic", "aegraph", "aegraph+classic"}) {
    EXPECT_NO_THROW(check("compile.cprop.engine", ok)) << ok;
  }
  for (const auto* ok : {"both", "first", "second"}) {
    EXPECT_NO_THROW(check("compile.cprop.engine_slots", ok)) << ok;
  }
  for (const auto* ok : {"synth", "sim", "auto"}) {
    EXPECT_NO_THROW(check("compile.aegraph.cost", ok)) << ok;
  }
  for (const auto* bad : {"fast", "none", "Sim", ""}) {
    EXPECT_THROW(check("compile.aegraph.cost", bad), lhd::Lhd_error) << bad;
  }
  // No "none": a compile without an engine would silently skip constant propagation.
  for (const auto* bad : {"none", "off", "Classic", "aegraph,classic", ""}) {
    EXPECT_THROW(check("compile.cprop.engine", bad), lhd::Lhd_error) << bad;
  }
  EXPECT_THROW(check("compile.cprop.engine_slots", "third"), lhd::Lhd_error);
  // One spelling per shared flag.
  for (const auto* shared : {"low_lanes", "loop_hoist", "engine", "engine_slots"}) {
    try {
      check(std::string{"compile.aegraph."} + shared, "true");
      FAIL() << shared << " accepted under compile.aegraph";
    } catch (const lhd::Lhd_error& error) {
      EXPECT_NE(error.hint.find(std::string{"compile.cprop."} + shared), std::string::npos) << error.hint;
    }
  }
  // after_bitwidth is kernel bookkeeping: not settable by a user.
  EXPECT_THROW(check("compile.aegraph.after_bitwidth", "true"), lhd::Lhd_error);
  const auto listed = lhd::list_set_options();
  const auto has    = [&](std::string_view name) {
    return std::any_of(listed.begin(), listed.end(), [&](const auto& option) { return option.name == name; });
  };
  EXPECT_TRUE(has("compile.cprop.engine"));
  EXPECT_TRUE(has("compile.cprop.engine_slots"));
  EXPECT_FALSE(has("compile.aegraph.engine")) << "INTERNAL shared labels are not listed";
  EXPECT_FALSE(has("compile.aegraph.low_lanes"));
  EXPECT_FALSE(has("compile.aegraph.loop_hoist"));
  EXPECT_FALSE(has("compile.aegraph.after_bitwidth"));
  // The LEC preflight engine switch is INTERNAL too: settable, never listed.
  EXPECT_FALSE(has("formal.lec.preflight_engine"));
}

// The kernel does the engine dispatch, so a non-classic engine reaching
// pass.cprop means a caller bypassed it: refuse rather than run classic and make
// an A/B comparison compare nothing.
TEST(LhdOptions, PassCpropRefusesNonClassicEngine) {
  (void)lhd::list_set_options();  // registers every pass method
  for (const char* engine : {"aegraph", "aegraph+classic", "none"}) {
    Eprp_var var;
    EXPECT_THROW(Pass::eprp.run_method_now("pass.cprop",
                                           var,
                                           {
                                               {"engine", engine}
    }),
                 std::exception)
        << engine;
  }
  Eprp_var var;
  EXPECT_NO_THROW(Pass::eprp.run_method_now("pass.cprop",
                                            var,
                                            {
                                                {"engine", "classic"}
  }));
  EXPECT_NO_THROW(Pass::eprp.run_method_now("pass.cprop", var, {}));
}

TEST(LhdOptions, RecommendedSharedDefaults) {
  const auto listed = lhd::list_set_options();
  for (const auto& [key, value] : std::vector<std::pair<std::string, std::string>>{
           {                "compile.specialize_modules",   "true"},
           {                       "pass.abc.area_relax",      "0"},
           {                         "pass.abc.boundary",  "false"},
           {                       "pass.abc.max_fanout",     "32"},
           {                          "synth.delay_frac",    "0.4"},
           {                           "synth.partition", "coarse"},
           {                  "synth.place.reset_fanout",     "32"},
           {            "synth.place.domino_rail_fanout",     "32"},
           {                        "synth.place.resize",   "true"},
           {                "synth.place.buffer_cleanup",  "false"},
           {                          "synth.place.halo",   "auto"},
           {                 "synth.place.fp.pin_adjust",   "true"},
           {              "synth.place.macro.pin_adjust",   "true"},
           {            "synth.place.final.timing_start",    "200"},
           {         "synth.place.final.timing_schedule",  "fixed"},
           {                     "synth.place.resize.gp",   "true"},
           {              "synth.place.resize.gp_margin",   "0.01"},
           {              "synth.place.resize.gp_trials",      "4"},
           {                   "synth.place.route.model",   "rsmt"},
           {                  "synth.place.rc_pessimism",      "1"},
           {  "synth.place.legalize.critical_pair_limit",    "256"},
           {     "synth.place.legalize.local_pair_limit",      "0"},
           { "synth.place.legalize.joint_minimize_total",   "true"},
           {"synth.place.legalize.remainder_wire_weight",      "4"},
           {                         "synth.state_sweep",   "true"},
           {                     "synth.merge_registers",   "true"},
           {                    "synth.place.io_resynth",      "5"}
  }) {
    const auto option = std::find_if(listed.begin(), listed.end(), [&](const auto& o) { return o.name == key; });
    ASSERT_NE(option, listed.end()) << key;
    EXPECT_EQ(option->default_value, value) << key;
  }
}

TEST(LhdOptions, WarpspaceStagesAreIndependentAndOptIn) {
  const auto options = lhd::list_set_options();
  for (const auto& stage : {std::string("fp"), std::string("macro")}) {
    const auto name  = "synth.place." + stage + ".warpspace";
    const auto found = std::find_if(options.begin(), options.end(), [&](const auto& option) { return option.name == name; });
    ASSERT_NE(found, options.end());
    EXPECT_EQ(found->default_value, "false");
  }
}

// 3g-partitions: the old grid/region/bin spellings stay accepted, rewrite to
// the canonical names before anything reads them, and stay out of the listing.
TEST(LhdOptions, DeprecatedPartitionSpellingsCanonicalize) {
  const auto listed = lhd::list_set_options();
  const auto in     = [&](const std::string& name) {
    return std::any_of(listed.begin(), listed.end(), [&](const auto& option) { return option.name == name; });
  };
  lhd::Options opts;
  opts.command = "synth";
  for (const auto& r : lhd::kSynthSetRenames) {
    SCOPED_TRACE(std::string(r.old_name));
    EXPECT_FALSE(in("synth." + std::string(r.old_name)));
    EXPECT_TRUE(in("synth." + std::string(r.new_name)));
    const auto spec = std::find_if(std::begin(lhd::kSynthSetOptions), std::end(lhd::kSynthSetOptions), [&](const auto& o) {
      return o.name == r.old_name;
    });
    ASSERT_NE(spec, std::end(lhd::kSynthSetOptions));
    EXPECT_TRUE(std::string_view{spec->help}.starts_with("DEPRECATED"));
    const bool boolean = spec->kind == lhd::Synth_set_option::Kind::boolean;
    opts.sets.emplace_back("synth." + std::string(r.old_name), boolean ? "true" : "7");
  }
  EXPECT_NO_THROW(lhd::check_known_set_passes(opts));
  lhd::canonicalize_synth_sets(opts);
  const auto value = [&](std::string_view key) {
    std::vector<std::string> values;
    for (const auto& [k, v] : opts.sets) {
      if (k == key) {
        values.push_back(v);
      }
    }
    return values;
  };
  EXPECT_EQ(value("synth.place.grid.density"), std::vector<std::string>{"7"});
  EXPECT_EQ(value("synth.place.region.max_cells"), std::vector<std::string>{"7"});
  EXPECT_EQ(value("synth.place.satlegalize.bin.grid"), std::vector<std::string>{"route"});
  EXPECT_TRUE(value("synth.place.bins").empty());
  // Both density booleans true is a combination grid.density_mode cannot
  // spell: they stay for the placement driver to read.
  EXPECT_EQ(value("synth.place.dynamic_bins"), std::vector<std::string>{"true"});
  EXPECT_EQ(value("synth.place.auto_bins"), std::vector<std::string>{"true"});
  EXPECT_EQ(opts.deprecated_sets.size(), std::size(lhd::kSynthSetRenames));
  EXPECT_NO_THROW(lhd::check_known_set_passes(opts));
  // One density boolean merges into the mode, so both spellings share a cache key.
  for (const auto& [sets, mode] : std::vector<std::pair<std::vector<std::pair<std::string, std::string>>, std::string>>{
           {                                    {{"synth.place.dynamic_bins", "true"}}, "dynamic"},
           {{{"synth.place.auto_bins", "true"}, {"synth.place.dynamic_bins", "false"}},   "macro"},
           {                                   {{"synth.place.dynamic_bins", "false"}},   "fixed"},
  }) {
    opts.sets = sets;
    lhd::canonicalize_synth_sets(opts);
    EXPECT_EQ(opts.sets,
              (std::vector<std::pair<std::string, std::string>>{
                  {"synth.place.grid.density_mode", mode}
    }));
  }
  // An explicit mode competes: the booleans stay so the driver rejects the mix.
  opts.sets = {
      {"synth.place.grid.density_mode", "fixed"},
      {     "synth.place.dynamic_bins",  "true"}
  };
  lhd::canonicalize_synth_sets(opts);
  EXPECT_EQ(value("synth.place.dynamic_bins"), std::vector<std::string>{"true"});
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
