// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_abc.hpp"

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <memory>
#include <print>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

#include "abc_incr.hpp"
#include "abc_map.hpp"
#include "diag.hpp"
#include "graph_library_singleton.hpp"
#include "mem_lower.hpp"
#include "node_util.hpp"
#include "occurrence_materialize.hpp"
#include "pass_partition.hpp"
#include "predict_abc_size.hpp"  // sat_add

static Pass_plugin sample("pass_abc", Pass_abc::setup);

Pass_abc::Pass_abc(const Eprp_var& var) : Pass("pass.abc", var) {}

void Pass_abc::setup() {
  Eprp_method m("pass.abc", "Technology-map each colored region to a standard-cell netlist (ABC)", &Pass_abc::work);
  // The top module is the shared kernel `--top` flag (lhd plumbs it into the
  // `top` label), not a per-pass --set option.
  m.add_label_optional("out", "output graph_library directory (the --emit-dir lg: slot)", "");
  m.add_label_optional("library", "Liberty .lib for read_lib (default $HAGENT_TECH_DIR/sky130_fd_sc_hd__tt_025C_1v80.lib)", "");
  m.add_label_optional(
      "flow",
      "ABC command string, run verbatim (empty => the built-in comb/seq default). "
      "Commands run in order, ';'-separated; {D}/{L} are substituted from the delay/load options. "
      "A custom flow must still include a technology-mapping step (`&nf {D}`) so the result is a cell netlist. "
      "The standard abc.rc synthesis scripts and their short-name building blocks are pre-registered as aliases, "
      "so flow=\"resyn2\" works just like in an interactive ABC shell.\n"
      "\n"
      "building blocks:  b=balance  rw=rewrite  rwz=rewrite -z  rf=refactor  rfz=refactor -z  rs=resub  rsz=resub -z  "
      "st=strash  f=fraig  dret=dretime\n"
      "AIG opt scripts:  resyn  resyn2  resyn2a  resyn3  compress  compress2  choice  choice2\n"
      "resub scripts:    resyn2rs  compress2rs  src_rw  src_rs  src_rws    (raw form: rs -K <cut-size> -N <max-nodes>)\n"
      "GIA (& space):    &get/&put move the AIG in/out; &dch &fraig &if &nf &deepsyn &resub &mfs &dc3 &dc4\n"
      "                  (bound &deepsyn with -J <no-improve> and/or -T <seconds>: with neither it runs ~1e5 passes)\n"
      "\n"
      "examples:\n"
      "  flow=\"strash; resyn2; &get -n; &dch -f; &nf {D}; &put\"            (AIG opt then map)\n"
      "  flow=\"strash; resyn2rs; &get -n; &nf {D}; &put\"                   (resub-heavy)\n"
      "  flow=\"b; rs -K 6; rw; rs -K 6 -N 2; rf; rs -K 8; &get -n; &nf {D}; &put\"   (hand-rolled resub)\n"
      "  flow=\"strash; &get -n; &deepsyn -I 4 -J 20; &dch -f; &nf {D}; &put\"   (deepsyn, bounded)\n"
      "command/alias reference: https://github.com/berkeley-abc/abc/blob/master/abc.rc "
      "(and `<cmd> -h` inside an ABC shell for each command's switches)",
      "");
  // Fanout cap for anything ABC MAPS. Without it ABC leaves nets far past the
  // Liberty's characterized load and pass.opentimer extrapolates off the end of
  // the NLDM table -- an `a21oi_1` measured 3090 ns against a ~0.05 ns intrinsic
  // delay. A cap of 16 bounds the mapped dino fanouts that originally exposed
  // the extrapolation. Nets driven by NATIVE (unblasted) nodes never reach ABC
  // and keep their fanout regardless.
  m.add_label_optional("max_fanout",
                       "cap the fanout of every net ABC maps, by appending `buffer -N <n>; dnsize` to the "
                       "built-in flow (0 disables it). A custom `flow` places `{F}` -- the bare number -- itself",
                       "16");
  m.add_label_optional("small_flow",
                       "optional ABC command string used for regions whose pre-ABC synthesis-GE estimate is in "
                       "[small_min_ge, small_ge]; "
                       "empty disables size-tiered mapping. Explicit region_opts flow overrides this selection",
                       "");
  m.add_label_optional("small_min_ge", "inclusive lower synthesis-GE bound for small_flow (0 means no lower bound)", "0");
  m.add_label_optional("small_ge", "non-negative synthesis-GE threshold for small_flow (0 disables it)", "0");
  m.add_label_optional("large_flow",
                       "ABC command string for indivisible over-large regions at or above large_ge; empty disables the tier. "
                       "An explicit global flow or region_opts flow wins",
                       "strash; &get -n; &nf {D}; &put");
  m.add_label_optional("large_ge",
                       "inclusive synthesis-GE threshold for large_flow (0 disables it); default protects wide indivisible "
                       "operations from unbounded &dch choice synthesis",
                       "200000");
  m.add_label_optional("register",
                       "true|false map flops to Liberty DFF cells (true, falls back to native flops when the library has no "
                       "DFF cell) vs keep them native as `always @(posedge)` (false)",
                       "true");
  m.add_label_optional("register_max_bits",
                       "with register=true, keep a region's flops native when their total Q width exceeds this many bits "
                       "(0 disables the guard)",
                       "4096");
  m.add_label_optional("memory",
                       "true|false bit-blast a Memory into a DFF-cell array + read/write mux logic (true) vs keep it as a "
                       "native memory instance (false)",
                       "false");
  m.add_label_optional("dff_cell",
                       "explicit Liberty DFF cell name for register=true (empty => auto-detect a plain posedge D-flop)",
                       "");
  m.add_label_optional("delay", "{D} substitution in flow", "");
  m.add_label_optional("load", "{L} substitution in flow", "");
  m.add_label_optional("verbose", "per-module ABC stats", "false");
  m.add_label_optional("stats", "report one mapped QoR row per (definition, color); incremental rows include resynth=1|0", "false");
  m.add_label_optional("adder", "combinational adder architecture for Sum/comparators: rca|cska|cla", "rca");
  m.add_label_optional("block_size", "CSKA skip-block / CLA lookahead-group width (0 => auto: W/4|W/2|W)", "0");
  m.add_label_optional("memory_budget_mb",
                       "memory-admission ceiling (additional process RSS, MiB) for one ABC color; "
                       "0 => physical RAM minus max(2 GiB, 20%) of OS reserve. Physical only, never swap",
                       "0");
  m.add_label_optional("time_budget_ms",
                       "soft wall-time limit for one mapped color in milliseconds (0 disables); a completed "
                       "oversize color fails with its name so color.max_ge can be reduced",
                       "0");
  m.add_label_optional("allow_oversize",
                       "true|false skip memory admission and map the region regardless. It may exhaust "
                       "physical memory and be killed by the OS",
                       "false");
  m.add_label_optional("multiplier",
                       "combinational multiplier architecture for Mult: array (partial-product adds use 'adder')",
                       "array");
  m.add_label_optional("qor",
                       "write per-region + total QoR JSON (mapped gates/area/critical delay, source-attributed) to this file "
                       "(`lhd pass abc` defaults it to <workdir>/qor.json when --workdir is set)",
                       "");
  m.add_label_optional("cache_dir",
                       "INTERNAL kernel plumbing: the INCREMENTAL synthesis region cache (2opt-incr) directory, always "
                       "<workdir>/abc_cache (set after user --set merging, so it is not customizable). A region whose "
                       "logic, boundary and resolved ABC recipe are unchanged since a prior run is cloned from the cache "
                       "instead of re-running ABC; salted by the Liberty content and the register/memory mapping mode. "
                       "Empty = no user --workdir, or `lhd.incremental=false` = no cache",
                       "");
  m.add_label_optional("flatten",
                       "auto|true|false whole-design flatten: inline the instance hierarchy and map the flat design as "
                       "one region (auto = flatten exactly when the active coloring is `pass.color flat`); the result "
                       "is a single netlist module named after the top",
                       "auto");
  m.add_label_optional("region_opts",
                       "per-region option overrides as JSON keyed by color id, e.g. "
                       "'{\"1\":{\"flow\":\"strash; resyn2; &get -n; &nf {D}; &put\",\"delay\":\"2\"},\"4\":{\"adder\":\"cla\"}}'. "
                       "Overridable per region: flow|delay|load|adder|block_size|multiplier. "
                       "Wins over a \"region_opts\" member embedded in the graph's coloring_info (the block-attribute channel); "
                       "unknown keys or malformed values are hard errors",
                       "");
  register_pass(m);
}

