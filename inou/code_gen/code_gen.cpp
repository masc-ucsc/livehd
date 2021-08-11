
#include "code_gen.hpp"

//#ifndef NDEBUG
//#define NDEBUG
//#endif
#include <algorithm>
#include <cassert>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/match.h"

//-------------------------------------------------------------------------------------
// Constructor:
//
Code_gen::Code_gen(Inou_code_gen::Code_gen_type code_gen_type, std::shared_ptr<Lnast> _lnast, const mmap_lib::str &_path,
                   const mmap_lib::str &_odir)
    : lnast(std::move(_lnast)), path(_path), odir(_odir) {
  if (code_gen_type == Inou_code_gen::Code_gen_type::Type_prp) {
    lnast_to = std::make_unique<Prp_parser>();
  } else if (code_gen_type == Inou_code_gen::Code_gen_type::Type_cpp) {
    lnast_to = std::make_unique<Cpp_parser>();
    // TODO  } else if (code_gen_type == Inou_code_gen::Code_gen_type::Type_cfg) {
    // TODO    lnast_to = std::make_unique<Cfg_parser>();
  } else if (code_gen_type == Inou_code_gen::Code_gen_type::Type_verilog) {
    lnast_to = std::make_unique<Ver_parser>();
  } else {
    I(false);  // Invalid
    lnast_to = std::make_unique<Prp_parser>();
  }
}

//-------------------------------------------------------------------------------------
// system starts here
// this processes the node "top"
//
void Code_gen::generate() {
  constexpr auto root_index = mmap_lib::Tree_index::root();

  const auto& node_data = lnast->get_data(root_index);
  fmt::print("\n\nprocessing LNAST tree\n\n");


  auto fname = lnast->get_top_module_name();
	auto main_filename = get_fname(fname, odir);
	//auto header_filename = get_fname(fname, odir);
	buffer_to_print = std::make_shared<File_output>(main_filename);

  if (node_data.type.is_top()) {
    fmt::print("\nprocessing LNAST tree root text: {} ", node_data.token.get_text());
    fmt::print("processing root->child");
    indendation = lnast_to->indent_final_system();

    do_stmts(lnast->get_child(root_index));
  } else if (node_data.type.is_invalid()) {
    fmt::print("INVALID NODE!");
  } else {
    fmt::print("UNKNOWN NODE TYPE!");
  }

  auto lang_type = lnast_to->get_lang_type();     // which lang is it? prp/cpp/verilog
  auto modname   = lnast->get_top_module_name();

  // for debugging purposes only:
  lnast_to->call_dump_maps();
  for (auto const& [key, val] : ref_map) {
    fmt::print("For map key: {}, val is: {}\n", key, val);
  }

  fmt::print("lnast_to_{}_parser path:{} \n", lang_type, path);


  // header file:
  auto basename_s = mmap_lib::str::concat(modname, "."_str, lnast_to->supporting_ftype());//header filename w/o the odir
  lnast_to->set_supporting_fstart(basename_s);
  lnast_to->set_supp_buffer_to_print(modname);
//  fmt::print("{}\n", lnast_to->supporting_fend(basename_s));

  // main file:
  auto basename = mmap_lib::str::concat(modname, "."_str, lang_type);
  // header inclusion:(#includes):
  lnast_to->set_main_fstart(basename, basename_s);
  lnast_to->set_final_print(modname, buffer_to_print);
  // main code segment
  // fmt::print("{}\n", buffer_to_print);
  fmt::print("<<EOF\n");

  // for odir part:
 // lnast_to->result_in_odir(fname, odir, buffer_to_print);
}

//-------------------------------------------------------------------------------------
mmap_lib::str Code_gen::get_fname(const mmap_lib::str &fname, const mmap_lib::str &outdir) {
return (lnast_to->get_lang_fname(fname, outdir));
}

//-------------------------------------------------------------------------------------
// the node "stmts" is processed here
// and all other nodes are checked in this
//
void Code_gen::do_stmts(const mmap_lib::Tree_index& stmt_node_index) {
  fmt::print("node:stmts\n");
  if (lnast->is_leaf(stmt_node_index)) {
    return;
  }  // check if no child node present

  auto curr_index = lnast->get_first_child(stmt_node_index);

  while (curr_index != lnast->invalid_index()) {
    const auto& curr_node_type = lnast->get_type(curr_index);
    auto        curlvl         = curr_index.level;
    fmt::print("Processing stmt child {}:{} at level {} \n",
               lnast->get_name(curr_index),
               lnast->get_type(curr_index).debug_name(),
               curlvl);

    assert(!curr_node_type.is_invalid());
    if (curr_node_type.is_assign() || curr_node_type.is_dp_assign()) {
      std::vector<mmap_lib::str> vec1;
      do_assign(curr_index, vec1, false);
    } else if (curr_node_type.is_if()) {
      do_if(curr_index);
    } else if (curr_node_type.is_tuple()) {
      Pass::error("\nUnexpected node \"tuple\" encountered in LN during code_gen.\n");
      //do_tuple(curr_index);
    } else if (curr_node_type.is_select() || curr_node_type.is_attr_get()) {
      do_select(curr_index, "selc"_str);
    } else if (curr_node_type.is_tuple_add()) {
      do_select(curr_index, "tuple_add"_str);
    } else if (curr_node_type.is_attr_set()) {
      Pass::error("Error in BitWidth Pass in LGraph optimization.\n");
    } else if (curr_node_type.is_tuple_get()) {
      do_select(curr_index, "tuple_get"_str);
    } else if (curr_node_type.is_func_def()) {
      do_func_def(curr_index);
    } else if (curr_node_type.is_func_call()) {
      do_func_call(curr_index);
    } else if (curr_node_type.is_for()) {
      do_for(curr_index);
    } else if (curr_node_type.is_while()) {
      do_while(curr_index);
    } else if (curr_node_type.is_get_mask()) {
      do_get_mask(curr_index);
    } else if (curr_node_type.is_set_mask()) {
      do_set_mask(curr_index);
    } else if (curr_node_type.is_tuple_concat()) {
      do_op(curr_index, "tup_concat"_str);
    } else if (curr_node_type.is_primitive_op()) {
      do_op(curr_index, "op"_str);
    } else {
      fmt::print("WARNING, unhandled case\n");
    }

    curr_index = lnast->get_sibling_next(curr_index);
  }
}

