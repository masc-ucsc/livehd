//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_func_extract.hpp"

#include <functional>
#include <utility>

#include "dlop.hpp"
#include "lnast_ntype.hpp"

static upass::uPass_plugin plugin_func_extract("func_extract", upass::uPass_wrapper<uPass_func_extract>::get_upass);

uPass_func_extract::uPass_func_extract(std::shared_ptr<upass::Lnast_manager>& _lm) : uPass(_lm) {}

std::string uPass_func_extract::strip_io_prefix(std::string_view name) { return std::string(name); }

void uPass_func_extract::copy_current_subtree(const std::shared_ptr<Lnast>& dst, const Lnast_nid& parent) {
  const auto type       = lm->current_type();
  auto       new_parent = (Lnast_ntype::is_ref(type) || Lnast_ntype::is_const(type)) ? dst->add_child(parent, lm->current_node())
                                                                                     : dst->add_child(parent, type);
  if (!lm->has_child()) {
    return;
  }

  move_to_child();
  do {
    copy_current_subtree(dst, new_parent);
  } while (move_to_sibling());
  move_to_parent();
}

void uPass_func_extract::copy_current_children(const std::shared_ptr<Lnast>& dst, const Lnast_nid& parent) {
  if (!lm->has_child()) {
    return;
  }

  move_to_child();
  do {
    copy_current_subtree(dst, parent);
  } while (move_to_sibling());
  move_to_parent();
}

// Copy the func_def's input/output `tuple_add(assign(ref,default,type?)...)`
// signature subtree into the extracted lnast under `io_idx`. The shape is
// preserved verbatim: composite tuple types stay as a `tuple_add` 3rd child
// of their assign. SSA will later expand those into flat dotted leaves.
bool uPass_func_extract::emit_io_tuple_from_decl(const std::shared_ptr<Lnast>& dst, const Lnast_nid& io_idx) {
  if (!lm->has_child()) {
    // Empty signature slot. Emit `tuple_add(ref '__empty_tuple')` so
    // constprop's inline path (which keys on the first-child ref name —
    // __input_tuple_ref / __output_tuple_ref / __empty_tuple — to read
    // signatures back) sees a marker rather than a shapeless tuple_add.
    // Without it, callees with no inputs look identical to a malformed
    // tuple_add and constprop falls through to its body-statement path.
    auto tup_idx = dst->add_child(io_idx, Lnast_ntype::create_tuple_add());
    dst->add_child(tup_idx, Lnast_node::create_ref("__empty_tuple"));
    return false;
  }
  auto tup_idx = dst->add_child(io_idx, Lnast_ntype::create_tuple_add());
  copy_current_children(dst, tup_idx);
  return true;
}

void uPass_func_extract::process_stmts() { ++stmts_depth; }

void uPass_func_extract::process_stmts_post() { --stmts_depth; }

void uPass_func_extract::process_assign() {
  // Definition-time closure-capture tracking. A name is recorded as a
  // visible constant for any comb defined later in this scope IFF its
  // only write so far was an unconditional, top-level `assign <ref>
  // <const>`. Any of the following pushes it into `outer_non_const`
  // permanently, so it never escapes the function boundary:
  //   * RHS is not a trivial Const (ref, computed, bundle).
  //   * The assign sits inside any nested stmts (an if-arm with a stmts
  //     wrapper, a loop body, a scope_statement, …).
  //   * The assign sits directly under an `if` node (the flat
  //     when/unless gated form — `c = 100 when cond`).
  //   * A second write to the same name (any shape, any place).
  if (!lm->has_child()) {
    return;
  }
  const auto saved = lm->save_cursor();
  lm->move_to_child();
  std::string lhs_name;
  if (Lnast_ntype::is_ref(lm->current_type())) {
    lhs_name = std::string(lm->current_text());
  }
  if (lhs_name.empty() || !lm->move_to_sibling()) {
    lm->restore_cursor(saved);
    return;
  }
  const bool rhs_is_const = Lnast_ntype::is_const(lm->current_type());
  std::string rhs_text;
  if (rhs_is_const) {
    rhs_text = std::string(lm->current_text());
  }
  lm->restore_cursor(saved);

  auto invalidate = [&]() {
    latest_outer_value.erase(lhs_name);
    outer_non_const.insert(lhs_name);
  };

  if (outer_non_const.contains(lhs_name)) {
    return;
  }
  // Second write to the same name → no longer a stable constant.
  if (latest_outer_value.contains(lhs_name)) {
    invalidate();
    return;
  }
  if (!rhs_is_const || stmts_depth > 1) {
    invalidate();
    return;
  }
  try {
    latest_outer_value[lhs_name] = *Dlop::from_pyrope(rhs_text);
  } catch (...) {
    invalidate();
  }
}

void uPass_func_extract::collect_body_refs(const std::shared_ptr<Lnast>& body, std::unordered_set<std::string>& refs) const {
  if (!body) {
    return;
  }
  std::function<void(const Lnast_nid&)> walk = [&](const Lnast_nid& nid) {
    if (nid.is_invalid()) {
      return;
    }
    if (Lnast_ntype::is_ref(body->get_type(nid))) {
      refs.insert(std::string(body->get_name(nid)));
    }
    for (auto c = body->get_child(nid); !c.is_invalid(); c = body->get_sibling_next(c)) {
      walk(c);
    }
  };
  walk(body->get_root());
}


