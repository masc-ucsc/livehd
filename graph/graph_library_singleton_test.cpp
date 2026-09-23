// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "graph_library_singleton.hpp"

#include <cstdlib>
#include <filesystem>
#include <stdexcept>

#include "gtest/gtest.h"

namespace {
using Registry = livehd::Hhds_graph_library;
class GraphLibrarySingleton : public ::testing::Test {
protected:
  std::string path;
  void        SetUp() override {
    path = (std::filesystem::temp_directory_path() / "livehd-library-XXXXXX").string();
    ASSERT_NE(mkdtemp(path.data()), nullptr);
  }
  void TearDown() override { std::filesystem::remove_all(path); }
};

TEST_F(GraphLibrarySingleton, RepeatedScopesReleaseBodiesAndLeaveDiskUntouched) {
  const auto                   count = Registry::registered_instances();
  std::weak_ptr<hhds::GraphIO> weak;
  for (unsigned i = 0; i < 32; ++i) {
    {
      Registry::Scoped_instance scope(path);
      auto&                     library = Registry::instance(path + "/./");
      auto                      io      = library.find_io("saved");
      if (i == 0) {
        ASSERT_FALSE(io);
        io = library.create_io("saved");
        io->add_input("a", 1);
        io->add_output("y", 2);
        auto graph = io->create_graph();
        graph->get_input_pin("a").connect_sink(graph->get_output_pin("y"));
        Registry::save(path);
      } else {
        ASSERT_TRUE(io);
        ASSERT_TRUE(io->get_graph());
      }
      weak = io;
      EXPECT_EQ(Registry::registered_instances(), count + 1);
    }
    EXPECT_TRUE(weak.expired());
    EXPECT_EQ(Registry::registered_instances(), count);
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(path) / "library.txt"));
  }
}

TEST_F(GraphLibrarySingleton, ExceptionReleasesScopeAndNestedClaimCannotReplaceIt) {
  const auto                   count = Registry::registered_instances();
  std::weak_ptr<hhds::GraphIO> weak;
  EXPECT_THROW(
      {
        Registry::Scoped_instance scope(path);
        auto&                     library = Registry::instance(path);
        weak                              = library.create_io("private");
        EXPECT_THROW(Registry::Scoped_instance duplicate(path + "/./"), std::logic_error);
        EXPECT_TRUE(library.find_io("private"));
        throw std::runtime_error("injected failure");
      },
      std::runtime_error);
  EXPECT_TRUE(weak.expired());
  EXPECT_EQ(Registry::registered_instances(), count);
}

TEST_F(GraphLibrarySingleton, PermanentRegistrationCannotBeScoped) {
  auto& library = Registry::instance(path);
  auto  io      = library.create_io("permanent");
  EXPECT_THROW(Registry::Scoped_instance scope(path), std::logic_error);
  EXPECT_EQ(Registry::instance(path).find_io("permanent"), io);
}
}  // namespace