//-------------------------------------------------------------------------------------
/*
void Code_gen::invalid_node() {
  fmt::print("INVALID NODE TYPE FOUND!");
  exit(1);
}
*/
//-------------------------------------------------------------------------------------
// Process the assign node:
void Code_gen::do_assign(const mmap_lib::Tree_index& assign_node_index, std::vector<mmap_lib::str>& hier_tup_vec,
                         bool hier_tup_assign) {
  fmt::print("node:assign: {}:{}\n", lnast->get_name(assign_node_index), lnast->get_type(assign_node_index).debug_name());
  auto                     curr_index = lnast->get_first_child(assign_node_index);
  std::vector<mmap_lib::str> assign_str_vect;

  while (curr_index != lnast->invalid_index()) {
    assert(!(lnast->get_type(curr_index)).is_invalid());
    // const auto& curr_node_data = lnast->get_data(curr_index);
    auto curlvl = curr_index.level;
    fmt::print("Processing assign child {} at level {} \n", lnast->get_name(curr_index), curlvl);
    assign_str_vect.emplace_back(lnast->get_name(curr_index));
    curr_index = lnast->get_sibling_next(curr_index);
  }  // data of all the child nodes of assign are in assign_str_vect

  assert(assign_str_vect.size() > 1);  // assign has 2 child nodes
  auto key = assign_str_vect.front();  // usually the ___b type of string_view//1st child node of assign
  auto ref = assign_str_vect[1];       // usually the const//2nd child node of assign

  // resolve the second child node of assign (ref)
  auto map_it = ref_map.find(ref);
  if (map_it != ref_map.end()) {
    ref = map_it->second;
  } else if (is_number(ref)) {
    ref = process_number(ref);
  }

  const auto& assign_node_data = lnast->get_data(assign_node_index);
  if (is_temp_var(key)) {
    auto it = ref_map.find(key);
    if (it == ref_map.end()) {
      mmap_lib::str key_var{"tmp"};
      ref_map.insert(std::pair<mmap_lib::str, mmap_lib::str>(key, mmap_lib::str::concat(key_var, key.substr(2))));
      it = ref_map.find(key);
    }
    auto key_sec         = it->second;
    bool param_converted = false;
    if (key_sec.ends_with(".__sbits") || key_sec.ends_with(".__ubits")) {
      param_converted = lnast_to->set_convert_parameters(key_sec, ref);  // for getting UInt<3> a from $a.___bits=3
    }
    if (!param_converted) {
      auto ref_map_inst_res = ref_map.insert(std::pair<mmap_lib::str, mmap_lib::str>(
          key,
          lnast_to->ref_name_str(ref)));   // The pair::second element in the pair is set to true if a new element was inserted or false
                                       // if an equivalent key already existed.
      if (!ref_map_inst_res.second) {  // this means an equivalent key already exists.
        // so append to main buffer:  key value, assign op, ref value
        if (hier_tup_assign) {
          // fmt::print("hier_tup_assign is {} for hier_tup_vec: {}", hier_tup_assign, hier_tup_vec);
          // hier_tup_vec.push_back("str");
          if (has_DblUndrScor(lnast_to->ref_name_str(key_sec))) {
            //   assign:
            //       const: __range_begin
            //       const: 0
            if (key == "__range_begin") {
              hier_tup_vec.emplace_back(lnast_to->ref_name_str(ref));
            } else if (key == "__range_end") {
              auto vec_replacement = mmap_lib::str::concat(hier_tup_vec.back(), ":", lnast_to->ref_name_str(ref));
              hier_tup_vec.pop_back();
              hier_tup_vec.emplace_back(vec_replacement);
            }
          } else {
            hier_tup_vec.emplace_back(
                mmap_lib::str::concat(
                    lnast_to->ref_name_str(key_sec)
                    ," "
                    ,lnast_to->debug_name_lang(assign_node_data.type)
                    ," "
                    ,lnast_to->ref_name_str(ref)
                  )
                );
          }
        } else {
          buffer_to_print->append(mmap_lib::str::concat(indent()
              ,lnast_to->ref_name_str(key_sec)
              ," "_str
              ,lnast_to->debug_name_lang(assign_node_data.type)
              ," "_str
              ,lnast_to->ref_name_str(ref)
              ,lnast_to->stmt_sep())
              );
          lnast_to->add_to_buff_vec_for_cpp(mmap_lib::str::concat(indent()
              ,lnast_to->ref_name_str(key_sec)
              ," "_str
              ,lnast_to->debug_name_lang(assign_node_data.type)
              ," "_str
              ,lnast_to->ref_name_str(ref)
              ,lnast_to->stmt_sep())
              );
        }
      }
    }
  } else {
    if (hier_tup_assign) {
      if (has_DblUndrScor(lnast_to->ref_name_str(key))) {
        //   assign:
        //       const: __range_begin
        //       const: 0
        if (key == "__range_begin") {
          hier_tup_vec.emplace_back(lnast_to->ref_name_str(ref));
        } else if (key == "__range_end") {
          auto vec_replacement = mmap_lib::str::concat(hier_tup_vec.back(), ":", lnast_to->ref_name_str(ref));
          hier_tup_vec.pop_back();
          hier_tup_vec.emplace_back(vec_replacement);
        }
      } else {
        hier_tup_vec.emplace_back(mmap_lib::str::concat(lnast_to->assign_node_strt(),
                                               lnast_to->ref_name_str(key),
                                               " ",
                                               lnast_to->debug_name_lang(assign_node_data.type),
                                               " ",
                                               lnast_to->ref_name_str(ref)));
      }
    } else {
      buffer_to_print->append(mmap_lib::str::concat(indent(),
                      lnast_to->assign_node_strt(),
                      lnast_to->ref_name_str(key),
                      " "_str,
                      lnast_to->debug_name_lang(assign_node_data.type),
                      " "_str,
                      lnast_to->ref_name_str(ref),
                      lnast_to->stmt_sep()));
      lnast_to->add_to_buff_vec_for_cpp(mmap_lib::str::concat(indent(),
                      lnast_to->assign_node_strt(),
                      lnast_to->ref_name_str(key),
                      " "_str,
                      lnast_to->debug_name_lang(assign_node_data.type),
                      " "_str,
                      lnast_to->ref_name_str(ref),
                      lnast_to->stmt_sep()));
      lnast_to->set_for_vcd_comb(lnast_to->ref_name_str(key, false), lnast_to->ref_name_str(key));
    }
  }
}
//-------------------------------------------------------------------------------------
// Process the while node:
// pattern: while -> cond , stmts
void Code_gen::do_while(const mmap_lib::Tree_index& while_node_index) {
  fmt::print("node:while\n");
  buffer_to_print->append( indent(), "while"_str);
  lnast_to->add_to_buff_vec_for_cpp(indent());
  lnast_to->add_to_buff_vec_for_cpp("while"_str);

  auto curr_index = lnast->get_first_child(while_node_index);
  auto ref        = lnast->get_name(curr_index);
  if (is_temp_var(ref)) {
    auto map_it = ref_map.find(ref);
    if (map_it != ref_map.end()) {
      ref = map_it->second;
    }
  }
  buffer_to_print->append(lnast_to->while_cond_beg(),
                  lnast_to->ref_name_str(ref),
                  lnast_to->while_cond_end(),
                  lnast_to->for_stmt_beg());
  lnast_to->add_to_buff_vec_for_cpp(mmap_lib::str::concat(lnast_to->while_cond_beg(),
                  lnast_to->ref_name_str(ref),
                  lnast_to->while_cond_end(),
                  lnast_to->for_stmt_beg()));

  curr_index = lnast->get_sibling_next(curr_index);
  indendation++;
  do_stmts(curr_index);
  indendation--;
  buffer_to_print->append(indent(), lnast_to->for_stmt_end());
  lnast_to->add_to_buff_vec_for_cpp(mmap_lib::str::concat(indent(), lnast_to->for_stmt_end()));
}
//-------------------------------------------------------------------------------------
// Process the for node:
// pattern: for -> stmts , ref "i" , ref "___a"
// example: for i in 0..3 {//stmts}
// 0..3 is resolved as ___a as tuple already.
void Code_gen::do_for(const mmap_lib::Tree_index& for_node_index) {
  fmt::print("node:for\n");
  buffer_to_print->append(indent(), "for"_str);
  lnast_to->add_to_buff_vec_for_cpp(mmap_lib::str::concat(indent(), "for"_str));

  auto stmt_index = lnast->get_first_child(for_node_index);

  auto curr_index = lnast->get_sibling_next(stmt_index);
  buffer_to_print->append(lnast_to->for_cond_beg(),
                  lnast_to->ref_name_str(lnast->get_name(curr_index)),
                  lnast_to->for_cond_mid());
  lnast_to->add_to_buff_vec_for_cpp(mmap_lib::str::concat(lnast_to->for_cond_beg(),
                  lnast_to->ref_name_str(lnast->get_name(curr_index)),
                  lnast_to->for_cond_mid()));

  curr_index = lnast->get_sibling_next(curr_index);
  auto ref   = lnast->get_name(curr_index);
  if (is_temp_var(ref)) {
    auto map_it = ref_map.find(ref);
    if (map_it != ref_map.end()) {
      ref = map_it->second;
    }
  }
  buffer_to_print->append(lnast_to->ref_name_str(ref), lnast_to->for_cond_end());
  lnast_to->add_to_buff_vec_for_cpp(mmap_lib::str::concat(lnast_to->ref_name_str(ref), lnast_to->for_cond_end()));

  buffer_to_print->append(lnast_to->for_stmt_beg());
  lnast_to->add_to_buff_vec_for_cpp(mmap_lib::str::concat(lnast_to->for_stmt_beg()));
  indendation++;
  do_stmts(stmt_index);
  indendation--;
  buffer_to_print->append( indent(), lnast_to->for_stmt_end());
  lnast_to->add_to_buff_vec_for_cpp(mmap_lib::str::concat( indent(), lnast_to->for_stmt_end()));
}
//-------------------------------------------------------------------------------------
// Process the "func_def" node:Eg.:
// 2                    func_def :
// 3                           ref : func_xor
// 3                          cond : $valid
// 3                         stmts : ___SEQ1
// 4                             xor :
// 5                               ref : ___a
// 5                               ref : $a
// 5                               ref : $b
// 4                          assign :
// 5                               ref : %out
// 5                               ref : ___a
// 3                           ref : $a
// 3                           ref : $b
// 3                           ref : $valid
// 3                           ref : %out
void Code_gen::do_func_def(const mmap_lib::Tree_index& func_def_node_index) {
  fmt::print("node:func_def\n");
  auto curr_index = lnast->get_first_child(func_def_node_index);
  auto func_name  = lnast->get_name(curr_index);

  curr_index    = lnast->get_sibling_next(curr_index);

  auto stmt_index = lnast->get_sibling_next(curr_index);

  curr_index = lnast->get_sibling_next(stmt_index);
  mmap_lib::str parameters;
  bool        param_exist = true;
  if (curr_index != lnast->invalid_index()) {
    while (curr_index != lnast->invalid_index()) {
      assert(!(lnast->get_type(curr_index)).is_invalid());
      if (parameters.empty())
        parameters = lnast->get_name(curr_index);
      else
        parameters = mmap_lib::str::concat(parameters, lnast_to->func_param_sep(), lnast_to->ref_name_str(lnast->get_name(curr_index)));
      curr_index = lnast->get_sibling_next(curr_index);
    }
  } else {
    param_exist = false;
  }

  auto cond_val = resolve_func_cond(curr_index);

  buffer_to_print->append(mmap_lib::str::concat(indent(),
                  lnast_to->func_begin(),
                  lnast_to->func_name(func_name),
                  lnast_to->param_start(param_exist),
                  parameters,
                  lnast_to->param_end(param_exist),
                  lnast_to->print_cond(cond_val),
                  lnast_to->func_stmt_strt()));
  lnast_to->add_to_buff_vec_for_cpp(mmap_lib::str::concat(indent(),
                  lnast_to->func_begin(),
                  lnast_to->func_name(func_name),
                  lnast_to->param_start(param_exist),
                  parameters,
                  lnast_to->param_end(param_exist),
                  lnast_to->print_cond(cond_val),
                  lnast_to->func_stmt_strt()));
  indendation++;
  do_stmts(stmt_index);
  indendation--;
  buffer_to_print->append( indent(), lnast_to->func_stmt_end(), lnast_to->func_end());
  lnast_to->add_to_buff_vec_for_cpp(mmap_lib::str::concat( indent(), lnast_to->func_stmt_end(), lnast_to->func_end()));
}
//-------------------------------------------------------------------------------------
// Process the func-cond node:
// cond node is generally either "true" -> nothing to be printed, true by default
// or it is like ___x -> value of ___x must be resolved and "when <reolved ___x>" must be printed
// or it is just the variable which must be printed as is
mmap_lib::str Code_gen::resolve_func_cond(const mmap_lib::Tree_index& func_cond_index) {
  fmt::print("node:function cond\n");

  auto ref = lnast->get_name(func_cond_index);
  if (is_temp_var(ref)) {
    auto map_it = ref_map.find(ref);
    if (map_it != ref_map.end()) {
      ref = map_it->second;
    }
  }else if (ref == "true") {
    return mmap_lib::str();
  }

  return ref;
}

