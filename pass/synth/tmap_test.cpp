// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "tmap.hpp"

#include <limits>

#include "gtest/gtest.h"

namespace livehd::synth {
TEST(Tmap, AbsentProviderIsExplicitAndDoesNotNeedALibrary) {
  EXPECT_FALSE(has_tmap_provider("abc"));
  auto result = technology_map("abc", {}, {});
  EXPECT_EQ(result.status, Tmap_status::unavailable);
  EXPECT_FALSE(result.design);
  EXPECT_NE(result.reason.find("abc"), std::string::npos);
}

TEST(Tmap, RejectsDuplicateRegistrationAndInvalidRequestsBeforeCallingProvider) {
  auto calls = std::make_shared<unsigned>(0);
  ASSERT_TRUE(register_tmap_provider("validation", [calls](const auto&, const auto&) {
    ++*calls;
    return Tmap_result{Tmap_status::refused, {}, "test refusal"};
  }));
  EXPECT_FALSE(register_tmap_provider("validation", [](const auto&, const auto&) { return Tmap_result{}; }));
  EXPECT_FALSE(register_tmap_provider("empty", {}));
  hhds::GraphLibrary library;
  auto               top = library.create_io("top")->create_graph();
  Tmap_options       options;
  options.library = "cells.lib";
  EXPECT_EQ(technology_map("validation", {}, options).status, Tmap_status::invalid);
  options.delay_ps = std::numeric_limits<double>::quiet_NaN();
  EXPECT_EQ(technology_map("validation", top, options).status, Tmap_status::invalid);
  EXPECT_EQ(*calls, 0U);
  options.delay_ps = 0;
  const auto valid = options;
  for (auto rounds : {0, 65}) {
    options.boundary_rounds = rounds;
    EXPECT_EQ(technology_map("validation", top, options).status, Tmap_status::invalid);
  }
  options = valid;
  for (auto fanout : {1U, 4097U}) {
    options.sharing_fanout = fanout;
    EXPECT_EQ(technology_map("validation", top, options).status, Tmap_status::invalid);
  }
  options         = valid;
  options.io_load = std::numeric_limits<float>::infinity();
  EXPECT_EQ(technology_map("validation", top, options).status, Tmap_status::invalid);
  options = valid;
  for (auto margin : {"", "-1", "nan", "inf", "1ps"}) {
    options.reg_margin = margin;
    EXPECT_EQ(technology_map("validation", top, options).status, Tmap_status::invalid);
  }
  EXPECT_EQ(*calls, 0U);
  options = valid;
  EXPECT_EQ(technology_map("validation", top, options).status, Tmap_status::refused);
  EXPECT_EQ(*calls, 1U);
}

TEST(Tmap, NeverPublishesFailedOrUnownedProviderOutput) {
  hhds::GraphLibrary library;
  auto               top = library.create_io("top")->create_graph();
  ASSERT_TRUE(register_tmap_provider("unowned", [](const auto& input, const auto&) {
    auto result = std::make_unique<Mapped_design>();
    result->top = input;
    return Tmap_result{Tmap_status::mapped, std::move(result), {}};
  }));
  auto result = technology_map("unowned", top, {.library = "cells.lib"});
  EXPECT_EQ(result.status, Tmap_status::invalid);
  EXPECT_FALSE(result.design);
  ASSERT_TRUE(register_tmap_provider("partial", [](const auto&, const auto&) {
    auto partial = std::make_unique<Mapped_design>();
    partial->top = partial->library.create_io("top")->create_graph();
    return Tmap_result{Tmap_status::refused, std::move(partial), "late refusal"};
  }));
  result = technology_map("partial", top, {.library = "cells.lib"});
  EXPECT_EQ(result.status, Tmap_status::refused);
  EXPECT_FALSE(result.design);
}

TEST(Tmap, RejectsChangedPortIdentityOrWidth) {
  hhds::GraphLibrary library;
  auto               io = library.create_io("top");
  io->add_input("a", 1);
  io->set_bits("a", 1);
  auto top = io->create_graph();
  ASSERT_TRUE(register_tmap_provider("changed-port", [](const auto&, const auto&) {
    auto result = std::make_unique<Mapped_design>();
    auto output = result->library.create_io("top");
    output->add_input("a", 1);
    output->set_bits("a", 2);
    result->top = output->create_graph();
    return Tmap_result{Tmap_status::mapped, std::move(result), {}};
  }));
  auto result = technology_map("changed-port", top, {.library = "cells.lib"});
  EXPECT_EQ(result.status, Tmap_status::invalid);
  EXPECT_FALSE(result.design);
  EXPECT_NE(result.reason.find("port interface"), std::string::npos);
}
}  // namespace livehd::synth
