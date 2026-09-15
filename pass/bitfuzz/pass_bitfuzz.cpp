//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_bitfuzz.hpp"

#include <format>
#include <string>

#include "bitfuzz.hpp"
#include "diag.hpp"
#include "str_tools.hpp"

namespace {

[[nodiscard]] int opt_int(std::string_view v, int dflt, int lo, int hi, std::string_view label) {
  if (v.empty()) {
    return dflt;
  }
  if (!str_tools::is_i(v)) {
    livehd::diag::err("pass.bitfuzz", "bad-option", "io").msg("{}:{} is not an integer", label, v).fatal();
    return dflt;
  }
  const int i = str_tools::to_i(v);
  if (i < lo || i > hi) {
    livehd::diag::err("pass.bitfuzz", "bad-option", "io").msg("{}:{} out of range [{}..{}]", label, i, lo, hi).fatal();
    return dflt;
  }
  return i;
}

}  // namespace

static Pass_plugin bitfuzz_plugin("pass_bitfuzz", Pass_bitfuzz::setup);

Pass_bitfuzz::Pass_bitfuzz(const Eprp_var& var) : Pass("pass.bitfuzz", var) {
  const auto mode_s = std::string(var.get("mode", "wires"));
  if (!livehd::bitfuzz::mode_from_string(mode_s, &opts.mode)) {
    livehd::diag::err("pass.bitfuzz", "bad-option", "io").msg("mode:{} is not one of off|wires|all", mode_s).fatal();
    return;
  }
  opts.seed    = static_cast<uint64_t>(opt_int(var.get("seed", ""), 0, 0, 1 << 30, "seed"));
  opts.reg_pct = opt_int(var.get("reg_pct", ""), 100, 0, 100, "reg_pct");
}

void Pass_bitfuzz::setup() {
  Eprp_method m("pass.bitfuzz",
                "Strip per-pin bits/sign annotations for recovery by subsequent cprop/bitwidth passes",
                &Pass_bitfuzz::work);
  m.add_label_optional("mode", "off | wires (default) | all (also clears register q pins)", "wires");
  m.add_label_optional("seed", "seed for the register subset selection in mode=all", "0");
  m.add_label_optional("reg_pct", "percent of register q pins to clear in mode=all", "100");
  register_pass(m);
}

void Pass_bitfuzz::work(Eprp_var& var) {
  Pass_bitfuzz p(var);
  if (p.opts.mode == livehd::bitfuzz::Mode::Off) {
    return;
  }

  for (const auto& g : var.graphs) {
    if (!g) {
      continue;
    }
    const auto st   = livehd::bitfuzz::strip_annotations(g, p.opts);
    const auto gio  = g->get_io();
    const auto name = gio ? std::string{gio->get_name()} : std::string{"<anon>"};
    livehd::diag::info("pass.bitfuzz", "bitfuzz-summary", "progress")
        .msg("bitfuzz[{}] on `{}`: cleared {} pin(s) ({} state); recovery deferred to cprop/bitwidth",
             livehd::bitfuzz::mode_name(p.opts.mode),
             name,
             st.cleared,
             st.cleared_state)
        .emit();
  }
}
