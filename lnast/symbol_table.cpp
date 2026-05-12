//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "symbol_table.hpp"

#include "absl/strings/str_cat.h"
#include "likely.hpp"
#include "lnast.hpp"

// Reject the legacy I/O / register prefixes ($, %, #) at every write site.
// Pyrope no longer carries direction or register-ness in the textual name —
// those are tracked structurally by lnast_to_lgraph / the attribute pass.
// Names with these prefixes only show up when an upstream pass forgot to
// normalize; surface that early instead of silently accepting them.
static void assert_no_prefix(std::string_view key) {
  auto var = Bundle::get_first_level(key);
  I(var.empty() || (var.front() != '#' && var.front() != '$' && var.front() != '%'),
    "symbol_table: variable name carries a legacy $/%/# prefix; upstream pass must normalize");
}

Symbol_table::Scope* Symbol_table::find_decl_scope(std::string_view var) {
  if (stack.empty()) {
    return nullptr;
  }
  for (auto* s = stack.back(); s != nullptr; s = s->parent) {
    if (s->varmap.find(var) != s->varmap.end()) {
      return s;
    }
    // Function scopes are visibility barriers: a callee body cannot reach
    // the caller's locals. Stop at AND INCLUDING the function scope so the
    // function's own params/locals stay reachable.
    if (s->type == Scope_type::Function) {
      return nullptr;
    }
  }
  return nullptr;
}

const Symbol_table::Scope* Symbol_table::find_decl_scope(std::string_view var) const {
  if (stack.empty()) {
    return nullptr;
  }
  for (const auto* s = stack.back(); s != nullptr; s = s->parent) {
    if (s->varmap.find(var) != s->varmap.end()) {
      return s;
    }
    if (s->type == Scope_type::Function) {
      return nullptr;
    }
  }
  return nullptr;
}

bool Symbol_table::var(std::string_view key) {
  I(!stack.empty());
  assert_no_prefix(key);
  auto [var, field] = get_var_field(key);

  auto& cur = *stack.back();
  const auto it = cur.varmap.find(var);
  if (unlikely(it != cur.varmap.end())) {
    Lnast::info("re-declaring {} which already exists in {}", var, cur.scope);
    return false;
  }

  auto bundle = std::make_shared<Bundle>(var);
  bundle->var(field, invalid_lconst);
  cur.varmap.emplace(var, bundle);
  cur.declared.emplace_back(std::string(var));
  return true;
}

// Tmp refs (`___N`) are LNAST-internal SSA-style values, not user variables.
// They have no real "scope" — a producer in a nested block (e.g. inside an
// if-stmts) writes a tmp that an outer-block consumer reads. If we stored
// the tmp in the innermost block, the outer reader's fold would fail once
// the inner block was left (block scope no longer reachable from the outer).
// Anchor every tmp at the nearest Function scope so it stays visible until
// the surrounding function returns.
static Symbol_table::Scope* anchor_for(Symbol_table::Scope* innermost, std::string_view var) {
  if (var.size() < 3 || var.substr(0, 3) != "___") {
    return innermost;
  }
  Symbol_table::Scope* s = innermost;
  while (s->parent != nullptr && s->type != Symbol_table::Scope_type::Function) {
    s = s->parent;
  }
  return s;
}

bool Symbol_table::set(std::string_view key, std::shared_ptr<Bundle> bundle) {
  I(bundle);
  I(!stack.empty());
  assert_no_prefix(key);

  auto [var, field] = get_var_field(key);

  // Bare `x = …`: prefer an existing declaration in any reachable scope so
  // a write inside `{ … }` updates the outer var. If none, declare in the
  // current (innermost) scope (or the enclosing function for tmps — see
  // anchor_for).
  Scope* target = find_decl_scope(var);
  if (target == nullptr) {
    target = anchor_for(stack.back(), var);
  }
  record_uncertain_modification(var);

  std::shared_ptr<Bundle> var_bundle;
  const auto it = target->varmap.find(var);
  if (unlikely(it == target->varmap.end())) {
    target->declared.emplace_back(std::string(var));
    if (var == key) {
      target->varmap.emplace(std::string(var), bundle);
      return true;
    }
    var_bundle = std::make_shared<Bundle>(var);
    target->varmap.emplace(std::string(var), var_bundle);
  } else {
    if (var == key) {
      it->second = bundle;
      return true;
    }
    var_bundle = it->second;
  }

  var_bundle->set(field, bundle);

  return true;
}

