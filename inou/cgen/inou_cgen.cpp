//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_cgen.hpp"

#include <charconv>
#include <map>

#include "cgen_sim.hpp"
#include "cgen_verilog.hpp"
#include "diag.hpp"  // livehd::diag::err — flag-value validation
#include "file_utils.hpp"
#include "node_util.hpp"
#include "perf_tracing.hpp"
#include "sim_color_plan.hpp"
#include "split_selfref.hpp"

static Pass_plugin sample("inou_cgen", Inou_cgen::setup);

Inou_cgen::Inou_cgen(const Eprp_var& var) : Pass("inou.cgen", var) {
  auto v  = var.get("verbose");
  verbose = v != "false" && v != "0";
  auto m  = var.get("srcmap");
  srcmap  = m == "true" || m == "1";
}

void Inou_cgen::setup() {
  Eprp_method m1("inou.cgen.verilog", "export verilog from an Lgraph", &Inou_cgen::to_cgen_verilog);

  m1.add_label_optional("verbose", "dump bits and wirename (true/false)", "false");
  m1.add_label_optional("srcmap", "emit an ECMA-426 source-map sidecar (.v.map + sourceMappingURL comment)", "false");
  register_inou("cgen", m1);

  // inou.cgen.sim — executable simulator code from an Lgraph (TODO 3d). Per-module
  // <name>.hpp written into `odir`; the standalone Bazel module scaffold around
  // them is written by the kernel's emit_sim_outputs.
  Eprp_method m2("inou.cgen.sim", "export executable simulator code from an Lgraph", &Inou_cgen::to_cgen_sim);
  m2.add_label_optional("backend", "sim.backend: slop (reference C++) or llvm (direct native color objects)", "slop");
  m2.add_label_optional("vcd",
                        "baked-in VCD trace path (the sim.vcd knob: the kernel maps false->none, true-><top>.vcd, "
                        "FILE->that path); empty = no VCD",
                        "");
  m2.add_label_optional("top", "top module name; only it emits VCD (avoids file collisions)", "");
  m2.add_label_optional("vcd_fake_delay",
                        "VCD data settles a few ticks after each clock edge, with X during the settle window "
                        "(sim.vcd_fake_delay); false = plain edge-aligned updates (no X, no delay)",
                        "true");
  m2.add_label_optional("flatten",
                        "sim.flatten=N: structurally inline a sub-instance whose callee body has <= N nodes into "
                        "its parent before occurrence-wide color planning, bottom-up. 0 = never",
                        "0");
  m2.add_label_optional("observe", "emit hierarchical value instrumentation for VCD/probe/query", "false");
  m2.add_label_optional("runtime_support", "emit checkpoint/probe/query state-walk methods", "true");
  m2.add_label_optional("slop_u",
                        "materialize every LGraph-proven unsigned value as canonical-unsigned Slop_u<n> "
                        "(one mask at the write) instead of a lazily-masked Slop<n> (one mask at every read). "
                        "Uniform across combinational temps, IO ports, memories, registers and color boundary "
                        "slots -- no exemption for state or module boundaries",
                        "true");
  m2.add_label_optional("color_dirty",
                        "cross-cycle color activation cache; false emits one unconditional evaluation per color in "
                        "the existing static phase order, with direct boundary assignments",
                        "true");
  m2.add_label_optional("debug",
                        "retain runtime Slop_u landing masks for checking bitwidth-proven unsigned values (true/false)",
                        "false");
  m2.add_label_optional("unknown_zero",
                        "sim.unknown_zero: fill every unknown (`?`) literal bit with 0 instead of a 0/1 drawn once "
                        "per literal from the run's seeded PRNG. true also lets the literal fold at C++ compile time",
                        "false");
  register_inou("cgen", m2);
}