//-------------------------------------------------------------------------------------
// Process the "func_call" node:
// Pattern: lhs, func_name, arguments
// all nodes are "ref" type
// arguments are "___x"
// refer to: https://masc.soe.ucsc.edu/lnast-doc/?coffescript#explicit-function-argument-assignment
void Code_gen::do_func_call(const mmap_lib::Tree_index& func_call_node_index) {
  fmt::print("node:func_call\n");
  auto curr_index = lnast->get_first_child(func_call_node_index);
  // const auto& curr_node_data = lnast->get_data(func_cond_index);//returns the entire node contents.
  auto lhs = lnast->get_name(curr_index);
  if (is_temp_var(lhs)) {
    auto map_it = ref_map.find(lhs);
    if (map_it != ref_map.end()) {
      lhs = map_it->second;
    }
  }
  fmt::print("func_call 1st child: {}\n", lhs);
  buffer_to_print->append(indent());
  lnast_to->add_to_buff_vec_for_cpp(indent());

  curr_index = lnast->get_sibling_next(curr_index);
  auto funcname = lnast->get_name(curr_index);
  fmt::print("func_call 2nd child: {}\n", funcname);

  curr_index = lnast->get_sibling_next(curr_index);
  auto ref   = lnast->get_name(curr_index);
  if (is_temp_var(ref)) {
    auto map_it = ref_map.find(ref);
    if (map_it != ref_map.end()) {
      ref = map_it->second;
    }
  }
  fmt::print("func_call 3rd child: {}\n", ref);

  if (is_temp_var(lhs)) {  //|| !op_is_unary) {
    ref_map.insert(std::pair<mmap_lib::str, mmap_lib::str>(lhs, mmap_lib::str::concat(funcname, lnast_to->ref_name_str(ref))));
  } else {
    buffer_to_print->append( lhs, " = "_str);  // lhs and assignment op to further assign the func name and arguments to lhs
    buffer_to_print->append(funcname);  // printitng the func name(the func called)
    buffer_to_print->append(lnast_to->ref_name_str(ref), lnast_to->stmt_sep());  // parameters for the func call
    lnast_to->add_to_buff_vec_for_cpp(mmap_lib::str::concat(lhs, " = "_str, funcname, lnast_to->ref_name_str(ref), lnast_to->stmt_sep()));
  }
}
//-------------------------------------------------------------------------------------
// Process the "if" node:
// pattern:
// if ->
//   cond (like ___a)
//   stmts
void Code_gen::do_if(const mmap_lib::Tree_index& if_node_index) {
  fmt::print("node:if\n");
  auto curr_index = lnast->get_first_child(if_node_index);
  int  node_num   = 0;

  bool if_closed = false;
  while (curr_index != lnast->invalid_index()) {
    assert(!(lnast->get_type(curr_index)).is_invalid());
    node_num++;
    const auto& curr_node_type = lnast->get_type(curr_index);
    auto        curlvl         = curr_index.level;  // for debugging message printing purposes only
    fmt::print("Processing if child {} at level {} \n", lnast->get_name(curr_index), curlvl);

    if (node_num > 2) {
      if (curr_node_type.is_ref() || curr_node_type.is_const()) {
        buffer_to_print->append( indent(), lnast_to->start_else_if());
        lnast_to->add_to_buff_vec_for_cpp(mmap_lib::str::concat( indent(), lnast_to->start_else_if()));
        do_cond(curr_index);
      } else if (curr_node_type.is_stmts()) {
        bool prev_was_cond = (lnast->get_data(lnast->get_sibling_prev(curr_index))).type.is_const()
                             || (lnast->get_data(lnast->get_sibling_prev(curr_index))).type.is_ref();
        if (!prev_was_cond) {
          buffer_to_print->append( indent(), lnast_to->start_else());
          lnast_to->add_to_buff_vec_for_cpp(mmap_lib::str::concat( indent(), lnast_to->start_else()));
        }
        indendation++;
        do_stmts(curr_index);
        indendation--;
        if (!prev_was_cond) {
          buffer_to_print->append( indent(), lnast_to->end_if_or_else());
          lnast_to->add_to_buff_vec_for_cpp(mmap_lib::str::concat( indent(), lnast_to->end_if_or_else()));
          if_closed = true;
        }
      }
    } else {
      if (curr_node_type.is_ref() || curr_node_type.is_const()) {
        buffer_to_print->append(indent(), lnast_to->start_cond());
        lnast_to->add_to_buff_vec_for_cpp(mmap_lib::str::concat(indent(), lnast_to->start_cond()));
        do_cond(curr_index);
      } else if (curr_node_type.is_stmts()) {
        indendation++;
        do_stmts(curr_index);
        indendation--;
      } else {
        fmt::print("ERROR:\n\t\t------CHECK THE NODE TYPE IN THIS IF -----!!\n");
      }
    }

    curr_index = lnast->get_sibling_next(curr_index);
  }

  if (node_num <= 2 || !if_closed) {
    buffer_to_print->append(indent(), lnast_to->end_if_or_else());
    lnast_to->add_to_buff_vec_for_cpp(mmap_lib::str::concat(indent(), lnast_to->end_if_or_else()));
  }
}

