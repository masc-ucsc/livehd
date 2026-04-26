//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lnastfmt.hpp"

#include <format>
#include <string>

#include "perf_tracing.hpp"

static Pass_plugin sample("Pass_lnastfmt", Pass_lnastfmt::setup);

Pass_lnastfmt::Pass_lnastfmt(const Eprp_var& var) : Pass("pass.lnastfmt", var) {}

void Pass_lnastfmt::setup() {
  Eprp_method m1("pass.lnastfmt", "Validate LNAST syntax; leave the tree unchanged", &Pass_lnastfmt::fmt_begin);
  register_pass(m1);
}

void Pass_lnastfmt::fmt_begin(Eprp_var& var) {
  TRACE_EVENT("pass", "lnastfmt");

  Pass_lnastfmt p(var);

  for (const auto& ln : var.lnasts) {
    p.validate(ln.get());
  }
}

// Location string matched to the `lnast.dump` columns so the user can jump
// straight from the error to the offending node:
//   `[L,P pos1-pos2 line N @ fname]` — L/P is the tree index (level,pos)
//   printed by dump as `(L,P)`, pos1-pos2 is the dump's leading range,
//   line/fname come from the source token when available.
static std::string node_loc(const Lnast* ln, const lh::Tree_index& it) {
  const auto& data = ln->get_data(it);
  const auto& tok  = data.token;
  std::string s = std::format("[{},{} pos {}-{}", it.level, it.pos, tok.pos1, tok.pos2);
  if (tok.line != 0) {
    s += std::format(" line {}", tok.line);
  }
  if (!tok.fname.empty()) {
    s += std::format(" @ {}", tok.fname);
  }
  s += "]";
  return s;
}

// A `ref` carries a single variable name. Allowed shapes (today):
//   - tmp ref      : `___<digits>` (Slice 1; will move to `Tree_index` per §13)
//   - quoted ident : `` `…` `` — Pyrope's escape for names containing
//                    characters outside `[A-Za-z0-9_]`. Producers must keep
//                    the backticks ONLY when the name actually has a
//                    special character; ``a`` is normalized to `a`
//                    (prp2lnast strips the escape).
//   - dotted path  : `[$%#]?<id>(\.<id>)*` where `<id>` is
//                    `[A-Za-z_][A-Za-z0-9_]*`. The optional leading `$`/`%`/`#`
//                    is the port-direction/register prefix per
//                    `lnast_todo.md` §12 — input/output/reg, not a free
//                    "random identifier" form.
// Anything else (commas, spaces, colons, slashes, …) is a producer bug —
// most often a `get_text(lvalue)` that swallowed surrounding syntax (type
// cast, comma list, brackets) or a stale escape that wasn't stripped at
// lowering time.
static bool is_valid_ref_text(std::string_view name) {
  if (name.empty()) {
    return false;
  }
  if (name.front() == '`') {
    // Pyrope backtick-escaped name: must be terminated and must actually
    // need the escape — i.e. the inner text contains at least one char
    // that's not `[A-Za-z0-9_]` (otherwise prp2lnast should have stripped
    // the backticks). Reject ``a`` to keep the LNAST canonical.
    if (name.size() < 2 || name.back() != '`') {
      return false;
    }
    auto inner = name.substr(1, name.size() - 2);
    if (inner.empty()) {
      return false;
    }
    for (char ch : inner) {
      bool plain = (ch == '_') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
      if (!plain) {
        return true;  // escape needed — keep it
      }
    }
    return false;  // pure alnum/underscore — should have been stripped
  }
  size_t i = 0;
  if (name[0] == '$' || name[0] == '%' || name[0] == '#') {
    // Direction/storage prefix per `lnast_todo.md` §12: `$` input port,
    // `%` output port, `#` register. The rest must be a normal identifier.
    if (name.size() == 1) {
      return false;
    }
    i = 1;
  }
  // ___<digits> tmp form
  if (name.size() - i >= 4 && name.substr(i, 3) == "___") {
    for (size_t j = i + 3; j < name.size(); ++j) {
      if (name[j] < '0' || name[j] > '9') {
        return false;
      }
    }
    return name.size() > i + 3;
  }
  // Dotted identifier path
  bool start_of_segment = true;
  for (size_t j = i; j < name.size(); ++j) {
    char ch = name[j];
    if (ch == '.') {
      if (start_of_segment) {
        return false;  // empty segment
      }
      start_of_segment = true;
      continue;
    }
    bool ok = (ch == '_') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')
              || (!start_of_segment && ch >= '0' && ch <= '9');
    if (!ok) {
      return false;
    }
    start_of_segment = false;
  }
  return !start_of_segment;
}

