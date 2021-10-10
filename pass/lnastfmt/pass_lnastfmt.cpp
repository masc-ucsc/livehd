//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lnastfmt.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "absl/strings/match.h"
#include "lbench.hpp"

static Pass_plugin sample("Pass_lnastfmt", Pass_lnastfmt::setup);

Pass_lnastfmt::Pass_lnastfmt(const Eprp_var& var) : Pass("pass.lnastfmt", var) {}

void Pass_lnastfmt::setup() {
  Eprp_method m1("pass.lnastfmt", mmap_lib::str("Formats LNAST: remove SSA and recreate tuples"), &Pass_lnastfmt::fmt_begin);
  m1.add_label_optional("odir", mmap_lib::str("path to put the LNAST"), ".");
  register_pass(m1);
}

void Pass_lnastfmt::fmt_begin(Eprp_var& var) {
  fmt::print("beginning LNAST formatting pass\n");
  Lbench b("pass.lnastfmt");

  Pass_lnastfmt p(var);

  for (const auto& ln : var.lnasts) {
    p.parse_ln(ln, var, ln->get_top_module_name());
  }
}

void Pass_lnastfmt::parse_ln(const std::shared_ptr<Lnast>& ln, Eprp_var& var, const mmap_lib::str & module_name) {
  std::shared_ptr<Lnast> lnastfmted = std::make_shared<Lnast>(module_name);

  observe_lnast(ln.get());  // 1st traversal through the original LN to record assign subtrees.

  // now we will make the formatted LNAST:
  lnastfmted->set_root(
      Lnast_node(Lnast_ntype::create_top(), Etoken(0, 0, 0, 0, ln->get_top_module_name())));  // root node of lnfmted
  const auto& stmt_index = ln->get_child(mmap_lib::Tree_index::root());                       // stmt node of ln
  const auto& stmt_index_fmt
      = lnastfmted->add_child(mmap_lib::Tree_index::root(),
                              duplicate_node(lnastfmted, ln, stmt_index));  // stmt node of lnfmted (copied from ln)

  auto curr_index = ln->get_child(stmt_index);  // 1st child of ln after stmt

  while (curr_index != ln->invalid_index()) {
    // iterate through all children of stmt of ln
    bool curr_incremented = false;
    // check if curr_index's child is leaf child?
    // const auto& leaf_child_check = ln->get_child(curr_index);
    bool all_are_leaves = true;
    for (const mmap_lib::Tree_index& it : ln->children(curr_index)) {
      // fmt::print("PARSING TO CHECK LEAVES:   {}:{}\n",ln->get_name(it), it.level );
      if (!(ln->is_leaf(it))) {
        all_are_leaves = false;
      }
    }
    // fmt::print("?????all children leaves????: {}\n", all_are_leaves);
    if (all_are_leaves) {  // ln->is_leaf(leaf_child_check))
      if (ln->get_type(curr_index).is_assign()) {
        // if assign was curr_index; process assign subtree:
        const auto& frst_child_indx = ln->get_first_child(curr_index);        // assign fisrt child
        const auto& sec_child_indx  = ln->get_sibling_next(frst_child_indx);  // assign sec child
        auto        it              = ref_hash_map.find(ln->get_name(sec_child_indx));
        // fmt::print("===it:{}\n",it->second);
        // fmt::print("it.frst.find _ :{}\n", !(is_temp_var(it->first)));
        // fmt::print("it.second.find ___ :{}\n", !(is_temp_var(it->second)));
        bool in_map = false;
        if (it != ref_hash_map.end()) {
          in_map = true;
        }
        // fmt::print("in_map:{}\n", in_map);
        if (in_map && !(is_temp_var(it->second)) && !(is_temp_var(it->first))
            && (is_ssa(it->second) || is_ssa(it->first))) {  // sec child is ssa; do not attach this node!
          // fmt::print("****not adding {} and {}\n", ln->get_name(frst_child_indx), ln->get_name(sec_child_indx));
          curr_index       = ln->get_sibling_next(curr_index);
          curr_incremented = true;
        }
      }
      // if curr_index node was not ssa-assign
      // traverse and keep adding to lnfmted
      // if value from map used? replcae it and move on.
      if (curr_index != ln->invalid_index() && !curr_incremented) {
        auto curr_index_fmt = lnastfmted->add_child(stmt_index_fmt, duplicate_node(lnastfmted, ln, curr_index));

        for (const mmap_lib::Tree_index& it : ln->children(curr_index)) {
          auto is = ref_hash_map.find(ln->get_name(it));
          if (is != ref_hash_map.end() && is_ssa(ln->get_name(it))) {
            // auto frst_fmt =
            lnastfmted->add_child(curr_index_fmt, Lnast_node::create_ref(is->second));
          } else {
            lnastfmted->add_child(curr_index_fmt, duplicate_node(lnastfmted, ln, it));
          }
        }
      }
    } else {  // This else is for nested subtrees (like an "if" subtree)

      auto curr_index_fmt = lnastfmted->add_child(stmt_index_fmt, duplicate_node(lnastfmted, ln, curr_index));
      auto curr_lev       = curr_index.level;
      auto curr_pos       = curr_index.pos;
      for (const mmap_lib::Tree_index& it : ln->depth_preorder(curr_index)) {
        //        if (((it.level == curr_index.level) && (it.pos > curr_index.pos)) || (it.level < curr_index.level)) {
        //         break;
        //       }//This if is needed because depth preorder traverses the next subtree as well. It does not stop after traversing
        //       the particular subtree (of root curr_index)
        auto new_lev = it.level;
        auto new_pos = it.pos;
        auto is      = ref_hash_map.find(ln->get_name(it));

        if (new_lev == curr_lev + 1) {
          curr_lev = new_lev;
          curr_pos = new_pos;
          if (is != ref_hash_map.end() && is_ssa(ln->get_name(it))) {
            curr_index_fmt = lnastfmted->add_child(curr_index_fmt, Lnast_node::create_ref(is->second));
          } else {
            curr_index_fmt = lnastfmted->add_child(curr_index_fmt, duplicate_node(lnastfmted, ln, it));
          }
        } else if (new_lev == curr_lev && new_pos > curr_pos) {
          if (is != ref_hash_map.end() && is_ssa(ln->get_name(it))) {
            curr_index_fmt = lnastfmted->append_sibling(curr_index_fmt, Lnast_node::create_ref(is->second));
          } else {
            curr_index_fmt = lnastfmted->append_sibling(curr_index_fmt, duplicate_node(lnastfmted, ln, it));
          }
          curr_lev = new_lev;
          curr_pos = new_pos;
        } else if (new_lev + 1 == curr_lev) {
          if (is != ref_hash_map.end() && is_ssa(ln->get_name(it))) {
            curr_index_fmt = lnastfmted->append_sibling(lnastfmted->get_parent(curr_index_fmt),
                                                        Lnast_node::create_ref(is->second));
          } else {
            curr_index_fmt = lnastfmted->append_sibling(lnastfmted->get_parent(curr_index_fmt), duplicate_node(lnastfmted, ln, it));
          }
          curr_lev = new_lev;
          curr_pos = new_pos;
        } else if (new_lev + 2 == curr_lev) {
          if (is != ref_hash_map.end() && is_ssa(ln->get_name(it))) {
            curr_index_fmt = lnastfmted->append_sibling(lnastfmted->get_parent(lnastfmted->get_parent(curr_index_fmt)),
                                                        Lnast_node::create_ref(is->second));
          } else {
            curr_index_fmt = lnastfmted->append_sibling(lnastfmted->get_parent(lnastfmted->get_parent(curr_index_fmt)),
                                                        duplicate_node(lnastfmted, ln, it));
          }
          curr_lev = new_lev;
          curr_pos = new_pos;
        } else if (new_lev + 3 == curr_lev) {
          if (is != ref_hash_map.end() && is_ssa(ln->get_name(it))) {
            curr_index_fmt
                = lnastfmted->append_sibling(lnastfmted->get_parent(lnastfmted->get_parent(lnastfmted->get_parent(curr_index_fmt))),
                                             Lnast_node::create_ref(is->second));
          } else {
            curr_index_fmt
                = lnastfmted->append_sibling(lnastfmted->get_parent(lnastfmted->get_parent(lnastfmted->get_parent(curr_index_fmt))),
                                             duplicate_node(lnastfmted, ln, it));
          }
          curr_lev = new_lev;
          curr_pos = new_pos;
        }
      }
    }
    if (curr_index != ln->invalid_index() && !curr_incremented) {
      curr_index = ln->get_sibling_next(curr_index);
    }
  }

  // fmt::print("****lnast fmted:  \n");
  // lnastfmted->dump();
  var.replace(ln, lnastfmted);  // just replace the pointer
  // var.add(std::move(lnastfmted));//FIXME. make replace function instead of add
}

