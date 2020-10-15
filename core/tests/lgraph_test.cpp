//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <vector>
#include <string>

#include "lbench.hpp"
#include "lrand.hpp"

#include "eprp_utils.hpp"
#include "lgraph.hpp"

using testing::HasSubstr;

class Setup_lgraph : public ::testing::Test {
protected:
  std::map<std::string, int> name2pos;
  std::map<std::string, int> name2bits;
  std::set<int> posinput;

  std::set<int> posused;

  Lrand_range<Port_ID> rand_pos;
  Lrand_range<Bits_t>  rand_bits;

  Setup_lgraph() : rand_pos(1, Port_invalid-1), rand_bits(1, Bits_max) {
  }

  void add_input(LGraph *lg, const std::string &name) {
    EXPECT_FALSE(lg->is_graph_output(name));
    EXPECT_FALSE(lg->is_graph_input(name));

    EXPECT_TRUE(name2pos.find(name) == name2pos.end());
    EXPECT_TRUE(name2bits.find(name) == name2bits.end());

    auto pos = rand_pos.any();
    while(posused.count(pos)) {
      pos = rand_pos.any();
    }
    name2pos[name] = pos;

    EXPECT_EQ(posused.count(pos),0);
    EXPECT_EQ(posinput.count(pos),0);

    posused.insert(pos);
    posinput.insert(pos);

    auto bits = rand_bits.any();
    name2bits[name] = bits;

    lg->add_graph_input(name, pos, bits);

    EXPECT_EQ(posused.count(pos),1);
    EXPECT_EQ(posinput.count(pos),1);
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
    while(posused.count(pos)) {
      pos = rand_pos.any();
    }
    name2pos[name] = pos;

    EXPECT_EQ(posused.count(pos),0);
    EXPECT_EQ(posinput.count(pos),0);

    posused.insert(pos);

    auto bits = rand_bits.any();
    name2bits[name] = bits;
    lg->add_graph_output(name, pos, bits);

    EXPECT_EQ(posused.count(pos),1);
    EXPECT_FALSE(name2pos.find(name) == name2pos.end());
    EXPECT_FALSE(name2bits.find(name) == name2bits.end());
    EXPECT_TRUE(lg->is_graph_output(name));
  }

  void SetUp() override {
  }

  void TearDown() override {
    //Graph_library::sync_all();
  }

  void check_ios(LGraph *lg) {

    auto &sub_node = lg->get_self_sub_node();

    for(auto it:name2pos) {
      if (posinput.count(it.second)) {
        EXPECT_TRUE(lg->is_graph_input(it.first));

        auto dpin = lg->get_graph_input(it.first);
        const auto &io_pin1 = sub_node.get_io_pin_from_instance_pid(dpin.get_pid());
        const auto &io_pin2 = sub_node.get_graph_input_io_pin(it.first);

        EXPECT_EQ(io_pin1.name, io_pin2.name);
        EXPECT_EQ(io_pin1.dir, io_pin2.dir);
        EXPECT_EQ(io_pin1.graph_io_pos, io_pin2.graph_io_pos);
        EXPECT_EQ(io_pin1.graph_io_pos, it.second);
        EXPECT_EQ(dpin.get_bits(), name2bits[it.first]);
      }else{
        EXPECT_TRUE(lg->is_graph_output(it.first));

        auto dpin = lg->get_graph_output_driver_pin(it.first);
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

  std::string_view lgdb = "lgdb_lgraph_test";

  Eprp_utils::clean_dir(lgdb);

  LGraph *lg1 = LGraph::create(lgdb, "lg1", "file1.xxx");

  check_ios(lg1);

  Lrand<bool> rbool;
  for(int i=2;i<40;++i) {
    if (rbool.any()) {
      add_input(lg1, std::string("inp_") + std::to_string(i));
    }else{
      add_output(lg1, std::string("out_") + std::to_string(i));
    }
  }

  check_ios(lg1);
}