void uPass_func_extract::process_func_def() {
  drop_current_func_def = false;

  if (!lm->has_child()) {
    return;
  }

  move_to_child();
  const auto func_name = std::string(current_text());
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  const auto func_kind = std::string(current_text());

  if (func_kind != "comb" || func_name.empty()) {
    move_to_parent();
    return;
  }

  const auto extracted_name = std::string(lm->get_top_module_name()) + "." + func_name;
  drop_current_func_def     = true;

  if (extracted_names.contains(extracted_name)) {
    move_to_parent();
    return;
  }
  extracted_names.insert(extracted_name);

  auto new_lnast = std::make_shared<Lnast>(extracted_name);
  auto root_nid  = new_lnast->set_root(Lnast_ntype::create_top());

  // Skip generics for this first comb-only extraction slice. The func_def
  // shape after the captures-slot removal is:
  //   func_def(name, kind, generics, inputs, outputs, stmts)
  // so from `kind` we need two sibling steps to reach `inputs`.
  // No signature => bare `top -> stmts` (no `io` child).
  if (!move_to_sibling() || !move_to_sibling()) {
    new_lnast->add_child(root_nid, Lnast_ntype::create_stmts());
    move_to_parent();
    extracted_lnasts.emplace_back(std::move(new_lnast));
    return;
  }

  // Signature present => `top -> [io, stmts]`. The io node directly contains
  // two tuple_add children — the inputs and outputs signatures copied
  // verbatim from the func_def (composite tuple-typed entries still nested).
  // SSA later rewrites tuple-typed entries into flat dotted-leaf form.
  auto io_idx = new_lnast->add_child(root_nid, Lnast_ntype::create_io());
  auto stmts  = new_lnast->add_child(root_nid, Lnast_ntype::create_stmts());

  emit_io_tuple_from_decl(new_lnast, io_idx);

  if (move_to_sibling()) {
    emit_io_tuple_from_decl(new_lnast, io_idx);
  } else {
    // No outputs slot in the source func_def — emit
    // `tuple_add(ref '__empty_tuple')` so io always has the
    // [inputs, outputs] pair and downstream readers can distinguish
    // "no slot" from "empty slot".
    auto tup_idx = new_lnast->add_child(io_idx, Lnast_ntype::create_tuple_add());
    new_lnast->add_child(tup_idx, Lnast_node::create_ref("__empty_tuple"));
  }

  if (move_to_sibling()) {
    // Closure capture for `comb`: any outer name the body reads and whose
    // current value was set by a trivial-Const `assign` in the surrounding
    // scope gets inlined at the head of the extracted body's stmts. The
    // copied body content lands AFTER these capture stmts, so constprop's
    // ordinary top-down walk sees the binding before the use — no
    // cross-pass closure plumbing, no extra branch in `try_eval_comb_call`,
    // no special symbol-table rule. Non-trivial outer bindings (bundles,
    // unresolved refs) are absent from `latest_outer_value` by
    // construction, so they naturally don't cross the function boundary —
    // that's the "constants flow up, non-constants stop at the function
    // scope" rule, expressed structurally.
    if (!latest_outer_value.empty()) {
      // Scan the SOURCE body (`lm` cursor is at the func_def's stmts) for
      // every name it reads, so we only inline captures the body actually
      // consumes.
      std::unordered_set<std::string> body_refs;
      {
        const auto& src      = lm->get_lnast();
        const auto  body_nid = lm->get_current_nid();
        std::function<void(const Lnast_nid&)> walk = [&](const Lnast_nid& nid) {
          if (nid.is_invalid()) {
            return;
          }
          if (Lnast_ntype::is_ref(src->get_type(nid))) {
            body_refs.insert(std::string(src->get_name(nid)));
          }
          for (auto c = src->get_child(nid); !c.is_invalid(); c = src->get_sibling_next(c)) {
            walk(c);
          }
        };
        walk(body_nid);
      }
      for (const auto& name : body_refs) {
        auto it = latest_outer_value.find(name);
        if (it == latest_outer_value.end()) {
          continue;
        }
        auto a_idx = new_lnast->add_child(stmts, Lnast_ntype::create_assign());
        new_lnast->add_child(a_idx, Lnast_node::create_ref(name));
        new_lnast->add_child(a_idx, Lnast_node::create_const(it->second.to_pyrope()));
      }
    }
    copy_current_children(new_lnast, stmts);
  }

  move_to_parent();
  extracted_lnasts.emplace_back(std::move(new_lnast));
}

upass::Emit_decision uPass_func_extract::classify_statement() {
  if (!drop_current_func_def) {
    return upass::Emit_decision::emit_node();
  }

  drop_current_func_def = false;
  return upass::Emit_decision::drop();
}

std::vector<std::shared_ptr<Lnast>> uPass_func_extract::take_new_lnasts() { return std::move(extracted_lnasts); }
