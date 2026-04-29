//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_func_extract.hpp"

#include <utility>

#include "lnast_ntype.hpp"

static upass::uPass_plugin plugin_func_extract("func_extract", upass::uPass_wrapper<uPass_func_extract>::get_upass);

namespace {
constexpr std::string_view input_tuple_name  = "__input_tuple_ref";
constexpr std::string_view output_tuple_name = "__output_tuple_ref";
constexpr std::string_view empty_tuple_name  = "__empty_tuple";
}  // namespace

uPass_func_extract::uPass_func_extract(std::shared_ptr<upass::Lnast_manager>& _lm) : uPass(_lm) {}

std::string uPass_func_extract::strip_io_prefix(std::string_view name) {
  if (!name.empty() && (name.front() == '$' || name.front() == '%')) {
    name.remove_prefix(1);
  }
  return std::string(name);
}

void uPass_func_extract::copy_current_subtree(const std::shared_ptr<Lnast>& dst, const Lnast_nid& parent) {
  auto new_parent = dst->add_child(parent, lm->current_node());
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

bool uPass_func_extract::emit_io_tuple_from_decl(const std::shared_ptr<Lnast>& dst, const Lnast_nid& stmts,
                                                 std::string_view tuple_name) {
  if (!lm->has_child()) {
    return false;
  }

  auto tuple_idx = dst->add_child(stmts, Lnast_node::create_tuple_add());
  dst->add_child(tuple_idx, Lnast_node::create_ref(tuple_name));

  move_to_child();
  do {
    if (!is_type(Lnast_ntype::Lnast_ntype_assign) || !lm->has_child()) {
      continue;
    }

    move_to_child();
    const auto internal_name = std::string(current_text());
    const auto field_name    = strip_io_prefix(internal_name);
    move_to_parent();

    auto assign_idx = dst->add_child(tuple_idx, Lnast_node::create_assign());
    dst->add_child(assign_idx, Lnast_node::create_ref(field_name));
    dst->add_child(assign_idx, Lnast_node::create_const("nil"));
  } while (move_to_sibling());
  move_to_parent();

  return true;
}

void uPass_func_extract::emit_empty_tuple(const std::shared_ptr<Lnast>& dst, const Lnast_nid& stmts) {
  auto tuple_idx = dst->add_child(stmts, Lnast_node::create_tuple_add());
  dst->add_child(tuple_idx, Lnast_node::create_ref(empty_tuple_name));
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
  auto root_nid  = new_lnast->set_root(Lnast_node::create_top());
  auto stmts     = new_lnast->add_child(root_nid, Lnast_node::create_stmts());

  // Skip generics and captures for this first comb-only extraction slice.
  if (!move_to_sibling() || !move_to_sibling() || !move_to_sibling()) {
    move_to_parent();
    extracted_lnasts.emplace_back(std::move(new_lnast));
    return;
  }

  const bool has_input_tuple = emit_io_tuple_from_decl(new_lnast, stmts, input_tuple_name);

  if (!move_to_sibling()) {
    if (!has_input_tuple) {
      emit_empty_tuple(new_lnast, stmts);
    }
    auto io_idx = new_lnast->add_child(stmts, Lnast_node::create_io());
    new_lnast->add_child(io_idx, Lnast_node::create_ref(has_input_tuple ? input_tuple_name : empty_tuple_name));
    new_lnast->add_child(io_idx, Lnast_node::create_ref(empty_tuple_name));
    move_to_parent();
    extracted_lnasts.emplace_back(std::move(new_lnast));
    return;
  }

  const bool has_output_tuple = emit_io_tuple_from_decl(new_lnast, stmts, output_tuple_name);

  if (!has_input_tuple || !has_output_tuple) {
    emit_empty_tuple(new_lnast, stmts);
  }

  auto io_idx = new_lnast->add_child(stmts, Lnast_node::create_io());
  new_lnast->add_child(io_idx, Lnast_node::create_ref(has_input_tuple ? input_tuple_name : empty_tuple_name));
  new_lnast->add_child(io_idx, Lnast_node::create_ref(has_output_tuple ? output_tuple_name : empty_tuple_name));

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