void Inou_cgen::to_cgen_verilog(Eprp_var& var) {
  TRACE_EVENT("inou", "verilog_gen");

  Inou_cgen pp(var);

  auto dir     = pp.get_odir(var);
  auto verbose = pp.verbose;
  auto srcmap  = pp.srcmap;

  // Consume var.graphs (HHDS handle): Eprp_var::add(Lgraph*) pushes the paired
  // shadow into var.graphs, so legacy producers (yosys.tolg, lgraph.match)
  // automatically feed this path. One .v per module, generated inline.
  // (Per-module Verilog cgen used to be dispatched onto a custom thread pool,
  // but that was an unused performance optimization; build-level parallelism
  // comes from independent lhd invocations.)
  // Internal graph names are the hierarchical, always-unique `file.entity`
  // (two files may define the same simple module name). Verilog module names are
  // flat, so pre-compute one flat name per co-emitted graph, shared by every
  // module header and sub-instance reference: a bare `entity` when it is unique
  // across this set, else the sanitized `file_entity` (keeps the netlist
  // collision-free without reintroducing the `lg="…"` override).
  absl::flat_hash_map<std::string, std::string> flat_names;
  {
    absl::flat_hash_map<std::string, std::vector<std::string>> by_entity;
    for (const auto& g : var.graphs) {
      if (!g) {
        continue;
      }
      std::string full(g->get_name());
      auto        pos = full.find_last_of("./");
      by_entity[pos == std::string::npos ? full : full.substr(pos + 1)].push_back(std::move(full));
    }
    auto sanitize = [](std::string s) {
      for (auto& c : s) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        if (!ok) {
          c = '_';
        }
      }
      return s;
    };
    for (auto& [entity, fulls] : by_entity) {
      if (fulls.size() == 1) {
        flat_names.emplace(fulls.front(), entity);  // unique entity -> clean flat name
      } else {
        for (auto& f : fulls) {
          flat_names.emplace(f, sanitize(f));  // shared entity -> sanitized file_entity
        }
      }
    }
  }

  for (const auto& g : var.graphs) {
    if (!g) {
      continue;
    }
    Cgen_verilog p(verbose, dir, srcmap, &flat_names);
    p.do_from_graph(g);
  }
}

