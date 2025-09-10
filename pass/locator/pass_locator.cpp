//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_locator.hpp"

#include "perf_tracing.hpp"

static Pass_plugin sample("pass_locator", Pass_locator::setup);

void Pass_locator::setup() {
  Eprp_method m1("pass.locator",
                 "after inou.pyrope or any other FE pass that creates LN.(For paper: LoC.) (Paper pass)",
                 &Pass_locator::begin_pass);
  // m1.add_label_optional("odir", "path to put the LNAST", ".");
  register_pass(m1);
}

Pass_locator::Pass_locator(const Eprp_var& var) : Pass("pass.locator", var) {
  if (var.has_label("top")) {
    top = var.get("top");
  }
}

void Pass_locator::begin_pass(Eprp_var& var) {
  std::cout << "beginning locator pass\n";
  TRACE_EVENT("pass", "locator");

  Pass_locator p(var);

  /*for (const auto& ln : var.lnasts) {
    p.parse_ln(ln, var);
  }*/
}

/*void Pass_locator::parse_ln(const std::shared_ptr<Lnast>& ln, Eprp_var& var) {
  std::string module_name=top;
  std::shared_ptr<Lnast> lnastfmted = std::make_shared<Lnast>(module_name);

  observe_lnast(ln.get());  // 1st traversal through the original LN to record assign subtrees.

  // now we will make the formatted LNAST:
  lnastfmted->set_root(
      Lnast_node(Lnast_ntype::create_top(), State_token(0, 0, 0, 0, ln->get_top_module_name())));  // root node of lnfmted
  const auto& stmt_index = ln->get_child(lh::Tree_index::root());                                  // stmt node of ln
  const auto& stmt_index_fmt
      = lnastfmted->add_child(lh::Tree_index::root(),
                              duplicate_node(lnastfmted, ln, stmt_index));  // stmt node of lnfmted (copied from ln)

  auto curr_index = ln->get_child(stmt_index);  // 1st child of ln after stmt

  while (curr_index != ln->invalid_index()) {
    // iterate through all children of stmt of ln
    bool curr_incremented = false;
    // check if curr_index's child is leaf child?
    // const auto& leaf_child_check = ln->get_child(curr_index);
    bool all_are_leaves = true;
    for (const lh::Tree_index& it : ln->children(curr_index)) {
      // std::print("PARSING TO CHECK LEAVES:   {}:{}\n",ln->get_name(it), it.level );
      if (!(ln->is_leaf(it))) {
        all_are_leaves = false;
      }
    }
    // std::print("?????all children leaves????: {}\n", all_are_leaves);
    if (all_are_leaves) {  // ln->is_leaf(leaf_child_check))
      if (ln->get_type(curr_index).is_assign()) {
        // if assign was curr_index; process assign subtree:
        const auto& frst_child_indx = ln->get_first_child(curr_index);        // assign fisrt child
        const auto& sec_child_indx  = ln->get_sibling_next(frst_child_indx);  // assign sec child
        auto        it              = ref_hash_map.find(ln->get_name(sec_child_indx));
        // std::print("===it:{}\n",it->second);
        // std::print("it.frst.find _ :{}\n", !(is_temp_var(it->first)));
        // std::print("it.second.find ___ :{}\n", !(is_temp_var(it->second)));
        bool in_map = false;
        if (it != ref_hash_map.end()) {
          in_map = true;
        }
        // std::print("in_map:{}\n", in_map);
        if (in_map && !(is_temp_var(it->second)) && !(is_temp_var(it->first))
            && (is_ssa(it->second) || is_ssa(it->first))) {  // sec child is ssa; do not attach this node!
          // std::print("****not adding {} and {}\n", ln->get_name(frst_child_indx), ln->get_name(sec_child_indx));
          curr_index       = ln->get_sibling_next(curr_index);
          curr_incremented = true;
        }
      }
      // if curr_index node was not ssa-assign
      // traverse and keep adding to lnfmted
      // if value from map used? replcae it and move on.
      if (curr_index != ln->invalid_index() && !curr_incremented) {
        auto curr_index_fmt = lnastfmted->add_child(stmt_index_fmt, duplicate_node(lnastfmted, ln, curr_index));

        for (const lh::Tree_index& it : ln->children(curr_index)) {
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
      for (const lh::Tree_index& it : ln->depth_preorder(curr_index)) {
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
            curr_index_fmt = lnastfmted->append_sibling(lnastfmted->get_parent(curr_index_fmt), Lnast_node::create_ref(is->second));
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

  // std::cout << "****lnast fmted:  \n";
  // lnastfmted->dump();
  var.replace(ln, lnastfmted);  // just replace the pointer
  // var.add(std::move(lnastfmted));//FIXME. make replace function instead of add
}
*/
