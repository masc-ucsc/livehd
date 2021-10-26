
#include "node_tree.hpp"

#include <functional>

#include "lgedgeiter.hpp"
#include "lgraph.hpp"

Node_tree::Node_tree(Lgraph* root_arg)
    : mmap_lib::tree<Node>(root_arg->get_path().to_s(), absl::StrCat(root_arg->get_name().to_s(), "_ntree")), root(root_arg), last_free() {
  set_root(Node());

#if 1
  assert(false); // TO DEPRECATE SOON
#else
  absl::flat_hash_set<Hierarchy_index>                      hidx_used;
  std::function<void(Lgraph*, Hierarchy_index, Tree_index)> add_lg_nodes = [&](Lgraph* lg, Hierarchy_index hidx, Tree_index tidx) {
    auto ht = root->ref_htree();

    Tree_index last_sib;
    for (auto fn : lg->fast()) {
      if (!fn.is_type_synth() && !fn.is_type_sub_present()) {
        continue;
      }

      auto cn = Node(root, hidx, fn.get_compact_class());

      Tree_index tree_cidx;  // current child index in tree
      if (last_sib.is_invalid()) {
        tree_cidx = add_child(tidx, cn);
      } else {
        tree_cidx = insert_next_sibling(last_sib, cn);
      }
      last_sib = tree_cidx;

      if (debug_verbose) {
        fmt::print("node {}: hidx:{}, tl:{}, tp:{}\n",
                   cn.debug_name(),
                   hidx,
                   (int)tree_cidx.level,
                   (int)tree_cidx.pos);
      }

      if (fn.is_type_sub_present()) {
        if (debug_verbose) {
          fmt::print("evaluating subnode {}\n", cn.debug_name());
        }

        auto sub_hidx = ht->get_first_child(hidx);

        bool found = false;
        while (sub_hidx != ht->invalid_index()) {
          Lgraph* sub_lg = Lgraph::open(root->get_path(), cn.get_type_sub());

          if (debug_verbose) {
            fmt::print("testing l:{}, p:{} ({})\n", (int)sub_hidx.level, (int)sub_hidx.pos, sub_lg->get_name());
          }

          if (ht->ref_lgraph(sub_hidx) == sub_lg && !hidx_used.contains(sub_hidx)) {
            found = true;
            if (debug_verbose) {
              fmt::print("found l:{}, p:{} ({}), recursing.\n", (int)sub_hidx.level, (int)sub_hidx.pos, sub_lg->get_name());
            }

            hidx_used.emplace(sub_hidx);
            add_lg_nodes(sub_lg, sub_hidx, tree_cidx);
            break;
          }
          sub_hidx = ht->get_sibling_next(sub_hidx);
        }

        I(found);
      } else {
        auto& p = last_free[tidx][size_t(cn.get_type_op()) - 1];
        if (p.is_invalid()) {
          p = last_sib;
        }
      }
    }

    if (debug_verbose) {
      fmt::print("done.\n");
    }
  };

  add_lg_nodes(root, Hierarchy_index(0, 0), Tree_index(0, 0));
#endif
}

void Node_tree::dump() const {
  for (const auto& index : depth_preorder()) {
    std::string indent(index.level, ' ');
    const auto& id = get_data(index);

    mmap_lib::str name;
    if (id.is_invalid()) {
      name = mmap_lib::str("(root module)");
    } else {
      if (id.has_instance_name()) {
        name = id.get_instance_name();
      } else {
        name = id.default_instance_name();
      }
    }

    fmt::print("{} name: {} loc: ({}, {}) livehd loc: ({})\n",
               indent,
               name,
               index.level,
               index.pos,
               id.get_hidx());
  }
}