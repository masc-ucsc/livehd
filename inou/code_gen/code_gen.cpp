
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
    fmt::print("\nprocessing LNAST tree root text: {} ", node_data.token.text);
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
  auto curr_index = lnast->get_first_child(stmt_node_index);

  while(curr_index!=lnast->invalid_index()) {
    const auto& curr_node_data = lnast->get_data(curr_index);
    auto curlvl = curr_index.level;
    fmt::print("Processing stmt child {} at level {} \n",Code_gen::get_node_name(curr_node_data), curlvl);

    if (curr_node_data.type.is_assign()) {
      do_assign(curr_index);
    } else if (curr_node_data.type.is_if()) {
      do_if(curr_index);
    } else if (curr_node_data.type.is_and() || curr_node_data.type.is_or() || curr_node_data.type.is_not() || curr_node_data.type.is_xor() || curr_node_data.type.is_logical_not() || curr_node_data.type.is_logical_and() || curr_node_data.type.is_logical_or() || curr_node_data.type.is_same() || curr_node_data.type.is_as() || curr_node_data.type.is_plus() || curr_node_data.type.is_minus() || curr_node_data.type.is_mult() || curr_node_data.type.is_mult() || curr_node_data.type.is_div() || curr_node_data.type.is_lt() || curr_node_data.type.is_le() || curr_node_data.type.is_gt() || curr_node_data.type.is_ge()) {
      do_op(curr_index);
    } else if (curr_node_data.type.is_dot()) {
      do_dot(curr_index);
    }

    curr_index = lnast->get_sibling_next(curr_index);
  }
}

