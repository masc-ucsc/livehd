//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_cgen.hpp"

#include <array>
#include <bit>
#include <charconv>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "cgen_verilog.hpp"
#include "diag.hpp"  // livehd::diag::err — flag-value validation
#include "file_name.hpp"
#include "file_utils.hpp"
#include "node_util.hpp"
#include "perf_tracing.hpp"
#include "specialize.hpp"
#include "split_selfref.hpp"

static Pass_plugin sample("inou_cgen", Inou_cgen::setup);

namespace {
// Internal graph names are the hierarchical, always-unique `file.entity` (two
// files may define the same simple module name). Both generators below split
// one off the other, and the DUT/root verdict is decided from the pair -- so
// the separator set and the no-separator fallback live here, once: a name that
// splits one way for `is_selected_root` and another way for the emission loop
// is a silently mis-selected top, not a compile error.
std::pair<std::string, std::string> split_entity(std::string_view name) {
  std::string full(name);
  const auto  pos    = full.find_last_of("./");
  std::string entity = pos == std::string::npos ? full : full.substr(pos + 1);
  return {std::move(full), std::move(entity)};
}
}  // namespace

Inou_cgen::Inou_cgen(const Eprp_var& var) : Pass("inou.cgen", var) {
  auto m = var.get("srcmap");
  srcmap = m == "true" || m == "1";
}

void Inou_cgen::setup() {
  Eprp_method m1("inou.cgen.verilog", "export verilog from an Lgraph", &Inou_cgen::to_cgen_verilog);

  m1.add_label_optional("srcmap", "emit an ECMA-426 source-map sidecar (.v.map + sourceMappingURL comment)", "false");
  register_inou("cgen", m1);
}

void Inou_cgen::to_cgen_verilog(Eprp_var& var) {
  TRACE_EVENT("inou", "verilog_gen");

  Inou_cgen pp(var);

  auto dir    = pp.get_odir(var);
  auto srcmap = pp.srcmap;

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
      auto [full, entity] = split_entity(g->get_name());
      by_entity[std::move(entity)].push_back(std::move(full));
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
    Cgen_verilog p(dir, srcmap, &flat_names);
    p.do_from_graph(g);
  }
}
