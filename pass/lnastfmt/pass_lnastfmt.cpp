//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lnastfmt.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "lbench.hpp"

static Pass_plugin sample("Pass_lnastfmt", Pass_lnastfmt::setup);

Pass_lnastfmt::Pass_lnastfmt(const Eprp_var &var) : Pass("pass.lnastfmt", var) {}

void Pass_lnastfmt::setup() {
  Eprp_method m1("pass.lnastfmt", "Formats LNAST: remove SSA and recreate tuples", &Pass_lnastfmt::fmt_begin);
  m1.add_label_optional("odir", "path to put the LNAST", ".");
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

void Pass_lnastfmt::parse_ln(std::shared_ptr<Lnast> ln, Eprp_var& var, std::string_view module_name) {
  std::unique_ptr<Lnast> lnastfmted = std::make_unique<Lnast>(module_name);

  observe_lnast(ln.get());//1st traversal through the original LN to record assign subtrees.
  
  //now we will make the formatted LNAST:
  lnastfmted->set_root(Lnast_node(Lnast_ntype::create_top(), Token(0, 0, 0, 0, ln->get_top_module_name()))); // root node of lnfmted
  const auto& stmt_index = ln->get_child(ln->get_root());//stmt node of ln
  const auto& stmt_index_fmt = lnastfmted->add_child(lnastfmted->get_root(), Lnast_node(ln->get_type(stmt_index), ln->get_token(stmt_index), ln->get_subs(stmt_index))); //stmt node of lnfmted (copied from ln)

  auto curr_index = ln->get_child(stmt_index);//1st child of ln after stmt

  while(curr_index!= ln->invalid_index()) {
    //iterate through all children of stmt of ln
    bool curr_incremented = false;
    //check if curr_index's child is leaf child?
    //const auto& leaf_child_check = ln->get_child(curr_index);
    bool all_are_leaves = true;
    for (const mmap_lib::Tree_index& it : ln->children(curr_index)) {
      //fmt::print("PARSING TO CHECK LEAVES:   {}:{}\n",ln->get_name(it), it.level );
      if (!(ln->is_leaf(it))) {
        all_are_leaves = false;
      }
    }
    //fmt::print("?????all children leaves????: {}\n", all_are_leaves);
    if(all_are_leaves) { //ln->is_leaf(leaf_child_check)) 
      if(ln->get_type(curr_index).is_assign()) {

        //if assign was curr_index; process assign subtree:
        const auto& frst_child_indx = ln->get_first_child(curr_index);//assign fisrt child
        const auto& sec_child_indx = ln->get_sibling_next(frst_child_indx);//assign sec child
        auto it = ref_hash_map.find(ln->get_name(sec_child_indx));
        //fmt::print("===it:{}\n",it->second);
        //fmt::print("it.frst.find _ :{}\n", !(is_temp_var(it->first)));
        //fmt::print("it.second.find ___ :{}\n", !(is_temp_var(it->second)));
        bool in_map = false;
        if(it != ref_hash_map.end()) { in_map = true;}
        //fmt::print("in_map:{}\n", in_map);
        if(in_map && !(is_temp_var(it->second)) && !(is_temp_var(it->first)) && (is_ssa(it->second) || is_ssa(it->first))) {//sec child is ssa; do not attach this node!
          //fmt::print("****not adding {} and {}\n", ln->get_name(frst_child_indx), ln->get_name(sec_child_indx));
          curr_index = ln->get_sibling_next(curr_index);
          curr_incremented = true;
        }
      } 
      //if curr_index node was not ssa-assign
      //traverse and keep adding to lnfmted
      //if value from map used? replcae it and move on.
      if(curr_index!= ln->invalid_index() && !curr_incremented) {
        auto curr_index_fmt = lnastfmted->add_child(stmt_index_fmt, Lnast_node(ln->get_type(curr_index), ln->get_token(curr_index), ln->get_subs(curr_index)));

        for (const mmap_lib::Tree_index& it : ln->children(curr_index)) {

          auto is = ref_hash_map.find(ln->get_name(it));
          if(is != ref_hash_map.end() && is_ssa(ln->get_name(it))) {
            //auto frst_fmt = 
            lnastfmted->add_child(curr_index_fmt, Lnast_node::create_ref(lnastfmted->add_string(is->second)));
          } else {
            lnastfmted->add_child(curr_index_fmt, Lnast_node(ln->get_type(it), ln->get_token(it), ln->get_subs(it)));
          }
        }

      }
    } else {//suppose it is "if" subtree

      auto curr_index_fmt = lnastfmted->add_child(stmt_index_fmt,  Lnast_node(ln->get_type(curr_index), ln->get_token(curr_index), ln->get_subs(curr_index)));
      auto curr_lev = curr_index.level;
      auto curr_pos = curr_index.pos;
      //fmt::print("curr_lev and pos: {}, {}\n", curr_lev, curr_pos);
      for (const mmap_lib::Tree_index& it : ln->depth_preorder(curr_index)) {

        auto new_lev = it.level;
        auto new_pos = it.pos;
        auto is = ref_hash_map.find(ln->get_name(it));

        if(new_lev == curr_lev+1) {
          curr_lev = new_lev; curr_pos = new_pos;
          if(is != ref_hash_map.end() && is_ssa(ln->get_name(it))) {
            curr_index_fmt = lnastfmted->add_child(curr_index_fmt, Lnast_node::create_ref(lnastfmted->add_string(is->second)));
          } else {
            curr_index_fmt = lnastfmted->add_child(curr_index_fmt, Lnast_node(ln->get_type(it), ln->get_token(it), ln->get_subs(it)));
          }
        } else if (new_lev==curr_lev && new_pos > curr_pos) {
          if(is != ref_hash_map.end() && is_ssa(ln->get_name(it))) {
            curr_index_fmt = lnastfmted->append_sibling(curr_index_fmt, Lnast_node::create_ref(lnastfmted->add_string(is->second)));
          } else {
            curr_index_fmt = lnastfmted->append_sibling(curr_index_fmt,Lnast_node(ln->get_type(it), ln->get_token(it), ln->get_subs(it)));
          }
          curr_lev = new_lev; curr_pos = new_pos;
        } else if (new_lev+1 == curr_lev) {
          if(is != ref_hash_map.end() && is_ssa(ln->get_name(it))) { 
            curr_index_fmt = lnastfmted->append_sibling(lnastfmted->get_parent(curr_index_fmt), Lnast_node::create_ref(lnastfmted->add_string(is->second)));
          } else {
            curr_index_fmt = lnastfmted->append_sibling(lnastfmted->get_parent(curr_index_fmt), Lnast_node(ln->get_type(it), ln->get_token(it), ln->get_subs(it)));
          }
          curr_lev = new_lev; curr_pos = new_pos;
        } else if (new_lev+2 == curr_lev) {
          if(is != ref_hash_map.end() && is_ssa(ln->get_name(it))) {
            curr_index_fmt = lnastfmted->append_sibling(lnastfmted->get_parent(lnastfmted->get_parent(curr_index_fmt)), Lnast_node::create_ref(lnastfmted->add_string(is->second)));
          } else {
            curr_index_fmt = lnastfmted->append_sibling(lnastfmted->get_parent(lnastfmted->get_parent(curr_index_fmt)), Lnast_node(ln->get_type(it), ln->get_token(it), ln->get_subs(it)));
          }
          curr_lev = new_lev; curr_pos = new_pos;
        }

      }
    }
    if(curr_index!= ln->invalid_index() && !curr_incremented) {
      curr_index = ln->get_sibling_next(curr_index);
    }
  } 

  fmt::print("****lnast fmted:  \n");
  lnastfmted->dump();
  fmt::print("\n:lnast fmted****\n");
  //var.replace(ln, std::move(lnastfmted));//just replace the pointer
  var.add(std::move(lnastfmted));
}

void Pass_lnastfmt::observe_lnast(Lnast* ln) {
  for (const mmap_lib::Tree_index& it : ln->depth_preorder(ln->get_root())) {
    process_node(ln, it);
  }

  fmt::print("ref_hash_map entry: key, value:\n");
  for (auto const& pair: ref_hash_map) {
    fmt::print("{}, {}\n",pair.first, pair.second );
  }
}

void Pass_lnastfmt::process_node(Lnast* ln, const mmap_lib::Tree_index& it) {
  const auto& node_data = ln->get_data(it);

  if(node_data.type.is_assign() || node_data.type.is_dp_assign()) {
    auto frst_child_indx = ln->get_first_child(it);
    I(ln->get_type(frst_child_indx).debug_name()=="ref", "unexpected node found! not ref!?");
    fmt::print("first child type and data: {}, {}\n", ln->get_type(frst_child_indx).debug_name(), ln->get_name(frst_child_indx));
   
    auto sec_child_indx = ln->get_sibling_next(frst_child_indx);
    fmt::print("sec child type and data: {}, {}\n", ln->get_type(sec_child_indx).debug_name(), ln->get_name(sec_child_indx));

    I(ln->get_sibling_next(sec_child_indx).is_invalid(),"This assign node has more than 2 children??");

    ref_hash_map.try_emplace(ln->get_name(sec_child_indx), ln->get_name(frst_child_indx));//insert key, value pair.

    
  }
}

bool Pass_lnastfmt::is_temp_var(std::string_view test_string) {
  return (test_string.find("___")==0 || test_string.find("_._")==0);
}
bool Pass_lnastfmt::is_ssa(std::string_view test_string) {
  return((test_string.find("_")!=0) && (test_string.find("_") != std::string_view::npos));
}
