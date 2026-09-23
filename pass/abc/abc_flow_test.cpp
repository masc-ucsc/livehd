// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "abc_flow.hpp"

#include "gtest/gtest.h"

// clang-format off
extern "C" {
#include "base/abc/abc.h"
#include "base/main/main.h"
#include "base/cmd/cmd.h"
}
// clang-format on

namespace livehd::abc {
namespace {
class AbcFlow : public testing::Test {
protected:
  Abc_Frame_t* frame    = nullptr;
  Abc_Frame_t* previous = nullptr;
  Flow_plan    plan;

  void SetUp() override {
    frame = Abc_FrameCreate();
    ASSERT_NE(frame, nullptr);
    previous = Abc_FrameEnter(frame);
    ASSERT_EQ(Cmd_CommandExecute(frame, "read_lib -s inou/prp/tests/abc/timing.lib"), 0);
    auto* network        = Abc_NtkAlloc(ABC_NTK_LOGIC, ABC_FUNC_SOP, 1);
    char  network_name[] = "flow_test";
    network->pName       = Abc_UtilStrsav(network_name);
    auto* a              = Abc_NtkCreatePi(network);
    auto* b              = Abc_NtkCreatePi(network);
    auto* gate           = Abc_NtkCreateNode(network);
    Abc_ObjAssignName(a, const_cast<char*>("a"), nullptr);
    Abc_ObjAssignName(b, const_cast<char*>("b"), nullptr);
    Abc_ObjAddFanin(gate, a);
    Abc_ObjAddFanin(gate, b);
    gate->pData  = Abc_SopRegister(static_cast<Mem_Flex_t*>(network->pManFunc), "11 1\n");
    auto* output = Abc_NtkCreatePo(network);
    Abc_ObjAddFanin(output, gate);
    Abc_ObjAssignName(output, const_cast<char*>("y"), nullptr);
    Abc_FrameReplaceCurrentNetwork(frame, network);
    plan.flow           = "strash; &get -n; &nf; &put -o";
    plan.area_flow      = plan.flow;
    plan.size_to_budget = "upsize -D 10000; dnsize -D 10000";
    plan.map_step       = "&nf";
    plan.remap_post     = "; &put -o";
    plan.budget         = 10000;
  }

  void TearDown() override {
    if (frame) {
      Abc_FrameLeave(previous);
      Abc_FrameDestroy(frame);
    }
  }
};

TEST_F(AbcFlow, EntryRefusalLeavesOriginalNetwork) {
  auto*      original = Abc_FrameReadNtk(frame);
  const auto result   = execute_flow(frame, plan, [](auto) { return false; });
  EXPECT_EQ(result.status, Flow_status::refused);
  EXPECT_EQ(result.stage, "entry");
  EXPECT_EQ(result.commands_completed, 0);
  EXPECT_EQ(Abc_FrameReadNtk(frame), original);
}

TEST_F(AbcFlow, PostMappingRefusalStopsBeforeTimingAndAreaCandidate) {
  plan.ladder = plan.area_candidate = true;
  unsigned   mapping_checks         = 0;
  const auto result = execute_flow(frame, plan, [&](auto stage) { return stage != "mapping" || ++mapping_checks < 2; });
  EXPECT_EQ(result.status, Flow_status::refused);
  EXPECT_EQ(result.stage, "mapping");
  EXPECT_EQ(result.commands_completed, 1);
  EXPECT_FALSE(result.delay_qor);
  EXPECT_FALSE(result.area_qor);
  EXPECT_TRUE(Abc_NtkIsMappedLogic(Abc_FrameReadNtk(frame)));
}

TEST_F(AbcFlow, CommandFailureIsDistinctFromResourceRefusal) {
  plan.flow         = "livehd_deliberately_invalid_command";
  const auto result = execute_flow(frame, plan);
  EXPECT_EQ(result.status, Flow_status::failed);
  EXPECT_EQ(result.stage, "mapping");
  EXPECT_EQ(result.command, plan.flow);
  EXPECT_EQ(result.commands_completed, 0);
}

TEST_F(AbcFlow, AreaCandidateTieKeepsDelayMapping) {
  plan.ladder = plan.area_candidate = true;
  const auto result                 = execute_flow(frame, plan);
  ASSERT_EQ(result.status, Flow_status::completed);
  ASSERT_TRUE(result.delay_qor);
  ASSERT_TRUE(result.area_qor);
  EXPECT_GT(result.delay_qor->first, 0);
  EXPECT_GT(result.delay_qor->second, 0);
  EXPECT_EQ(result.delay_qor, result.area_qor);
  EXPECT_EQ(result.candidate, "delay");
  EXPECT_EQ(result.commands_completed, 2);
  EXPECT_EQ(physical_flow_qor(Abc_FrameReadNtk(frame)), result.delay_qor);
}

TEST_F(AbcFlow, AreaRefusalPreservesCompletedDelayMapping) {
  plan.ladder = plan.area_candidate = true;
  const auto result                 = execute_flow(frame, plan, [](auto stage) { return stage != "area-candidate"; });
  EXPECT_EQ(result.status, Flow_status::refused);
  EXPECT_EQ(result.stage, "area-candidate");
  EXPECT_EQ(result.commands_completed, 1);
  ASSERT_TRUE(result.delay_qor);
  EXPECT_FALSE(result.area_qor);
  EXPECT_EQ(physical_flow_qor(Abc_FrameReadNtk(frame)), result.delay_qor);
}

TEST_F(AbcFlow, RecoveryRetainsLiveGiaUndoState) {
  // A tiny &nf can map in place; production's preceding &st establishes
  // the saved GIA that recovery must retain across command invocations.
  plan.flow   = "strash; &get -n; &st; &nf; &put -o";
  plan.ladder = plan.remappable = true;
  plan.area_relax_pct           = 200;
  const auto result             = execute_flow(frame, plan);
  ASSERT_EQ(result.status, Flow_status::completed);
  EXPECT_EQ(result.commands_completed, 2);
  ASSERT_TRUE(result.delay_qor);
  EXPECT_GT(result.delay_qor->first, 0);
  EXPECT_LE(result.delay_qor->first, plan.budget);
  EXPECT_EQ(physical_flow_qor(Abc_FrameReadNtk(frame)), result.delay_qor);
}
}  // namespace
}  // namespace livehd::abc
