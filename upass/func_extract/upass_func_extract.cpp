//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_func_extract.hpp"

#include <utility>

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