//-------------------------------------------------------------------------------------
// Process the if-cond node:
void Code_gen::do_cond(const mmap_lib::Tree_index& cond_node_index) {
  fmt::print("node:cond\n");
  // const auto& curr_node_data = lnast->get_data(cond_node_index);
  auto ref    = lnast->get_name(cond_node_index);
  auto map_it = ref_map.find(ref);
  if (map_it != ref_map.end()) {
    ref = map_it->second;
  }
  buffer_to_print->append(lnast_to->ref_name_str(ref));
  buffer_to_print->append(lnast_to->end_cond());
  lnast_to->add_to_buff_vec_for_cpp(mmap_lib::str::concat(lnast_to->ref_name_str(ref), lnast_to->end_cond()));
}

//-------------------------------------------------------------------------------------
// Process the operator (like and,or,etc.) node:
void Code_gen::do_op(const mmap_lib::Tree_index& op_node_index, const mmap_lib::str& op_type) {
  fmt::print("node:{}: {}:{}\n", op_type, lnast->get_name(op_node_index), lnast->get_type(op_node_index).debug_name());
  auto                     curr_index = lnast->get_first_child(op_node_index);
  std::vector<mmap_lib::str> op_str_vect;

  while (curr_index != lnast->invalid_index()) {
    assert(!(lnast->get_type(curr_index)).is_invalid());
    // const auto& curr_node_data = lnast->get_data(curr_index);
    auto curlvl = curr_index.level;  // for debugging message printing purposes only
    auto curpos = curr_index.pos;
    fmt::print("Processing op child {} at level {} pos {}\n", lnast->get_name(curr_index), curlvl, curpos);
    // if it is shl subtree and it is doing left shift by 1 then do not store the "1"
    if (lnast->get_type(op_node_index).is_shl()
        && (curpos == lnast->get_sibling_next(lnast->get_first_child(op_node_index)).pos) /*we are on 2nd child*/
        && (lnast->get_name(curr_index) == "1") /*it is const 1*/) {
      /*For set_mask cases*/
      curr_index = lnast->get_sibling_next(curr_index);
      continue;
    }
    if (lnast->get_type(curr_index).is_const()) {
      Code_gen::const_vect.emplace_back(lnast->get_name(curr_index));
    }
    op_str_vect.emplace_back(lnast->get_name(curr_index));
    curr_index = lnast->get_sibling_next(curr_index);
  }
  // op_str_vect now has all the children of the operation "op"

  auto key         = op_str_vect.front();
  bool op_is_unary = false;
  // if(is_temp_var(key) && op_str_vect.size()==2){
  // This is because of the SSA assignments (key can be like "o2_0")
  if (op_str_vect.size() == 2) {
    op_is_unary = true;
  }

  const auto& op_node_data = lnast->get_data(op_node_index);
  mmap_lib::str val;
  for (unsigned i = 1; i < op_str_vect.size(); i++) {
    auto ref    = op_str_vect[i];
    auto map_it = ref_map.find(ref);
    if (map_it != ref_map.end()) {
      if (map_it->second.contains(" ")) {
        ref = mmap_lib::str::concat("(", map_it->second, ")");
      } else {
        ref = map_it->second;
        if (lnast_to->is_unsigned(ref) && lnast_to->get_lang_type() == "cpp") {
          lnast_to->set_make_unsigned(key);
        }
      }
    } else if (is_number(ref)) {
      ref = process_number(ref);
    }
    // check if a number
    if (op_is_unary && lnast->get_type(op_node_index).is_shl()) {
      /*do not append any op type*/
      /*test case: partial.prp. (For set_mask cases)*/
    } else if (op_is_unary) {
      val = val.append(lnast_to->debug_name_lang(op_node_data.type));
      //absl::StrAppend(&val, lnast_to->debug_name_lang(op_node_data.type));
    }

    bool ref_is_const = false;
    // TODO:check if ref is const type (used for masking) or not
    if (std::find(const_vect.begin(), const_vect.end(), ref) != const_vect.end()) {
      ref_is_const = true;
      if (lnast_to->is_unsigned(op_str_vect[i - 1])) {
        fmt::print("\nNow, op str vect i-1 is {} and ref is {}\n", op_str_vect[i - 1], ref);

        auto bw_num = Lconst::from_pyrope(ref);  //(int)log2(ref)+1;

        fmt::print("{}\n", bw_num.get_bits());
        ref = mmap_lib::str::concat("UInt<", bw_num.get_bits(), ">(", ref.to_s(), ")");
      }
    }

    if (op_type == "tup_concat") {
      val = mmap_lib::str::concat(val,
                      lnast_to->str_qoute(!is_number(ref) && ref_is_const),
                      lnast_to->ref_name_str(ref),
                      lnast_to->str_qoute(!is_number(ref) && ref_is_const));
    } else {
      val = val.append(lnast_to->ref_name_str(ref));
    }
    if ((i + 1) != op_str_vect.size()
        && !op_is_unary) {  // check that another entry is left in op_str_vect && it is a binary operation
      val = mmap_lib::str::concat(val, " ", lnast_to->debug_name_lang(op_node_data.type), " ");
    }
  }

  if (is_temp_var(key)) {  //|| !op_is_unary) {
    ref_map.insert(std::pair<mmap_lib::str, mmap_lib::str>(key, mmap_lib::str(lnast_to->ref_name_str(val))));
  } else {
    // absl::StrAppend (&buffer_to_print->append(indent(), lnast_to->ref_name_str(key), " ", lnast_to->debug_name_lang(op_node_data.type), " ",
    // lnast_to->ref_name_str(val), lnast_to->stmt_sep());
    buffer_to_print->append(mmap_lib::str::concat(indent(),
                    lnast_to->ref_name_str(key),
                    " "_str,
                    "="_str,
                    " "_str,
                    lnast_to->ref_name_str(mmap_lib::str(val)),
                    lnast_to->stmt_sep()));
    lnast_to->add_to_buff_vec_for_cpp(mmap_lib::str::concat(indent(),
                    lnast_to->ref_name_str(key),
                    " "_str,
                    "="_str,
                    " "_str,
                    lnast_to->ref_name_str(mmap_lib::str(val)),
                    lnast_to->stmt_sep()));
  }
}

