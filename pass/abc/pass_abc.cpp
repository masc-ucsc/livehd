// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_abc.hpp"

#include <charconv>
#include <cstdlib>
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
  m.add_label_optional("top", "top module whose coloring is mapped", "");
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

  auto adder = livehd::abc::arith::parse_adder_kind(adder_s);
  if (!adder.has_value()) {
    livehd::diag::err("pass.abc", "bad-adder", "io").msg("pass.abc: unknown adder '{}' (use rca|cska|cla)", adder_s).fatal();
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
  if (!mapper.start()) {
    return;  // diag already emitted
  }

  bool dbg = false;
  Pass_partition::build_decomposition(var.graphs, &outlib, top, dbg, [&mapper](const livehd::partition::Region_body& rb) {
    mapper.map_region(rb);
  });

  mapper.stop();
}