bool Symbol_table::set(std::string_view key, const Const& trivial) {
  I(!stack.empty());
  assert_no_prefix(key);
  auto [var, field] = get_var_field(key);

  Scope* target = find_decl_scope(var);
  if (target == nullptr) {
    target = anchor_for(stack.back(), var);
  }
  record_uncertain_modification(var);

  std::shared_ptr<Bundle> bundle;
  const auto it = target->varmap.find(var);
  if (unlikely(it == target->varmap.end())) {
    target->declared.emplace_back(std::string(var));
    bundle = std::make_shared<Bundle>(var);
    target->varmap.emplace(std::string(var), bundle);
  } else {
    bundle = it->second;
  }

  bundle->set(field, trivial);

  return true;
}

bool Symbol_table::mut(std::string_view key, const Const& trivial) {
  assert_no_prefix(key);
  auto [var, field] = get_var_field(key);

  Scope* target = find_decl_scope(var);
  if (unlikely(target == nullptr)) {
    Lnast::info("mutate {} but variable not declared in {}",
                var,
                stack.empty() ? std::string_view{""} : std::string_view{stack.back()->scope});
    return false;
  }

  auto it = target->varmap.find(var);
  if (unlikely(!it->second->has_trivial(field))) {
    Lnast::info("mutate {} but field {} not declared in {}", var, field, target->scope);
    return false;
  }

  it->second->mut(field, trivial);
  record_uncertain_modification(var);

  return true;
}

bool Symbol_table::let(std::string_view key, std::shared_ptr<Bundle> bundle) {
  I(!stack.empty());
  assert_no_prefix(key);
  auto [var, field] = get_var_field(key);

  auto& cur = *stack.back();
  const auto it = cur.varmap.find(var);
  if (unlikely(it != cur.varmap.end())) {
    Lnast::info("let {} but variable already declared in {}", var, cur.scope);
    return false;
  }

  bundle->set_immutable();
  cur.varmap.emplace(std::string(var), bundle);
  cur.declared.emplace_back(std::string(var));
  return true;
}

void Symbol_table::always_scope() {
  I(!stack.empty());

  // WARNING: keep same scope-id because shadowing can not happen here.
  scope_storage.emplace_back(Scope_type::Always, stack.back()->func_id, stack.back()->scope);
  auto* s = &scope_storage.back();
  s->parent = stack.back();
  stack.emplace_back(s);
}

void Symbol_table::conditional_scope() {
  I(!stack.empty());
  scope_storage.emplace_back(Scope_type::Conditional, stack.back()->func_id, stack.back()->scope);
  auto* s = &scope_storage.back();
  s->parent = stack.back();
  stack.emplace_back(s);
}

void Symbol_table::function_scope(std::string_view func_id) {
  std::string scope(func_id);
  for (int i = static_cast<int>(stack.size()) - 1; i >= 0; --i) {
    const auto* s = stack[i];
    if (s->func_id != func_id) {
      continue;
    }
    I(s->scope.back() != '/');
    scope = absl::StrCat(s->scope, "/", func_id);
    break;
  }

  scope_storage.emplace_back(Scope_type::Function, func_id, scope);
  auto* s = &scope_storage.back();
  s->parent = stack.empty() ? nullptr : stack.back();
  stack.emplace_back(s);
}

Symbol_table::Scope* Symbol_table::block_scope(uint64_t key) {
  I(!stack.empty());
  auto it = block_scope_index.find(key);
  Scope* s = nullptr;
  if (it == block_scope_index.end()) {
    // First entry — create a persistent scope under the current parent.
    auto scope_id = absl::StrCat(stack.back()->scope, "/b", key);
    scope_storage.emplace_back(Scope_type::Block, stack.back()->func_id, scope_id);
    s = &scope_storage.back();
    block_scope_index.emplace(key, s);
  } else {
    s = it->second;
  }
  // Re-attach parent every push: the runner may invoke this scope from a
  // different lexical position (e.g. when an outer scope is itself a block
  // pushed during this iteration). Parent at push-time is what the lookup
  // walk needs.
  s->parent = stack.back();
  // Reset uncertainty bookkeeping per entry — the same Scope object is
  // reused across upass iterations, but the uncertainty bit is set by the
  // runner each time and the modification list is rebuilt each time.
  s->uncertain_cond = false;
  s->modified_under_uncertainty.clear();
  stack.emplace_back(s);
  return s;
}

void Symbol_table::mark_current_uncertain() {
  I(!stack.empty());
  stack.back()->uncertain_cond = true;
}

