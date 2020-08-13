
#include "code_gen.hpp"

//#ifndef NDEBUG
//#define NDEBUG
//#endif
#include <assert.h>

//-------------------------------------------------------------------------------------
//Constructor:
//
Code_gen::Code_gen(Inou_code_gen::Code_gen_type code_gen_type, std::shared_ptr<Lnast> _lnast, std::string_view _path) : lnast(std::move(_lnast)), path(_path) {
  if (code_gen_type == Inou_code_gen::Code_gen_type::Type_prp) {
    lnast_to = std::make_unique<Prp_parser>();
  } else if (code_gen_type == Inou_code_gen::Code_gen_type::Type_cpp) {
    lnast_to = std::make_unique<Cpp_parser>();
//TODO  } else if (code_gen_type == Inou_code_gen::Code_gen_type::Type_cfg) {
//TODO    lnast_to = std::make_unique<Cfg_parser>();
  } else if (code_gen_type == Inou_code_gen::Code_gen_type::Type_verilog) {
    lnast_to = std::make_unique<Ver_parser>();
  } else {
    I(false);  // Invalid
    lnast_to = std::make_unique<Prp_parser>();
  }
}

//-------------------------------------------------------------------------------------
//system starts here
//this processes the node "top"
//
void Code_gen::generate(){
  const auto& root_index = lnast->get_root();
  const auto& node_data = lnast->get_data(root_index);
  fmt::print("\n\nprocessing LNAST tree\n\n");
  if (node_data.type.is_top()) {
    fmt::print("\nprocessing LNAST tree root text: {} ", node_data.token.get_text());
    fmt::print("processing root->child");
    do_stmts(lnast->get_child(root_index));
  } else if (node_data.type.is_invalid()) {
    fmt::print("INVALID NODE!");
  } else {
    fmt::print("UNKNOWN NODE TYPE!");
  }

  auto lang_type = lnast_to->get_lang_type();
  auto basename = absl::StrCat(lnast->get_top_module_name(), ".", lang_type);

  fmt::print("lnast_to_{}_parser path:{} file:{}\n", lang_type, path, basename);

  fmt::print("{}\n", buffer_to_print);
  fmt::print("<<EOF\n");
}

