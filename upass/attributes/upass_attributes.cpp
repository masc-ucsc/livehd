//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_attributes.hpp"

#include <memory>

#include "upass_attributes_sticky.hpp"

// Plugin registration. Name "attributes" is what pass_upass appends to the
// default upass order.
static upass::uPass_plugin plugin_attributes("attributes", upass::uPass_wrapper<uPass_attributes>::get_upass);

uPass_attributes::uPass_attributes(std::shared_ptr<upass::Lnast_manager>& _lm) : upass::uPass(_lm) {
  // Phase 1: register the sticky pattern handler. Later phases will register
  // additional exact-name handlers (bits/ubits/sbits/max/min, clock_pin,
  // reset_pin, …) and a default pass-through handler for category C/D.
  reg.register_sticky_pattern(std::make_shared<upass::attributes::Sticky_handler>());
  // TODO(phase 5+): register exact-name handlers for category-A attrs.
  // TODO(phase 6): register handlers for category-B wiring attrs.
  // TODO(phase 7): register the default pass-through handler.
}

void uPass_attributes::begin_iteration() {
  upass::uPass::begin_iteration();
  // TODO: forward begin_iteration to every registered handler.
}

void uPass_attributes::end_run() {
  // TODO: forward end_run to every registered handler so they can finalize
  // diagnostics (e.g. unresolved comptime attrs).
}

uPass_attributes::Op_view uPass_attributes::scan_op() {
  // TODO: walk the current op node's children, capture the LHS ref (child 0)
  // and every subsequent `ref` operand. Set `is_alias` true when the op is
  // an `assign` whose single RHS operand is also a ref (no expression).
  return Op_view{};
}

void uPass_attributes::dispatch_attr_set(std::string_view /*attr_name*/, std::string_view /*lhs*/,
                                         std::string_view /*value_text*/) {
  // TODO: lookup handler in `reg` and forward.
}

void uPass_attributes::dispatch_attr_get(std::string_view /*attr_name*/, std::string_view /*lhs*/) {
  // TODO: lookup handler in `reg` and forward.
}

void uPass_attributes::on_assign_like(bool /*is_expression*/) {
  // TODO: scan current op, then for every registered handler call either
  // on_alias_assign (direct ref alias) or on_expr_assign (expression
  // result). Phase 1 only the sticky handler reacts.
}

void uPass_attributes::process_assign() {
  // assign with a ref RHS is alias semantics; assign with const RHS is a
  // plain value write that drops non-sticky attrs. on_assign_like() decides.
  on_assign_like(/*is_expression=*/false);
}

#define EXPR_PROCESS(NAME) \
  void uPass_attributes::process_##NAME() { on_assign_like(/*is_expression=*/true); }

EXPR_PROCESS(plus)
EXPR_PROCESS(minus)
EXPR_PROCESS(mult)
EXPR_PROCESS(div)
EXPR_PROCESS(mod)
EXPR_PROCESS(shl)
EXPR_PROCESS(sra)
EXPR_PROCESS(bit_and)
EXPR_PROCESS(bit_or)
EXPR_PROCESS(bit_not)
EXPR_PROCESS(bit_xor)
EXPR_PROCESS(log_and)
EXPR_PROCESS(log_or)
EXPR_PROCESS(log_not)
EXPR_PROCESS(red_or)
EXPR_PROCESS(red_and)
EXPR_PROCESS(red_xor)
EXPR_PROCESS(ne)
EXPR_PROCESS(eq)
EXPR_PROCESS(lt)
EXPR_PROCESS(le)
EXPR_PROCESS(gt)
EXPR_PROCESS(ge)
EXPR_PROCESS(sext)
EXPR_PROCESS(get_mask)
EXPR_PROCESS(set_mask)

#undef EXPR_PROCESS

void uPass_attributes::process_attr_set() {
  // TODO: extract attr name + target lhs + value text, then dispatch.
}

void uPass_attributes::process_attr_get() {
  // TODO: extract attr name + target lhs, then dispatch.
}

void uPass_attributes::process_if() {
  // TODO: walk if/else arms; for each arm collect the condition's referenced
  // refs + attr-reads, dispatch on_if_arm_enter / on_if_arm_exit, then
  // OR-merge sticky state across runtime arms. Comptime-folded ifs already
  // collapsed by constprop should not reach here as multi-arm.
}

void uPass_attributes::notify_uncertain_arm_begin() {
  // TODO: mark the next pushed arm so on_if_arm_exit knows to OR-merge into
  // the parent rather than discard.
}

void uPass_attributes::notify_uncertain_arm_end() {
  // TODO: clear the marker.
}