namespace {

// Default Liberty path for dev/test when --set pass.abc.library is unset.
std::string default_library() {
  const char* tech = std::getenv("HAGENT_TECH_DIR");
  if (tech == nullptr || tech[0] == '\0') {
    return {};
  }
  std::string dir{tech};
  if (dir.back() != '/') {
    dir.push_back('/');
  }
  return dir + "sky130_fd_sc_hd__tt_025C_1v80.lib";
}

bool truthy(std::string_view v) { return v != "false" && v != "0" && v != ""; }

// Minimal JSON string escape (module names / file paths can carry quotes or
// backslashes; anything below 0x20 is escaped numerically).
std::string jesc(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '"' : out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      case '\r': out += "\\r"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
        } else {
          out.push_back(c);
        }
    }
  }
  return out;
}

// Aggregate the per-region QoR rows, print the one-line summary (the step log
// under lhd), and optionally write the qor.json sidecar (2opt-freq A). The
// design max delay is the worst REGION delay — an ABC estimate blind to
// cross-region paths; pass.opentimer is the whole-design scorer.
// Physical instantiation counts, read off the EMITTED netlist library: the
// decomposition absorbs same-color child bodies into the parent's region (the
// parent row then holds that logic per copy, and the child's standalone rows
// are mapped but never instantiated — the rolled matched filter's tap chain
// and `tap` itself end up inside the top), while every other region def is a
// Sub instance somewhere under the top. Walking the output library from the
// top and counting Sub instances per def, top-down, is therefore the one
// count that matches what was built: a region row weighs gates x instances,
// an absorbed def's own rows weigh 0.
struct Abc_hier {
  absl::flat_hash_map<std::string, absl::flat_hash_map<std::string, uint64_t>> children;  // def -> child def -> Sub count
};

