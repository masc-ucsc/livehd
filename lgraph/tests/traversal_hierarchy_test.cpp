//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <format>
#include <iostream>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "lhtree.hpp"
#include "lrand.hpp"

class Tree_lgdb_setup : public ::testing::Test {
protected:
  struct Node_data {
    int           create_pos;
    Node::Compact cnode;
    int           fwd_pos;
    int           bwd_pos;
    bool          leaf;
    std::string   name;
  };

  lh::tree<Node_data> tree;
  std::vector<Node>   node_order;
  Lgraph             *lg_root;

  absl::flat_hash_map<Node::Compact, uint64_t> absl_fwd_pos;
  absl::flat_hash_map<Node::Compact, uint64_t> absl_bwd_pos;

  static constexpr char fwd_name[] = "fwd_pos";
  static constexpr char bwd_name[] = "bwd_pos";

  void map_tree_to_lgraph() {
    std::vector<lh::Tree_index> index_order;

    tree.each_top_down_fast([&index_order](const lh::Tree_index &index, const Node_data &node) {
      (void)node;
      // std::print(" level:{} pos:{} create_pos:{} fwd:{} bwd:{} leaf:{}\n", index.level, index.pos, node.create_pos, node.fwd_pos,
      // node.bwd_pos, node.leaf);

      if (index.level || index.pos) {
        index_order.emplace_back(index);
      }
    });

    auto *lib = Graph_library::instance("lgdb_hier_test");

    lg_root = lib->create_lgraph("node_l0p0", "node_l0p0");
    lg_root->add_graph_output("o0", 0, 17);
    lg_root->add_graph_input("i0", 1, 31);

    absl_fwd_pos.clear();
    absl_bwd_pos.clear();

    node_order.clear();

    Lrand<int>  rint;
    Lrand<bool> rbool;

    for (const auto &index : index_order) {
      const auto &data = tree.get_data(index);

      I(index.level || index.pos);  // skip root

      auto        parent_index = tree.get_parent(index);
      const auto &parent_data  = tree.get_data(parent_index);

      Lgraph *parent_lg = lib->open_lgraph(parent_data.name);
      I(parent_lg);
      Node node;
      if (data.leaf && rbool.any()) {
        node = parent_lg->create_node(Ntype_op::Sum, 10);
      } else {
        node           = parent_lg->create_node_sub(data.name);
        Lgraph *sub_lg = lib->create_lgraph(data.name, data.name);
        I(sub_lg);
        I(node.get_class_lgraph() == parent_lg);
        I(node.get_type_sub() == sub_lg->get_lgid());

        int n_inputs  = rint.between(1, 2);
        int n_outputs = rint.between(1, 2);
        int max_pos   = 0;
        for (int i = 0; i < n_inputs; ++i) {
          std::string name(std::string("i") + std::to_string(i));
          int         pos = max_pos + rint.between(1, 5);
          max_pos         = pos;
          sub_lg->add_graph_input(name, pos, rint.max(60));
        }
        for (int i = 0; i < n_outputs; ++i) {
          std::string name(std::string("o") + std::to_string(i));
          int         pos = max_pos + rint.between(1, 5);
          max_pos         = pos;
          sub_lg->add_graph_output(name, pos, rint.max(60));
        }
      }
      tree.ref_data(index)->cnode = node.get_compact();

      node.set_name(data.name);

      absl_fwd_pos[node.get_compact()] = data.fwd_pos;
      absl_bwd_pos[node.get_compact()] = data.bwd_pos;

      // std::print("create {} class {}\n", hnode.debug_name(), hnode.get_class_lgraph()->get_name());
      node_order.emplace_back(node);
    }

    {
      auto dpin = lg_root->get_graph_input("i0");
      node_order[0].get_sink_pin().connect_driver(dpin);
    }

    for (size_t i = 1; i < node_order.size(); ++i) {
      const auto &curr_index = index_order[i];
      const auto &prev_index = index_order[i - 1];
      const auto &curr_data  = tree.get_data(curr_index);
      const auto &prev_data  = tree.get_data(prev_index);

      // const auto &parent_index = tree.get_parent(curr_index);
      // const auto &parent_data = tree.get_data(parent_index);
      // Lgraph *parent_lg = Lgraph_open("lgdb_hier_test", parent_data.name);

      auto &curr_node = node_order[i];
      auto &prev_node = node_order[i - 1];

      //    std::print("prev   {} class {}\n", curr_node.debug_name(), curr_node.get_class_lgraph()->get_name());
      //    std::print("curr   {} class {}\n", prev_node.debug_name(), prev_node.get_class_lgraph()->get_name());

      Node_pin dpin;
      if (prev_node.get_type_op() == Ntype_op::Sum) {
        I(prev_data.leaf);
        dpin = prev_node.setup_driver_pin();
      } else {
        Lgraph *prev_lg = lib->open_lgraph(prev_data.name);
        I(prev_node.get_class_lgraph() != prev_lg);
        dpin = prev_node.setup_driver_pin("o0");
        I(prev_node.get_type_op() == Ntype_op::Sub);
      }

      Node_pin spin;
      if (curr_node.get_type_op() == Ntype_op::Sum) {
        I(curr_data.leaf);
        if (rbool.any()) {
          spin = curr_node.setup_sink_pin("A");
        } else {
          spin = curr_node.setup_sink_pin("B");
        }
      } else {
        Lgraph *curr_lg = lib->open_lgraph(curr_data.name);
        I(curr_node.get_class_lgraph() != curr_lg);
        spin = curr_node.setup_sink_pin("i0");
        I(curr_node.get_type_op() == Ntype_op::Sub);
      }

      bool connect_inp = rbool.any();
      bool connect_out = rbool.any() || (i + 1) == node_order.size();  // Add the last one
      if (prev_node.get_class_lgraph() == curr_node.get_class_lgraph()) {
        dpin.connect_sink(spin);
      } else {
        connect_inp = true;
        connect_out = true;
      }
      if (connect_inp) {  // Connect local to input
        auto local_dpin = curr_node.get_class_lgraph()->get_graph_input("i0");
        local_dpin.connect_sink(spin);
      }

      if (connect_out) {  // Connect last to output
        auto next_spin = prev_node.get_class_lgraph()->get_graph_output("o0");
        dpin.connect_sink(next_spin);
      }
    }

    lib->each_sub([this, lib](Lg_type_id lgid, std::string_view name) {
      (void)lgid;
      Lgraph *lg = lib->open_lgraph(name);
      I(lg);
      if (lg->is_empty()) {
        return;
      }

      int  sz = 0;
      Node last_node;
      for (auto node : lg->fast()) {
        if (node.is_type_io()) {
          continue;
        }
        last_node = node;
        sz++;
      }
      if (sz == 0) {
        auto dpin = lg->get_graph_input("i0");

        for (auto e : dpin.get_node().out_edges()) {
          if (e.sink.is_graph_io()) {
            return;
          }
        }

        auto spin = lg->get_graph_output("o0");
        spin.connect_driver(dpin);
      } else if (sz == 1) {
        for (auto e : last_node.out_edges()) {
          if (e.sink.is_graph_io()) {
            return;
          }
        }

        auto     spin = lg->get_graph_output("o0");
        Node_pin dpin;
        if (last_node.get_type_op() == Ntype_op::Sum) {
          dpin = last_node.setup_driver_pin("Y");
        } else {
          I(last_node.get_class_lgraph() == lg);
          dpin = last_node.setup_driver_pin("o0");
          I(last_node.get_type_op() == Ntype_op::Sub);
        }
        spin.connect_driver(dpin);
      }
    });

    for (const auto &node : node_order) {
      if (node.is_type_sub()) {
        Lgraph *sub_lg = lib->open_lgraph(node.get_type_sub());
        I(sub_lg);
        auto inp_pin = sub_lg->get_graph_input("i0");
        I(inp_pin.has_outputs());
        auto out_pin = sub_lg->get_graph_output("o0");
        I(out_pin.has_inputs());
      }
    }
  }

