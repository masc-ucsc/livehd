// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_abc.hpp"

#include <cstdlib>
#include <string>

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
  m.add_label_optional("flow", "ABC command string (empty => built-in comb/seq default)", "");
  m.add_label_optional("seq", "true|false sequential vs combinational", "false");
  m.add_label_optional("delay", "{D} substitution in flow", "");
  m.add_label_optional("load", "{L} substitution in flow", "");
  m.add_label_optional("verbose", "per-module ABC stats", "false");
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
  bool seq     = truthy(var.get("seq", "false"));
  auto delay   = std::string{var.get("delay", "")};
  auto load    = std::string{var.get("load", "")};
  bool verbose = truthy(var.get("verbose", "false"));

  if (seq) {
    // Sequential mapping (flops -> ABC latches, name preservation, dretime) is a
    // separate subtask; comb mapping (default) lands first. Fail loudly rather
    // than silently mishandling flops.
    livehd::diag::err("pass.abc", "seq-unsupported", "unsupported")
        .msg("pass.abc seq=true is not implemented yet; use the default combinational mapping (seq=false)")
        .fatal();
    return;
  }
  livehd::abc::Map_options opts;
  opts.flow    = flow;
  opts.seq     = seq;
  opts.delay   = delay;
  opts.load    = load;
  opts.verbose = verbose;

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
  Pass_partition::build_decomposition(var.graphs, &outlib, top, dbg,
                                      [&mapper](const livehd::partition::Region_body& rb) { mapper.map_region(rb); });

  mapper.stop();
}
