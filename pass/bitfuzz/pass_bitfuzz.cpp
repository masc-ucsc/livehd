//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_bitfuzz.hpp"

#include <format>
#include <string>

#include "bitfuzz.hpp"
#include "diag.hpp"
#include "str_tools.hpp"

namespace {

[[nodiscard]] bool opt_bool(std::string_view v, bool dflt) {
  if (v.empty()) {
    return dflt;
  }
  return v == "true" || v == "1" || v == "on";
}

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
    livehd::diag::err("pass.bitfuzz", "bad-option", "io")
        .msg("{}:{} out of range [{}..{}]", label, i, lo, hi)
        .fatal();
    return dflt;
  }
  return i;
}

}  // namespace

static Pass_plugin bitfuzz_plugin("pass_bitfuzz", Pass_bitfuzz::setup);

Pass_bitfuzz::Pass_bitfuzz(const Eprp_var& var) : Pass("pass.bitfuzz", var) {
  const auto mode_s = std::string(var.get("mode", "wires"));
  if (!livehd::bitfuzz::mode_from_string(mode_s, &opts.mode)) {
    livehd::diag::err("pass.bitfuzz", "bad-option", "io")
        .msg("mode:{} is not one of off|wires|all", mode_s)
        .fatal();
    return;
  }
  opts.seed           = static_cast<uint64_t>(opt_int(var.get("seed", ""), 0, 0, 1 << 30, "seed"));
  opts.reg_pct        = opt_int(var.get("reg_pct", ""), 100, 0, 100, "reg_pct");
  opts.max_iterations = opt_int(var.get("max_iterations", ""), 10, 1, 100, "max_iterations");
  opts.repair         = opt_bool(var.get("repair", ""), true);
  opts.verbose        = opt_bool(var.get("verbose", ""), false);
}

void Pass_bitfuzz::setup() {
  Eprp_method m("pass.bitfuzz",
                "Strip per-pin bits/sign annotations and make bitwidth inference recover them (verification canary; "
                "never on the synthesis path)",
                &Pass_bitfuzz::work);
  m.add_label_optional("mode", "off | wires (default) | all (also clears register q pins)", "wires");
  m.add_label_optional("seed", "seed for the register subset selection in mode=all", "0");
  m.add_label_optional("reg_pct", "percent of register q pins to clear in mode=all", "100");
  m.add_label_optional("max_iterations", "bitwidth inference iteration budget per re-run", "10");
  m.add_label_optional("repair", "restore the annotation on pins inference could not bound", "true");
  m.add_label_optional("verbose", "list every disagreeing pin, not just the summary", "false");
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
    const auto st   = livehd::bitfuzz::fuzz(g, p.opts);
    const auto gio  = g->get_io();
    const auto name = gio ? std::string{gio->get_name()} : std::string{"<anon>"};
    if (st.cleared == 0) {
      continue;
    }

    livehd::diag::info("pass.bitfuzz", "bitfuzz-summary", "progress")
        .msg("bitfuzz[{}] on `{}`: cleared {} pin(s) ({} state); recovered {} identical, {} narrower, {} wider, {} "
             "sign-changed, {} unrecovered ({} with no inference rule), {} repaired in {} round(s), {} const-collapsed",
             livehd::bitfuzz::mode_name(p.opts.mode), name, st.cleared, st.cleared_state, st.same, st.narrower,
             st.wider, st.sign_changed, st.unrecovered, st.no_rule, st.repaired, st.repair_rounds, st.vanished)
        .emit();

    // A WIDER recovery is the target bug class: inference, which sees only
    // explicit cells, needs more bits than the front end declared, so the
    // declaration was doing semantic work a Get_mask should have done.
    for (const auto& f : st.findings) {
      const bool interesting = f.kind == "wider" || f.kind == "sign"
                               || (f.kind == "unrecovered" && !f.no_rule);
      if (!interesting && !p.opts.verbose) {
        continue;
      }
      if (f.kind == "wider") {
        livehd::diag::warn("pass.bitfuzz", "bitfuzz-wider", "bitwidth")
            .msg("`{}` ({}) was declared {} bits but inference recovered {}: the declaration was truncating -- a "
                 "Get_mask is missing",
                 f.pin, f.op, f.was_bits, f.now_bits)
            .hint("the narrowing must be an explicit cell; a bits attribute has no meaning outside cgen")
            .emit();
      } else if (f.kind == "unrecovered" && !f.no_rule) {
        livehd::diag::warn("pass.bitfuzz", "bitfuzz-unrecovered", "bitwidth")
            .msg("`{}` ({}) declared {} bits could not be re-inferred{}{}", f.pin, f.op, f.was_bits,
                 f.is_state ? " from its input cone (register width is not recoverable)" : "",
                 f.cyclic ? " (part of a combinational ring where every cell is blocked by another)" : "")
            .hint("the annotation was restored; either the width is a real contract that needs an explicit wrap, or "
                  "bitwidth is missing a rule")
            .emit();
      } else if (f.kind == "sign") {
        livehd::diag::warn("pass.bitfuzz", "bitfuzz-sign", "bitwidth")
            .msg("`{}` ({}) was declared {} but inference recovered {} at the same {} bits", f.pin, f.op,
                 f.was_unsign ? "unsigned" : "signed", f.now_unsign ? "unsigned" : "signed", f.was_bits)
            .emit();
      } else if (p.opts.verbose) {
        livehd::diag::info("pass.bitfuzz", "bitfuzz-detail", "progress")
            .msg("`{}` ({}) {}: {} -> {} bits", f.pin, f.op, f.kind, f.was_bits, f.now_bits)
            .emit();
      }
    }
  }
}