void Pass_lnastfmt::observe_lnast(Lnast* ln) {
  for (const mmap_lib::Tree_index& it : ln->depth_preorder()) {
    process_node(ln, it);
  }

  fmt::print("ref_hash_map entry: key, value:\n");
  for (auto const& pair : ref_hash_map) {
    fmt::print("{}, {}\n", pair.first, pair.second);
  }
}

void Pass_lnastfmt::process_node(Lnast* ln, const mmap_lib::Tree_index& it) {
  const auto& node_data = ln->get_data(it);

  if (node_data.type.is_assign() || node_data.type.is_dp_assign()) {
    auto frst_child_indx = ln->get_first_child(it);
    I(ln->get_type(frst_child_indx).debug_name() == "ref", "unexpected node found! not ref!?");
    fmt::print("first child type and data: {}, {}\n", ln->get_type(frst_child_indx).debug_name(), ln->get_name(frst_child_indx));

    auto sec_child_indx = ln->get_sibling_next(frst_child_indx);
    fmt::print("sec child type and data: {}, {}\n", ln->get_type(sec_child_indx).debug_name(), ln->get_name(sec_child_indx));

    I(ln->get_sibling_next(sec_child_indx).is_invalid(), "This assign node has more than 2 children??");

    ref_hash_map.try_emplace(ln->get_name(sec_child_indx), ln->get_name(frst_child_indx));  // insert key, value pair.
  }
}

