//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_cgen.hpp"

#include <array>
#include <charconv>
#include <map>
#include <tuple>

#include "cgen_sim.hpp"
#include "cgen_verilog.hpp"
#include "diag.hpp"  // livehd::diag::err — flag-value validation
#include "file_utils.hpp"
#include "node_util.hpp"
#include "perf_tracing.hpp"
#include "port_reach.hpp"
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

  // inou.cgen.sim — executable Slop C++ from an Lgraph (TODO 3d). Per-module
  // <name>.hpp written into `odir`; the standalone Bazel module scaffold around
  // them is written by the kernel's emit_sim_outputs.
  Eprp_method m2("inou.cgen.sim", "export executable slop C++ from an Lgraph", &Inou_cgen::to_cgen_sim);
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
                        "its parent, bottom-up. 0 = never (every instance stays its own struct behind a "
                        "cycle()/__settle() call)",
                        "0");
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
  // comes from independent lhd invocations. If sim/cgen ever needs
  // intra-process parallelism it will be reintroduced via taskflow.)
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

  Inou_cgen pp(var);
  auto      dir       = pp.get_odir(var);
  auto      vcd_out   = var.get("vcd");
  auto      top       = var.get("top");
  auto      fakedelay = var.get("vcd_fake_delay");
  // Boolean grammar, validated loudly: anything outside the canonical set would
  // otherwise silently mean "true" (the sim.* namespace validates its own copy,
  // but the sim.vcd_fake_delay knob reaches this label directly).
  if (!fakedelay.empty() && fakedelay != "true" && fakedelay != "1" && fakedelay != "on" && fakedelay != "false" && fakedelay != "0"
      && fakedelay != "off") {
    livehd::diag::err("inou.cgen.sim", "bad-flag-value", "usage")
        .msg("sim.vcd_fake_delay expects true|false, got '{}'", fakedelay)
        .emit();
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
  // Drive prepare/pre-scan/emission from the LIBRARY, not from `var.graphs`:
  // the closure copy above makes a callee definition VISIBLE to the emitter
  // (get_subnode_io() resolves where it used to return null), so a parent now
  // writes `#include "<callee>.hpp"` and `__settle_g<k>()` calls for it. A list
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
  // BEFORE the pre-scan below measures anything. The pre-scan holds Pin_class
  // handles into callee bodies, and the rewrites delete nodes and rewire edges
  // in exactly those bodies — measuring first would hand the emitter class
  // indices that no longer mean what was measured.
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

  // PRE-SCAN for callee partitioning: any definition instantiated on a
  // word-level cycle (a Sub the single-pass schedule sees on a strict-model
  // cycle in SOME parent) emits per-output-group `__settle_g<k>` methods so
  // the parent can evaluate it in pieces instead of inlining it. Groups are
  // outputs sharing the same comb input-support, from the hierarchical
  // port-reach summaries; support-free outputs stay on the cheaper prebind
  // path and get no group.
  Cgen_sim::Split_map splits;
  {
    livehd::port_reach::Cache                                             reach;
    absl::node_hash_map<const hhds::Graph*, std::shared_ptr<hhds::Graph>> need;
    absl::flat_hash_set<hhds::Node_class>                                 cyc;
    for (const auto& g : sim_graphs) {
      if (!g) {
        continue;
      }
      cyc.clear();
      livehd::graph_util::word_level_cycle_nodes(g.get(), /*strict=*/true, cyc);
      for (const auto& n : cyc) {
        if (livehd::graph_util::type_op_of(n) != Ntype_op::Sub) {
          continue;
        }
        if (auto cg = n.get_subnode_graph(); cg) {
          need.emplace(cg.get(), cg);
        }
      }
    }
    for (const auto& [ptr, sp] : need) {
      const auto&                                                           dr = reach.of(sp);
      // signature (sorted support atoms) -> group under construction
      std::map<std::vector<std::array<uint32_t, 3>>, Cgen_sim::Split_group> by_sig;
      auto add_entry = [&](Cgen_sim::Split_group::Out out, const std::vector<std::array<uint32_t, 3>>& sig) {
        auto& grp = by_sig[sig];
        if (grp.support.empty() && !sig.empty()) {
          for (const auto& a : sig) {
            grp.support.push_back({a[0], a[1], a[2]});
          }
        }
        grp.outs.push_back(std::move(out));
      };
      const bool noslice = ::getenv("LIVEHD_SIM_NOSLICE") != nullptr;  // bisect aid
      for (const auto& [opid, ins] : dr.out2ins) {
        if (auto sit = dr.out_slices.find(opid); !noslice && sit != dr.out_slices.end()) {
          // SLICED output. If every slice is input-independent the whole port
          // stays on the prebind path; a MIXED output puts ALL its slices into
          // groups (the whole-pin value is then assembled from the exports).
          bool any_dep = false;
          for (const auto& sl : sit->second) {
            if (!sl.ins.empty()) {
              any_dep = true;
              break;
            }
          }
          if (!any_dep) {
            continue;  // registry-verified state-only, slice-confirmed
          }
          for (const auto& sl : sit->second) {
            std::vector<std::array<uint32_t, 3>> sig;
            for (const auto& a : sl.ins) {
              sig.push_back({a.pid, a.lo, a.len});
            }
            std::sort(sig.begin(), sig.end());
            sig.erase(std::unique(sig.begin(), sig.end()), sig.end());
            // `shifted` is per SLICE and comes from port_reach, which is the
            // only place that knows which arm produced it: a decided-once-per-
            // port guess mis-reads a concat that happens to replicate one
            // instance output into two fields.
            add_entry({opid, sl.lo, sl.len, sl.leaf, sl.shifted}, sig);
          }
          continue;
        }
        if (ins.empty()) {
          continue;  // state-only output: the prebind path owns it
        }
        std::vector<std::array<uint32_t, 3>> sig;
        for (const uint32_t ipid : ins) {
          sig.push_back({ipid, 0, 0});
        }
        std::sort(sig.begin(), sig.end());
        add_entry({opid, 0, 0, {}, false}, sig);
      }
      std::vector<Cgen_sim::Split_group> groups;
      for (auto& [sig, grp] : by_sig) {
        std::sort(grp.outs.begin(), grp.outs.end(), [](const auto& a, const auto& b) {
          return std::tie(a.pid, a.lo) < std::tie(b.pid, b.lo);
        });
        groups.push_back(std::move(grp));
      }
      if (::getenv("LIVEHD_SIM_SPLIT_DEBUG") != nullptr) {
        auto                                       cio = sp->get_io();
        absl::flat_hash_map<uint32_t, std::string> in_names, out_names;
        if (cio) {
          for (const auto& d : cio->get_input_pin_decls()) {
            in_names[d.port_id] = d.name;
          }
          for (const auto& d : cio->get_output_pin_decls()) {
            out_names[d.port_id] = d.name;
          }
        }
        std::string dbg
            = std::string("[splitdbg] ") + std::string(sp->get_name()) + ": " + std::to_string(groups.size()) + " group(s)\n";
        for (size_t gi2 = 0; gi2 < groups.size(); ++gi2) {
          dbg += "  g" + std::to_string(gi2) + " outs:";
          for (const auto& o : groups[gi2].outs) {
            dbg += " " + (out_names.contains(o.pid) ? out_names[o.pid] : std::to_string(o.pid));
            if (o.len != 0) {
              dbg += "[" + std::to_string(o.lo + o.len - 1) + ":" + std::to_string(o.lo) + "]";
            }
          }
          dbg += " <- ins:";
          for (const auto& a : groups[gi2].support) {
            dbg += " " + (in_names.contains(a.pid) ? in_names[a.pid] : std::to_string(a.pid));
            if (a.len != 0) {
              dbg += "[" + std::to_string(a.lo + a.len - 1) + ":" + std::to_string(a.lo) + "]";
            }
          }
          dbg += "\n";
        }
        std::fputs(dbg.c_str(), stderr);
      }
      splits.emplace(ptr, std::move(groups));
    }
  }

  // Synchronous (one .hpp per module): the designs are small and the kernel's
  // sim_into() checks each <module>.hpp exists right after this returns.
  for (const auto& g : sim_graphs) {
    if (!g) {
      continue;
    }
    Cgen_sim p(dir, vcd_out, top, fakedelay, flatten_budget, &splits);
    p.do_from_graph(g);
  }
}
