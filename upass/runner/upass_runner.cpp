//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_runner.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "upass_runner_common.hpp"
#include "upass_shared.hpp"

namespace {

struct Lnast_pass_noop_shared final : public upass::uPass {
  using upass::uPass::uPass;

  void process_top() override { upass::run_noop_shared(*lm, "uPass"); }
};

struct Lnast_pass_scan_shared final : public upass::uPass {
  using upass::uPass::uPass;

  void process_assign() override {
    if (emitted) {
      return;
    }
    const auto rep = upass::run_scan_shared(*lm, "uPass");
    std::print("uPass - shared scan summary nodes:{} const:{} arith:{}\n", rep.node_count, rep.const_count, rep.arithmetic_count);
    emitted = true;
  }

private:
  bool emitted{false};
};

struct Lnast_pass_decide_shared final : public upass::uPass {
  using upass::uPass::uPass;

  void process_assign() override {
    if (emitted) {
      return;
    }
    const auto rep = upass::run_decide_shared(*lm, "uPass");
    std::print("uPass - shared decide summary fold_candidates:{} has_fold_candidates:{}\n",
               rep.fold_candidate_count,
               rep.has_fold_candidates ? "true" : "false");
    emitted = true;
  }

private:
  bool emitted{false};
};

static upass::uPass_plugin plugin_noop_shared("noop_shared", upass::uPass_wrapper<Lnast_pass_noop_shared>::get_upass);
static upass::uPass_plugin plugin_scan_shared("scan_shared", upass::uPass_wrapper<Lnast_pass_scan_shared>::get_upass);
static upass::uPass_plugin plugin_decide_shared("decide_shared", upass::uPass_wrapper<Lnast_pass_decide_shared>::get_upass);

}  // namespace

uPass_runner::uPass_runner(std::shared_ptr<upass::Lnast_manager>& _lm, const std::vector<std::string>& upass_names)
    : uPass_struct(_lm) {
  auto upass_registry = upass::uPass_plugin::get_registry();
  std::string order_error;
  auto        resolved = resolve_order(upass_names, &order_error);
  if (!order_error.empty()) {
    configuration_error     = true;
    configuration_error_msg = order_error;
  }
  if (!resolved.empty()) {
    std::print("uPass - resolved order:");
    for (const auto &name : resolved) {
      std::print(" {}", name);
    }
    std::print("\n");
  }

  for (const auto& name : resolved) {
    const auto it = upass_registry.find(name);
    if (it == upass_registry.end()) {
      std::print("{} is not defined.\n", name);
      continue;
    }

    std::print("uPass - add {}\n", name);
    upasses.emplace_back(Pass_entry{.name = name, .pass = it->second.setup_fn(_lm)});
  }
}

std::vector<std::string> uPass_runner::resolve_order(const std::vector<std::string>& requested_names, std::string *error_msg) const {
  return upass::resolve_order_impl(upass::uPass_plugin::get_registry(),
                                   requested_names, "uPass", error_msg);
}

std::vector<std::string> uPass_runner::changed_passes() const {
  std::vector<std::string> changed;
  for (const auto &entry : upasses) {
    if (entry.pass->has_changed()) {
      changed.emplace_back(entry.name);
    }
  }
  return changed;
}

void uPass_runner::run(std::size_t max_iters) {
  if (configuration_error) {
    std::print("uPass - invalid configuration: {}\n", configuration_error_msg);
    return;
  }

  if (upasses.empty()) {
    std::print("uPass - no passes configured\n");
    return;
  }

  upass::Runner_fixed_point::run(
      "uPass",
      max_iters,
      [this]() {
        for (auto &entry : upasses) {
          entry.pass->begin_iteration();
        }
      },
      [this]() { process_lnast(); },
      [this]() { return changed_passes(); });
}

void uPass_runner::process_lnast() {
#define PROCESS_BLOCK(NAME) \
  case Lnast_ntype::Lnast_ntype_##NAME: process_##NAME(); break;

// Each pass is invoked in a try-catch so that a single malformed node (e.g.
// caught by the verifier) is reported but does not abort traversal of the
// rest of the tree.  All bad nodes are surfaced rather than just the first.
#define PROCESS_NODE(NAME)                                                 \
  case Lnast_ntype::Lnast_ntype_##NAME:                                   \
    write_node();                                                          \
    for (const auto &entry : upasses) {                                   \
      try {                                                                \
        entry.pass->process_##NAME();                                     \
      } catch (const std::runtime_error &_e) {                           \
        std::print(stderr, "uPass [{}] node error: {}", entry.name, _e.what()); \
      }                                                                    \
    }                                                                      \
    break;

  switch (get_raw_ntype()) {
    PROCESS_BLOCK(top)
    PROCESS_BLOCK(stmts)

    // Assignment
    PROCESS_NODE(assign)

    // Bitwidth
    PROCESS_NODE(bit_and)
    PROCESS_NODE(bit_or)
    PROCESS_NODE(bit_not)
    PROCESS_NODE(bit_xor)

    // Bitwidth Insensitive Reduce
    PROCESS_NODE(red_or)
    PROCESS_NODE(red_and)
    PROCESS_NODE(red_xor)

    // Logical
    PROCESS_NODE(log_and)
    PROCESS_NODE(log_or)
    PROCESS_NODE(log_not)

    // Arithmetic
    PROCESS_NODE(plus)
    PROCESS_NODE(minus)
    PROCESS_NODE(mult)
    PROCESS_NODE(div)
    PROCESS_NODE(mod)

    // Shift
    PROCESS_NODE(shl)
    PROCESS_NODE(sra)

    // Bit Manipulation
    PROCESS_NODE(sext)
    PROCESS_NODE(set_mask)
    PROCESS_NODE(get_mask)
    PROCESS_NODE(mask_and)
    PROCESS_NODE(mask_popcount)
    PROCESS_NODE(mask_xor)

    // Comparison
    PROCESS_NODE(ne)
    PROCESS_NODE(eq)
    PROCESS_NODE(lt)
    PROCESS_NODE(le)
    PROCESS_NODE(gt)
    PROCESS_NODE(ge)

    // Function Call
    PROCESS_NODE(func_call)

    default: break;
  }
#undef PROCESS_BLOCK
#undef PROCESS_NODE
}

void uPass_runner::process_top() {
  move_to_child();
  do {
    process_lnast();
  } while (move_to_sibling());
  move_to_parent();
}

void uPass_runner::process_stmts() {
  move_to_child();
  do {
    process_lnast();
  } while (move_to_sibling());
  move_to_parent();
}