void Symbol_table::record_uncertain_modification(std::string_view name) {
  // Tmps (___N) are SSA values anchored at the function scope (see
  // anchor_for). They aren't user-visible and shouldn't be invalidated when
  // an uncertain arm exits, or downstream uses inside the arm itself break.
  if (name.size() >= 3 && name.substr(0, 3) == "___") {
    return;
  }
  // Innermost uncertain scope wins: when an outer uncertain scope eventually
  // leaves, anything set after the inner left will get re-recorded if the
  // outer arm is still active. No need to mark on every uncertain ancestor.
  for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
    if ((*it)->uncertain_cond) {
      auto& mods = (*it)->modified_under_uncertainty;
      for (const auto& existing : mods) {
        if (existing == name) {
          return;
        }
      }
      mods.emplace_back(name);
      return;
    }
  }
}

std::shared_ptr<Bundle> Symbol_table::leave_scope() {
  I(!stack.empty());

  // Uncertain-arm cleanup. Any var assigned inside this scope had its
  // declaring-scope value updated while we walked the body (so casserts
  // *inside* the arm could fold against the in-progress value). On exit we
  // can no longer claim that value is comptime-known, since the arm itself
  // wasn't guaranteed to run, so flip the trivial back to invalid in the
  // declaring scope. The Scope object is reused across iterations; the
  // bookkeeping is reset on the next block_scope() entry.
  if (stack.back()->uncertain_cond) {
    auto* arm = stack.back();
    for (const auto& var_name : arm->modified_under_uncertainty) {
      // Find the declaring scope by walking from this arm's parent outward
      // (skipping the arm itself, which we're about to pop). Stops at and
      // includes the nearest Function scope.
      Scope* declaring = nullptr;
      for (auto* s = arm->parent; s != nullptr; s = s->parent) {
        if (s->varmap.find(var_name) != s->varmap.end()) {
          declaring = s;
          break;
        }
        if (s->type == Scope_type::Function) {
          break;
        }
      }
      if (declaring == nullptr) {
        continue;
      }
      auto it = declaring->varmap.find(var_name);
      // Setting the scalar field to invalid is enough — readers go through
      // get_trivial / is_known_const, which both check is_invalid(). We
      // don't tear out sub-fields: a non-scalar bundle (e.g. a tuple) gets
      // its scalar slot invalidated and any reader that relied on the slot
      // sees unknown; tuple-shape readers still see the structure, which
      // is the conservative answer for a mutation we couldn't prove.
      it->second->set("0", Const{});
    }
  }

#ifndef NDEBUG
  dump();
#endif

  // Pop the active pointer; the underlying Scope stays in scope_storage so
  // the next iteration can re-enter it (block_scope) and accumulated state
  // remains consistent.
  stack.pop_back();
  return nullptr;
}

bool Symbol_table::is_known_const(std::string_view name) const {
  if (!has_trivial(name)) return false;
  const auto& val = get_trivial(name);
  return !val.is_invalid() && !val.has_unknowns();
}

bool Symbol_table::has_trivial(std::string_view key) const {
  auto [var, field] = get_var_field(key);

  const auto* s = find_decl_scope(var);
  if (s == nullptr) {
    return false;
  }
  const auto it = s->varmap.find(var);
  return it->second->has_trivial(field);
}

const Const& Symbol_table::get_trivial(std::string_view key) const {
  auto [var, field] = get_var_field(key);

  const auto* s = find_decl_scope(var);
  if (s == nullptr) {
    return invalid_lconst;
  }
  const auto it = s->varmap.find(var);
  return it->second->get_trivial(field);
}

std::shared_ptr<Bundle> Symbol_table::get_bundle(std::string_view key) const {
  auto [var, field] = get_var_field(key);

  const auto* s = find_decl_scope(var);
  if (s == nullptr) {
    return nullptr;
  }
  const auto it = s->varmap.find(var);

  if (var == key) {
    return it->second;
  }
  return it->second->get_bundle(field);
}

bool Symbol_table::has_bundle(std::string_view key) const {
  auto [var, field] = get_var_field(key);

  const auto* s = find_decl_scope(var);
  if (s == nullptr) {
    return false;
  }
  const auto it = s->varmap.find(var);
  return var == key || it->second->has_bundle(field);
}

bool Symbol_table::is_declared(std::string_view key) const {
  auto [var, field] = get_var_field(key);
  (void)field;
  return find_decl_scope(var) != nullptr;
}

void Symbol_table::dump() const {
  if (stack.empty()) {
    return;
  }
  std::print("Symbol_table::leave_scope func_id:{} scope:{}\n", stack.back()->func_id, stack.back()->scope);

  for (auto var : stack.back()->declared) {
    std::print("var:{}\n", var);
    auto it = stack.back()->varmap.find(var);
    if (it != stack.back()->varmap.end()) {
      if (it->second) {
        it->second->dump();
      } else {
        std::cout << "nullptr bundle\n";
      }
    }
  }
}
