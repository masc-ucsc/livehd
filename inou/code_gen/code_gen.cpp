
#include "code_gen.hpp"

Code_gen::Code_gen(std::shared_ptr<Lnast> _lnast, std::string_view _path) : lnast(std::move(_lnast)), path(_path) {}


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

  auto basename = absl::StrCat(lnast->get_top_module_name(), ".prp");

  fmt::print("lnast_to_prp_parser path:{} file:{}\n", path, basename);
  fmt::print("{}\n", buffer_to_print);
  fmt::print("<<EOF\n");
}

void Code_gen::do_stmts(const mmap_lib::Tree_index& stmt_node_index) {
  auto curr_index = lnast->get_first_child(stmt_node_index);
  while(curr_index!=lnast->invalid_index()) {
    const auto& curr_node_data = lnast->get_data(curr_index);
    auto curlvl = curr_index.level;
    fmt::print("Processing stmt child {} at level {} \n",Code_gen::get_node_name(curr_node_data), curlvl);
    if (curr_node_data.type.is_assign()) {
      do_assign(curr_index);
    } else if (curr_node_data.type.is_or()) {
      do_op(curr_index);
    }
    curr_index = lnast->get_sibling_next(curr_index);
  }
}

//Process the assign node:
void Code_gen::do_assign(const mmap_lib::Tree_index& assign_node_index) {
  fmt::print("node:assign\n");
  auto curr_index = lnast->get_first_child(assign_node_index);
  std::vector<std::string_view> assign_str_vect;
  std::string_view key;
  std::string_view ref;
  while(curr_index!=lnast->invalid_index()) {
    const auto& curr_node_data = lnast->get_data(curr_index);
    auto curlvl = curr_index.level;
    fmt::print("Processing assign child {} at level {} \n",Code_gen::get_node_name(curr_node_data), curlvl);
    assign_str_vect.push_back(Code_gen::get_node_name(curr_node_data));
    curr_index = lnast->get_sibling_next(curr_index);
  }//data of all the child nodes of assign are in assign_str_vect
  key = assign_str_vect.at(0);
  ref = assign_str_vect.at(1);
  auto map_it = ref_map.find(ref);
  if (map_it != ref_map.end()) {
    ref = map_it->second;
  } else if (is_number(ref)) {
   ref = process_number(ref);
  }
  if (is_temp_var(key)) {
    auto ref_map_inst_res = ref_map.insert(std::pair<std::string_view, std::string>(key, (std::string)ref));
    if(!ref_map_inst_res.second) {
      absl::StrAppend(&buffer_to_print, ref_map.find(key)->second, " ", "=", " ", (std::string)ref);//insert stmt sep here
    }
  } else {
    absl::StrAppend(&buffer_to_print, key, " ", "=", " ", (std::string)ref);//stmt sep here;
  }
}

//Process the operator (like and,or,etc.) node:
void Code_gen::do_op(const mmap_lib::Tree_index& op_node_index) {
  auto curr_index = lnast->get_first_child(op_node_index);
  const auto& op_node_data = lnast->get_data(op_node_index);
  std::vector<std::string_view> op_str_vect;
  std::string_view key;
  std::string val;
  //TODO: make a func to convert the subtree to vector of strings (as done in following while loop) and return the vect of strings
  while(curr_index!=lnast->invalid_index()) {
    const auto& curr_node_data = lnast->get_data(curr_index);
    auto curlvl = curr_index.level;
    fmt::print("Processing assign child {} at level {} \n",Code_gen::get_node_name(curr_node_data), curlvl);
    op_str_vect.push_back(Code_gen::get_node_name(curr_node_data));
    curr_index = lnast->get_sibling_next(curr_index);
  }
  //op_str_vect now has all the children of the operation "op"
  key = op_str_vect.at(0);
  for (int i = 1; i < op_str_vect.size(); i++) {
    std::string_view ref = op_str_vect.at(i);
    absl::StrAppend(&val, ref);
    if ((i+1) != op_str_vect.size()) {
      absl::StrAppend(&val, " ", op_node_data.type.debug_name_pyrope(), " ");
    }
  }

  if(is_temp_var(key)) {
    ref_map.insert(std::pair<std::string_view, std::string>(key, val));
  } else {
    absl::StrAppend (&buffer_to_print, key, " ", op_node_data.type.debug_name_pyrope(), " ", val);//put stmt separator here;
  }

}
//Get the textual value of node. Eg., get "$a" from the node "ref, $a":
std::string_view Code_gen::get_node_name(Lnast_node node) { return node.token.get_text(); }

//check if the node has "___"
bool Code_gen::is_temp_var(std::string_view test_string) {
  return test_string.find("___")==0;
}


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

std::string_view Code_gen::process_number(std::string_view num_string) {
  if (num_string.find("0d") == 0) {
    return num_string.substr(2);
  }
  return num_string;
}

