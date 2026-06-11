//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_semacheck.hpp"

#include <format>

#include "diag.hpp"
#include "lnast.hpp"
#include "lnast_ntype.hpp"

// Static initializer fires at program startup so the runner discovers
// "semacheck" via uPass_plugin::get_registry() without explicit wiring.
static upass::uPass_plugin plugin_semacheck("semacheck", upass::uPass_wrapper<uPass_semacheck>::get_upass);

namespace {

// Task 1t — derived / read-only attributes. These are READ-ONLY views computed
// from the type or bundle (see the LiveHD docs and
// upass/attributes is_builtin_attr Category A) — an `attr_set` of any is a
// compile error: read them with `.[attr]` (attr_get), never write them.
//   * `max`/`min` are additionally settable, but ONLY through a *type*
//     (`:int(max=N,min=M)`, `:uN`/`:sN`), never via attr_set.
//   * `range` is not an attribute at all — `:[range=0..=15]` was legacy sugar
//     for max/min and is now rejected the same way (declare a type instead).
//   * `bits`/`size`/`sign`/`key` are pure reads — there is no way to set them.
// Returns a human-readable reason when `attr` is one of these, else "".
//
// NOTE — deliberately NOT listed (yet): `ubits`/`sbits` (the producer emits
// attr_set for these as a transient width mechanism on tuple_get bit-range
// writes), `typename` (the producer sets a named type's identity via attr_set),
// `comptime`/`type`/`wrap`/`sat` (decl mode + per-write overflow). Those are
// also derived/mode and SHOULD become attr_set errors once T3/T6 stop the
// producer from emitting them; flagging them now would reject the compiler's
// own output. The seven below are never emitted by the producer, so they are
// safe to enforce today.
std::string_view derived_attr_violation(std::string_view attr) {
  if (attr == "max" || attr == "min") {
    return "settable only through a type (`:int(max=,min=)`, `:uN`/`:sN`), never via an attribute write";
  }
  if (attr == "range") {
    return "not an attribute; declare the bounds through a type instead (`:int(max=,min=)`, `:uN`/`:sN`)";
  }
  if (attr == "bits" || attr == "size" || attr == "sign" || attr == "key") {
    return "a derived read-only attribute (computed from the type/bundle); it can be read but not set";
  }
  return {};
}

// Task 1t — declare-once. The storage kind of a `declare` is the FIRST
// space-separated token of its mode const ("const", "mut", "reg", "type", …).
// (Kept for parity with the relocated lnastfmt check; all kinds redeclare-error.)

}  // namespace

void uPass_semacheck::emit_sema_error(std::string_view code, std::string_view category, const std::string& msg,
                                      std::string_view hint, const Lnast* ln, const Lnast_nid& nid) {
  auto span = ln->span_of(nid);  // SourceId resolved through the Lnast's locator ([[1f]])
  livehd::diag::sink().emit(livehd::diag::Diagnostic{
      .severity = livehd::diag::Severity::error,
      .code     = std::string{code},
      .category = std::string{category},
      .pass     = "upass.semacheck",
      .message  = msg,
      .span     = std::move(span),
      .hint     = std::string{hint},
  });
}