void Pass_lnastfmt::validate(const Lnast* ln) {
  // Validation-only pass. Any normalization (SSA stripping, tuple rebuild,
  // …) must be safe for every producer before it can run here — until then
  // lnastfmt only checks and leaves the LNAST untouched.
  for (const lh::Tree_index& it : ln->depth_preorder()) {
    const auto& data = ln->get_data(it);

    if (data.type.is_ref()) {
      const auto name = data.token.get_text();
      if (!is_valid_ref_text(name)) {
        Pass::error(
            "lnastfmt: {} ref text '{}' is not a valid identifier — must be a single name "
            "(`tmp ___N`, `[$%#]?<id>(.<id>)*` over alnum/underscore, or "
            "`` `…` `` only if the name carries non-alnum characters). "
            "Commas/spaces/colons typically mean the producer captured surrounding syntax "
            "(type cast, lvalue list, …) into the ref; a backtick-escaped pure-alnum name "
            "(like ``a``) means the producer didn't normalize it.",
            node_loc(ln, it),
            name);
        return;
      }
    }

    if (data.type.is_const()) {
      const auto name = data.token.get_text();
      if (name == "__create_flop" || name == "__last_value") {
        Pass::error(
            "lnastfmt: {} const '{}' appears in LNAST; migrate the producer to `attr_set X storage reg` (§15.1) or `delay_assign` (§15.2)",
            node_loc(ln, it),
            name);
        return;
      }
    }

    if (data.type.is_delay_assign()) {
      auto c0 = ln->get_first_child(it);
      auto c1 = c0.is_invalid() ? c0 : ln->get_sibling_next(c0);
      auto c2 = c1.is_invalid() ? c1 : ln->get_sibling_next(c1);
      if (c0.is_invalid() || c1.is_invalid() || c2.is_invalid() || !ln->get_sibling_next(c2).is_invalid()) {
        Pass::error("lnastfmt: {} delay_assign must have exactly 3 children (dst, src, offset) per §15.2",
                    node_loc(ln, it));
        return;
      }
      if (!ln->get_type(c0).is_ref() || !Lnast::is_tmp(ln->get_name(c0))) {
        Pass::error("lnastfmt: {} delay_assign child 0 {} must be a tmp ref (___<n>), got '{}' of type {}",
                    node_loc(ln, it),
                    node_loc(ln, c0),
                    ln->get_name(c0),
                    ln->get_type(c0).debug_name());
        return;
      }
      if (!ln->get_type(c1).is_ref()) {
        Pass::error("lnastfmt: {} delay_assign child 1 {} must be a ref (declared variable name), got type {}",
                    node_loc(ln, it),
                    node_loc(ln, c1),
                    ln->get_type(c1).debug_name());
        return;
      }
      if (!ln->get_type(c2).is_const() && !ln->get_type(c2).is_ref()) {
        Pass::error("lnastfmt: {} delay_assign child 2 {} (offset) must be a const or comptime ref, got type {}",
                    node_loc(ln, it),
                    node_loc(ln, c2),
                    ln->get_type(c2).debug_name());
        return;
      }
    }

    if (data.type.is_assign() || data.type.is_dp_assign()) {
      auto c0 = ln->get_first_child(it);
      if (c0.is_invalid() || !ln->get_type(c0).is_ref()) {
        if (c0.is_invalid()) {
          Pass::error("lnastfmt: {} {} LHS is missing", node_loc(ln, it), data.type.debug_name());
        } else {
          Pass::error("lnastfmt: {} {} LHS {} must be a ref, got {} '{}'",
                      node_loc(ln, it),
                      data.type.debug_name(),
                      node_loc(ln, c0),
                      ln->get_type(c0).debug_name(),
                      ln->get_name(c0));
        }
        return;
      }
      auto c1 = ln->get_sibling_next(c0);
      if (c1.is_invalid()) {
        Pass::error("lnastfmt: {} {} with LHS '{}' is missing its RHS (expected exactly 2 children)",
                    node_loc(ln, it),
                    data.type.debug_name(),
                    ln->get_name(c0));
        return;
      }
      if (!ln->get_sibling_next(c1).is_invalid()) {
        Pass::error("lnastfmt: {} {} with LHS '{}' has more than 2 children",
                    node_loc(ln, it),
                    data.type.debug_name(),
                    ln->get_name(c0));
        return;
      }
    }
  }
}