absl::flat_hash_map<std::string, uint64_t> physical_instances(const Abc_hier& hier, std::string_view top) {
  absl::flat_hash_map<std::string, uint64_t> inst;
  inst[std::string(top)] = 1;
  // The hierarchy is acyclic: iterate to a fixed point (at most depth rounds).
  for (bool changed = true; changed;) {
    absl::flat_hash_map<std::string, uint64_t> next;
    next[std::string(top)] = 1;
    for (const auto& [src, kids] : hier.children) {
      const auto it = inst.find(src);
      if (it == inst.end() || it->second == 0) {
        continue;
      }
      for (const auto& [child, n] : kids) {
        next[child] += it->second * n;
      }
    }
    changed = next != inst;
    inst    = std::move(next);
  }
  return inst;
}

void emit_qor(const std::vector<livehd::abc::Region_qor>& qor, std::string_view top, const livehd::abc::Map_options& opts,
              const std::string& qor_path, const livehd::abc::Incr_cache* incr, bool abc_started, const Abc_hier& hier) {
  // PHYSICAL totals: a region's gates times the number of times its module is
  // instantiated (a replicated loop body N times, a shared `tap` 64 times).
  // The per-module sums are kept beside them as module_gates/module_area —
  // they answer "how much did abc map", not "how big is the chip".
  const auto instances = physical_instances(hier, top);
  const auto inst_of   = [&](const livehd::abc::Region_qor& q) -> uint64_t {
    if (q.module == top) {
      return 1;
    }
    if (hier.children.empty()) {
      return 1;  // no netlist was emitted (stats-only) — nothing to weigh by
    }
    const auto it = instances.find(q.module);
    return it == instances.end() ? 0 : it->second;
  };
  uint64_t tgates_phys       = 0;
  double   tarea_phys        = 0.0;
  int      tgates            = 0;
  double   tarea             = 0.0;
  int      tdivbb            = 0;  // blackboxed div/mod cones (the score under-reports)
  uint64_t tinput_nodes      = 0;
  uint64_t tinput_ge         = 0;
  uint64_t tpred_aig         = 0;
  uint64_t peak_rss_kb       = 0;
  uint64_t color_peak_rss_kb = 0;
  int      worst             = -1;  // index of the region with the worst delay
  // Where the run's time went, split by what the cache did with each region.
  // hits/misses alone cannot distinguish "the cache saved nothing" from "the
  // cache saved everything there was to save" — these can.
  double   hit_ms = 0.0, miss_ms = 0.0;
  for (size_t r = 0; r < qor.size(); ++r) {
    tgates       += qor[r].gates;
    tarea        += qor[r].area;
    tgates_phys  += static_cast<uint64_t>(qor[r].gates) * inst_of(qor[r]);
    tarea_phys   += qor[r].area * static_cast<double>(inst_of(qor[r]));
    tdivbb       += qor[r].div_blackbox;
    tinput_nodes += qor[r].input_nodes;
    tinput_ge    += qor[r].input_ge;
    tpred_aig     = livehd::graph_util::sat_add(tpred_aig, qor[r].pred_aig);
    if (qor[r].resynth) {
      peak_rss_kb       = std::max(peak_rss_kb, qor[r].peak_rss_kb);
      color_peak_rss_kb = std::max(color_peak_rss_kb, qor[r].color_peak_rss_kb);
    }
    (std::string_view{qor[r].cache} == "hit" ? hit_ms : miss_ms) += qor[r].ms;
    if (qor[r].delay >= 0 && (worst < 0 || qor[r].delay > qor[static_cast<size_t>(worst)].delay)) {
      worst = static_cast<int>(r);
    }
  }
  std::string crit;
  if (worst >= 0) {
    const auto& w = qor[static_cast<size_t>(worst)];
    crit          = std::format(", max delay {:.2f} (region '{}'", w.delay, w.module);
    if (!w.crit_output.empty()) {
      crit += std::format(" output '{}'", w.crit_output);
    }
    if (!w.crit_src.empty()) {
      crit += std::format(" @ {}", w.crit_src);
    }
    crit += ")";
  }
  std::print(
      "pass.abc qor: {} region(s), {} gates, area {:.2f} (physical: every region x its instantiations; mapped once: {} gates, area "
      "{:.2f}){}{}\n",
      qor.size(),
      tgates_phys,
      tarea_phys,
      tgates,
      tarea,
      crit,
      tdivbb == 0 ? std::string{} : std::format(" [PARTIAL: {} blackboxed div/mod cone(s) unscored]", tdivbb));

  if (incr != nullptr) {
    // The number that actually answers "did incremental help": what the
    // non-reused regions cost. A high hit RATE over cheap regions is not a
    // speedup, and only this line makes that visible.
    std::print("pass.abc incremental: {} hit ({:.1f}s), {} miss ({:.1f}s)\n",
               incr->hits(),
               hit_ms / 1000.0,
               incr->misses(),
               miss_ms / 1000.0);
  }

  if (qor_path.empty()) {
    return;
  }
  std::string j  = "{";
  j             += "\"schema_version\":1,\"kind\":\"abc-map\",";
  j             += std::format("\"top\":\"{}\",", jesc(top));
  j             += std::format("\"library\":\"{}\",", jesc(opts.library));
  j += std::format("\"register\":{},\"memory\":{},", opts.map_register ? "true" : "false", opts.map_memory ? "true" : "false");
  j += std::format("\"delay_target\":\"{}\",", jesc(opts.delay));
  // `gates`/`area` are PHYSICAL (region x instantiations); `module_gates`/
  // `module_area` are the per-mapped-module sums (what abc worked on once).
  j += std::format(
      "\"total\":{{\"regions\":{},\"input_nodes\":{},\"input_ge\":{},\"pred_aig\":{},\"gates\":{},\"area\":{:.4f},"
      "\"module_gates\":{},\"module_area\":{:.4f}",
      qor.size(),
      tinput_nodes,
      tinput_ge,
      tpred_aig,
      tgates_phys,
      tarea_phys,
      tgates,
      tarea);
  if (peak_rss_kb != 0) {
    j += std::format(",\"peak_rss_kb\":{}", peak_rss_kb);
  }
  if (color_peak_rss_kb != 0) {
    j += std::format(",\"color_peak_rss_kb\":{}", color_peak_rss_kb);
  }
  if (tdivbb > 0) {
    j += std::format(",\"div_blackbox\":{}", tdivbb);
  }
  if (worst >= 0) {
    const auto& w  = qor[static_cast<size_t>(worst)];
    j             += std::format(",\"max_delay\":{:.4f},\"critical_region\":\"{}\"", w.delay, jesc(w.module));
    if (!w.crit_output.empty()) {
      j += std::format(",\"critical_output\":\"{}\"", jesc(w.crit_output));
    }
    if (!w.crit_src.empty()) {
      j += std::format(",\"critical_src\":\"{}\"", jesc(w.crit_src));
    }
  }
  j += "}";
  if (incr != nullptr) {
    // The agent loop reads its "did the edit change anything" answer here: a
    // NoChange edit is hits == regions, misses == 0, in O(#regions) lookups.
    j += std::format(",\"incremental\":{{\"hits\":{},\"misses\":{},\"hit_ms\":{:.1f},\"miss_ms\":{:.1f},\"abc_started\":{}}}",
                     incr->hits(),
                     incr->misses(),
                     hit_ms,
                     miss_ms,
                     abc_started ? 1 : 0);
  }
  j += ",\"regions\":[";
  for (size_t r = 0; r < qor.size(); ++r) {
    const auto& q = qor[r];
    if (r != 0) {
      j += ",";
    }
    j += std::format(
        "{{\"module\":\"{}\",\"color\":{},\"input_nodes\":{},\"input_ge\":{},\"pred_aig\":{},\"gates\":{},"
        "\"area\":{:.4f},\"instances\":{},\"ms\":{:.1f},\"resynth\":{}",
        jesc(q.module),
        q.color,
        q.input_nodes,
        q.input_ge,
        q.pred_aig,
        q.gates,
        q.area,
        inst_of(q),
        q.ms,
        q.resynth ? 1 : 0);
    if (q.peak_rss_kb != 0) {
      j += std::format(",\"peak_rss_kb\":{}", q.peak_rss_kb);
    }
    if (q.color_peak_rss_kb != 0) {
      j += std::format(",\"color_peak_rss_kb\":{}", q.color_peak_rss_kb);
    }
    if (q.cache[0] != '\0') {
      j += std::format(",\"cache\":\"{}\"", q.cache);
    }
    if (q.div_blackbox > 0) {
      j += std::format(",\"div_blackbox\":{}", q.div_blackbox);
    }
    if (q.delay >= 0) {
      j += std::format(",\"delay\":{:.4f}", q.delay);
    }
    if (!q.crit_output.empty()) {
      j += std::format(",\"critical_output\":\"{}\"", jesc(q.crit_output));
    }
    if (!q.crit_src.empty()) {
      j += std::format(",\"critical_src\":\"{}\"", jesc(q.crit_src));
    }
    j += "}";
  }
  j += "]}";

  std::ofstream ofs(qor_path, std::ios::binary | std::ios::trunc);
  if (!ofs) {
    livehd::diag::err("pass.abc", "qor-write", "io").msg("pass.abc: cannot write qor file '{}'", qor_path).fatal();
    return;
  }
  ofs << j << "\n";
}

}  // namespace