// Reject `attr_set` of a derived / read-only attribute. The attribute being
// written is the LAST selector — the child immediately before the value (the
// value is always the last child; an attribute path `a.b."attr"` lowers to
// attr_set(a, "b", "attr", value), so walk to the second-to-last child rather
// than assuming child 1).
void uPass_semacheck::check_attr_writes(const Lnast* ln) {
  for (const Lnast_nid& it : ln->depth_preorder()) {
    if (!Lnast_ntype::is_attr_set(ln->get_type(it))) {
      continue;
    }
    auto c0 = ln->get_first_child(it);
    auto c1 = c0.is_invalid() ? c0 : ln->get_sibling_next(c0);
    auto c2 = c1.is_invalid() ? c1 : ln->get_sibling_next(c1);
    if (c0.is_invalid() || c1.is_invalid() || c2.is_invalid()) {
      continue;  // malformed shape — lnastfmt's structural validator owns that
    }
    auto attr_nid  = c1;
    auto value_nid = c2;
    for (auto n = ln->get_sibling_next(c2); !n.is_invalid(); n = ln->get_sibling_next(n)) {
      attr_nid  = value_nid;
      value_nid = n;
    }
    if (!Lnast_ntype::is_const(ln->get_type(attr_nid))) {
      continue;
    }
    auto attr = ln->get_name(attr_nid);
    if (auto why = derived_attr_violation(attr); !why.empty()) {
      emit_sema_error("read-only-attr-write",
                      "type",
                      std::format("cannot write attribute `.[{}]` — `{}` is {}", attr, attr, why),
                      "read it with `.[attr]`; set max/min by declaring a type, e.g. `:u8` or `:int(max=,min=)`",
                      ln,
                      it);
      return;  // first violation only — matches the relocated lnastfmt behavior
    }
  }
}

// Declare-once + no-shadowing on the source tree. A `declare` colliding with
// the current frame is a redeclaration; with an enclosing frame is shadowing.
// Sibling frames are independent; a function body is a fresh namespace (outer
// locals are not visible inside a comb — the Pyrope closure rule).
//
// NOTE: assign-before-declare (`a = 3` with no prior declaration) is NOT
// checked here — that check runs in prp2lnast on the producer tree (see
// Prp2lnast::check_undeclared_writes), where inliner/SSA-synthesized stores
// can't false-positive.
void uPass_semacheck::check_scope(const Lnast* ln, const Lnast_nid& scope_stmts,
                                  const std::unordered_set<std::string>& visible) {
  std::unordered_set<std::string> here;
  for (auto c = ln->get_first_child(scope_stmts); !c.is_invalid(); c = ln->get_sibling_next(c)) {
    const auto ct = ln->get_type(c);
    if (Lnast_ntype::is_declare(ct)) {
      auto c0 = ln->get_first_child(c);
      if (!c0.is_invalid() && Lnast_ntype::is_ref(ln->get_type(c0))) {
        auto name = std::string(ln->get_name(c0));
        if (here.contains(name)) {
          emit_sema_error("redeclaration",
                          "name",
                          std::format("redeclaration of variable '{}' in the same scope (declare it only once)", name),
                          std::format("to change its value, reassign (`{} = …`) instead of re-declaring", name),
                          ln,
                          c);
          return;
        }
        if (visible.contains(name)) {
          emit_sema_error(
              "variable-shadowing",
              "name",
              std::format("declaration of '{}' shadows an outer-scope variable (variable shadowing is not allowed)", name),
              "rename the inner variable",
              ln,
              c);
          return;
        }
        here.insert(name);
      }
    }
    const bool is_func_body = Lnast_ntype::is_func_def(ct);
    for (auto cc = ln->get_first_child(c); !cc.is_invalid(); cc = ln->get_sibling_next(cc)) {
      if (Lnast_ntype::is_stmts(ln->get_type(cc))) {
        if (is_func_body) {
          check_scope(ln, cc, {});
        } else {
          std::unordered_set<std::string> combined = visible;
          combined.insert(here.begin(), here.end());
          check_scope(ln, cc, combined);
        }
      }
    }
  }
}

void uPass_semacheck::begin_iteration() {
  if (ran_) {
    return;  // one-shot per LNAST (single-walk model; guard for safety)
  }
  ran_ = true;

  const auto& ln_sp = lm->get_lnast();
  if (!ln_sp) {
    return;
  }
  const Lnast* ln = ln_sp.get();

  check_attr_writes(ln);

  // Walk each top-level `stmts` block; nested scopes recurse with the
  // enclosing visible names.
  const auto root = ln->get_root();
  if (root.is_invalid()) {
    return;
  }
  for (auto c = ln->get_first_child(root); !c.is_invalid(); c = ln->get_sibling_next(c)) {
    if (Lnast_ntype::is_stmts(ln->get_type(c))) {
      check_scope(ln, c, {});
    }
  }
}
