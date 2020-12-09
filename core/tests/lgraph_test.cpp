//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgraph.hpp"

#include <string>
#include <vector>

#include "eprp_utils.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lrand.hpp"

using testing::HasSubstr;

class Setup_lgraph : public ::testing::Test {
protected:
  std::map<std::string, int> name2pos;
  std::map<std::string, int> name2bits;
  std::set<int>              posinput;

  std::set<int> posused;

  Lrand_range<Port_ID> rand_pos;
  Lrand_range<Bits_t>  rand_bits;
  Lrand<size_t>        rand_size;
  Lrand<bool>          rbool;

  Setup_lgraph() : rand_pos(1, Port_invalid - 1), rand_bits(1, Bits_max) {}

  void randomly_delete_one_io(LGraph *lg) {
    if (posused.empty())
      return;

    auto it = name2pos.begin();
    std::advance(it, rand_size.max(posused.size()));

    auto name = it->first;
    auto pos  = it->second;

    Node_pin pin;
    if (posinput.count(pos)) {
      EXPECT_TRUE(lg->is_graph_input(name));
      pin = lg->get_graph_input(name);
    } else {
      EXPECT_TRUE(lg->is_graph_output(name));
      if (rbool.any())
        pin = lg->get_graph_output_driver_pin(name);
      else
        pin = lg->get_graph_output(name);
    }

    // FIXME: we should get this working both ways (hier and non_hier)
    pin.get_non_hierarchical().del();  // del pin and connected edges (no dest nodes)

    EXPECT_FALSE(lg->is_graph_input(name));
    EXPECT_FALSE(lg->is_graph_output(name));

    posused.erase(pos);
    posinput.erase(pos);  // may be there or not

    name2pos.erase(name);
    name2bits.erase(name);
  }

  void add_input(LGraph *lg, const std::string &name) {
    EXPECT_FALSE(lg->is_graph_output(name));
    EXPECT_FALSE(lg->is_graph_input(name));

    EXPECT_TRUE(name2pos.find(name) == name2pos.end());
    EXPECT_TRUE(name2bits.find(name) == name2bits.end());

    auto pos = rand_pos.any();
    while (posused.count(pos)) {
      pos = rand_pos.any();
    }
    name2pos[name] = pos;

    EXPECT_EQ(posused.count(pos), 0);
    EXPECT_EQ(posinput.count(pos), 0);

    posused.insert(pos);
    posinput.insert(pos);

    auto bits       = rand_bits.any();
    name2bits[name] = bits;

    lg->add_graph_input(name, pos, bits);

    EXPECT_EQ(posused.count(pos), 1);
    EXPECT_EQ(posinput.count(pos), 1);
    EXPECT_FALSE(name2pos.find(name) == name2pos.end());
    EXPECT_FALSE(name2bits.find(name) == name2bits.end());
    EXPECT_TRUE(lg->is_graph_input(name));
  }

  void add_output(LGraph *lg, const std::string &name) {
    EXPECT_FALSE(lg->is_graph_output(name));
    EXPECT_FALSE(lg->is_graph_input(name));

    EXPECT_TRUE(name2pos.find(name) == name2pos.end());
    EXPECT_TRUE(name2bits.find(name) == name2bits.end());

    auto pos = rand_pos.any();
    while (posused.count(pos)) {
      pos = rand_pos.any();
    }
    name2pos[name] = pos;

    EXPECT_EQ(posused.count(pos), 0);
    EXPECT_EQ(posinput.count(pos), 0);

    posused.insert(pos);

    auto bits       = rand_bits.any();
    name2bits[name] = bits;
    lg->add_graph_output(name, pos, bits);

    EXPECT_EQ(posused.count(pos), 1);
    EXPECT_FALSE(name2pos.find(name) == name2pos.end());
    EXPECT_FALSE(name2bits.find(name) == name2bits.end());
    EXPECT_TRUE(lg->is_graph_output(name));
  }

  void SetUp() override {}

  void TearDown() override {
    // Graph_library::sync_all();
  }

