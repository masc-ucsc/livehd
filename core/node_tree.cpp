#include "node_tree.hpp"

#include <functional>

#include "lgedgeiter.hpp"
#include "lgraph.hpp"

Node_tree::Node_tree(LGraph* root_arg) : mmap_lib::tree<Node>(root_arg->get_path(), absl::StrCat(root_arg->get_name(), "_ntree")), root(root_arg) {
  auto ht = root->ref_htree();
  
  set_root(Node());

  absl::flat_hash_set<Hierarchy_index> hidx_used;

  std::function<void(LGraph*, Hierarchy_index, Tree_index)> add_lg_nodes = [&](LGraph* lg, Hierarchy_index hidx, Tree_index tidx) {
    Tree_index last_sib;
    for (auto fn : lg->fast()) {
      if (!fn.is_type_synth() && !fn.is_type_sub_present()) {
        continue;  // skip subnodes
      }

      auto cn = Node(root, hidx, fn.get_compact_class());

      Tree_index tree_cidx;
      if (last_sib.is_invalid()) {
        tree_cidx = add_child(tidx, cn);
        last_sib  = tree_cidx;
      } else {
        tree_cidx = append_sibling(last_sib, cn);
      }

      //fmt::print("node {}: hl: {}, hp: {}, tl: {}, tp: {}\n", cn.debug_name(), hidx.level, hidx.pos, tree_cidx.level, tree_cidx.pos);

      if (fn.is_type_sub_present()) {
        //fmt::print("evaluating subnode {}\n", cn.debug_name());
        auto sub_hidx = ht->get_first_child(hidx);

        bool found = false;
        while (sub_hidx != ht->invalid_index()) {
          LGraph* sub_lg = LGraph::open(root->get_path(), cn.get_type_sub());

          //fmt::print("testing l: {}, p: {} ({})\n", sub_hidx.level, sub_hidx.pos, sub_lg->get_name());
          if (ht->ref_lgraph(sub_hidx) == sub_lg && !hidx_used.contains(sub_hidx)) {
            found = true;
            //fmt::print("found l: {}, p: {} ({}), recursing.\n", sub_hidx.level, sub_hidx.pos, sub_lg->get_name());
            hidx_used.emplace(sub_hidx);
            add_lg_nodes(sub_lg, sub_hidx, tree_cidx);
            break;
          }
          sub_hidx = ht->get_sibling_next(sub_hidx);
        }

        I(found);
      }
    }

    // fmt::print("done.\n");
  };

  add_lg_nodes(root, Hierarchy_index(0, 0), Tree_index(0, 0));
}

void Node_tree::dump() const {
  for (const auto &index : depth_preorder()) {
    std::string indent(index.level, ' ');
    const auto &index_data = get_data(index);
    fmt::print("{} level: {} pos: {} name: {}\n", indent, index.level, index.pos, index_data.debug_name());
  }
}