//-------------------------------------------------------------------------------------
// processing tposs operator
//                 pattern: tposs --> ref,___L5        ref,$a
// Another possible pattern: tposs --> ref,___L5        ref,___L7
// this means $a is unsigned
void Code_gen::do_tposs(const mmap_lib::Tree_index& tposs_node_index) {
  fmt::print("node:op: {}:{}\n", lnast->get_name(tposs_node_index), lnast->get_type(tposs_node_index).debug_name());

  auto first_child_index = lnast->get_first_child(tposs_node_index);
  auto first_child       = lnast->get_name(first_child_index);
  auto sec_child         = lnast->get_name(lnast->get_sibling_next(first_child_index));

  auto map_it = ref_map.find(sec_child);
  // bool sec_child_is_temp = false;
  if (map_it != ref_map.end()) {
    // sec_child_is_temp = true;
    sec_child = map_it->second;
  }
  if (is_temp_var(first_child)) {
    ref_map.insert(std::pair<mmap_lib::str, mmap_lib::str>(first_child, sec_child));
  } else {
    I(false, "Error: expected temp str as first child of Tposs.\n\tMight need to check this issue!\n");
  }
}

//-------------------------------------------------------------------------------------
// processing set_mask operator

void Code_gen::do_set_mask(const mmap_lib::Tree_index& smask_node_index) {
  fmt::print("node:set_mask\n");

  auto                     curr_index = lnast->get_first_child(smask_node_index);
  std::vector<mmap_lib::str> smask_str_vect;
  while (curr_index != lnast->invalid_index()) {
    assert(!(lnast->get_type(curr_index)).is_invalid());
    auto curlvl = curr_index.level;
    fmt::print("Processing gmask child {}:{} at level {} \n",
               lnast->get_name(curr_index),
               lnast->get_type(curr_index).debug_name(),
               curlvl);
    smask_str_vect.emplace_back(lnast->get_name(curr_index));
    curr_index = lnast->get_sibling_next(curr_index);
  }
  // dot_str_vect now has all the children of the operation "op"

  I(smask_str_vect.size() > 2);

  auto        key = smask_str_vect.front();
	mmap_lib::str val;

  for (unsigned i = 1; i < smask_str_vect.size() - 1; i++) {
    auto ref    = smask_str_vect[i];
    auto map_it = ref_map.find(ref);
    if (map_it != ref_map.end()) {
      ref = map_it->second;
    }

    val = mmap_lib::str::concat(val, ref.to_s(), lnast_to->gmask_op(), (i == 1 ? "(" : ""));
  }
  val = val.substr(0,val.size()-1);//pop_back()
  val = val.append(")");

  if (is_temp_var(key)) {
    ref_map.insert(std::pair<mmap_lib::str, mmap_lib::str>(key, val));
  } else {
    buffer_to_print->append(mmap_lib::str::concat(indent(), lnast_to->ref_name_str(val), "="_str, smask_str_vect.back(), "\n"_str));
    lnast_to->add_to_buff_vec_for_cpp(mmap_lib::str::concat(indent(), lnast_to->ref_name_str(val), "="_str, smask_str_vect.back(), "\n"_str));
    // I(false, "Error: expected temp str as first child of get_mask.\n\tMight need to check this issue!\n");
  }
}
//-------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------
// processing get_mask operator
// example: x@(0) is like:
// get mask: { ref: ___L1 , ref: x , const:0 }
// or equivalently
// tup_add: {ref:___L0, const: 0} ; get mask: { ref: ___L1 , ref: x , ref: ___L0 }
void Code_gen::do_get_mask(const mmap_lib::Tree_index& gmask_node_index) {
  fmt::print("node:get_mask\n");

  auto                     curr_index = lnast->get_first_child(gmask_node_index);
  std::vector<mmap_lib::str> gmask_str_vect;
  while (curr_index != lnast->invalid_index()) {
    assert(!(lnast->get_type(curr_index)).is_invalid());
    auto curlvl = curr_index.level;
    fmt::print("Processing gmask child {}:{} at level {} \n",
               lnast->get_name(curr_index),
               lnast->get_type(curr_index).debug_name(),
               curlvl);
    gmask_str_vect.emplace_back(lnast->get_name(curr_index));
    curr_index = lnast->get_sibling_next(curr_index);
  }
  // dot_str_vect now has all the children of the operation "op"

  I(gmask_str_vect.size() > 2);

  auto        key = gmask_str_vect.front();
	mmap_lib::str val;
  bool ref_is_ref = false;
  for (unsigned i = 1; i < gmask_str_vect.size(); i++) {
    auto ref    = gmask_str_vect[i];
    auto map_it = ref_map.find(ref);
    if (map_it != ref_map.end()) {
			ref_is_ref = true;
      ref = map_it->second;
    } else {
			ref_is_ref = false;
		}

		//absl::StrAppend(&val, ref.to_s(), ((i==1)?lnast_to->gmask_op():""), ((i>1)?"":"(") );
    val = mmap_lib::str::concat(val, ((!ref_is_ref&&i>1)?"("_str:""_str) , ref, ((i==1)?lnast_to->gmask_op():""_str));
  }
  val = val.append(ref_is_ref?""_str:")"_str );

  if (is_temp_var(key)) {
    ref_map.insert(std::pair<mmap_lib::str, mmap_lib::str>(key,val));
  } else {
    I(false, "Error: expected temp str as first child of get_mask.\n\tMight need to check this issue!\n");
  }
}
//-------------------------------------------------------------------------------------
// processing dot operator
// best testing case: cfg/tests/ring.prp
void Code_gen::do_dot(const mmap_lib::Tree_index& dot_node_index, const mmap_lib::str& select_type) {
  fmt::print("node:dot\n");

  auto                     curr_index = lnast->get_first_child(dot_node_index);
  std::vector<mmap_lib::str> dot_str_vect;
  while (curr_index != lnast->invalid_index()) {
    assert(!(lnast->get_type(curr_index)).is_invalid());
    auto curlvl = curr_index.level;
    fmt::print("Processing dot child {}:{} at level {} \n",
               lnast->get_name(curr_index),
               lnast->get_type(curr_index).debug_name(),
               curlvl);
    dot_str_vect.emplace_back(lnast->get_name(curr_index));
    curr_index = lnast->get_sibling_next(curr_index);
  }
  // dot_str_vect now has all the children of the operation "op"

  assert(dot_str_vect.size() > 2);

  auto key            = dot_str_vect.back();
  bool add_to_ref_map = false;
  if (is_temp_var(dot_str_vect.front())) {
    add_to_ref_map = true;
    key            = dot_str_vect.front();
  }

  auto i = 0u;
  if (add_to_ref_map) {
    i = 1u;
  }
	mmap_lib::str  value;
  // const auto& dot_node_data = lnast->get_data(dot_node_index);
  while ((select_type == "tuple_add" && i < (dot_str_vect.size() - 1) && is_temp_var(key))
         || (i < dot_str_vect.size() && is_temp_var(key) && select_type == "selc")
         || (i < (dot_str_vect.size() - 1)
             && !is_temp_var(
                 key))) {  // condition set as per if.prp and adder_stage.prp test cases. To accomodate attr_get and tuple_add.
    auto ref    = dot_str_vect[i];
    auto map_it = ref_map.find(ref);
    if (map_it != ref_map.end()) {
      ref = map_it->second;
    }
    if (ref == "__valid") {
      value = value.substr(0,value.size()-1);//value.pop_back();
      value = value.append("?");
    } else if (ref == "__retry") {
      value = value.substr(0,value.size()-1);//value.pop_back();
      value = value.append("!");
    } else if (is_number(ref)) {
      value = value.append(process_number(ref));
    } else {
      value = value.append(ref);
    }
    // now returns "select". So making it more pyrope specific for time being.//  absl::StrAppend(&value,
    // lnast_to->debug_name_lang(dot_node_data.type));  // appends "." after the value in case of pyrope
    value = value.append(lnast_to->dot_type_op());// appends "." after the value in case of pyrope
    i++;
  }
  value = value.substr(0,value.size()-1);//value.pop_back();

  if (is_temp_var(key)) {
    if (select_type == "tuple_add") {
      auto map_it = ref_map.find(key);
      if (map_it != ref_map.end()) {
        key = map_it->second;
      } else
        I(false, "this tuple_add key is supposed to be fetched from ref_map. This must already be there.");

      buffer_to_print->append(indent(), lnast_to->ref_name_str(value), " = "_str, key, "\n"_str);
      lnast_to->add_to_buff_vec_for_cpp(mmap_lib::str::concat(indent(), lnast_to->ref_name_str(value), " = "_str, key, "\n"_str));
    } else {
      // ref_map.insert(std::pair<mmap_lib::str_view, mmap_lib::str>(key, lnast_to->ref_name_str(value)));
      // this value is preserved with "$"/"%"/"#" so that during "set_convert_parameters()", we have the char to decide i/p or o/p
      // or reg
      auto ref_map_inst_succ = ref_map.insert(std::pair<mmap_lib::str, mmap_lib::str>(key, value));
      I(ref_map_inst_succ.second,
        "\n\nThe ref value was already in the ref_map. Thus redundant keypresent. BUG!\nParent_node : dot\n\n");
    }
  } else {
    buffer_to_print->append(indent(), lnast_to->ref_name_str(value), " = "_str, key, "\n"_str);
    lnast_to->add_to_buff_vec_for_cpp(mmap_lib::str::concat(indent(), lnast_to->ref_name_str(value), " = "_str, key, "\n"_str));
  }
}
//-------------------------------------------------------------------------------------
// Process the select node:
// ref LNAST subtree: select,""  ->  ref,"___l" , ref,"A" , const,"0"
void Code_gen::do_select(const mmap_lib::Tree_index& select_node_index, const mmap_lib::str& select_type) {
  fmt::print("node:select\n");
  auto                     curr_index = lnast->get_first_child(select_node_index);
  std::vector<mmap_lib::str> sel_str_vect;
  bool                     lastIsRef = false;
  while (curr_index != lnast->invalid_index()) {
    assert(!(lnast->get_type(curr_index)).is_invalid());
    auto curlvl = curr_index.level;
    fmt::print("Processing {} child {}:{} at level {} \n",
               select_type,
               lnast->get_name(curr_index),
               lnast->get_type(curr_index).debug_name(),
               curlvl);
    if (lnast->get_first_child(curr_index).pos != lnast->invalid_index().pos) {
      // it is nested tuple
      // resolve the entire subtree, make into a string and pushback to sel_str_vect
      I(((lnast->get_type(curr_index)).is_assign() || (lnast->get_type(curr_index)).is_dp_assign()),
        "FIXME: subtree of tuple_add/get has op other than assign. check and add feature.");
      do_assign(curr_index, sel_str_vect, true);
    } else {
      // const auto& curr_node_data = lnast->get_data(curr_index);
      sel_str_vect.emplace_back(lnast->get_name(curr_index));
    }
    if (lnast->get_type(curr_index).is_ref()) {
      lastIsRef = true;
    } else
      lastIsRef = false;
    curr_index = lnast->get_sibling_next(curr_index);
  }

  if (select_type == "tuple_get") {
    I(sel_str_vect.size() >= 3, "\n\nunexpected tuple_get type. Please check.\n\n");

    auto        key   = sel_str_vect.front();
    auto        value = sel_str_vect[1];

    auto i = 2u;
    while (i < sel_str_vect.size()) {
      auto ref = sel_str_vect[i];

      auto map_it = ref_map.find(ref);
      if (map_it != ref_map.end()) {
        ref = map_it->second;
      }

      if (is_number(ref)) {  // for numbers or temp vars only; we want "[]"
        value = mmap_lib::str::concat(value,
                        lnast_to->select_init(select_type),
                        lnast_to->ref_name_str(ref),
                        lnast_to->select_end(select_type));  // test case:tuple_nested1.prp
      } else if ((map_it != ref_map.end() && is_temp_var(map_it->first)) || /*last child is of type ref(not const)*/ lastIsRef) {
        // if ref_map.end is not checked then ERROR: std::bad_alloc (std::exception) is thrown
        value = mmap_lib::str::concat(value,
                        lnast_to->select_init(select_type),
                        lnast_to->ref_name_str(ref),
                        lnast_to->select_end(select_type));  // test case:tuple_nested1.prp
      } else {                                               // for alphanumeric, we want"."
        value = mmap_lib::str::concat(value, lnast_to->dot_type_op(), lnast_to->ref_name_str(ref));
      }
      i++;
    }

    if (is_temp_var(key)) {
      ref_map.insert(std::pair<mmap_lib::str, mmap_lib::str>(key, value));
    } else {
      fmt::print("ERROR:\n\t\t------CHECK THE NODE TYPE IN THIS TUPLE_GET -----!!\n");
    }

  } else if (select_type == "tuple_add") {
    // do not treat like dot operator
    auto key = sel_str_vect.front();
    if (sel_str_vect.size() == 1) {
      // example: tmp = index@()  : from tuple_nested2.prp
      ref_map.insert(std::pair<mmap_lib::str, mmap_lib::str>(key, "()"));
    } else {
      assert(sel_str_vect.size() >= 2);
      if (is_temp_var(key)) {
        auto value = mmap_lib::str::concat(lnast_to->select_init(select_type), sel_str_vect[1]);

        auto i = 2u;
        // if (i == sel_str_vect.size()) {
        //  absl::StrAppend(&value, lnast_to->select_init(select_type), lnast_to->select_end(select_type));
        //}
        while (i < sel_str_vect.size()) {
          value = mmap_lib::str::concat(value, ",");
          auto ref = sel_str_vect[i];

          auto map_it = ref_map.find(ref);
          if (map_it != ref_map.end()) {
            ref = map_it->second;
          }

          value = mmap_lib::str::concat(value, lnast_to->ref_name_str(ref));
          i++;
        }
        value = mmap_lib::str::concat(value, lnast_to->select_end(select_type));

        // mmap_lib::str value = absl::StrCat(sel_str_vect[1], "[", ref, "]");
        ref_map.insert(std::pair<mmap_lib::str, mmap_lib::str>(key, value));
      } else {
        // fmt::print("ERROR:\n\t\t------CHECK THE NODE TYPE IN THIS {} -----!!\n", select_type);
        do_dot(select_node_index, select_type);  // FIXME: you can pass sel_str_vec here so that do_dot does not calc it again!
      }
    }

  } else if (has_DblUndrScor(sel_str_vect.back()) || has_DblUndrScor(*(sel_str_vect.rbegin() + 1))) {  // treat like dot operator
    do_dot(select_node_index, select_type);      // TODO: pass the vector also, no need to calc it again!
  } else if (is_number(sel_str_vect.back())) {  // do not treat like dot operator

    if (select_type == "bit") {
      assert(sel_str_vect.size() >= 2);
    } else {
      assert(sel_str_vect.size() >= 3);
    }

    auto key   = sel_str_vect.front();
    auto value = sel_str_vect[1];

    auto i = 2u;
    if (i == sel_str_vect.size()) {
      value = mmap_lib::str::concat(value, lnast_to->select_init(select_type), lnast_to->select_end(select_type));
    }
    while (i < sel_str_vect.size()) {
      auto ref = sel_str_vect[i];

      auto map_it = ref_map.find(ref);
      if (map_it != ref_map.end()) {
        ref = map_it->second;
      }

      value = mmap_lib::str::concat(value, lnast_to->select_init(select_type), lnast_to->ref_name_str(ref), lnast_to->select_end(select_type));
      i++;
    }

    if (is_temp_var(key)) {
      ref_map.insert(std::pair(key, value));
    } else {
      fmt::print("ERROR:\n\t\t------CHECK THE NODE TYPE IN THIS {} -----!!\n", select_type);
      do_dot(select_node_index, select_type);
    }
  } else {
    I(false, "Unexpected node. Please check.");
  }
}

