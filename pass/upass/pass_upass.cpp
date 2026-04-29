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

#include "upass_constprop.hpp"
#include "upass_runner.hpp"
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
  m1.add_label_optional("assert", "enable assert test", "true");
  m1.add_label_optional(
      "order",
      "comma-separated upass names; overrides verifier/constprop/assert toggles (example: verifier,constprop,verifier)",
      "");
  m1.add_label_optional("max_iters", "maximum upass iterations to run", "1");
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

  auto max_iters_txt = get_label("max_iters");
  if (!max_iters_txt.empty()) {
    try {
      auto parsed = std::stoul(std::string(max_iters_txt));
      if (parsed == 0) {
        Pass::warn("pass.upass max_iters=0 is invalid, forcing 1");
        max_iters = 1;
      } else {
        max_iters = parsed;
      }
    } catch (...) {
      Pass::warn("pass.upass invalid max_iters:{} (using 1)", max_iters_txt);
      max_iters = 1;
    }
  }

  auto order_txt = get_label("order");
  if (!order_txt.empty()) {
    upass_order = parse_order_csv(order_txt);
  }

  auto vif_txt = std::string(get_label("verifier_include_funcs", "false"));
  std::transform(vif_txt.begin(), vif_txt.end(), vif_txt.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  verifier_include_funcs
      = !(vif_txt.empty() || vif_txt == "0" || vif_txt == "false" || vif_txt == "no" || vif_txt == "off");

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

  upass_order.emplace_back("func_extract");

  if (do_constprop) {
    upass_order.emplace_back("constprop");
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
      runner.run(up.max_iters);

      auto staged = runner.take_staging();
      if (staged) {
        var.replace(ln, staged);
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

  uPass_constprop::set_function_registry(var.lnasts);

  // Walk as a queue: structural passes can append new LNASTs (e.g. extracted
  // comb functions), and those generated trees should run through the same
  // configured upass pipeline before downstream stages see them.
  for (std::size_t idx = 0; idx < var.lnasts.size(); ++idx) {
    const auto ln     = var.lnasts.at(idx);
    auto       lm     = std::make_shared<upass::Lnast_manager>(ln);

    // For func_extract-spawned lnasts (idx beyond the original entry-point
    // count), strip the verifier from the order unless the test opted in
    // via verifier_include_funcs:true. Function-body casserts that constprop
    // already proved at call sites still get tallied via mark_inlined_cassert_*
    // in try_eval_comb_call, so dropping the verifier here just avoids
    // double-walking unproven function bodies into the aggregate.
    auto order = up.upass_order;
    if (idx >= original_lnast_count && !up.verifier_include_funcs) {
      order.erase(std::remove(order.begin(), order.end(), "verifier"), order.end());
    }
    auto runner = uPass_runner(lm, order, up.pass_options);
    if (runner.has_configuration_error()) {
      fail_upass_runtime(std::format("pass.upass invalid pass configuration: {}", runner.get_configuration_error()));
    }
    runner.run(up.max_iters);

    // Swap the rewritten staging tree into var.lnasts so downstream passes
    // (lnast.dump, …) see the folded/DCE'd IR.
    auto staged = runner.take_staging();
    if (staged) {
      var.replace(ln, staged);
    }
    auto new_lnasts = runner.take_new_lnasts();
    for (const auto& new_ln : new_lnasts) {
      var.add(new_ln);
    }
  }

  uPass_verifier::finalize_aggregate();
}