//-------------------------------------------------------------------------------------
//Process the assign node:
void Code_gen::do_assign(const mmap_lib::Tree_index& assign_node_index) {
  fmt::print("node:assign\n");
  auto curr_index = lnast->get_first_child(assign_node_index);
  std::vector<std::string_view> assign_str_vect;

  while(curr_index!=lnast->invalid_index()) {
    const auto& curr_node_data = lnast->get_data(curr_index);
    auto curlvl = curr_index.level;
    fmt::print("Processing assign child {} at level {} \n",Code_gen::get_node_name(curr_node_data), curlvl);
    assign_str_vect.push_back(Code_gen::get_node_name(curr_node_data));
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
    auto ref_map_inst_res = ref_map.insert(std::pair<std::string_view, std::string>(key, (std::string)ref));
    if(!ref_map_inst_res.second) {
      absl::StrAppend(&buffer_to_print, indent(), ref_map.find(key)->second, " ", lnast_to->debug_name_lang(assign_node_data.type), " ", (std::string)ref, lnast_to->stmt_sep());
    }
  } else {
    absl::StrAppend(&buffer_to_print, indent(), key, " ", lnast_to->debug_name_lang(assign_node_data.type), " ", (std::string)ref, lnast_to->stmt_sep());
  }
}
//-------------------------------------------------------------------------------------
//Process the operator (like and,or,etc.) node:
void Code_gen::do_if(const mmap_lib::Tree_index& if_node_index) {
  auto curr_index = lnast->get_first_child(if_node_index);
  int node_num = 0;

  //absl::StrAppend(&buffer_to_print, "lnast_to->start_if()\n");

  while(curr_index!=lnast->invalid_index()) {
    node_num++;
    const auto& curr_node_data = lnast->get_data(curr_index);
    auto curlvl = curr_index.level;//for debugging message printing purposes only
    fmt::print("Processing assign child {} at level {} \n",Code_gen::get_node_name(curr_node_data), curlvl);

    if(node_num>3) {
      if(curr_node_data.type.is_cstmts()) {
        //absl::StrAppend(&buffer_to_print, "xx1\n" );
        //absl::StrAppend(&buffer_to_print, indent(), lnast_to->start_else_if() );
        do_stmts(curr_index);
        //absl::StrAppend(&buffer_to_print, lnast_to->end_else_if());
      } else if (curr_node_data.type.is_cond()) {
        absl::StrAppend(&buffer_to_print, indent(), lnast_to->start_else_if());
        do_cond(curr_index);
      } else if (curr_node_data.type.is_stmts()) {
        //absl::StrAppend(&buffer_to_print, "xx2\n" );
        //absl::StrAppend(&buffer_to_print, lnast_to->end_if_or_else());
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
      if(curr_node_data.type.is_cstmts()) {
        //absl::StrAppend(&buffer_to_print," do cstmts here \n");
        do_stmts(curr_index);
      } else if (curr_node_data.type.is_cond()) {
        absl::StrAppend(&buffer_to_print, indent(), lnast_to->start_cond());
        do_cond(curr_index);
        //do_cond:
      } else if (curr_node_data.type.is_stmts()) {
        indendation++;
        //absl::StrAppend(&buffer_to_print," starting stmts here \n");
        do_stmts(curr_index);
        //absl::StrAppend(&buffer_to_print," ending stmts here \n");
        indendation--;
      } else {
        fmt::print("ERROR:\n\t\t------CHECK THE NODE TYPE IN THIS IF -----!!\n");
      }
    }

    curr_index = lnast->get_sibling_next(curr_index);
  }

  if(node_num<=3) absl::StrAppend(&buffer_to_print, indent(), lnast_to->end_if_or_else());
}

//-------------------------------------------------------------------------------------
//Process the operator (like and,or,etc.) node:
void Code_gen::do_cond(const mmap_lib::Tree_index& cond_node_index) {
  const auto& curr_node_data = lnast->get_data(cond_node_index);
  std::string_view ref = get_node_name(curr_node_data);
  auto map_it = ref_map.find(ref);
  if(map_it != ref_map.end()) {
    ref = map_it->second;
  }
  absl::StrAppend(&buffer_to_print, ref);
  absl::StrAppend(&buffer_to_print, lnast_to->end_cond());
}
//-------------------------------------------------------------------------------------
//Process the operator (like and,or,etc.) node:
void Code_gen::do_op(const mmap_lib::Tree_index& op_node_index) {
  //TODO: make a func to convert the subtree to vector of strings (as done in following while loop) and return the vect of strings
  auto curr_index = lnast->get_first_child(op_node_index);
  std::vector<std::string_view> op_str_vect;

  while(curr_index!=lnast->invalid_index()) {
    const auto& curr_node_data = lnast->get_data(curr_index);
    auto curlvl = curr_index.level;//for debugging message printing purposes only
    fmt::print("Processing assign child {} at level {} \n",Code_gen::get_node_name(curr_node_data), curlvl);
    op_str_vect.push_back(Code_gen::get_node_name(curr_node_data));
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
    auto ref = op_str_vect[i];
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
    absl::StrAppend(&val, ref);
    if ((i+1) != op_str_vect.size() && !op_is_unary) {
      absl::StrAppend(&val, " ", lnast_to->debug_name_lang(op_node_data.type), " ");
    }
  }

  if(is_temp_var(key)) {
    ref_map.insert(std::pair<std::string_view, std::string>(key, val));
  } else {
    absl::StrAppend (&buffer_to_print, indent(), key, " ", lnast_to->debug_name_lang(op_node_data.type), " ", val, lnast_to->stmt_sep());
  }

}

//-------------------------------------------------------------------------------------
//processing dot operator
void Code_gen::do_dot(const mmap_lib::Tree_index& dot_node_index) {

  auto curr_index = lnast->get_first_child(dot_node_index);
  std::vector<std::string_view> dot_str_vect;
  while(curr_index!=lnast->invalid_index()) {
    const auto& curr_node_data = lnast->get_data(curr_index);
    auto curlvl = curr_index.level;
    fmt::print("Processing dot child {} at level {} \n",Code_gen::get_node_name(curr_node_data), curlvl);
    dot_str_vect.push_back(Code_gen::get_node_name(curr_node_data));
    curr_index = lnast->get_sibling_next(curr_index);
  }
  //dot_str_vect now has all the children of the operation "op"

  assert(dot_str_vect.size()>2);
  auto key = dot_str_vect.front();
  auto ref = dot_str_vect[1];

  auto map_it = ref_map.find(ref);
  if (map_it != ref_map.end()) {
    ref = map_it->second;
  }

  const auto& dot_node_data = lnast->get_data(dot_node_index);
  std::string value = absl::StrCat(ref, lnast_to->debug_name_lang(dot_node_data.type), process_number(dot_str_vect[2]));

  if (is_temp_var(key)) {
    ref_map.insert(std::pair<std::string_view, std::string>(key, value));
  } else {
    absl::StrAppend(&buffer_to_print, indent(), key, " saved as ", value, "\n");
    // this should never be possible
  }

}

//-------------------------------------------------------------------------------------
//Get the textual value of node. Eg., get "$a" from the node "ref, $a":
std::string_view Code_gen::get_node_name(Lnast_node node) { return node.token.get_text(); }

//-------------------------------------------------------------------------------------
//check if the node has "___"
bool Code_gen::is_temp_var(std::string_view test_string) {
  return test_string.find("___")==0;
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