//-------------------------------------------------------------------------------------
// processing tuple
void Code_gen::do_tuple(const mmap_lib::Tree_index& tuple_node_index) {
  fmt::print("node:tuple\n");

  // Process the first child-node in key and move to the next node:
  auto curr_index = lnast->get_first_child(tuple_node_index);
  auto key        = lnast->get_name(curr_index);
  fmt::print("processing tuple's 1st child {}\n", key);
  fmt::print("same index value from lnast data stack: {}\n", lnast->get_data(curr_index).token.get_text());

  // Process remaining nodes/sub-trees:
  curr_index              = lnast->get_sibling_next(curr_index);
  mmap_lib::str tuple_value;
  while (curr_index != lnast->invalid_index()) {
    assert(!(lnast->get_type(curr_index)).is_invalid());
    if (lnast->is_leaf(curr_index)) {
      auto ref = lnast->get_name(curr_index);
      // For case like:
      // tuple :
      //    ref : ___b
      //    ref : ___a
      if (is_temp_var(ref)) {
        auto map_it = ref_map.find(ref);
        if (map_it != ref_map.end()) {
          ref = map_it->second;
        }
      }
      fmt::print("tuple's next leaf child: {}\n", ref);
      if (lnast->get_type(curr_index).is_const()) {
        tuple_value = mmap_lib::str::concat(tuple_value, "\"", lnast_to->ref_name_str(ref), "\"", lnast_to->tuple_stmt_sep());
      } else {
        tuple_value = mmap_lib::str::concat(tuple_value, lnast_to->ref_name_str(ref), lnast_to->tuple_stmt_sep());
      }
    } else {
      tuple_value = mmap_lib::str::concat(tuple_value, resolve_tuple_assign(curr_index));
    }
    curr_index = lnast->get_sibling_next(curr_index);
  }

  // for formatting purposes:
  if (tuple_value.size() > 2) {
    if (tuple_value.substr(tuple_value.size() - 2) == lnast_to->tuple_stmt_sep()) {
      tuple_value = mmap_lib::str::concat(lnast_to->tuple_begin(), tuple_value);
      tuple_value = tuple_value.substr(0,tuple_value.size()-2);// to remove the extra (last) tuple stmt sep inserted
      tuple_value = tuple_value.append(lnast_to->tuple_end());
    }
  } else if (tuple_value == "") {
    tuple_value = mmap_lib::str::concat(lnast_to->tuple_begin(), lnast_to->tuple_end());
  }  // to cater to scenario like: out = () :in ring.prp

  // insert to map:
  fmt::print("final tuple value for the above key: {}\n", tuple_value);
  if (is_temp_var(key)) {
    ref_map.insert(std::pair<mmap_lib::str, mmap_lib::str>(key, tuple_value));
  } else {
    fmt::print("key: {}\n tuple_value:{}\n", key, tuple_value);
    buffer_to_print->append(key, " saved as ", tuple_value, "\n");
    lnast_to->add_to_buff_vec_for_cpp(mmap_lib::str::concat(key, " saved as ", tuple_value, "\n"));
    // this should never be possible
  }
}

