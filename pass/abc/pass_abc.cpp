// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_abc.hpp"

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <print>
#include <string>
#include <system_error>
#include <unordered_map>

#include "abc_incr.hpp"
#include "abc_map.hpp"
#include "diag.hpp"
#include "graph_library_singleton.hpp"
#include "mem_lower.hpp"
#include "node_util.hpp"
#include "pass_partition.hpp"

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
  m.add_label_optional("register",
                       "true|false map flops to Liberty DFF cells (true, falls back to native flops when the library has no "
                       "DFF cell) vs keep them native as `always @(posedge)` (false)",
                       "true");
  m.add_label_optional("memory",
                       "true|false bit-blast a Memory into a DFF-cell array + read/write mux logic (true) vs keep it as a "
                       "native memory instance (false)",
                       "false");
  m.add_label_optional("dff_cell",
                       "explicit Liberty DFF cell name for register=true (empty => auto-detect a plain posedge D-flop)", "");
  m.add_label_optional("delay", "{D} substitution in flow", "");
  m.add_label_optional("load", "{L} substitution in flow", "");
  m.add_label_optional("verbose", "per-module ABC stats", "false");
  m.add_label_optional("adder", "combinational adder architecture for Sum/comparators: rca|cska|cla", "rca");
  m.add_label_optional("block_size", "CSKA skip-block / CLA lookahead-group width (0 => auto: W/4|W/2|W)", "0");
  m.add_label_optional("memory_budget_mb",
                       "memory-admission ceiling (total process RSS, MiB) for one ABC region; "
                       "0 => physical RAM minus max(2 GiB, 20%) of OS reserve. Physical only, never swap",
                       "0");
  m.add_label_optional("allow_oversize",
                       "true|false skip memory admission and map the region regardless. It may exhaust "
                       "physical memory and be killed by the OS",
                       "false");
  m.add_label_optional("multiplier", "combinational multiplier architecture for Mult: array (partial-product adds use 'adder')",
                       "array");
  m.add_label_optional("use_proven_assume", "true|false feed pass.formal-PROVEN assume conditions to ABC as don't-cares",
                       "true");
  m.add_label_optional("use_all_assume",
                       "true|false also feed DECLARED (unproven) assume conditions (aggressive: undefined outside contract)",
                       "false");
  m.add_label_optional("qor",
                       "write per-region + total QoR JSON (mapped gates/area/critical delay, source-attributed) to this file "
                       "(`lhd pass abc` defaults it to <workdir>/qor.json when --workdir is set)",
                       "");
  m.add_label_optional("cache",
                       "true|false INCREMENTAL synthesis (2opt-incr): keep a persistent cache of previously mapped "
                       "regions under --workdir (abc_cache/), content-addressed by a canonical region digest. A "
                       "region whose logic, boundary and resolved ABC recipe are unchanged since a prior run is "
                       "cloned from the cache instead of re-running ABC -- a small RTL edit then re-synthesizes only "
                       "the regions it touched. Salted by the Liberty content and the register/memory mapping mode. "
                       "Only active with a user --workdir (a scratch dir would start cold every run) -- the "
                       "formal.cache convention",
                       "true");
  m.add_label_optional("cache_dir",
                       "INTERNAL kernel plumbing: the cache directory, always <workdir>/abc_cache (set after user "
                       "--set merging, so it is not customizable). Empty = no workdir = no cache",
                       "");
  m.add_label_optional("flatten",
                       "auto|true|false whole-design flatten: inline the instance hierarchy and map the flat design as "
                       "one region (auto = flatten exactly when the active coloring is `pass.color flat`); the result "
                       "is a single netlist module named after the top",
                       "auto");
  m.add_label_optional(
      "region_opts",
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
      case '"': out += "\\\""; break;
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
void emit_qor(const std::vector<livehd::abc::Region_qor>& qor, std::string_view top, const livehd::abc::Map_options& opts,
              const std::string& qor_path, const livehd::abc::Incr_cache* incr) {
  int    tgates = 0;
  double tarea  = 0.0;
  int    tdivbb = 0;  // blackboxed div/mod cones (the score under-reports)
  int    worst  = -1;  // index of the region with the worst delay
  for (size_t r = 0; r < qor.size(); ++r) {
    tgates += qor[r].gates;
    tarea += qor[r].area;
    tdivbb += qor[r].div_blackbox;
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
  std::print("pass.abc qor: {} region(s), {} gates, area {:.2f}{}{}\n", qor.size(), tgates, tarea, crit,
             tdivbb == 0 ? std::string{} : std::format(" [PARTIAL: {} blackboxed div/mod cone(s) unscored]", tdivbb));

  if (qor_path.empty()) {
    return;
  }
  std::string j = "{";
  j += "\"schema_version\":1,\"kind\":\"abc-map\",";
  j += std::format("\"top\":\"{}\",", jesc(top));
  j += std::format("\"library\":\"{}\",", jesc(opts.library));
  j += std::format("\"register\":{},\"memory\":{},", opts.map_register ? "true" : "false", opts.map_memory ? "true" : "false");
  j += std::format("\"delay_target\":\"{}\",", jesc(opts.delay));
  j += std::format("\"total\":{{\"regions\":{},\"gates\":{},\"area\":{:.4f}", qor.size(), tgates, tarea);
  if (tdivbb > 0) {
    j += std::format(",\"div_blackbox\":{}", tdivbb);
  }
  if (worst >= 0) {
    const auto& w = qor[static_cast<size_t>(worst)];
    j += std::format(",\"max_delay\":{:.4f},\"critical_region\":\"{}\"", w.delay, jesc(w.module));
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
    j += std::format(",\"incremental\":{{\"hits\":{},\"misses\":{},\"stored\":{},\"uncacheable\":{}}}",
                     incr->hits(),
                     incr->misses(),
                     incr->stores(),
                     incr->refused());
  }
  j += ",\"regions\":[";
  for (size_t r = 0; r < qor.size(); ++r) {
    const auto& q = qor[r];
    if (r != 0) {
      j += ",";
    }
    j += std::format("{{\"module\":\"{}\",\"color\":{},\"gates\":{},\"area\":{:.4f}", jesc(q.module), q.color, q.gates, q.area);
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
  auto top     = std::string{var.get("top", "")};
  auto out     = std::string{var.get("out", "")};
  auto library = std::string{var.get("library", "")};
  auto flow    = std::string{var.get("flow", "")};
  bool map_register = truthy(var.get("register", "true"));
  bool map_memory   = truthy(var.get("memory", "false"));
  auto delay   = std::string{var.get("delay", "")};
  auto load    = std::string{var.get("load", "")};
  bool verbose = truthy(var.get("verbose", "false"));
  auto adder_s = std::string{var.get("adder", "rca")};
  auto bs_s    = std::string{var.get("block_size", "0")};
  auto mult_s  = std::string{var.get("multiplier", "array")};
  bool use_proven_assume = truthy(var.get("use_proven_assume", "true"));
  bool use_all_assume    = truthy(var.get("use_all_assume", "false"));
  auto qor_path          = std::string{var.get("qor", "")};
  auto region_opts_s     = std::string{var.get("region_opts", "")};
  auto mem_budget_s      = std::string{var.get("memory_budget_mb", "0")};
  bool allow_oversize    = truthy(var.get("allow_oversize", "false"));
  auto flatten           = livehd::partition::parse_flatten_mode(var.get("flatten", "auto"), "pass.abc");

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
  opts.flow       = flow;
  opts.map_register = map_register;
  opts.map_memory   = map_memory;
  opts.dff_cell     = std::string{var.get("dff_cell", "")};
  opts.delay      = delay;
  opts.load       = load;
  opts.verbose    = verbose;
  opts.adder      = adder.value();
  opts.block_size = block_size;
  opts.multiplier = multiplier.value();
  opts.use_proven_assume = use_proven_assume;
  opts.use_all_assume    = use_all_assume;
  opts.memory_budget_mb  = memory_budget_mb;
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
    livehd::abc::report_stats(var.graphs, top, opts);
    return;
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
    for (const auto& g : var.graphs) {
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
            .hint("--set pass.abc.allow_oversize=true synthesizes it anyway -- it may exhaust the machine "
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
    livehd::abc::lower_memories(var.graphs);
  }

  auto& outlib = livehd::Hhds_graph_library::instance(out);

  // Incremental region cache (2opt-incr A+C). ON by default, but only WITH a
  // place to live: the kernel points cache_dir at <workdir>/abc_cache exactly
  // when the user passed --workdir (the formal.cache convention -- a fabricated
  // scratch workdir would start cold every run and cache into a dir about to
  // vanish). Constructed before the mapper so a salt mismatch (edited Liberty,
  // different mapping mode) starts cold before any region is digested. The out
  // dir is wiped by the kernel every run, so a cache living inside it would
  // self-destruct -- refuse the overlap.
  auto cache_dir = std::string{var.get("cache_dir", "")};
  if (!truthy(var.get("cache", "true"))) {
    cache_dir.clear();
  }
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
        livehd::abc::Incr_cache::make_salt(opts.library, opts.map_register, opts.map_memory, opts.dff_cell,
                                           opts.use_proven_assume, opts.use_all_assume));
  }

  livehd::abc::Mapper mapper(opts);
  mapper.set_outlib(&outlib);
  mapper.set_region_opts(std::move(region_opts));
  if (incr) {
    mapper.set_incr(incr.get());
  }
  if (!mapper.start()) {
    return;  // diag already emitted
  }

  bool dbg = false;
  Pass_partition::build_decomposition(
      var.graphs,
      &outlib,
      top,
      dbg,
      [&mapper](const livehd::partition::Region_body& rb) { mapper.map_region(rb); },
      flatten);

  mapper.stop();

  // Memory admission (2opt-incr subtask 0). Raised HERE, not from map_region:
  // .fatal() throws, and build_decomposition's callback runs above stop(), so
  // throwing from the region would skip Abc_Stop and leak the frame plus every
  // live network -- the opposite of what a memory guard should do.
  if (const auto* refusal = mapper.admission_refusal()) {
    livehd::diag::err("pass.abc", "memory-oversize", "unsupported")
        .msg("{}", *refusal)
        .hint(std::format("re-color into smaller regions and check them first: "
                          "`lhd pass color synth --top {} lg:... --stats`",
                          top))
        .hint("--set pass.abc.memory_budget_mb=N pins the ceiling explicitly (reproducible hosts, CI)")
        .hint("--set pass.abc.allow_oversize=true runs it anyway -- it may exhaust the machine (a whole-design "
              "XSCore run reached 221 GB before the OS killed it)")
        .fatal();
  }

  if (incr) {
    // Persist before reporting: a crash between the two loses a line of text,
    // not the snapshot work. save() is a no-op when nothing was stored.
    incr->save();
    std::print("pass.abc cache: {} hit(s), {} miss(es), {} stored{} ({})\n",
               incr->hits(),
               incr->misses(),
               incr->stores(),
               incr->refused() == 0 ? std::string{} : std::format(", {} uncacheable", incr->refused()),
               incr->dir());
  }

  emit_qor(mapper.qor(), top, opts, qor_path, incr.get());
}