  void populate_tree(const int max_depth, const int size, const double leaf_ratio_goal, const bool unique) {
    tree.clear();

    Node_data root_data;
    root_data.create_pos = 0;

    tree.set_root(root_data);

    I(max_depth > 1);

    Lrand<int>  rint;
    Lrand<bool> rbool;

    int max_level = 1;
    int n_leafs   = 0;
    for (int i = 0; i < size; ++i) {
      int level = 0;
      level     = rint.max(max_level);
      I(level < max_depth);

      lh::Tree_index index(level, rint.max(tree.get_tree_width(level)));

      Node_data data;
      data.create_pos = i + 1;
      data.fwd_pos    = -1;
      data.bwd_pos    = -1;

      double leaf_ratio = (double)n_leafs / (1.0 + i);

      // std::print("leaf_ratio:{} {} {}\n", leaf_ratio,n_leafs, i);

      if (leaf_ratio < leaf_ratio_goal && index.level) {  // Not to root
        tree.append_sibling(index, data);
        n_leafs++;
      } else {
        // index.pos = tree.get_tree_width(index.level)-1; // Add child at the end
        if (!tree.is_leaf(index)) {
          n_leafs++;
        }

        tree.add_child(index, data);
        if ((index.level + 1) == max_level && max_level < max_depth) {
          max_level++;
        }
        I(max_level <= max_depth);
      }
    }

    int pos = 0;
    n_leafs = 0;
    for (auto index : tree.depth_preorder()) {
      auto *data    = tree.ref_data(index);
      data->fwd_pos = pos;
      data->bwd_pos = size - pos;
      data->leaf    = tree.is_leaf(index);
      if (data->leaf) {
        std::string name("leaf_l" + std::to_string(index.level) + "p" + std::to_string(index.pos));
        data->name = name;
        n_leafs++;
      } else {
        std::string name("node_l" + std::to_string(index.level) + "p" + std::to_string(index.pos));
        data->name = name;
      }
      ++pos;
    }

    if (!unique) {
      for (int i = 0; i < size / 32; i++) {
        lh::Tree_index insert_point(rint.max(max_level), rint.max(tree.get_tree_width(max_level)));
        lh::Tree_index copy_point(rint.max(max_level), rint.max(tree.get_tree_width(max_level)));

        if (tree.is_child_of(copy_point, insert_point)) {  // No recursion insert
          continue;
        }

#if 0
        HERE! Create a "copy" and "move" in the tree.hpp
        for (auto index : tree.breadth_first()) {
          HERE!
        }
#endif
      }
    }

    std::print("Tree with {} nodes {} leafs and {} depth\n", size, n_leafs, max_level);

    EXPECT_TRUE(pos == (size + 1));  // Missing nodes??? (tree.hpp bug)
  }
};