//-------------------------------------------------------------------------------------
// function called to process the tuple:
mmap_lib::str Code_gen::resolve_tuple_assign(const mmap_lib::Tree_index& tuple_assign_index) {
  auto                     curr_index = lnast->get_first_child(tuple_assign_index);
  std::vector<mmap_lib::str> op_str_vect;

  while (curr_index != lnast->invalid_index()) {
    assert(!(lnast->get_type(curr_index)).is_invalid());
    // const auto& curr_node_data = lnast->get_data(curr_index);
    op_str_vect.emplace_back(lnast->get_name(curr_index));
    curr_index = lnast->get_sibling_next(curr_index);
  }
  // op_str_vect now has all the children of the operation "op"

  auto        key          = op_str_vect.front();
  bool        is_const     = false;
  mmap_lib::str val;
  const auto& op_node_data = lnast->get_data(tuple_assign_index);  // the operator (assign or as)

  if (key == "null") {
    is_const = true;
    val      = op_str_vect.back();
  } else {
    bool op_is_unary = false;
    if (is_temp_var(key) && op_str_vect.size() == 2) {
      op_is_unary = true;
    }

    for (unsigned i = 1; i < op_str_vect.size(); i++) {
      auto ref = op_str_vect[i];
      if (ref == "null") {
        val = ""_str;
        break;
      }
      auto map_it = ref_map.find(ref);
      if (map_it != ref_map.end()) {
        if (map_it->second.contains(" ")) {
          ref = mmap_lib::str::concat("(", map_it->second, ")");
        } else {
          ref = map_it->second;
        }
        // fmt::print("map_it find: {} | {}\n", map_it->first, ref);
      } else if (is_number(ref)) {
        ref = process_number(ref);
      }
      // check if a number
      if (op_is_unary) {
        val = val.append(lnast_to->debug_name_lang(op_node_data.type));
      }
      val = val.append(lnast_to->ref_name_str(ref));
      if ((i + 1) != op_str_vect.size() && !op_is_unary) {
        val = mmap_lib::str::concat(val, " ", lnast_to->debug_name_lang(op_node_data.type), " ");
      }
    }
  }

  if (is_temp_var(key)) {
    ref_map.insert(std::pair<mmap_lib::str, mmap_lib::str>(key, val));
    assert(false);
    return "ERROR"_str; // ("\n\nERROR:\n\t----------------UNEXPECTED TUPLE VALUE!--------------------\n\n");
  } else if (is_const) {
    return mmap_lib::str::concat(indent(), val, lnast_to->tuple_stmt_sep());
  } else if (key == "__range_begin") {
    return mmap_lib::str::concat(val, ".");
  } else if (key == "__range_end") {
    return mmap_lib::str::concat(".", val);
  } else {
    return mmap_lib::str::concat(indent(), key, " "_str, lnast_to->debug_name_lang(op_node_data.type), " "_str, val, lnast_to->tuple_stmt_sep());
  }
}

//-------------------------------------------------------------------------------------
// check if the node has "___"
bool Code_gen::is_temp_var(const mmap_lib::str &test_string) {
  auto txt = test_string.substr(0,3);

  return (txt == "___") || (txt == "_._");
}

//-------------------------------------------------------------------------------------
// check if the node has "__"
bool Code_gen::has_DblUndrScor(const mmap_lib::str &test_string) {
  return test_string.starts_with("__");
}

//-------------------------------------------------------------------------------------
bool Code_gen::is_number(const mmap_lib::str &test_string) {
  return test_string.is_i();
}

//-------------------------------------------------------------------------------------
mmap_lib::str Code_gen::process_number(const mmap_lib::str &num_string) {
	auto lc = Lconst::from_pyrope(num_string);// lc(num_string);
  I(lc.is_i());
  return lc.to_pyrope(); // this can simplify the number a bit (language dependent, it may need to call to_pyrope/to_verilog/...
}

//-------------------------------------------------------------------------------------
mmap_lib::str Code_gen::indent() const { return mmap_lib::str(indendation * 2, ' '); }

//-------------------------------------------------------------------------------------
