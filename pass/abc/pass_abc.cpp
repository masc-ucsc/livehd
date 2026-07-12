// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_abc.hpp"

#include <charconv>
#include <cstdlib>
#include <format>
#include <fstream>
#include <print>
#include <string>
#include <system_error>

#include "abc_map.hpp"
#include "diag.hpp"
#include "graph_library_singleton.hpp"
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
  m.add_label_optional("seq", "true|false sequential vs combinational", "true");
  m.add_label_optional("delay", "{D} substitution in flow", "");
  m.add_label_optional("load", "{L} substitution in flow", "");
  m.add_label_optional("verbose", "per-module ABC stats", "false");
  m.add_label_optional("adder", "combinational adder architecture for Sum/comparators: rca|cska|cla", "rca");
  m.add_label_optional("block_size", "CSKA skip-block / CLA lookahead-group width (0 => auto: W/4|W/2|W)", "0");
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
              const std::string& qor_path) {
  int    tgates = 0;
  double tarea  = 0.0;
  int    worst  = -1;  // index of the region with the worst delay
  for (size_t r = 0; r < qor.size(); ++r) {
    tgates += qor[r].gates;
    tarea += qor[r].area;
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
  std::print("pass.abc qor: {} region(s), {} gates, area {:.2f}{}\n", qor.size(), tgates, tarea, crit);

  if (qor_path.empty()) {
    return;
  }
  std::string j = "{";
  j += "\"schema_version\":1,\"kind\":\"abc-map\",";
  j += std::format("\"top\":\"{}\",", jesc(top));
  j += std::format("\"library\":\"{}\",", jesc(opts.library));
  j += std::format("\"seq\":{},", opts.seq ? "true" : "false");
  j += std::format("\"delay_target\":\"{}\",", jesc(opts.delay));
  j += std::format("\"total\":{{\"regions\":{},\"gates\":{},\"area\":{:.4f}", qor.size(), tgates, tarea);
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
  j += "},\"regions\":[";
  for (size_t r = 0; r < qor.size(); ++r) {
    const auto& q = qor[r];
    if (r != 0) {
      j += ",";
    }
    j += std::format("{{\"module\":\"{}\",\"color\":{},\"gates\":{},\"area\":{:.4f}", jesc(q.module), q.color, q.gates, q.area);
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
  bool seq     = truthy(var.get("seq", "true"));
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
  opts.seq        = seq;
  opts.delay      = delay;
  opts.load       = load;
  opts.verbose    = verbose;
  opts.adder      = adder.value();
  opts.block_size = block_size;
  opts.multiplier = multiplier.value();
  opts.use_proven_assume = use_proven_assume;
  opts.use_all_assume    = use_all_assume;

  if (out.empty()) {
    // Stats-only (no --emit-dir): no Liberty needed.
    opts.library = library;
    livehd::abc::report_stats(var.graphs, top, opts);
    return;
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

  auto& outlib = livehd::Hhds_graph_library::instance(out);

  livehd::abc::Mapper mapper(opts);
  mapper.set_outlib(&outlib);
  mapper.set_region_opts(std::move(region_opts));
  if (!mapper.start()) {
    return;  // diag already emitted
  }

  bool dbg = false;
  Pass_partition::build_decomposition(var.graphs, &outlib, top, dbg, [&mapper](const livehd::partition::Region_body& rb) {
    mapper.map_region(rb);
  });

  mapper.stop();

  emit_qor(mapper.qor(), top, opts, qor_path);
}
