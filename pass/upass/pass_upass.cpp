//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_upass.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <memory>
#include <print>
#include <stdexcept>
#include <string>
#include <vector>

#include "lgraph_manager.hpp"
#include "upass_runner.hpp"
#include "upass_runner_lgraph.hpp"

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
}  // namespace

void Pass_upass::setup() {
  Eprp_method m1("pass.upass", "lnast micropass (upass) controller", &Pass_upass::work);
  m1.add_label_optional("verifier", "enable lnast verifier upass", "true");
  m1.add_label_optional("constprop", "enable constant propagation upass", "true");
  m1.add_label_optional("assert", "enable assert test", "true");
  m1.add_label_optional(
      "order",
      "comma-separated upass names; overrides verifier/constprop/assert toggles (example: verifier,constprop,verifier)",
      "");
  m1.add_label_optional("max_iters", "maximum upass iterations to run", "1");
  m1.add_label_optional("ir", "IR mode: lnast (default) or lgraph", "lnast");
  m1.add_label_optional("dry_run", "lgraph-only: collect rewrite metrics without mutating graph", "false");
  m1.add_label_optional("inherit", "inherit labels from prior pipeline stages (default true); false resets sticky upass options", "true");
  register_pass(m1);
}

Pass_upass::Pass_upass(const Eprp_var &var) : Pass("pass.upass", var) {
  auto inherit_txt = std::string(var.get_stage("inherit", "true"));
  std::transform(
      inherit_txt.begin(), inherit_txt.end(), inherit_txt.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  inherit_labels = !(inherit_txt.empty() || inherit_txt == "0" || inherit_txt == "false" || inherit_txt == "no" || inherit_txt == "off");

  const auto get_label = [&](std::string_view label, std::string_view default_value = "") -> std::string_view {
    if (inherit_labels) {
      return var.get(label, default_value);
    }
    return var.get_stage(label, default_value);
  };

  auto ir_txt = std::string(get_label("ir", "lnast"));
  std::transform(ir_txt.begin(), ir_txt.end(), ir_txt.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (ir_txt != "lnast" && ir_txt != "lgraph") {
    fail_upass_runtime(std::format("pass.upass invalid ir:{} (expected lnast or lgraph)", ir_txt));
  }
  ir_mode = std::move(ir_txt);

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

  auto dry_run_txt = std::string(get_label("dry_run", "false"));
  std::transform(
      dry_run_txt.begin(), dry_run_txt.end(), dry_run_txt.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  dry_run = !(dry_run_txt.empty() || dry_run_txt == "0" || dry_run_txt == "false" || dry_run_txt == "no" || dry_run_txt == "off");

  if (!upass_order.empty()) {
    return;
  }

  if (ir_mode == "lgraph") {
    upass_order = {"visit", "fold_scan"};
    return;
  }

  auto verifier_txt = get_label("verifier");
  bool do_verifier  = verifier_txt != "false" && verifier_txt != "0";

  auto assert_txt = get_label("assert");
  bool do_assert  = assert_txt != "false" && assert_txt != "0";

  auto constp_txt   = get_label("constprop");
  bool do_constprop = constp_txt != "false" && constp_txt != "0";

  if (do_verifier) {  // 1st and last pass
    upass_order.emplace_back("verifier");
  }

  if (do_constprop) {
    upass_order.emplace_back("constprop");
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

void Pass_upass::work(Eprp_var &var) {
  Pass_upass up(var);

  if (up.ir_mode == "lgraph") {
    if (var.lgs.empty()) {
      fail_upass_runtime("pass.upass ir:lgraph requires lgraph input (e.g. ... |> pass.lnast_tolg |> pass.upass ir:lgraph)");
    }
    for (auto *lg : var.lgs) {
      auto gm     = std::make_shared<upass::Lgraph_manager>(lg);
      auto runner = uPass_runner_lgraph(gm, up.upass_order, up.dry_run);
      if (runner.has_configuration_error()) {
        fail_upass_runtime(std::format("pass.upass invalid lgraph pass configuration: {}", runner.get_configuration_error()));
      }
      runner.run(up.max_iters);
    }
    return;
  }

  for (const auto &ln : var.lnasts) {
    auto lm     = std::make_shared<upass::Lnast_manager>(ln);
    auto runner = uPass_runner(lm, up.upass_order);
    if (runner.has_configuration_error()) {
      fail_upass_runtime(std::format("pass.upass invalid pass configuration: {}", runner.get_configuration_error()));
    }
    runner.run(up.max_iters);
  }
}
