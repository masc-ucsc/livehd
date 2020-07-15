
#include "code_gen.hpp"

Code_gen::Code_gen(std::shared_ptr<Lnast> _lnast, std::string_view _path) : lnast(std::move(_lnast)), path(_path) {}


void Code_gen::generate(){
  const auto& root_index = lnast->get_root();
  const auto& node_data = lnast->get_data(root_index);
  fmt::print("\n\nprocessing LNAST tree\n\n");
  if (node_data.type.is_top()) {
    //never used//const auto& top_child_index = lnast->get_first_child(root_index);
   // fmt::print("\n\nprocessing LNAST tree root: {} ", static_cast<Lnast_ntype::Lnast_ntype_int>(node_data.type.get_raw_ntype()));
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
    }
    curr_index = lnast->get_sibling_next(curr_index);
  }
}

//Process the assign node:
void Code_gen::do_assign(const mmap_lib::Tree_index& assign_node_index) {
  fmt::print("node:assign\n");
  auto curr_index = lnast->get_first_child(assign_node_index);
  std::vector<std::string_view> assign_str_vect;
  std::string key;
  std::string val;
  while(curr_index!=lnast->invalid_index()) {
    const auto& curr_node_data = lnast->get_data(curr_index);
    auto curlvl = curr_index.level;
    fmt::print("Processing assign child {} at level {} \n",Code_gen::get_node_name(curr_node_data), curlvl);
//    if (is_temp_var(get_node_name(curr_node_data))) {
//      auto ref_map_inst_res = ref_map.insert(std::pair<std::string_view, std::string>(key, (std::string)ref));
//    }
    assign_str_vect.push_back(Code_gen::get_node_name(curr_node_data));
  //  absl::StrAppend(&assign_str, Code_gen::get_node_name(curr_node_data));
    curr_index = lnast->get_sibling_next(curr_index);
  //  absl::StrAppend(&assign_str, "=");
  }
  if (assign_str_vect.size()==2) {
    key = assign_str_vect.at(0);
    val = assign_str_vect.at(1);
  } else {
    fmt::print("check vector.\n");
  }
//  absl::StrAppend(&buffer_to_print, assign_str);
  absl::StrAppend(&buffer_to_print, key, " ", "=", " ", val);
//  assign_str = "";
}

//Get the textual value of node. Eg., get "$a" from the node "ref, $a":
std::string_view Code_gen::get_node_name(Lnast_node node) { return node.token.get_text(); }

//check if the node has "___"
bool Code_gen::is_temp_var(std::string_view test_string) {
  return test_string.find("___")==0;
}