void Pass_abc::work(Eprp_var& var) {
  // ABC consumes a physical scratch design. Copy first so occurrence
  // materialization and every later mapping rewrite leave the native source
  // graph untouched.
  hhds::GraphLibrary                        occurrence_library;
  std::vector<std::shared_ptr<hhds::Graph>> occurrence_graphs;
  for (const auto& source : var.graphs) {
    if (!source) {
      continue;
    }
    auto  io  = source->get_io();
    auto* lib = io ? io->get_library() : nullptr;
    if (lib == nullptr) {
      livehd::diag::err("pass.abc", "scratch-copy", "internal")
          .msg("could not copy '{}' into ABC's private physical library", source->get_name())
          .emit();
      return;
    }
    // copy_from is DEFINITION-LOCAL: it never pulls in a callee, and a copied
    // parent resolves get_subnode_graph() through the DESTINATION library only.
    // Copy the whole callee closure (as pass/lec's copy_loop_scratch does) so a
    // child def that `var.graphs` does not happen to list still resolves here --
    // otherwise its instances silently become blackboxes in the mapped netlist.
    for (const auto& graph : source->definitions().graphs()) {
      if (occurrence_library.find_io(graph->get_name())) {
        continue;  // shared callee already copied for an earlier source
      }
      if (!occurrence_library.copy_from(*lib, graph->get_name())) {
        livehd::diag::err("pass.abc", "scratch-copy", "internal")
            .msg("could not copy '{}' into ABC's private physical library", graph->get_name())
            .emit();
        return;
      }
    }
  }
  for (const auto& source : var.graphs) {
    auto io = source ? occurrence_library.find_io(source->get_name()) : std::shared_ptr<hhds::GraphIO>{};
    occurrence_graphs.push_back(io ? io->get_graph() : std::shared_ptr<hhds::Graph>{});
  }
  // Materialize everything the closure copy brought in, not just the
  // `var.graphs`-named entries: a compact loop Sub left inside a closure-only
  // callee is not something the mapper can read, so it would map one replica
  // and drop the rest.
  std::vector<std::shared_ptr<hhds::Graph>> scratch_graphs;
  for (const auto gid : occurrence_library.all_gids()) {
    if (auto graph = occurrence_library.get_graph(gid)) {
      scratch_graphs.push_back(std::move(graph));
    }
  }
  if (!livehd::graph_util::materialize_occurrences_all(scratch_graphs, "pass.abc")) {
    return;
  }
  // Def list handed to the hierarchy walks below (size gate, decomposition).
  // resolve_order builds its gid2graph EXCLUSIVELY from the vector it gets, so a
  // closure-only callee missing here is a Sub the DFS cannot follow and no
  // region is ever built for it. `occurrence_graphs` (i.e. `var.graphs`) stays
  // FIRST because top is the first matching entry and all_gids() is name-hash
  // order: top selection must not depend on it.
  std::vector<std::shared_ptr<hhds::Graph>> resolve_graphs = occurrence_graphs;
  {
    std::unordered_set<hhds::Gid> listed;
    for (const auto& graph : occurrence_graphs) {
      if (graph) {
        listed.insert(graph->get_gid());
      }
    }
    for (const auto& graph : scratch_graphs) {
      if (listed.insert(graph->get_gid()).second) {
        resolve_graphs.push_back(graph);
      }
    }
  }

  auto top                 = std::string{var.get("top", "")};
  auto out                 = std::string{var.get("out", "")};
  auto library             = std::string{var.get("library", "")};
  auto flow                = std::string{var.get("flow", "")};
  auto small_flow          = std::string{var.get("small_flow", "")};
  auto small_min_ge_s      = std::string{var.get("small_min_ge", "0")};
  auto small_ge_s          = std::string{var.get("small_ge", "0")};
  auto large_flow          = std::string{var.get("large_flow", "")};
  auto large_ge_s          = std::string{var.get("large_ge", "200000")};
  bool map_register        = truthy(var.get("register", "true"));
  bool map_memory          = truthy(var.get("memory", "false"));
  auto register_max_bits_s = std::string{var.get("register_max_bits", "4096")};
  auto delay               = std::string{var.get("delay", "")};
  auto load                = std::string{var.get("load", "")};
  bool verbose             = truthy(var.get("verbose", "false"));
  auto adder_s             = std::string{var.get("adder", "rca")};
  auto bs_s                = std::string{var.get("block_size", "0")};
  auto mult_s              = std::string{var.get("multiplier", "array")};
  auto qor_path            = std::string{var.get("qor", "")};
  auto region_opts_s       = std::string{var.get("region_opts", "")};
  auto mem_budget_s        = std::string{var.get("memory_budget_mb", "0")};
  auto time_budget_s       = std::string{var.get("time_budget_ms", "0")};
  bool allow_oversize      = truthy(var.get("allow_oversize", "false"));
  auto flatten             = livehd::partition::parse_flatten_mode(var.get("flatten", "auto"), "pass.abc");

  livehd::abc::Region_opts_map region_opts;
  if (!region_opts_s.empty()) {
    auto parsed = livehd::abc::parse_region_opts(region_opts_s, "--set pass.abc.region_opts");
    if (!parsed.has_value()) {
      return;  // diag already emitted
    }
    region_opts = std::move(parsed.value());
  }

  auto adder = livehd::abc::arith::parse_adder_kind(adder_s);
  if (!adder.has_value()) {
    livehd::diag::err("pass.abc", "bad-adder", "io").msg("pass.abc: unknown adder '{}' (use rca|cska|cla)", adder_s).fatal();
    return;
  }
  auto multiplier = livehd::abc::arith::parse_mult_kind(mult_s);
  if (!multiplier.has_value()) {
    livehd::diag::err("pass.abc", "bad-multiplier", "io").msg("pass.abc: unknown multiplier '{}' (use array)", mult_s).fatal();
    return;
  }
  int memory_budget_mb = 0;
  {
    auto* b      = mem_budget_s.data();
    auto* e      = mem_budget_s.data() + mem_budget_s.size();
    auto [p, ec] = std::from_chars(b, e, memory_budget_mb);
    if (ec != std::errc{} || p != e || memory_budget_mb < 0) {
      livehd::diag::err("pass.abc", "bad-memory-budget", "io")
          .msg("pass.abc: memory_budget_mb must be a non-negative integer, got '{}'", mem_budget_s)
          .fatal();
      return;
    }
  }
  uint64_t time_budget_ms = 0;
  {
    auto* b      = time_budget_s.data();
    auto* e      = time_budget_s.data() + time_budget_s.size();
    auto [p, ec] = std::from_chars(b, e, time_budget_ms);
    if (ec != std::errc{} || p != e) {
      livehd::diag::err("pass.abc", "bad-time-budget", "io")
          .msg("pass.abc: time_budget_ms must be a non-negative integer, got '{}'", time_budget_s)
          .fatal();
      return;
    }
  }
  uint64_t small_ge = 0;
  {
    auto* b      = small_ge_s.data();
    auto* e      = small_ge_s.data() + small_ge_s.size();
    auto [p, ec] = std::from_chars(b, e, small_ge);
    if (ec != std::errc{} || p != e) {
      livehd::diag::err("pass.abc", "bad-small-ge", "io")
          .msg("pass.abc: small_ge must be a non-negative integer, got '{}'", small_ge_s)
          .fatal();
      return;
    }
  }
  uint64_t small_min_ge = 0;
  {
    auto* b      = small_min_ge_s.data();
    auto* e      = small_min_ge_s.data() + small_min_ge_s.size();
    auto [p, ec] = std::from_chars(b, e, small_min_ge);
    if (ec != std::errc{} || p != e) {
      livehd::diag::err("pass.abc", "bad-small-min-ge", "io")
          .msg("pass.abc: small_min_ge must be a non-negative integer, got '{}'", small_min_ge_s)
          .fatal();
      return;
    }
  }
  if (small_ge != 0 && small_min_ge > small_ge) {
    livehd::diag::err("pass.abc", "bad-small-ge-range", "io")
        .msg("pass.abc: small_min_ge ({}) must not exceed small_ge ({})", small_min_ge, small_ge)
        .fatal();
    return;
  }
  uint64_t large_ge = 0;
  {
    auto* b      = large_ge_s.data();
    auto* e      = large_ge_s.data() + large_ge_s.size();
    auto [p, ec] = std::from_chars(b, e, large_ge);
    if (ec != std::errc{} || p != e) {
      livehd::diag::err("pass.abc", "bad-large-ge", "io")
          .msg("pass.abc: large_ge must be a non-negative integer, got '{}'", large_ge_s)
          .fatal();
      return;
    }
  }
  // No silent fallback: a mis-typed cap would quietly change every mapped
  // netlist's fanout and, through it, every reported delay.
  uint64_t max_fanout = 16;
  {
    const auto s_mf = std::string{var.get("max_fanout", "16")};
    auto*      b    = s_mf.data();
    auto*      e    = s_mf.data() + s_mf.size();
    auto [p, ec]    = std::from_chars(b, e, max_fanout);
    // The RANGE check is part of "no silent fallback": Map_options::max_fanout is
    // a uint32_t, so an out-of-range value would truncate -- 2^32 lands on 0,
    // which silently means "no fanout cap at all", the exact opposite of what was
    // asked for.
    if (ec != std::errc{} || p != e || max_fanout > std::numeric_limits<uint32_t>::max()) {
      livehd::diag::err("pass.abc", "bad-max-fanout", "io")
          .msg("pass.abc: max_fanout must be an integer in [0, {}], got '{}'", std::numeric_limits<uint32_t>::max(), s_mf)
          .hint("0 disables the `buffer -N` tail on the built-in flow")
          .fatal();
      return;
    }
  }
  uint64_t register_max_bits = 4096;
  {
    auto* b      = register_max_bits_s.data();
    auto* e      = register_max_bits_s.data() + register_max_bits_s.size();
    auto [p, ec] = std::from_chars(b, e, register_max_bits);
    if (ec != std::errc{} || p != e) {
      livehd::diag::err("pass.abc", "bad-register-max-bits", "io")
          .msg("pass.abc: register_max_bits must be a non-negative integer, got '{}'", register_max_bits_s)
          .fatal();
      return;
    }
  }
  int block_size = 0;
  {
    auto* b      = bs_s.data();
    auto* e      = bs_s.data() + bs_s.size();
    auto [p, ec] = std::from_chars(b, e, block_size);
    if (ec != std::errc{} || p != e || block_size < 0) {
      livehd::diag::err("pass.abc", "bad-block-size", "io")
          .msg("pass.abc: block_size must be a non-negative integer, got '{}'", bs_s)
          .fatal();
      return;
    }
  }

  livehd::abc::Map_options opts;
  opts.flow              = flow;
  opts.max_fanout        = static_cast<uint32_t>(max_fanout);
  opts.small_flow        = small_flow;
  opts.small_min_ge      = small_min_ge;
  opts.small_ge          = small_ge;
  opts.large_flow        = large_flow;
  opts.large_ge          = large_ge;
  opts.map_register      = map_register;
  opts.map_memory        = map_memory;
  opts.register_max_bits = register_max_bits;
  opts.dff_cell          = std::string{var.get("dff_cell", "")};
  opts.delay             = delay;
  opts.load              = load;
  opts.verbose           = verbose;
  opts.adder             = adder.value();
  opts.block_size        = block_size;
  opts.multiplier        = multiplier.value();
  opts.memory_budget_mb  = memory_budget_mb;
  opts.time_budget_ms    = time_budget_ms;
  opts.allow_oversize    = allow_oversize;
  if (allow_oversize) {
    // Loud on purpose: this is the flag that lets a run take the machine down,
    // so it must be visible in the log of whatever ran afterwards.
    livehd::diag::warn("pass.abc", "allow-oversize", "unsupported")
        .msg("pass.abc.allow_oversize=true: memory admission is DISABLED for every region")
        .hint("an oversize region can exhaust physical memory and be killed by the OS")
        .emit();
  }

  if (out.empty()) {
    // Stats-only (no --emit-dir): no Liberty needed.
    opts.library = library;
    livehd::abc::report_stats(occurrence_graphs, top, opts);
    return;
  }

  // `--top` is optional for `lhd pass abc`, and build_decomposition resolves an
  // empty one into its OWN local copy (pass_partition.cpp resolve_order) — the
  // caller's `top` is never written back. Resolve it here by the same rule
  // (first non-null resolve_graphs entry, which is why the top is pushed first
  // above), or emit_qor seeds physical_instances with "" , matches no def, and
  // reports 0 physical gates / 0 instances for every region. It also fills in
  // qor.json's "top" field and the module name in the refusal hints below.
  if (top.empty()) {
    for (const auto& g : resolve_graphs) {
      if (g) {
        top = std::string{g->get_name()};
        break;
      }
    }
  }

  // Size gate. When ABC is about to inline the WHOLE hierarchy and bit-blast it
  // as a single unit, refuse a very large design up front. The per-region RSS
  // admission (Mapper::over_budget) only samples DURING bit-blast; the
  // whole-design flatten + partition that precedes it is unsampled, and that is
  // where a huge design first exhausts memory (a flat XSCore run reached 221 GB).
  // Only meaningful for whole-design flatten -- the per-def path bit-blasts small
  // units, each already guarded region-by-region, so the aggregate count there
  // would false-refuse a run that is actually fine.
  if (const uint64_t threshold = livehd::graph_util::large_design_node_threshold(); !allow_oversize && threshold != UINT64_MAX) {
    std::unordered_map<hhds::Gid, hhds::Graph*> gid2graph;
    hhds::Graph*                                top_g = nullptr;
    for (const auto& g : resolve_graphs) {
      if (!g) {
        continue;
      }
      gid2graph[g->get_gid()] = g.get();
      if (top_g == nullptr && (top.empty() || g->get_name() == top)) {
        top_g = g.get();
      }
    }
    if (top_g != nullptr && livehd::partition::flatten_is_whole_design(top_g, flatten)) {
      const uint64_t nodes = livehd::graph_util::flat_node_count(top_g, [&](hhds::Gid gid) -> hhds::Graph* {
        auto it = gid2graph.find(gid);
        return it == gid2graph.end() ? nullptr : it->second;
      });
      if (nodes > threshold) {
        livehd::diag::err("pass.abc", "large-design", "unsupported")
            .msg("refusing to synthesize a very large flattened design as one unit: {} nodes (over {})", nodes, threshold)
            .hint(std::format("color into smaller regions and synthesize per-region: "
                              "`lhd pass color synth --top {} lg:... --stats`",
                              top))
            .hint(
                "--set pass.abc.allow_oversize=true synthesizes it anyway -- it may exhaust the machine "
                "(a whole-design XSCore run reached 221 GB before the OS killed it)")
            .fatal();
      }
    }
  }

  if (library.empty()) {
    library = default_library();
  }
  if (library.empty()) {
    livehd::diag::err("pass.abc", "no-library", "unsupported")
        .msg("pass.abc needs a Liberty file: set --set pass.abc.library=<file.lib> (or export HAGENT_TECH_DIR)")
        .fatal();
    return;
  }
  opts.library = library;

  // memory=true: bit-blast every Memory into native flops + comb BEFORE
  // partitioning, so the normal flow tech-maps the resulting muxes/flops. Deleted
  // Memory nodes never reach the boundary code; any memory left native (an
  // unsupported shape) still cuts as a boundary (the memory=false behavior).
  if (map_memory) {
    // Whole scratch library, not just the `var.graphs`-named subset: flatten
    // inlines closure-only callees into the top, so a Memory left native inside
    // one would reach the mapper as a boundary in a memory=true run.
    livehd::abc::lower_memories(scratch_graphs);
  }

  auto& outlib = livehd::Hhds_graph_library::instance(out);

  // Incremental region cache (2opt-incr A+C). ON by default (`lhd.incremental`),
  // but only WITH a place to live: the kernel points cache_dir at
  // <workdir>/abc_cache exactly when the user passed --workdir (the formal-cache
  // convention -- a fabricated scratch workdir would start cold every run and
  // cache into a dir about to vanish). Constructed before the mapper so a salt
  // mismatch (edited Liberty, different mapping mode) starts cold before any
  // region is digested. The out dir is wiped by the kernel every run, so a cache
  // living inside it would self-destruct -- refuse the overlap.
  auto                                     cache_dir = std::string{var.get("cache_dir", "")};
  std::unique_ptr<livehd::abc::Incr_cache> incr;
  if (!cache_dir.empty()) {
    std::error_code ec;
    const auto      canon_cache = std::filesystem::weakly_canonical(cache_dir, ec);
    const auto      canon_out   = std::filesystem::weakly_canonical(out, ec);
    if (canon_cache == canon_out) {
      livehd::diag::err("pass.abc", "cache-dir", "io")
          .msg("pass.abc: cache directory '{}' must differ from the --emit-dir lg: output (the output is purged every run)",
               cache_dir)
          .fatal();
      return;
    }
    incr = std::make_unique<livehd::abc::Incr_cache>(
        cache_dir,
        livehd::abc::Incr_cache::make_salt(opts.library, opts.map_register, opts.map_memory, opts.dff_cell));
  }

  // A whole-design flatten maps ONE region and its netlist must hold exactly one
  // module; the mapper drops its shared helper defs in that mode (set_flat).
  bool flat_whole_design = false;
  for (const auto& g : resolve_graphs) {
    if (!g || (!top.empty() && g->get_name() != top)) {
      continue;
    }
    flat_whole_design = livehd::partition::flatten_is_whole_design(g.get(), flatten);
    break;
  }

  livehd::abc::Mapper mapper(opts);
  mapper.set_outlib(&outlib);
  mapper.set_flat(flat_whole_design);
  mapper.set_region_opts(std::move(region_opts));
  if (incr) {
    mapper.set_incr(incr.get());
  }
  bool dbg = false;
  Pass_partition::build_decomposition(
      resolve_graphs,
      &outlib,
      top,
      dbg,
      [&mapper](const livehd::partition::Region_body& rb) { mapper.map_region(rb); },
      flatten,
      /*want_pre_bodies=*/mapper.incremental());

  mapper.stop();  // no-op for an all-hit incremental run

  // Instantiation counts from the netlist that was just emitted (see Abc_hier).
  Abc_hier hier;
  for (const auto gid : outlib.all_gids()) {
    auto g = outlib.get_graph(gid);
    if (!g) {
      continue;
    }
    auto& kids = hier.children[std::string(g->get_name())];
    for (auto n : g->body().nodes()) {
      if (livehd::graph_util::type_op_of(n) != Ntype_op::Sub) {
        continue;
      }
      if (auto cio = n.get_subnode_io(); cio != nullptr) {
        ++kids[std::string(cio->get_name())];
      } else if (auto child = n.get_subnode_graph(); child != nullptr) {
        ++kids[std::string(child->get_name())];
      }
    }
  }

  // Memory admission (2opt-incr subtask 0). Raised HERE, not from map_region:
  // .fatal() throws, and build_decomposition's callback runs above stop(), so
  // throwing from the region would skip Abc_Stop and leak the frame plus every
  // live network -- the opposite of what a memory guard should do.
  if (const auto* refusal = mapper.admission_refusal()) {
    livehd::diag::err("pass.abc", "memory-oversize", "unsupported")
        .msg("{}", *refusal)
        .hint(std::format("re-color into SMALLER regions with a tighter size window, then check them first: "
                          "`lhd pass color synth --top {} lg:... --set color.max_ge=<smaller> --stats` "
                          "(the region-splitting ceiling; lower it until the region fits). Under "
                          "`--set color.synth_alg=cones` the knob is `color.max_gate` instead -- max_ge does not "
                          "shape a cones coloring",
                          top))
        .hint("--set pass.abc.memory_budget_mb=N pins the ceiling explicitly (reproducible hosts, CI)")
        .hint(
            "--set pass.abc.allow_oversize=true runs it anyway -- it may exhaust the machine (a whole-design "
            "XSCore run reached 221 GB before the OS killed it)")
        .fatal();
  }

  if (incr) {
    // Persist before reporting: a crash between the two loses a line of text,
    // not the snapshot work. save() is a no-op when nothing was stored.
    incr->save();
    std::print("pass.abc cache: {} hit(s), {} miss(es) ({})\n", incr->hits(), incr->misses(), incr->dir());
  }

  emit_qor(mapper.qor(), top, opts, qor_path, incr.get(), mapper.abc_started(), hier);
  if (const auto* refusal = mapper.time_refusal()) {
    livehd::diag::err("pass.abc", "color-time-oversize", "unsupported")
        .msg("{}", *refusal)
        .hint(std::format("re-color into more, smaller regions: `lhd pass color synth --top {} lg:... "
                          "--set color.max_ge=<smaller>` (or `--set color.max_gate=<smaller>` when the coloring used "
                          "synth_alg=cones); full/cold may take longer, warm runs should reuse the extra colors",
                          top))
        .fatal();
  }
}