  void check_ios(LGraph *lg) {
    auto &sub_node = lg->get_self_sub_node();

    for (auto it : name2pos) {
      if (posinput.count(it.second)) {
        EXPECT_TRUE(lg->is_graph_input(it.first));

        auto        dpin    = lg->get_graph_input(it.first);
        const auto &io_pin1 = sub_node.get_io_pin_from_instance_pid(dpin.get_pid());
        const auto &io_pin2 = sub_node.get_graph_input_io_pin(it.first);

        EXPECT_EQ(io_pin1.name, io_pin2.name);
        EXPECT_EQ(io_pin1.dir, io_pin2.dir);
        EXPECT_EQ(io_pin1.graph_io_pos, io_pin2.graph_io_pos);
        EXPECT_EQ(io_pin1.graph_io_pos, it.second);
        EXPECT_EQ(dpin.get_bits(), name2bits[it.first]);
      } else {
        EXPECT_TRUE(lg->is_graph_output(it.first));

        auto        dpin    = lg->get_graph_output_driver_pin(it.first);
        const auto &io_pin1 = sub_node.get_io_pin_from_instance_pid(dpin.get_pid());
        const auto &io_pin2 = sub_node.get_graph_output_io_pin(it.first);

        EXPECT_EQ(io_pin1.name, io_pin2.name);
        EXPECT_EQ(io_pin1.dir, io_pin2.dir);
        EXPECT_EQ(io_pin1.graph_io_pos, io_pin2.graph_io_pos);
        EXPECT_EQ(io_pin1.graph_io_pos, it.second);
        EXPECT_EQ(dpin.get_bits(), name2bits[it.first]);
      }
    }

    lg->each_graph_input([&sub_node, this](Node_pin &dpin) {
      const auto &io = sub_node.get_io_pin_from_instance_pid(dpin.get_pid());
      EXPECT_EQ(posused.count(io.graph_io_pos), 1);
      EXPECT_EQ(posinput.count(io.graph_io_pos), 1);
    });

    lg->each_graph_output([&sub_node, this](Node_pin &dpin) {
      const auto &io = sub_node.get_io_pin_from_instance_pid(dpin.get_pid());
      EXPECT_EQ(posused.count(io.graph_io_pos), 1);
      EXPECT_EQ(posinput.count(io.graph_io_pos), 0);
    });
  }
};

TEST_F(Setup_lgraph, add_remove_inputs) {
  Lbench b("core.LGRAPH_add_remove_inputs");

  std::string_view lgdb = "lgdb_lgraph_test";

  Eprp_utils::clean_dir(lgdb);

  LGraph *lg1 = LGraph::create(lgdb, "lg1", "file1.xxx");

  check_ios(lg1);

  for (int i = 2; i < 40; ++i) {
    if (rbool.any()) {
      add_input(lg1, std::string("inp_") + std::to_string(i));
    } else {
      add_output(lg1, std::string("out_") + std::to_string(i));
    }
  }

  check_ios(lg1);

  for (int i = 2; i < 40; ++i) {
    randomly_delete_one_io(lg1);
    check_ios(lg1);
  }

  int conta = 0;
  for (auto node : lg1->fast()) {
    (void)node;
    ++conta;
  }
  EXPECT_EQ(conta, 0);  // everything should be deleted

  conta = 0;
  lg1->each_sorted_graph_io([&conta](Node_pin &pin, Port_ID pid) {
    (void)pin;
    (void)pid;
    ++conta;
  });
  EXPECT_EQ(conta, 0);  // everything should be deleted

  check_ios(lg1);

  for (int i = 2; i < 1000; ++i) {
    if (rbool.any()) {
      add_input(lg1, std::string("inp_") + std::to_string(i));
    } else if (rbool.any()) {
      add_output(lg1, std::string("out_") + std::to_string(i));
    } else {
      randomly_delete_one_io(lg1);
    }

    if (posused.size() > 100) {
      if (rbool.any()) {
        randomly_delete_one_io(lg1);
      }
    }
    check_ios(lg1);
  }

  conta = 0;
  lg1->each_sorted_graph_io([&conta](Node_pin &pin, Port_ID pid) {
    (void)pin;
    (void)pid;
    ++conta;
  });
  EXPECT_EQ(conta, posused.size());
}
