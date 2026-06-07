//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_upass.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <memory>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "upass_attributes.hpp"  // NOLINT: ensures plugin "attributes" is linked
#include "upass_bitwidth.hpp"    // NOLINT: ensures plugin "bitwidth" is linked
#include "upass_constprop.hpp"
#include "upass_pipe.hpp"
#include "upass_runner.hpp"
#include "upass_semacheck.hpp"  // NOLINT: ensures plugin "semacheck" is linked
#include "upass_ssa.hpp"
#include "upass_tolg.hpp"
#include "upass_typecheck.hpp"  // NOLINT: ensures plugin "typecheck" is linked
#include "upass_verifier.hpp"

static Pass_plugin sample("pass.upass", Pass_upass::setup);

namespace {
std::vector<std::string> parse_order_csv(std::string_view txt_view) {
  std::string              txt(txt_view);
  std::vector<std::string> out;
  std::size_t              pos = 0;

  while (pos <= txt.size()) {
    auto next = txt.find(',', pos);
    auto part = txt.substr(pos, next == std::string::npos ? txt.size() - pos : next - pos);

    part.erase(part.begin(), std::find_if(part.begin(), part.end(), [](unsigned char c) { return !std::isspace(c); }));
    part.erase(std::find_if(part.rbegin(), part.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), part.end());

    if (!part.empty()) {
      out.emplace_back(std::move(part));
    }
    if (next == std::string::npos) {
      break;
    }
    pos = next + 1;
  }

  return out;
}

[[noreturn]] void fail_upass_runtime(std::string_view msg) {
  std::print("ERROR: {}\n", msg);
  throw std::runtime_error(std::string(msg));
}

// Parse a non-negative integer option. Returns -1 on absent/empty/invalid
// to match the "don't check" sentinel used by the verifier.
int parse_expected_count(const upass::Options_map& opts, std::string_view key) {
  auto it = opts.find(std::string{key});
  if (it == opts.end() || it->second.empty()) {
    return -1;
  }
  int         value = -1;
  const auto* begin = it->second.data();
  const auto* end   = begin + it->second.size();
  auto        r     = std::from_chars(begin, end, value);
  if (r.ec != std::errc() || r.ptr != end) {
    return -1;
  }
  return value;
}
}  // namespace

void Pass_upass::setup() {
  Eprp_method m1("pass.upass", "lnast micropass (upass) controller", &Pass_upass::work);
  m1.add_label_optional("verifier", "enable lnast verifier upass", "true");
  m1.add_label_optional("constprop", "enable constant propagation upass", "true");
  m1.add_label_optional("coalescer", "enable deferred-emit / DSE coalescer upass", "true");
  m1.add_label_optional("attributes", "enable Pyrope attribute upass (sticky propagation, comptime checks)", "true");
  m1.add_label_optional("bitwidth",
                        "enable LNAST bitwidth range inference (publishes max/min for lnast_to_lgraph; see upass/upass.md §2)",
                        "true");
  m1.add_label_optional("ssa",
                        "enable SSA normalisation: harvest I/O metadata into tree_io, expand I/O tuple nodes, "
                        "rename multi-assigned user variables to SSA-unique names",
                        "true");
  m1.add_label_optional("assert", "enable assert test", "true");
  m1.add_label_optional("func_extract", "enable func_extract upass (spawn helper lnasts for comb func_defs)", "true");
  m1.add_label_optional("typecheck", "enable typecheck upass (kind/operator/nil checks; runs after attributes)", "true");
  m1.add_label_optional("semacheck",
                        "enable semacheck upass (semantic legality: read-only-attr writes, declare-once/shadowing; "
                        "the user-facing checks formerly in pass.lnastfmt)",
                        "true");
  m1.add_label_optional("tolg",
                        "lower each fully-specified function tree to an LGraph (task 1l); leaves graphs on the pass var "
                        "for inou.cgen.verilog. Terminal step: runs after ssa+bitwidth. Default off.",
                        "false");
  m1.add_label_optional("toln",
                        "materialize the rewritten post-upass LNAST. toln:0 skips the whole staging build (emit_* "
                        "no-ops), the post-walk DCE, the coalescer, and the body swap when nothing downstream consumes "
                        "the LNAST (diagnostics-only runs, e.g. the LSP) — every pass still dispatches, so diagnostics "
                        "and io/bw side-channels are unchanged; forced back on while tolg:1 needs the rewritten tree. "
                        "Default on.",
                        "true");
  m1.add_label_optional(
      "order",
      "comma-separated upass names; overrides verifier/constprop/assert toggles (example: verifier,constprop,verifier)",
      "");
  m1.add_label_optional("inherit",
                        "inherit labels from prior pipeline stages (default true); false resets sticky upass options",
                        "true");
  m1.add_label_optional("verifier_pass",
                        "verifier: expected count of comptime-known-true casserts (omit or -1 to disable check)",
                        "");
  m1.add_label_optional("verifier_fail",
                        "verifier: expected count of comptime-known-false casserts (omit or -1 to disable check)",
                        "");
  m1.add_label_optional("verifier_include_funcs",
                        "verifier: also tally casserts inside func_extract-spawned function bodies (default false)",
                        "false");
  register_pass(m1);
}

