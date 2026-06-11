//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <unistd.h>

#include <filesystem>
#include <memory>
#include <string>

#include "eprp_var.hpp"
#include "gmock/gmock.h"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "pass_sample.hpp"

class SampleMainTest : public ::testing::Test {
protected:
  void SetUp() override {}
};

TEST_F(SampleMainTest, EmptyLgraph) {
  std::filesystem::remove_all("pass_test_lgdb");

  auto& lib    = livehd::Hhds_graph_library::instance("pass_test_lgdb");
  auto  top_io = lib.create_io("empty");
  auto  g      = top_io->create_graph();

  Eprp_var var;
  var.add(g);
  var.add("data", "hello");

  EXPECT_FALSE(static_cast<bool>(lib.find_graph("pass_sample")));

  Pass_sample::work(var);

  EXPECT_TRUE(static_cast<bool>(lib.find_graph("pass_sample")));
}
