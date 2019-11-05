//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <vector>
#include <string>

#include "rng.hpp"
#include "mmap_tree.hpp"

#include "attribute.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

using testing::HasSubstr;

class Tree_lgdb_setup : public ::testing::Test {
protected:
  struct Node_data {
    int create_pos;
    Node::Compact cnode;
    int fwd_pos;
    int bwd_pos;
    bool leaf;
    std::string name;
  };

  mmap_lib::tree<Node_data> tree;
  std::vector<Node> node_order;
  LGraph *lg_root;

  absl::flat_hash_map<Node::Compact, uint64_t> absl_fwd_pos;
  absl::flat_hash_map<Node::Compact, uint64_t> absl_bwd_pos;

  static constexpr char fwd_name[]     = "fwd_pos";
  static constexpr char bwd_name[]     = "bwd_pos";
  using Fwd_pos_attr  = Attribute<fwd_name,Node ,mmap_lib::map<Node::Compact, uint64_t> >;
  using Bwd_pos_attr  = Attribute<bwd_name,Node ,mmap_lib::map<Node::Compact, uint64_t> >;

  void map_tree_to_lgraph(int rseed) {
    std::vector<mmap_lib::Tree_index> index_order;

    tree.each_top_down_fast([&index_order](const mmap_lib::Tree_index &index, const Node_data &node) {
      fmt::print(" level:{} pos:{} create_pos:{} fwd:{} bwd:{} leaf:{}\n", index.level, index.pos, node.create_pos, node.fwd_pos, node.bwd_pos, node.leaf);

      if (index.level || index.pos)
        index_order.emplace_back(index);
    });

    lg_root = LGraph::create("lgdb_hierarchy_test", "node_l0p0", "hierarchy_test");
    lg_root->add_graph_output("o0",0,17);
    lg_root->add_graph_input("i0",1,31);

    absl_fwd_pos.clear();
    absl_bwd_pos.clear();
    Fwd_pos_attr::clear(lg_root);
    Bwd_pos_attr::clear(lg_root);

    node_order.clear();

    Rng rint(rseed);
    RandomBool rbool;

    for(const auto &index:index_order) {
      const auto &data = tree.get_data(index);

      I(index.level || index.pos); // skip root

      auto parent_index = tree.get_parent(index);
      const auto &parent_data = tree.get_data(parent_index);

      LGraph *parent_lg = LGraph::open("lgdb_hierarchy_test", parent_data.name);
      I(parent_lg);
      Node node;
      if (data.leaf && rbool(rint)) {
        node = parent_lg->create_node(Sum_Op,10);
      } else {
        node = parent_lg->create_node_sub(data.name);
        LGraph *sub_lg = LGraph::create("lgdb_hierarchy_test", data.name, "hierarchy_test");
        I(sub_lg);
        I(node.get_class_lgraph() == parent_lg);
        I(node.get_type_sub() == sub_lg->get_lgid());

        int n_inputs  = rint.uniform<int>(1)+1; // At least one
        int n_outputs = rint.uniform<int>(1)+1; // At least one
        int max_pos = 0;
        for(int i=0;i<n_inputs;++i) {
          std::string name = std::string("i") + std::to_string(i);
          int pos = max_pos+rint.uniform<int>(4)+1;
          max_pos = pos;
          sub_lg->add_graph_input(name, pos, rint.uniform<int>(60));
        }
        for(int i=0;i<n_outputs;++i) {
          std::string name = std::string("o") + std::to_string(i);
          int pos = max_pos+rint.uniform<int>(4)+1;
          max_pos = pos;
          sub_lg->add_graph_output(name, pos, rint.uniform<int>(60));
        }
      }
      tree.ref_data(index)->cnode = node.get_compact();

      node.set_name(data.name);

      absl_fwd_pos[node.get_compact()] = data.fwd_pos;
      absl_bwd_pos[node.get_compact()] = data.bwd_pos;
      Fwd_pos_attr::ref(lg_root)->set(node.get_compact(), data.fwd_pos);
      Bwd_pos_attr::ref(lg_root)->set(node.get_compact(), data.bwd_pos);

      //fmt::print("create {} class {}\n", hnode.debug_name(), hnode.get_class_lgraph()->get_name());
      node_order.emplace_back(node);
    }

    {
      auto dpin = lg_root->get_graph_input("i0");
      node_order[0].get_sink_pin(0).connect_driver(dpin);
    }

    for (size_t i=1;i<node_order.size();++i) {
      const auto &curr_index = index_order[i];
      const auto &prev_index = index_order[i-1];
      const auto &curr_data = tree.get_data(curr_index);
      const auto &prev_data = tree.get_data(prev_index);

      //const auto &parent_index = tree.get_parent(curr_index);
      //const auto &parent_data = tree.get_data(parent_index);
      //LGraph *parent_lg = LGraph::open("lgdb_hierarchy_test", parent_data.name);

      auto &curr_node = node_order[i];
      auto &prev_node = node_order[i-1];

  //    fmt::print("prev   {} class {}\n", curr_node.debug_name(), curr_node.get_class_lgraph()->get_name());
  //    fmt::print("curr   {} class {}\n", prev_node.debug_name(), prev_node.get_class_lgraph()->get_name());

      Node_pin dpin;
      if (prev_node.get_type().op == Sum_Op) {
        I(prev_data.leaf);
        dpin = prev_node.setup_driver_pin(0);
      }else{
        LGraph *prev_lg = LGraph::open("lgdb_hierarchy_test", prev_data.name);
        I(prev_node.get_class_lgraph() != prev_lg);
        auto d_pid = prev_node.get_class_lgraph()->get_self_sub_node().get_instance_pid("o0");
        dpin = prev_node.setup_driver_pin(d_pid);
        I(prev_node.get_type().op == SubGraph_Op);
      }

      Node_pin spin;
      if (curr_node.get_type().op == Sum_Op) {
        I(curr_data.leaf);
        spin = curr_node.setup_sink_pin(0);
      }else{
        LGraph *curr_lg = LGraph::open("lgdb_hierarchy_test", curr_data.name);
        I(curr_node.get_class_lgraph() != curr_lg);
        auto s_pid = curr_node.get_class_lgraph()->get_self_sub_node().get_instance_pid("i0");
        spin = curr_node.setup_sink_pin(s_pid);
        I(curr_node.get_type().op == SubGraph_Op);
      }

      bool connect_inp = rbool(rint);
      bool connect_out = rbool(rint) || (i+1) == node_order.size(); // Add the last one
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

    lg_root->get_library().each_lgraph([this](Lg_type_id lgid, std::string_view name) {
        LGraph *lg = LGraph::open(lg_root->get_path(), name);
        I(lg);
        if (lg->is_empty())
          return;

        int sz=0;
        Node last_node;
        for (auto node : lg->fast()) {
          if (node.is_type_io())
            continue;
          last_node = node;
          sz++;
        }
        if (sz == 0) {
          auto dpin = lg->get_graph_input("i0");

          for (auto e:dpin.get_node().out_edges()) {
            if (e.sink.is_graph_io())
               return;
          }

          auto spin = lg->get_graph_output("o0");
          spin.connect_driver(dpin);
        } else if (sz == 1) {
          for (auto e:last_node.out_edges()) {
            if (e.sink.is_graph_io())
               return;
          }

          auto spin = lg->get_graph_output("o0");
          Node_pin dpin;
          if (last_node.get_type().op == Sum_Op) {
            dpin = last_node.setup_driver_pin(0);
          } else {
            I(last_node.get_class_lgraph() == lg);
            auto d_pid = last_node.get_class_lgraph()->get_self_sub_node().get_instance_pid("o0");
            dpin       = last_node.setup_driver_pin(d_pid);
            I(last_node.get_type().op == SubGraph_Op);
          }
          spin.connect_driver(dpin);
        }
    });

    for (const auto node:node_order) {
      if (node.is_type_sub()) {
        LGraph *sub_lg = LGraph::open("lgdb_hierarchy_test", node.get_type_sub());
        I(sub_lg);
        auto inp_pin = sub_lg->get_graph_input("i0");
        I(inp_pin.has_outputs());
        auto out_pin = sub_lg->get_graph_output("o0");
        I(out_pin.has_inputs());
      }
    }
  }

  void populate_tree(int rseed, const int max_depth, const int size, const double leaf_ratio_goal, const bool unique) {

    tree.clear();

    Node_data root_data;
    root_data.create_pos = 0;

    tree.set_root(root_data);

    I(max_depth>1);

    Rng rint(rseed);
    RandomBool rbool;

    int max_level=1;
    int n_leafs = 0;
    for(int i=0;i<size;++i) {
      int level = 0;
      level = rint.uniform<int>(max_level);
      I(level<max_depth);

      mmap_lib::Tree_index index(level, rint.uniform<int>(tree.get_tree_width(level)));

      Node_data data;
      data.create_pos = i+1;
      data.fwd_pos = -1;
      data.bwd_pos = -1;

      double leaf_ratio = (double)n_leafs/(1.0 + i);

      //fmt::print("leaf_ratio:{} {} {}\n", leaf_ratio,n_leafs, i);

      if (leaf_ratio < leaf_ratio_goal && index.level) { // Not to root
        tree.add_next_sibling(index, data);
        n_leafs++;
      }else{
        //index.pos = tree.get_tree_width(index.level)-1; // Add child at the end
        if (!tree.is_leaf(index))
          n_leafs++;

        tree.add_child(index, data);
        if ((index.level+1) == max_level && max_level<max_depth)
          max_level++;
        I(max_level<=max_depth);
      }
    }

    int pos = 0;
    n_leafs = 0;
    for (auto index : tree.depth_preorder()) {
      auto *data = tree.ref_data(index);
      data->fwd_pos = pos;
      data->bwd_pos = size-pos;
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
      for(int i=0;i<size/32;i++) {
        mmap_lib::Tree_index insert_point(rint.uniform<int>(max_level), rint.uniform<int>(tree.get_tree_width(max_level)));
        mmap_lib::Tree_index copy_point(rint.uniform<int>(max_level), rint.uniform<int>(tree.get_tree_width(max_level)));

        if (tree.is_child_of(copy_point,insert_point)) // No recursion insert
          continue;

#if 0
        HERE! Create a "copy" and "move" in the tree.hpp
        for (auto index : tree.breadth_first()) {
          HERE!
        }
#endif
      }
    }

    fmt::print("Tree with {} nodes {} leafs and {} depth\n", size, n_leafs, max_level);

    EXPECT_TRUE(pos == (size+1)); // Missing nodes??? (tree.hpp bug)
  }

};

