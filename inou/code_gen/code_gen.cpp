
#include "code_gen.hpp"

Code_gen::Code_gen(std::shared_ptr<Lnast> _lnast, std::string_view _path) : lnast(std::move(_lnast)), path(_path) {}


void Code_gen::generate(){
  const auto& root_index = lnast->get_root();
  const auto& node_data = lnast->get_data(root_index);
  fmt::print("\n\nprocessing LNAST tree\n\n");
  if (node_data.type.is_top()) {
    const auto& top_child_index = lnast->get_first_child(root_index);
   // fmt::print("\n\nprocessing LNAST tree root: {} ", static_cast<Lnast_ntype::Lnast_ntype_int>(node_data.type.get_raw_ntype()));
    fmt::print("\nprocessing LNAST tree root text: {} ", node_data.token.text);
    fmt::print("processing root->child");
    do_stmts(lnast->get_child(root_index));
  } else if (node_data.type.is_invalid()) {
    fmt::print("INVALID NODE!");
  } else {
    fmt::print("UNKNOWN NODE TYPE!");
  }
}

void Code_gen::do_stmts(const mmap_lib::Tree_index& stmt_node_index) {
  auto curr_index = lnast->get_first_child(stmt_node_index);
  while(curr_index!=lnast->invalid_index()) {
    auto curlvl = curr_index.level;
    fmt::print("\nProcessing stmt child {} at level {} ",lnast->get_data(curr_index).token.text, curlvl);
    curr_index = lnast->get_sibling_next(curr_index);
  }
}
