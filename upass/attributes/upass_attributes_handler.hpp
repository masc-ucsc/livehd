//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Per-attribute-name handler interface used by uPass_attributes.
//
// upass/attributes is the home of the Pyrope attribute pass (see
// attribute_todo.md). Each known attribute name has its own handler with the
// per-attribute semantics (sticky propagation, comptime check, lowering to
// LGraph wiring, etc.). Unknown names fall through to the default
// pass-through handler. Phase 1 only registers the sticky handler; later
// phases register the rest.

struct uPass_attributes;  // forward decl; defined in upass_attributes.hpp

namespace upass {
namespace attributes {

// Lifecycle hooks invoked by uPass_attributes during the LNAST node walk.
// All hooks default to no-op; concrete handlers override what they need.
struct Attribute_handler {
  virtual ~Attribute_handler() = default;

  // Called once at the start of each upass iteration so the handler can reset
  // any per-pass state.
  virtual void begin_iteration(uPass_attributes& /*owner*/) {}

  // Called once at the end of an iteration so the handler can finalize
  // diagnostics (e.g. report unresolved comptime attrs).
  virtual void end_run(uPass_attributes& /*owner*/) {}

  // Declaration-site set: `lhs::[name=value]` or `lhs::[name]`.
  virtual void on_attr_set(uPass_attributes& /*owner*/, std::string_view /*lhs*/, std::string_view /*value_text*/) {}

  // Read site: `dst = base.[name]`. `dst` is the destination ref (typically
  // a tmp) the read result lands in; `base` is the variable being read from.
  virtual void on_attr_get(uPass_attributes& /*owner*/, std::string_view /*dst*/, std::string_view /*base*/) {}

  // Direct alias: `const y = a` or `mut b = a` or `c = b`.
  // Both sticky and non-sticky attrs migrate from rhs to lhs.
  virtual void on_alias_assign(uPass_attributes& /*owner*/, std::string_view /*lhs*/, std::string_view /*rhs*/) {}

  // Expression-result assign: `lhs = <expr involving rhs_refs...>`.
  // Sticky attrs migrate; non-sticky drop.
  virtual void on_expr_assign(uPass_attributes& /*owner*/, std::string_view /*lhs*/,
                              const std::vector<std::string_view>& /*rhs_refs*/) {}

  // Entering a conditional arm whose condition reads the listed refs (or
  // attribute-reads, encoded in cond_attr_reads). Sticky control taint flows
  // from these into any assignment performed inside the arm.
  virtual void on_if_arm_enter(uPass_attributes& /*owner*/,
                               const std::vector<std::string_view>& /*cond_refs*/,
                               const std::vector<std::pair<std::string_view, std::string_view>>& /*cond_attr_reads*/) {}

  // Leaving the arm. Used to pop scope-local taint and OR-merge into the
  // parent at runtime joins.
  virtual void on_if_arm_exit(uPass_attributes& /*owner*/) {}
};

// Registry indexed by attribute name. Phase 1 registers a single sticky
// handler that matches `debug`, `_debug`, and any name starting with `_`.
// Unknown names use the default pass-through handler (kept in
// upass_attributes.cpp).
class Handler_registry {
public:
  // Lookup: prefers exact-name match, then falls back to the sticky `_*`
  // pattern, then to the default pass-through handler.
  Attribute_handler* lookup(std::string_view name) const;

  // Register/replace a handler for an exact attribute name.
  void register_exact(std::string name, std::shared_ptr<Attribute_handler> h);

  // Register the catch-all handler for names matching the `_*` sticky
  // pattern (debug-style propagation).
  void register_sticky_pattern(std::shared_ptr<Attribute_handler> h);

  // Register the default handler used for any name not otherwise matched.
  void register_default(std::shared_ptr<Attribute_handler> h);

  // Visit every distinct registered handler exactly once. Used for
  // broadcast events (begin_iteration / end_run / on_alias_assign /
  // on_expr_assign / on_if_arm_enter / on_if_arm_exit) that aren't tied to
  // a specific attribute name.
  void for_each_handler(const std::function<void(Attribute_handler&)>& fn) const;

private:
  std::map<std::string, std::shared_ptr<Attribute_handler>> exact;
  std::shared_ptr<Attribute_handler>                        sticky_pattern;  // for `_*`
  std::shared_ptr<Attribute_handler>                        default_handler;
};

}  // namespace attributes
}  // namespace upass