//-------------------------------------------------------------------------------------
//the node "stmts" is processed here
//and all other nodes are checked in this
//
void Code_gen::do_stmts(const mmap_lib::Tree_index& stmt_node_index) {
  fmt::print("node:stmts\n");
  if(lnast->is_leaf(stmt_node_index)) {return;} //check if no child node present

  auto curr_index = lnast->get_first_child(stmt_node_index);

  while(curr_index!=lnast->invalid_index()) {
    const auto& curr_node_type = lnast->get_type(curr_index);
    auto curlvl = curr_index.level;
    fmt::print("Processing stmt child {} at level {} \n",lnast->get_name(curr_index), curlvl);

    assert(!curr_node_type.is_invalid());
    if (curr_node_type.is_assign() || curr_node_type.is_dp_assign()) {
      do_assign(curr_index);
    } else if (curr_node_type.is_if()) {
      do_if(curr_index);
    } else if (curr_node_type.is_and() ||
               curr_node_type.is_or() ||
               curr_node_type.is_not() ||
               curr_node_type.is_xor() ||
               curr_node_type.is_logical_not() ||
               curr_node_type.is_logical_and() ||
               curr_node_type.is_logical_or() ||
               curr_node_type.is_same() ||
               curr_node_type.is_as() ||
               curr_node_type.is_plus() ||
               curr_node_type.is_minus() ||
               curr_node_type.is_mult() ||
               curr_node_type.is_div() ||
               curr_node_type.is_lt() ||
               curr_node_type.is_le() ||
               curr_node_type.is_gt() ||
               curr_node_type.is_ge() ||
               curr_node_type.is_tuple_concat() ||
               curr_node_type.is_tuple_delete() ||
               curr_node_type.is_shift_left() ||
               curr_node_type.is_shift_right()) {
      do_op(curr_index);
    } else if (curr_node_type.is_dot()) {
      do_dot(curr_index);
    } else if (curr_node_type.is_tuple()) {
      do_tuple(curr_index);
    } else if (curr_node_type.is_select()) {
      do_select(curr_index, "select");
    } else if (curr_node_type.is_bit_select()) {
      do_select(curr_index, "bit");
    } else if (curr_node_type.is_func_def()) {
      do_func_def(curr_index);
    } else if (curr_node_type.is_func_call()) {
      do_func_call(curr_index);
    } else if (curr_node_type.is_for()) {
      do_for(curr_index);
    } else if (curr_node_type.is_while()) {
      do_while(curr_index);
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
//Process the assign node:
void Code_gen::do_assign(const mmap_lib::Tree_index& assign_node_index) {
  fmt::print("node:assign\n");
  auto curr_index = lnast->get_first_child(assign_node_index);
  std::vector<std::string_view> assign_str_vect;

  while(curr_index!=lnast->invalid_index()) {
    assert (!(lnast->get_type(curr_index)).is_invalid());
    //const auto& curr_node_data = lnast->get_data(curr_index);
    auto curlvl = curr_index.level;
    fmt::print("Processing assign child {} at level {} \n",lnast->get_name(curr_index), curlvl);
    assign_str_vect.push_back(lnast->get_name(curr_index));
    curr_index = lnast->get_sibling_next(curr_index);
  }//data of all the child nodes of assign are in assign_str_vect

  assert(assign_str_vect.size()>1);
  auto key = assign_str_vect.front();//usually the ___b type of string
  auto ref = assign_str_vect[1];//usually the const

  auto map_it = ref_map.find(ref);
  if (map_it != ref_map.end()) {
    ref = map_it->second;
  } else if (is_number(ref)) {
   ref = process_number(ref);
  }

  const auto& assign_node_data = lnast->get_data(assign_node_index);
  if (is_temp_var(key)) {
    auto ref_map_inst_res = ref_map.insert(std::pair<std::string_view, std::string>(key, lnast_to->ref_name(ref)));
    if(!ref_map_inst_res.second) {
      absl::StrAppend(&buffer_to_print, indent(), lnast_to->ref_name(ref_map.find(key)->second), " ", lnast_to->debug_name_lang(assign_node_data.type), " ", lnast_to->ref_name(ref), lnast_to->stmt_sep());
    }
  } else {
    absl::StrAppend(&buffer_to_print, indent(), lnast_to->assign_node_strt(), lnast_to->ref_name(key), " ", lnast_to->debug_name_lang(assign_node_data.type), " ", lnast_to->ref_name(ref), lnast_to->stmt_sep());
  }
}
//-------------------------------------------------------------------------------------
//Process the while node:
//pattern: while -> cond , stmts
void Code_gen::do_while(const mmap_lib::Tree_index& while_node_index) {
  fmt::print("node:while\n");
  absl::StrAppend(&buffer_to_print, indent(),  "while");

  auto curr_index = lnast->get_first_child(while_node_index);
  auto ref = lnast->get_name(curr_index);
  if(is_temp_var(ref)) {
    auto map_it = ref_map.find(ref);
    if(map_it != ref_map.end()) {
      ref = map_it->second;
    }
  }
  absl::StrAppend(&buffer_to_print, lnast_to->while_cond_beg(), lnast_to->ref_name(ref), lnast_to->while_cond_end(), lnast_to->for_stmt_beg());

  curr_index = lnast->get_sibling_next(curr_index);
  indendation++;
  do_stmts(curr_index);
  indendation--;
  absl::StrAppend(&buffer_to_print, indent(), lnast_to->for_stmt_end());

}
//-------------------------------------------------------------------------------------
//Process the for node:
//pattern: for -> stmts , ref "i" , ref "___a"
//example: for i in 0..3 {//stmts}
//0..3 is resolved as ___a as tuple already.
void Code_gen::do_for(const mmap_lib::Tree_index& for_node_index) {
  fmt::print("node:for\n");
  absl::StrAppend(&buffer_to_print, indent(),  "for");

  auto stmt_index = lnast->get_first_child(for_node_index);

  auto curr_index = lnast->get_sibling_next(stmt_index);
  absl::StrAppend(&buffer_to_print, lnast_to->for_cond_beg(), lnast_to->ref_name(lnast->get_name(curr_index)), lnast_to->for_cond_mid());

  curr_index = lnast->get_sibling_next(curr_index);
  auto ref = lnast->get_name(curr_index);
  if(is_temp_var(ref)) {
    auto map_it = ref_map.find(ref);
    if(map_it != ref_map.end()) {
      ref = map_it->second;
    }
  }
  absl::StrAppend(&buffer_to_print, lnast_to->ref_name(ref), lnast_to->for_cond_end());

  absl::StrAppend(&buffer_to_print, lnast_to->for_stmt_beg());
  indendation++;
  do_stmts(stmt_index);
  indendation--;
  absl::StrAppend(&buffer_to_print, indent(),  lnast_to->for_stmt_end());

}
//-------------------------------------------------------------------------------------
//Process the "func_def" node:Eg.:
//2                    func_def :
//3                           ref : func_xor
//3                          cond : $valid
//3                         stmts : ___SEQ1
//4                             xor :
//5                               ref : ___a
//5                               ref : $a
//5                               ref : $b
//4                          assign :
//5                               ref : %out
//5                               ref : ___a
//3                           ref : $a
//3                           ref : $b
//3                           ref : $valid
//3                           ref : %out
void Code_gen::do_func_def(const mmap_lib::Tree_index& func_def_node_index) {
  fmt::print("node:func_def\n");
  auto curr_index = lnast->get_first_child(func_def_node_index);
  std::string_view func_name = lnast->get_name(curr_index);

  curr_index = lnast->get_sibling_next(curr_index);
  std::string cond_val = resolve_func_cond(curr_index);

  auto stmt_index = lnast->get_sibling_next(curr_index);

  curr_index = lnast->get_sibling_next(stmt_index);
  std::string parameters;
  bool param_exist = true;
  if(curr_index!=lnast->invalid_index()) {
    while (curr_index!=lnast->invalid_index()) {
      assert(!(lnast->get_type(curr_index)).is_invalid());
      absl::StrAppend(&parameters, lnast_to->ref_name(lnast->get_name(curr_index)), lnast_to->func_param_sep());
      curr_index = lnast->get_sibling_next(curr_index);
    }
    parameters.pop_back();
    parameters.pop_back();
  } else { param_exist = false;}

  absl::StrAppend(&buffer_to_print, indent(), lnast_to->func_begin(), lnast_to->func_name(func_name), lnast_to->param_start(param_exist), parameters, lnast_to->param_end(param_exist), lnast_to->print_cond(cond_val), lnast_to->func_stmt_strt());
  indendation++;
  do_stmts(stmt_index);
  indendation--;
  absl::StrAppend(&buffer_to_print, indent(), lnast_to->func_stmt_end(), lnast_to->func_end());
}
//-------------------------------------------------------------------------------------
//Process the func-cond node:
//cond node is generally either "true" -> nothing to be printed, true by default
//or it is like ___x -> value of ___x must be resolved and "when <reolved ___x>" must be printed
//or it is just the variable which must be printed as is
std::string Code_gen::resolve_func_cond(const mmap_lib::Tree_index& func_cond_index) {
  fmt::print("node:function cond\n");
  //const auto& curr_node_data = lnast->get_data(func_cond_index);
  auto ref = lnast->get_name(func_cond_index);
  if(is_temp_var(ref)) {
    auto map_it = ref_map.find(ref);
    if(map_it != ref_map.end()) {
      ref = map_it->second;
    }
  } else if (ref == "true") {
    ref = "";
  }
  return std::string(ref);
}

//-------------------------------------------------------------------------------------
//Process the "func_call" node:
//Pattern: lhs, func_name, arguments
//all nodes are "ref" type
//arguments are "___x"
//refer to: https://masc.soe.ucsc.edu/lnast-doc/?coffescript#explicit-function-argument-assignment
void Code_gen::do_func_call(const mmap_lib::Tree_index& func_call_node_index) {
  fmt::print("node:func_call\n");
  auto curr_index = lnast->get_first_child(func_call_node_index);
  //const auto& curr_node_data = lnast->get_data(func_cond_index);//returns the entire node contents.
  auto lhs = lnast->get_name(curr_index);
  if (is_temp_var(lhs)) {
    auto map_it = ref_map.find(lhs);
    if(map_it != ref_map.end()) {
      lhs = map_it->second;
    }
  }
  absl::StrAppend(&buffer_to_print, indent(), lhs, " = ");//lhs and assignment op to further assign the func name and arguments to lhs

  curr_index = lnast->get_sibling_next(curr_index);
  absl::StrAppend(&buffer_to_print, lnast->get_name(curr_index));//printitng the func name(the func called)

  curr_index = lnast->get_sibling_next(curr_index);
  auto ref = lnast->get_name(curr_index);
  if(is_temp_var(ref)) {
    auto map_it = ref_map.find(ref);
    if(map_it != ref_map.end()) {
      ref = map_it->second;
    }
  }
  absl::StrAppend(&buffer_to_print, lnast_to->ref_name(ref), lnast_to->stmt_sep());//parameters for the func call
}
//-------------------------------------------------------------------------------------
//Process the "if" node:
//pattern:
//if ->
//   cond (like ___a)
//   stmts
void Code_gen::do_if(const mmap_lib::Tree_index& if_node_index) {
  fmt::print("node:if\n");
  auto curr_index = lnast->get_first_child(if_node_index);
  int node_num = 0;

  while(curr_index!=lnast->invalid_index()) {
    assert(!(lnast->get_type(curr_index)).is_invalid());
    node_num++;
    const auto& curr_node_type = lnast->get_type(curr_index);
    auto curlvl = curr_index.level;//for debugging message printing purposes only
    fmt::print("Processing assign child {} at level {} \n",lnast->get_name(curr_index), curlvl);

    if(node_num>2) {
      //if(curr_node_type.is_cstmts()) {
      //  do_stmts(curr_index);
      //} else
      if (curr_node_type.is_cond()) {
        absl::StrAppend(&buffer_to_print, indent(), lnast_to->start_else_if());
        do_cond(curr_index);
      } else if (curr_node_type.is_stmts()) {
        bool prev_was_cond = (lnast->get_data(lnast->get_sibling_prev(curr_index))).type.is_cond();
        if (!prev_was_cond) {
          absl::StrAppend(&buffer_to_print, indent(), lnast_to->start_else());
        }
        indendation++;
        do_stmts(curr_index);
        indendation--;
        if (!prev_was_cond) {
          absl::StrAppend(&buffer_to_print, indent(), lnast_to->end_if_or_else());
        }
      }
    } else {
      //if(curr_node_type.is_cstmts()) {
      //  do_stmts(curr_index);
      //} else
      if (curr_node_type.is_cond()) {
        absl::StrAppend(&buffer_to_print, indent(), lnast_to->start_cond());
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

  if(node_num<=2) absl::StrAppend(&buffer_to_print, indent(), lnast_to->end_if_or_else());
}

//-------------------------------------------------------------------------------------
//Process the if-cond node:
void Code_gen::do_cond(const mmap_lib::Tree_index& cond_node_index) {
  fmt::print("node:cond\n");
  //const auto& curr_node_data = lnast->get_data(cond_node_index);
  std::string_view ref = lnast->get_name(cond_node_index);
  auto map_it = ref_map.find(ref);
  if(map_it != ref_map.end()) {
    ref = map_it->second;
  }
  absl::StrAppend(&buffer_to_print, lnast_to->ref_name(ref));
  absl::StrAppend(&buffer_to_print, lnast_to->end_cond());
}

//-------------------------------------------------------------------------------------
//Process the operator (like and,or,etc.) node:
void Code_gen::do_op(const mmap_lib::Tree_index& op_node_index) {
  fmt::print("node:op\n");
  auto curr_index = lnast->get_first_child(op_node_index);
  std::vector<std::string_view> op_str_vect;

  while(curr_index!=lnast->invalid_index()) {
    assert(!(lnast->get_type(curr_index)).is_invalid());
    //const auto& curr_node_data = lnast->get_data(curr_index);
    auto curlvl = curr_index.level;//for debugging message printing purposes only
    fmt::print("Processing op child {} at level {} \n",lnast->get_name(curr_index), curlvl);
    //std::string tmp_trm = lnast_to->ref_name(std::string(lnast->get_name(curr_index)));
    op_str_vect.push_back(lnast->get_name(curr_index));
    curr_index = lnast->get_sibling_next(curr_index);
  }
  //op_str_vect now has all the children of the operation "op"

  auto key = op_str_vect.front();
  bool op_is_unary = false;
  if(is_temp_var(key) && op_str_vect.size()==2){
    op_is_unary = true;
  }

  const auto& op_node_data = lnast->get_data(op_node_index);
  std::string val;
  for (unsigned i = 1; i < op_str_vect.size(); i++) {
    auto ref = std::string(op_str_vect[i]);
    auto map_it = ref_map.find(ref);
    if (map_it != ref_map.end()) {
      if (std::count(map_it->second.begin(), map_it->second.end(), ' ')) {
        ref = absl::StrCat("(", map_it->second, ")");
      } else {
        ref = map_it->second;
      }
    } else if (is_number(ref)) {
      ref = process_number(ref);
    }
    // check if a number
    if(op_is_unary) {absl::StrAppend(&val,lnast_to->debug_name_lang(op_node_data.type));}
    absl::StrAppend(&val, lnast_to->ref_name(ref));
    if ((i+1) != op_str_vect.size() && !op_is_unary) {
      absl::StrAppend(&val, " ", lnast_to->debug_name_lang(op_node_data.type), " ");
    }
  }

  if(is_temp_var(key)) {
    ref_map.insert(std::pair<std::string_view, std::string>(key, lnast_to->ref_name(val)));
  } else {
    absl::StrAppend (&buffer_to_print, indent(), lnast_to->ref_name(key), " ", lnast_to->debug_name_lang(op_node_data.type), " ", lnast_to->ref_name(val), lnast_to->stmt_sep());
  }

}

//-------------------------------------------------------------------------------------
//processing dot operator
//best testing case: cfg/tests/ring.prp
void Code_gen::do_dot(const mmap_lib::Tree_index& dot_node_index) {
  fmt::print("node:dot\n");

  auto curr_index = lnast->get_first_child(dot_node_index);
  std::vector<std::string_view> dot_str_vect;
  while(curr_index!=lnast->invalid_index()) {
    assert(!(lnast->get_type(curr_index)).is_invalid());
    //auto curlvl = curr_index.level;
    //fmt::print("Processing dot child {} at level {} \n",lnast->get_name(curr_index), curlvl);
    dot_str_vect.push_back(lnast->get_name(curr_index));
    curr_index = lnast->get_sibling_next(curr_index);
  }
  //dot_str_vect now has all the children of the operation "op"

  assert(dot_str_vect.size()>2);
  auto key = dot_str_vect.front();

  auto i = 1u;
  std::string value;
  const auto& dot_node_data = lnast->get_data(dot_node_index);
  while (i<dot_str_vect.size()) {
    auto ref = std::string(dot_str_vect[i]);
    auto map_it = ref_map.find(ref);
    if (map_it != ref_map.end()) {
      ref = map_it->second;
    }
    //absl::StrAppend(&value, ref);
    if (ref=="__valid") {
      value.pop_back();
      absl::StrAppend(&value, "?");
    } else if (ref=="__retry") {
      value.pop_back();
      absl::StrAppend(&value, "!");
    } else {
      absl::StrAppend(&value, ref);
    }
    if (is_number(ref)) {
      absl::StrAppend(&value, process_number(ref));
    }
    absl::StrAppend(&value, lnast_to->debug_name_lang(dot_node_data.type));//appends "." after the value in case of pyrope
    i++;
  }
  value.pop_back();

  if (is_temp_var(key)) {
    ref_map.insert(std::pair<std::string_view, std::string>(key, lnast_to->ref_name(value)));
  } else {
    absl::StrAppend(&buffer_to_print, indent(), key, " saved as ", lnast_to->ref_name(value), "\n");
    // this should never be possible
  }

}
//-------------------------------------------------------------------------------------
//Process the select node:
//ref LNAST subtree: select,""  ->  ref,"___l" , ref,"A" , const,"0"
void Code_gen::do_select(const mmap_lib::Tree_index& select_node_index, std::string select_type) {
  fmt::print("node:select\n");
  auto curr_index = lnast->get_first_child(select_node_index);
  std::vector<std::string_view> sel_str_vect;
  while(curr_index!=lnast->invalid_index()) {
    assert(!(lnast->get_type(curr_index)).is_invalid());
    //const auto& curr_node_data = lnast->get_data(curr_index);
    sel_str_vect.push_back(lnast->get_name(curr_index));
    curr_index = lnast->get_sibling_next(curr_index);
  }

  if (select_type=="bit") { assert(sel_str_vect.size()>=2); } else { assert(sel_str_vect.size()>=3);}
  auto key = sel_str_vect.front();
  std::string value = std::string(sel_str_vect[1]);

  auto i = 2u;
  if (i==sel_str_vect.size()) {
    absl::StrAppend(&value, lnast_to->select_init(select_type), lnast_to->select_end(select_type));
  }
  while (i < sel_str_vect.size()) {
    auto ref = sel_str_vect[i];

    auto map_it = ref_map.find(ref);
    if (map_it != ref_map.end()) {
      ref = map_it->second;
    }

    absl::StrAppend(&value, lnast_to->select_init(select_type), lnast_to->ref_name(ref), lnast_to->select_end(select_type));
    i++;
  }

  if (is_temp_var(key)) {
   // std::string value = absl::StrCat(sel_str_vect[1], "[", ref, "]");
    ref_map.insert(std::pair<std::string_view, std::string>(key, value));
  } else {
    fmt::print("ERROR:\n\t\t------CHECK THE NODE TYPE IN THIS IF -----!!\n");
  }
}

//-------------------------------------------------------------------------------------
//processing tuple
void Code_gen::do_tuple(const mmap_lib::Tree_index& tuple_node_index) {
  fmt::print("node:tuple\n");

  //Process the first child node in key and move to the next node:
  auto curr_index = lnast->get_first_child(tuple_node_index);
  std::string_view key = lnast->get_name(curr_index);

  //Process remaining nodes/sub-trees:
  curr_index = lnast->get_sibling_next(curr_index);
  std::string tuple_value = "";
  while(curr_index!=lnast->invalid_index() ) {
    assert(!(lnast->get_type(curr_index)).is_invalid());
    if (lnast->is_leaf(curr_index)) {
      auto ref = std::string(lnast->get_name(curr_index));
      //For case like:
      //tuple :
      //    ref : ___b
      //    ref : ___a
      if(is_temp_var(ref)) {
        auto map_it = ref_map.find(ref);
        if(map_it != ref_map.end()) {
          ref = map_it->second;
        }
      }
      absl::StrAppend(&tuple_value, lnast_to->ref_name(ref), lnast_to->tuple_stmt_sep());
    } else {
      absl::StrAppend(&tuple_value, resolve_tuple_assign(curr_index));
    }
    curr_index = lnast->get_sibling_next(curr_index);
  }

  //for formatting purposes:
  if (tuple_value.length()>2) {
    if (tuple_value.substr(tuple_value.length()-2) == lnast_to->tuple_stmt_sep()) {
      tuple_value = absl::StrCat(lnast_to->tuple_begin(), tuple_value);
      tuple_value.pop_back();
      tuple_value.pop_back();//to remove the extra (last) tuple stmt sep inserted
      absl::StrAppend(&tuple_value, lnast_to->tuple_end());
    }
  } else if (tuple_value=="") { tuple_value = absl::StrCat(lnast_to->tuple_begin(), lnast_to->tuple_end()) ;}//to cater to scenario like: out = () :in ring.prp

  //insert to map:
  if(is_temp_var(key)) {
    ref_map.insert(std::pair<std::string_view, std::string>(key, tuple_value));
  } else {
    absl::StrAppend(&buffer_to_print, key, " saved as ", tuple_value, "\n");
    // this should never be possible
  }

}

//-------------------------------------------------------------------------------------
//function called to process the tuple:
std::string Code_gen::resolve_tuple_assign(const mmap_lib::Tree_index& tuple_assign_index) {

  auto curr_index = lnast->get_first_child(tuple_assign_index);
  std::vector<std::string_view> op_str_vect;

  while(curr_index!=lnast->invalid_index()) {
    assert(!(lnast->get_type(curr_index)).is_invalid());
    //const auto& curr_node_data = lnast->get_data(curr_index);
    op_str_vect.push_back(lnast->get_name(curr_index));
    curr_index = lnast->get_sibling_next(curr_index);
  }
  //op_str_vect now has all the children of the operation "op"

  auto key = op_str_vect.front();
  bool is_const = false;
  std::string val = "";
  const auto& op_node_data = lnast->get_data(tuple_assign_index); //the operator (assign or as)

  if (key == "null") {
    is_const = true;
    val = op_str_vect.back();
    //val = op_str_vect[1];
  } else {
    bool op_is_unary = false;
    if(is_temp_var(key) && op_str_vect.size()==2) {
      op_is_unary = true;
    }

    for (unsigned i = 1; i < op_str_vect.size(); i++) {
      auto ref = std::string(op_str_vect[i]);
      if (ref=="null") {
        val="";
        break;
      }
      auto             map_it = ref_map.find(ref);
      if (map_it != ref_map.end()) {
        if (std::count(map_it->second.begin(), map_it->second.end(), ' ')) {
          ref = absl::StrCat("(", map_it->second, ")");
        } else {
          ref = map_it->second;
        }
       // fmt::print("map_it find: {} | {}\n", map_it->first, ref);
      } else if (is_number(ref)) {
        ref = process_number(ref);
      }
      // check if a number
      if(op_is_unary) {absl::StrAppend(&val,lnast_to->debug_name_lang(op_node_data.type));}
      absl::StrAppend(&val, lnast_to->ref_name(ref));
      if ((i+1) != op_str_vect.size() && !op_is_unary) {
        absl::StrAppend(&val, " ", lnast_to->debug_name_lang(op_node_data.type), " ");
      }
    }
  }

  if(is_temp_var(key)) {
    ref_map.insert(std::pair<std::string_view, std::string>(key, val));
    return ("\n\nERROR:\n\t----------------UNEXPECTED TUPLE VALUE!--------------------\n\n");
  } else if (is_const) {
    std::string ret_tup_str = absl::StrCat (indent(), val, lnast_to->tuple_stmt_sep());
    return ret_tup_str;
  } else if (key=="__range_begin") {
    return  absl::StrCat(val, ".");
  } else if (key == "__range_end") {
    return absl::StrCat(".", val);
  } else {
    std::string ret_tup_str = absl::StrCat (indent(), key, " ", lnast_to->debug_name_lang(op_node_data.type), " ", val, lnast_to->tuple_stmt_sep());
    return (ret_tup_str);
  }
}

//-------------------------------------------------------------------------------------
//check if the node has "___"
bool Code_gen::is_temp_var(std::string_view test_string) {
  return (test_string.find("___")==0 || test_string.find("_._")==0) ;
}

//-------------------------------------------------------------------------------------
bool Code_gen::is_number(std::string_view test_string) {
  if (test_string.find("0d") == 0) {
    return true;
  } else if (test_string.find("0b") == 0) {
    return true;
  } else if (test_string.find("0x") == 0) {
    return true;
  }
  return false;
}

//-------------------------------------------------------------------------------------
std::string_view Code_gen::process_number(std::string_view num_string) {
  if (num_string.find("0d") == 0) {
    return num_string.substr(2);
  }
  return num_string;
}

//-------------------------------------------------------------------------------------
std::string Code_gen::indent() { return std::string(indendation * 2, ' '); }

//-------------------------------------------------------------------------------------
