
#include "lnast_to_cfg_parser.hpp"

void Lnast_to_cfg_parser::start_sub_sts() {
  ;
}

void Lnast_to_cfg_parser::stringify() {
  fmt::print("start Lnast_to_cfg_parser::stringify");
  fmt::print("starting point: ");

  std::map<Tree_pos, std::map<Tree_level, std::string>> nodes;
  for (const auto &it: lnast->depth_preorder(lnast->get_root())) {
    const auto& node_data = lnast->get_data(it);
    std::string name(node_data.token.get_text(memblock)); // str_view to string
    std::string type = lnast_parser.ntype_dbg(node_data.type);
    auto node_scope = node_data.scope;

    fmt::print("tree index: pos:{} level:{}\n", it.pos, it.level);
    fmt::print("node: n:{} t:{} s:{} k:{} b:{}\n\n", name, type, node_scope, node_data.knum, node_data.sbs);

/*
    if (name == "") {
      fmt::print("\n{} {} ", knum++, type);
    } else {
      fmt::print("{} ", name);
    }
*/

    auto node = nodes.find(it.pos);
    if (node == nodes.end()) {
      std::map<Tree_level, std::string> tmp;
      nodes.insert(std::pair<Tree_pos, std::map<Tree_level, std::string>>(it.pos, tmp));
    } else {
      node->second.insert(std::pair<Tree_level, std::string>(it.level, name));
    }
  }

  fmt::print("\n\nformal print:\n");
  for (auto const& node : nodes) {
    fmt::print("{} ", node.first);
    for (auto const& ele : node.second) {
      fmt::print(" {}", ele.second);
    }
    fmt::print("\n");
  }

  fmt::print("end\n\n");
}