Pass_upass::Pass_upass(const Eprp_var& var) : Pass("pass.upass", var) {
  auto inherit_txt = std::string(var.get_stage("inherit", "true"));
  std::transform(inherit_txt.begin(), inherit_txt.end(), inherit_txt.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  inherit_labels
      = !(inherit_txt.empty() || inherit_txt == "0" || inherit_txt == "false" || inherit_txt == "no" || inherit_txt == "off");

  const auto get_label = [&](std::string_view label, std::string_view default_value = "") -> std::string_view {
    if (inherit_labels) {
      return var.get(label, default_value);
    }
    return var.get_stage(label, default_value);
  };

  auto order_txt = get_label("order");
  if (!order_txt.empty()) {
    upass_order = parse_order_csv(order_txt);
  }

  auto vif_txt = std::string(get_label("verifier_include_funcs", "false"));
  std::transform(vif_txt.begin(), vif_txt.end(), vif_txt.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  verifier_include_funcs = !(vif_txt.empty() || vif_txt == "0" || vif_txt == "false" || vif_txt == "no" || vif_txt == "off");

  // Per-pass config forwarded to the runner. Pick up labels whose meaning
  // is pass-specific (as opposed to runner/order labels above).
  auto capture_opt = [&](std::string_view label) {
    auto v = std::string(get_label(label, ""));
    if (!v.empty()) {
      pass_options[std::string(label)] = std::move(v);
    }
  };
  capture_opt("verifier_pass");
  capture_opt("verifier_fail");
  capture_opt("coalescer");

  if (!upass_order.empty()) {
    return;
  }

  auto verifier_txt = get_label("verifier");
  bool do_verifier  = verifier_txt != "false" && verifier_txt != "0";

  auto assert_txt = get_label("assert");
  bool do_assert  = assert_txt != "false" && assert_txt != "0";

  auto constp_txt   = get_label("constprop");
  bool do_constprop = constp_txt != "false" && constp_txt != "0";

  auto coalescer_txt = get_label("coalescer");
  bool do_coalescer  = coalescer_txt != "false" && coalescer_txt != "0";

  auto attrs_txt = get_label("attributes");
  bool do_attrs  = attrs_txt != "false" && attrs_txt != "0";

  auto bw_txt      = get_label("bitwidth");
  bool do_bitwidth = bw_txt != "false" && bw_txt != "0";

  auto ssa_txt = get_label("ssa");
  bool do_ssa  = ssa_txt != "false" && ssa_txt != "0";
  run_ssa      = do_ssa;

  // tolg is a terminal LNAST->LGraph step (task 1l), not part of the runner
  // order. Default off; enabled with tolg:1. Runs after the main walk so
  // ssa (io_meta) and bitwidth (bw_meta) have populated their side-channels.
  auto tolg_txt = get_label("tolg");
  run_tolg      = tolg_txt != "false" && tolg_txt != "0";

  // toln gates materializing the rewritten LNAST (the staging→replace_body
  // swap after each tree's walk). toln:0 skips it for diagnostics-only runs
  // (the LSP passes tolg:0 toln:0); the tolg sub-pass reads the post-upass
  // tree, so tolg:1 forces the swap regardless. Default on.
  auto toln_txt = get_label("toln");
  run_toln      = (toln_txt != "false" && toln_txt != "0") || run_tolg;

  auto semacheck_txt = get_label("semacheck");
  bool do_semacheck  = semacheck_txt != "false" && semacheck_txt != "0";

  auto func_extract_txt = get_label("func_extract");
  bool do_func_extract  = func_extract_txt != "false" && func_extract_txt != "0";

  auto typecheck_txt = get_label("typecheck");
  bool do_typecheck  = typecheck_txt != "false" && typecheck_txt != "0";

  // The assert pass declares depends_on={"constprop"}, so the runner's
  // resolve_order will silently pull constprop back in if assert is enabled
  // — even when the user explicitly passed constprop:0. Honor the explicit
  // disable here: with constprop off, assert can't run anyway (it reads the
  // symbol-table values constprop populates), so drop it. The user can
  // override by passing assert:0 explicitly or by using the order=…  arg
  // to spell out exactly which passes to run.
  if (!do_constprop && do_assert) {
    Pass::warn("pass.upass: constprop:0 forces assert:0 (assert depends on constprop)");
    do_assert = false;
  }

  if (do_verifier) {  // 1st and last pass
    upass_order.emplace_back("verifier");
  }

  // semacheck runs first among the semantic passes (goal 1h): its checks
  // (read-only-attr writes, declare-once/shadowing) want the tree closest to
  // user source and fail fast before the value passes do work. It is a
  // one-shot begin_iteration walk, so its slot in the per-node dispatch order
  // is irrelevant — only that it is registered before the walk starts.
  if (do_semacheck) {
    upass_order.emplace_back("semacheck");
  }

  if (do_func_extract) {
    upass_order.emplace_back("func_extract");
  }

  // Phase 1 of the attribute pass runs before constprop so sticky/attribute
  // state is available when constprop folds `.[attr]` reads (Phase 2 in
  // attribute_todo.md). Later phases of attributes hook in after constprop;
  // they share the same `attributes` plugin and are toggled internally.
  if (do_attrs) {
    upass_order.emplace_back("attributes");
  }

  // typecheck (kind-only) runs after attributes (declared kinds available) and
  // before constprop (sees original ops + dead branches; fails fast on a type
  // error before the value passes do work).
  if (do_typecheck) {
    upass_order.emplace_back("typecheck");
  }

  // With no consumer of the rewritten LNAST (toln:0 && tolg:0 — run_toln
  // already folds tolg in), the coalescer has nothing to do: it is pure
  // deferred-emit / dead-store elimination with zero diagnostics, and the
  // tree it shapes is never materialized. Drop it from the default order
  // (an explicit order=… csv is honored untouched).
  if (!run_toln) {
    do_coalescer = false;
  }

  if (do_constprop) {
    upass_order.emplace_back("constprop");
  }
  // Bitwidth is the LAST opt pass in the single runner walk (Goal 1n): it has
  // no fold_ref (decoupled from the iterative const-fold), is read-only (votes
  // keep), observes every store PRE-DCE so it can capture overflow even on dead
  // comptime consts, and publishes max/min into bw_meta at end_run for the
  // future lnast_to_lgraph. See upass/upass.md §2 "bitwidth".
  if (do_bitwidth) {
    upass_order.emplace_back("bitwidth");
  }
  // Coalescer runs after constprop so its handle_op sees an up-to-date
  // runner_fold_fn — the known-const guard skips parking when constprop has
  // already proven the LHS comptime, letting constprop's classify drop fire.
  if (do_coalescer) {
    upass_order.emplace_back("coalescer");
  }
  if (do_assert) {  // last before codegen
    upass_order.emplace_back("assert");
  }

  if (do_verifier) {  // 1st and last pass
    upass_order.emplace_back("verifier");
  }

  if (upass_order.empty()) {
    fail_upass_runtime("pass.upass has all the passed disabled??");
  }
}

void Pass_upass::work(Eprp_var& var) {
  Pass_upass up(var);

  // Test-level expectations (verifier_pass / verifier_fail) describe the
  // whole program, including any helper lnasts func_extract spawns from
  // comb func_defs. Counts are aggregated across every verifier instance
  // and checked once via uPass_verifier::finalize_aggregate after the
  // last lnast has been processed.
  uPass_verifier::reset_aggregate();
  uPass_verifier::set_aggregate_expected(parse_expected_count(up.pass_options, "verifier_pass"),
                                         parse_expected_count(up.pass_options, "verifier_fail"));

  // Capture original entry-point count BEFORE func_extract spawns helper
  // lnasts. Anything appended past this index in the func_extract loop is
  // a function-body spawn — used below to gate the verifier off for
  // spawn lnasts unless verifier_include_funcs:true was passed.
  const auto original_lnast_count = var.lnasts.size();

  const auto extract_it = std::find(up.upass_order.begin(), up.upass_order.end(), "func_extract");
  if (extract_it != up.upass_order.end()) {
    std::vector<std::string> extract_order{"func_extract"};
    for (std::size_t idx = 0; idx < var.lnasts.size(); ++idx) {
      const auto ln     = var.lnasts.at(idx);
      auto       lm     = std::make_shared<upass::Lnast_manager>(ln);
      auto       runner = uPass_runner(lm, extract_order, up.pass_options);
      if (runner.has_configuration_error()) {
        fail_upass_runtime(std::format("pass.upass invalid pass configuration: {}", runner.get_configuration_error()));
      }
      runner.run();

      auto staged = runner.take_staging();
      if (staged) {
        // In-place body swap via TreeIO::replace. `ln` (the same shared_ptr
        // already in var.lnasts) now refers to the rewritten tree; the
        // staging wrapper is dropped.
        ln->replace_body(staged->tree_ptr());
      }
      auto new_lnasts = runner.take_new_lnasts();
      for (const auto& new_ln : new_lnasts) {
        var.add(new_ln);
      }
    }
    up.upass_order.erase(std::remove(up.upass_order.begin(), up.upass_order.end(), "func_extract"), up.upass_order.end());
  }

  if (up.upass_order.empty()) {
    uPass_verifier::finalize_aggregate();
    return;
  }

  // ── SSA normalisation (ssa:1): run after func_extract, before the main
  // runner.  Harvests I/O metadata into lnast->io_meta(), expands I/O
  // tuple_add nodes into a flat stmts body, drops the top-level 'io' node.
  // Only function-body LNASTs (the ones func_extract spawned) have the
  // io+stmts layout; run() is a no-op on top-level LNASTs.
  if (up.run_ssa) {
    for (const auto& ln : var.lnasts) {
      uPass_ssa::run(ln);
    }
  }

  uPass_constprop::set_function_registry(var.lnasts);

  // Walk as a queue: structural passes can append new LNASTs (e.g. extracted
  // comb functions), and those generated trees should run through the same
  // configured upass pipeline before downstream stages see them.
  for (std::size_t idx = 0; idx < var.lnasts.size(); ++idx) {
    const auto ln = var.lnasts.at(idx);
    auto       lm = std::make_shared<upass::Lnast_manager>(ln);

    // For func_extract-spawned lnasts (idx beyond the original entry-point
    // count), strip the verifier from the order unless the test opted in
    // via verifier_include_funcs:true — dropping the verifier here avoids
    // double-walking unproven function bodies into the aggregate.
    auto order = up.upass_order;
    const bool is_function_body = idx >= original_lnast_count;
    if (is_function_body && !up.verifier_include_funcs) {
      order.erase(std::remove(order.begin(), order.end(), "verifier"), order.end());
    }
    auto runner = uPass_runner(lm, order, up.pass_options);
    // toln:0 && tolg:0: nothing consumes the rewritten LNAST, so skip building
    // the staging tree (emit_* no-ops) and the post-walk DCE entirely — the
    // walk still dispatches every pass, so diagnostics and side-channels
    // (io_meta/bw_meta) are unchanged. The func_extract pre-loop above keeps
    // materializing (the main walk consumes its rewrite).
    runner.set_materialize(up.run_toln);
    runner.set_is_function_body(is_function_body);
    runner.set_function_registry(var.lnasts);  // 1i: comb bodies to inline from
    if (runner.has_configuration_error()) {
      fail_upass_runtime(std::format("pass.upass invalid pass configuration: {}", runner.get_configuration_error()));
    }
    runner.run();

    // Swap the rewritten staging tree into the existing Lnast so downstream
    // passes (lnast.dump, …) see the folded/DCE'd IR. Slot identity (the
    // shared_ptr in var.lnasts and the underlying TreeIO Tid) is preserved.
    // toln:0 (diagnostics-only runs, e.g. the LSP) skips the swap — nothing
    // downstream reads the rewritten tree, so the original body stays put and
    // the staging rebuild is dropped. run_toln is forced on while tolg:1 (the
    // tolg sub-pass below reads the post-upass tree).
    if (up.run_toln) {
      auto staged = runner.take_staging();
      if (staged) {
        ln->replace_body(staged->tree_ptr());
      }
    }
    auto new_lnasts = runner.take_new_lnasts();
    for (const auto& new_ln : new_lnasts) {
      var.add(new_ln);
    }
  }

  // ── LN pipe upass (task 1q). Inserts the per-output pipeline flop —
  // declare(reg)+stages plus the din/q rebind stores — into every `pipe`
  // function tree (io_meta outputs with stages_min >= 1; comb trees no-op).
  // Runs AFTER the main walk so constprop never folds the reg loop away,
  // and BEFORE the terminal consumers so both see the inserted reg: tolg
  // below, and the future lnast_to_slop. toln:0 (diagnostics-only, e.g. the
  // LSP) skips it along with everything else that shapes the output tree.
  if (up.run_toln) {
    for (const auto& ln : var.lnasts) {
      uPass_pipe::run(ln);
    }
  }

  // ── Terminal LNAST->LGraph lowering (tolg:1, task 1l). Runs last, after
  // ssa populated io_meta() and bitwidth populated bw_meta() for every lnast.
  // Each fully-specified function tree becomes one hhds::Graph pushed onto the
  // pass var so a downstream inou.cgen.verilog stage can emit it.
  if (up.run_tolg) {
    for (const auto& ln : var.lnasts) {
      auto g = uPass_tolg::run(ln, "lgdb_tolg");
      if (g) {
        var.add(g);
      }
    }
  }

  uPass_verifier::finalize_aggregate();
}