bool Pass_lnastfmt::is_temp_var(const mmap_lib::str & test_string) {
  return test_string.starts_with("___") || test_string.starts_with("_._");
}

bool Pass_lnastfmt::is_ssa(const mmap_lib::str  &test_string) {
  return test_string.substr(0,2) != "__";
}

Lnast_node Pass_lnastfmt::duplicate_node(std::shared_ptr<Lnast>& lnastfmted, const std::shared_ptr<Lnast>& ln,
                                         const mmap_lib::Tree_index& it) {
  (void) lnastfmted;
  // auto orig_node_token = ln->get_token(it);
  // auto orig_node_subs = ln->get_subs(it);
  auto       orig_node_name = ln->get_name(it);
  auto       orig_node_type = ln->get_type(it);
  Lnast_node new_node;
  if (orig_node_type.is_ref()) {
    new_node = Lnast_node::create_ref(orig_node_name);
  } else if (orig_node_type.is_top()) {
    new_node = Lnast_node::create_top();
  } else if (orig_node_type.is_stmts()) {
    new_node = Lnast_node::create_stmts();
  } else if (orig_node_type.is_if()) {
    new_node = Lnast_node::create_if();
  } else if (orig_node_type.is_uif()) {
    new_node = Lnast_node::create_uif();
  } else if (orig_node_type.is_for()) {
    new_node = Lnast_node::create_for();
  } else if (orig_node_type.is_while()) {
    new_node = Lnast_node::create_while();
  } else if (orig_node_type.is_phi()) {
    new_node = Lnast_node::create_phi();
  } else if (orig_node_type.is_hot_phi()) {
    new_node = Lnast_node::create_hot_phi();
  } else if (orig_node_type.is_func_call()) {
    new_node = Lnast_node::create_func_call();
  } else if (orig_node_type.is_func_def()) {
    new_node = Lnast_node::create_func_def();
  } else if (orig_node_type.is_assign()) {
    new_node = Lnast_node::create_assign();
  } else if (orig_node_type.is_dp_assign()) {
    new_node = Lnast_node::create_dp_assign();
  } else if (orig_node_type.is_mut()) {
    new_node = Lnast_node::create_mut();
  } else if (orig_node_type.is_bit_and()) {
    new_node = Lnast_node::create_bit_and();
  } else if (orig_node_type.is_bit_or()) {
    new_node = Lnast_node::create_bit_or();
  } else if (orig_node_type.is_bit_not()) {
    new_node = Lnast_node::create_bit_not();
  } else if (orig_node_type.is_bit_xor()) {
    new_node = Lnast_node::create_bit_xor();
  } else if (orig_node_type.is_logical_and()) {
    new_node = Lnast_node::create_logical_and();
  } else if (orig_node_type.is_logical_or()) {
    new_node = Lnast_node::create_logical_or();
  } else if (orig_node_type.is_logical_not()) {
    new_node = Lnast_node::create_logical_not();
  } else if (orig_node_type.is_reduce_or()) {
    new_node = Lnast_node::create_reduce_or();
  } else if (orig_node_type.is_plus()) {
    new_node = Lnast_node::create_plus();
  } else if (orig_node_type.is_minus()) {
    new_node = Lnast_node::create_minus();
  } else if (orig_node_type.is_mult()) {
    new_node = Lnast_node::create_mult();
  } else if (orig_node_type.is_div()) {
    new_node = Lnast_node::create_div();
  } else if (orig_node_type.is_mod()) {
    new_node = Lnast_node::create_mod();
  } else if (orig_node_type.is_shl()) {
    new_node = Lnast_node::create_shl();
  } else if (orig_node_type.is_sra()) {
    new_node = Lnast_node::create_sra();
  } else if (orig_node_type.is_sext()) {
    new_node = Lnast_node::create_sext();
  } else if (orig_node_type.is_get_mask()) {
    new_node = Lnast_node::create_get_mask();
  } else if (orig_node_type.is_mask_and()) {
    new_node = Lnast_node::create_mask_and();
  } else if (orig_node_type.is_mask_popcount()) {
    new_node = Lnast_node::create_mask_popcount();
  } else if (orig_node_type.is_mask_xor()) {
    new_node = Lnast_node::create_mask_xor();
  } else if (orig_node_type.is_set_mask()) {
    new_node = Lnast_node::create_set_mask();
  } else if (orig_node_type.is_is()) {
    new_node = Lnast_node::create_is();
  } else if (orig_node_type.is_ne()) {
    new_node = Lnast_node::create_ne();
  } else if (orig_node_type.is_eq()) {
    new_node = Lnast_node::create_eq();
  } else if (orig_node_type.is_lt()) {
    new_node = Lnast_node::create_lt();
  } else if (orig_node_type.is_le()) {
    new_node = Lnast_node::create_le();
  } else if (orig_node_type.is_gt()) {
    new_node = Lnast_node::create_gt();
  } else if (orig_node_type.is_ge()) {
    new_node = Lnast_node::create_ge();
  } else if (orig_node_type.is_tuple_concat()) {
    new_node = Lnast_node::create_tuple_concat();
  } else if (orig_node_type.is_ref()) {
    new_node = Lnast_node::create_ref(orig_node_name);
  } else if (orig_node_type.is_const()) {
    new_node = Lnast_node::create_const(orig_node_name);
  } else if (orig_node_type.is_err_flag()) {
    new_node = Lnast_node::create_err_flag();
  } else if (orig_node_type.is_tuple_add()) {
    new_node = Lnast_node::create_tuple_add();
  } else if (orig_node_type.is_tuple_get()) {
    new_node = Lnast_node::create_tuple_get();
  } else if (orig_node_type.is_attr_set()) {
    new_node = Lnast_node::create_attr_set();
  } else if (orig_node_type.is_attr_get()) {
    new_node = Lnast_node::create_attr_get();
  } else {
    I(false, "Please check the node type and add it");
  }
  // Lnast_node new_node = Lnast_node::create_ref(orig_node_name);

  return new_node;
}