void Inou_cgen::to_cgen_sim(Eprp_var& var) {
  TRACE_EVENT("inou", "sim_gen");

  Inou_cgen  pp(var);
  auto       dir               = pp.get_odir(var);
  auto       vcd_out           = var.get("vcd");
  auto       top               = var.get("top");
  auto       fakedelay         = var.get("vcd_fake_delay");
  auto       observe_s         = var.get("observe");
  auto       runtime_support_s = var.get("runtime_support");
  auto       slop_u_s          = var.get("slop_u");
  auto       color_dirty_s     = var.get("color_dirty");
  auto       debug_s           = var.get("debug");
  auto       unknown_zero_s    = var.get("unknown_zero");
  auto       backend           = var.get("backend");
  if (backend != "slop" && backend != "llvm") {
    livehd::diag::err("inou.cgen.sim", "bad-flag-value", "usage").msg("sim.backend expects slop|llvm, got '{}'", backend).emit();
    return;
  }
  // Boolean grammar, validated loudly: anything outside the canonical set would
  // otherwise silently mean "true" (the sim.* namespace validates its own copy,
  // but these labels are also reachable directly).
  bool       bad_flag = false;
  const auto flag_on  = [&bad_flag](std::string_view label, std::string_view v) {
    if (!v.empty() && v != "true" && v != "1" && v != "on" && v != "false" && v != "0" && v != "off") {
      livehd::diag::err("inou.cgen.sim", "bad-flag-value", "usage").msg("{} expects true|false, got '{}'", label, v).emit();
      bad_flag = true;
    }
    return v == "true" || v == "1" || v == "on";
  };
  const bool observe_on         = flag_on("observe", observe_s);
  const bool runtime_support_on = flag_on("runtime_support", runtime_support_s);
  const bool slop_u_on          = flag_on("sim.slop_u", slop_u_s);
  const bool color_dirty_on     = flag_on("sim.color_dirty", color_dirty_s);
  const bool debug_on           = flag_on("sim.debug", debug_s);
  const bool unknown_zero_on    = flag_on("sim.unknown_zero", unknown_zero_s);
  flag_on("sim.vcd_fake_delay", fakedelay);  // validated only: passed on as text
  if (bad_flag) {
    return;
  }
  // sim.flatten=N — inline any callee of <= N nodes. Validated here as well as
  // in the sim.* namespace, because this label is also reachable directly.
  int  flatten_budget = 0;
  auto flatten_s      = var.get("flatten");
  if (!flatten_s.empty()) {
    const auto* b = flatten_s.data();
    const auto* e = b + flatten_s.size();
    auto [p, ec]  = std::from_chars(b, e, flatten_budget);
    if (ec != std::errc{} || p != e || flatten_budget < 0) {
      livehd::diag::err("inou.cgen.sim", "bad-flag-value", "usage")
          .msg("sim.flatten expects a non-negative node count, got '{}'", flatten_s)
          .hint("0 keeps every sub-instance as its own struct; N inlines any callee whose body has <= N nodes")
          .emit();
      return;
    }
  }
  // Simulator lowering still performs backend-specific structural rewrites
  // (clock-gate folding, optional small-instance flattening, and compact-loop
  // realization). Build those into a private output library: the EPRP input
  // graphs remain native and read-only, and every pre-scan/emission handle
  // below belongs to the same scratch bodies it measures.
  hhds::GraphLibrary                        sim_library;
  std::vector<std::shared_ptr<hhds::Graph>> sim_graphs;
  sim_graphs.reserve(var.graphs.size());
  for (const auto& source : var.graphs) {
    if (!source) {
      continue;
    }
    auto  source_io      = source->get_io();
    auto* source_library = source_io ? source_io->get_library() : nullptr;
    if (source_library == nullptr) {
      livehd::diag::err("inou.cgen.sim", "scratch-copy", "internal")
          .msg("could not copy '{}' into the simulator's private output library", source->get_name())
          .emit();
      return;
    }
    // copy_from is DEFINITION-LOCAL and a copied parent resolves
    // get_subnode_graph() through the DESTINATION library only, so copy the
    // whole callee closure — a child def missing from `var.graphs` would
    // otherwise resolve to null here and its instances would silently become
    // body-less blackboxes that compute nothing at simulation time.
    for (const auto& graph : source->definitions().graphs()) {
      if (sim_library.find_io(graph->get_name())) {
        continue;  // shared callee already copied for an earlier source
      }
      if (!sim_library.copy_from(*source_library, graph->get_name())) {
        livehd::diag::err("inou.cgen.sim", "scratch-copy", "internal")
            .msg("could not copy '{}' into the simulator's private output library", graph->get_name())
            .emit();
        return;
      }
    }
  }
  // Drive preparation, planning, and emission from the LIBRARY, not from `var.graphs`:
  // the closure copy above makes a callee definition VISIBLE to the emitter
  // (get_subnode_io() resolves where it used to return null), so a parent now
  // writes `#include "<callee>.hpp"` and directly binds its storage. A list
  // built from `var.graphs` alone would never emit that header/struct and the
  // sim host-compile would die on the missing include. In the normal `lhd` flow
  // `var.graphs` IS the whole library, so this is the same set — and the emit
  // dir's BUILD srcs/manifest, which sim_into() derives from `var.graphs`, keeps
  // listing every .cpp written here.
  for (const auto gid : sim_library.all_gids()) {
    if (auto g = sim_library.get_graph(gid)) {
      sim_graphs.push_back(std::move(g));
    }
  }

  // Run every STRUCTURAL rewrite the emitter makes, over the WHOLE library,
  // BEFORE Color_plan retains occurrence handles. The rewrites delete nodes
  // and rewire edges, so planning first would retain stale class indices.
  {
    Cgen_sim prep(dir, vcd_out, top, fakedelay, flatten_budget);
    for (const auto& g : sim_graphs) {
      // A body that cannot be prepared cannot be emitted correctly (the
      // diagnostic came from prepare_graph): stop the whole emission rather
      // than write a partial design whose remaining modules look fine.
      if (g && !prep.prepare_graph(g)) {
        return;
      }
    }
  }

  // The replacement scheduler starts with a read-only occurrence-wide plan.
  // Build it only after ALL simulator-private structural preparation above:
  // retained HHDS occurrence handles assert if a later rewrite advances the
  // library epoch.  Select roots rather than every definition, otherwise a
  // large hierarchy would be rediscovered once per callee.
  std::map<const hhds::Graph*, livehd::sim::Color_plan> root_color_plans;
  absl::flat_hash_set<std::string>                      instantiated;
  for (const auto& g : sim_graphs) {
    if (!g) {
      continue;
    }
    for (const auto node : g->body().nodes()) {
      if (livehd::graph_util::type_op_of(node) == Ntype_op::Sub) {
        if (auto child = node.get_subnode_graph()) {
          instantiated.insert(std::string(child->get_name()));
        }
      }
    }
  }
  const auto file_stem = [](std::string_view name) {
    std::string result(name);
    for (auto& c : result) {
      if (c == '/' || c == '\\') {
        c = '_';
      }
    }
    return result;
  };
  bool wrote_plan = false;
  for (const auto& g : sim_graphs) {
    if (!g) {
      continue;
    }
    const std::string full(g->get_name());
    const auto        pos      = full.find_last_of("./");
    const std::string entity   = pos == std::string::npos ? full : full.substr(pos + 1);
    const bool        selected = !top.empty() ? (top == full || top == entity) : !instantiated.contains(full);
    if (!selected || (!entity.empty() && entity.front() == '%')) {
      continue;
    }
    auto plan = livehd::sim::Color_plan::discover(g.get(), observe_on || !vcd_out.empty());
    plan.write_report(absl::StrCat(dir, "/", file_stem(full), ".color-plan.txt"));
    if (!plan.complete()) {
      livehd::diag::err("inou.cgen.sim", "color-plan-incomplete", "unsupported")
          .msg("occurrence-wide simulator discovery is incomplete for '{}'", full)
          .hint(plan.errors().empty() ? "inspect the generated .color-plan.txt report" : plan.errors().front())
          .emit();
      return;
    }
    root_color_plans.emplace(g.get(), std::move(plan));
    wrote_plan = true;
  }
  if (!wrote_plan && !sim_graphs.empty()) {
    livehd::diag::err("inou.cgen.sim", "color-plan-root", "usage")
        .msg("could not select top '{}' for occurrence-wide simulator discovery", top)
        .emit();
    return;
  }

  // A compact loop is one outer color whose native ordinal walk currently
  // calls a definition-local body kernel. Mark that callee closure so ordinary
  // hierarchy definitions can be emitted as storage-only records, while the
  // compact kernel and anything it directly invokes retain their body surface.
  absl::flat_hash_set<const hhds::Graph*>   compact_kernel_defs;
  std::vector<std::shared_ptr<hhds::Graph>> compact_work;
  for (const auto& g : sim_graphs) {
    if (!g) {
      continue;
    }
    for (const auto node : g->body().nodes()) {
      if (node.subnode_loop()) {
        if (auto child = node.get_subnode_graph()) {
          compact_work.push_back(std::move(child));
        }
      }
    }
  }
  while (!compact_work.empty()) {
    auto definition = std::move(compact_work.back());
    compact_work.pop_back();
    if (!definition || !compact_kernel_defs.insert(definition.get()).second) {
      continue;
    }
    for (const auto node : definition->body().nodes()) {
      if (livehd::graph_util::type_op_of(node) == Ntype_op::Sub) {
        if (auto child = node.get_subnode_graph()) {
          compact_work.push_back(std::move(child));
        }
      }
    }
  }

  // Synchronous (one .hpp per module): the designs are small and the kernel's
  // sim_into() checks each <module>.hpp exists right after this returns.
  for (const auto& g : sim_graphs) {
    if (!g) {
      continue;
    }
    const auto  plan_it = root_color_plans.find(g.get());
    const auto* plan    = plan_it == root_color_plans.end() ? nullptr : &plan_it->second;
    Cgen_sim    p(dir,
                  vcd_out,
                  top,
                  fakedelay,
                  flatten_budget,
                  plan,
                  compact_kernel_defs.contains(g.get()),
                  observe_on,
                  runtime_support_on,
                  slop_u_on,
                  color_dirty_on,
                  debug_on,
                  unknown_zero_on,
                  backend == "llvm");
    p.do_from_graph(g);
  }
}
