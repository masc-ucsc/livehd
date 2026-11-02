// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "abc_flow.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "gtest/gtest.h"

// clang-format off
extern "C" {
#include "base/abc/abc.h"
#include "base/main/main.h"
#include "base/cmd/cmd.h"
#include "map/scl/sclSize.h"
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

TEST_F(AbcFlow, LargeFanoutBufferingPreservesEveryOutputAndDegreeLimit) {
  // This exercises several heap levels, tied fanout priorities, and both
  // buffer/inverter realizations. Exhaust all inputs after buffering so an
  // inverted branch or lost output cannot pass as a mere timing improvement.
  auto* original = Abc_NtkDup(Abc_FrameReadNtk(frame));
  for (const bool buffers : {false, true}) {
    Abc_FrameReplaceCurrentNetwork(frame, Abc_NtkDup(original));
    auto* network = Abc_FrameReadNtk(frame);
    auto* gate = Abc_ObjFanin0(Abc_NtkPo(network, 0));
    for (int i = 0; i < 1024; ++i) {
      auto* output = Abc_NtkCreatePo(network);
      Abc_ObjAddFanin(output, gate);
      const auto name = std::string("fan_") + std::to_string(i);
      Abc_ObjAssignName(output, const_cast<char*>(name.c_str()), nullptr);
    }
    const int degree = buffers ? 4 : 16;
    const auto command = plan.flow + "; unbuffer -i; buffer " + (buffers ? "-b " : "") + "-N " + std::to_string(degree);
    ASSERT_EQ(Cmd_CommandExecute(frame, command.c_str()), 0);
    network = Abc_FrameReadNtk(frame);
    ASSERT_TRUE(Abc_NtkIsMappedLogic(network));
    EXPECT_EQ(Abc_NtkPoNum(network), 1025);
    EXPECT_GT(Abc_NtkNodeNum(network), 16);
    Abc_Obj_t* node = nullptr;
    int index = 0;
    Abc_NtkForEachNode(network, node, index) {
      EXPECT_LE(Abc_ObjFanoutNum(node), degree);
    }
    // ABC's inverter buffering records edge phases out of band. Materialize
    // those phases before invoking the ordinary AIG simulator.
    auto* concrete = Abc_SclBufferPhase(network, 0);
    ASSERT_NE(concrete, nullptr);
    Abc_FrameReplaceCurrentNetwork(frame, concrete);
    ASSERT_EQ(Cmd_CommandExecute(frame, "strash"), 0);
    network = Abc_FrameReadNtk(frame);
    for (int pattern = 0; pattern < 4; ++pattern) {
      int inputs[] = {pattern & 1, (pattern >> 1) & 1};
      auto* values = Abc_NtkVerifySimulatePattern(network, inputs);
      ASSERT_NE(values, nullptr);
      int mismatches = 0;
      for (int output = 0; output < Abc_NtkPoNum(network); ++output) {
        mismatches += values[output] != (inputs[0] & inputs[1]);
      }
      EXPECT_EQ(mismatches, 0) << "buffers=" << buffers << " pattern=" << pattern;
      std::free(values);
    }
  }
  Abc_NtkDelete(original);
}

TEST(AbcFanin, IndexedReplacementMatchesOrdinaryPortAndPolarityUpdates) {
  struct Graph {
    Abc_Ntk_t* network;
    Abc_Obj_t* old_driver;
    Abc_Obj_t* new_driver;
    std::vector<Abc_Obj_t*> readers;
    explicit Graph(bool managed) {
      network = Abc_NtkAlloc(ABC_NTK_LOGIC, ABC_FUNC_SOP, managed);
      auto* a = Abc_NtkCreatePi(network);
      auto* b = Abc_NtkCreatePi(network);
      const auto create = [&] {
        auto* node = Abc_NtkCreateNode(network);
        node->pData = Abc_SopRegister(static_cast<Mem_Flex_t*>(network->pManFunc), "11 1\n");
        return node;
      };
      old_driver = create();
      new_driver = create();
      for (auto* node : {old_driver, new_driver}) {
        Abc_ObjAddFanin(node, a);
        Abc_ObjAddFanin(node, b);
      }
      for (int i = 0; i < 6; ++i) {
        auto* node = create();
        Abc_ObjAddFanin(node, old_driver);
        Abc_ObjAddFanin(node, i == 2 ? old_driver : b);  // parallel edge
        readers.push_back(node);
        Abc_ObjAddFanin(Abc_NtkCreatePo(network), node);
      }
      auto* output = Abc_NtkCreatePo(network);
      Abc_ObjAddFanin(output, old_driver);
      readers.push_back(output);
    }
    ~Graph() { Abc_NtkDelete(network); }
  };
  for (bool managed : {false, true}) {
    for (bool inverted : {false, true}) {
      Graph ordinary(managed), indexed(managed);
      // First/middle/last slots, a CO, and both edges of one repeated input.
      for (int target : {0, 3, 2, 6, 5, 1, 4, 2}) {
        auto* object = indexed.readers[target];
        const int slot = Vec_IntFind(&indexed.old_driver->vFanouts, object->Id);
        ASSERT_GE(slot, 0);
        const int size = indexed.old_driver->vFanouts.nSize;
        const int last = Vec_IntEntry(&indexed.old_driver->vFanouts, size - 1);
        Abc_ObjPatchFanin(ordinary.readers[target], ordinary.old_driver, Abc_ObjNotCond(ordinary.new_driver, inverted));
        const int moved = Abc_ObjPatchFaninFast(object, indexed.old_driver, Abc_ObjNotCond(indexed.new_driver, inverted), slot);
        EXPECT_EQ(moved, slot + 1 < size ? last : -1);
        ASSERT_EQ(Abc_NtkObjNumMax(ordinary.network), Abc_NtkObjNumMax(indexed.network));
        for (int id = 0; id < Abc_NtkObjNumMax(ordinary.network); ++id) {
          auto* a = Abc_NtkObj(ordinary.network, id);
          auto* b = Abc_NtkObj(indexed.network, id);
          ASSERT_EQ(bool(a), bool(b));
          if (!a) {
            continue;
          }
          ASSERT_EQ(a->vFanins.nSize, b->vFanins.nSize);
          for (int pin = 0; pin < a->vFanins.nSize; ++pin) {
            EXPECT_EQ(Vec_IntEntry(&a->vFanins, pin), Vec_IntEntry(&b->vFanins, pin));
          }
          EXPECT_EQ(a->fCompl0, b->fCompl0);
          EXPECT_EQ(a->fCompl1, b->fCompl1);
          std::vector<int> actual, expected;
          for (int pin = 0; pin < a->vFanouts.nSize; ++pin) {
            expected.push_back(Vec_IntEntry(&a->vFanouts, pin));
          }
          for (int pin = 0; pin < b->vFanouts.nSize; ++pin) {
            actual.push_back(Vec_IntEntry(&b->vFanouts, pin));
          }
          std::sort(actual.begin(), actual.end());
          std::sort(expected.begin(), expected.end());
          EXPECT_EQ(actual, expected);
        }
      }
    }
  }
}

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

TEST_F(AbcFlow, MappedSizingNeverRunsBooleanMappingOrAreaCandidate) {
  ASSERT_EQ(execute_flow(frame, plan).status, Flow_status::completed);
  plan.flow.clear();
  plan.ladder = true;
  plan.budget = 0.001f;  // Deliberately unattainable: exercises both sizing rungs.
  std::vector<std::string> stages;
  const auto               result = execute_flow(frame, plan, [&](auto stage) {
    stages.emplace_back(stage);
    return true;
  });
  ASSERT_EQ(result.status, Flow_status::completed);
  EXPECT_EQ(result.commands_completed, 2);
  ASSERT_TRUE(result.delay_qor);
  EXPECT_TRUE(Abc_NtkIsMappedLogic(Abc_FrameReadNtk(frame)));
  EXPECT_EQ(std::count(stages.begin(), stages.end(), "mapping"), 0);
  EXPECT_EQ(std::count(stages.begin(), stages.end(), "area-candidate"), 0);
  EXPECT_EQ(std::count(stages.begin(), stages.end(), "budget-sizing"), 2);
  EXPECT_EQ(std::count(stages.begin(), stages.end(), "conditional-sizing"), 2);
}

TEST_F(AbcFlow, MappedSizingRefusesUnmappedInputAndResynthesisPlans) {
  const auto mapping = plan.flow;
  plan.flow.clear();
  EXPECT_EQ(execute_flow(frame, plan).status, Flow_status::failed);
  ASSERT_EQ(Cmd_CommandExecute(frame, mapping.c_str()), 0);
  plan.area_candidate = true;
  EXPECT_EQ(execute_flow(frame, plan).status, Flow_status::failed);
  plan.area_candidate = false;
  plan.remappable     = true;
  EXPECT_EQ(execute_flow(frame, plan).status, Flow_status::failed);
}

TEST_F(AbcFlow, MappedRemapRecoversAreaWithoutRunningABooleanFlow) {
  // The tmap branch runs exactly this mapping-only prefix itself.
  ASSERT_EQ(Cmd_CommandExecute(frame, "strash; &get -n; &st; &nf; &put -o"), 0);
  plan.flow.clear();
  plan.ladder = plan.remappable = plan.mapped_remap = true;
  plan.area_relax_pct                               = 200;
  std::vector<std::string> stages;
  const auto               result = execute_flow(frame, plan, [&](auto stage) {
    stages.emplace_back(stage);
    return true;
  });
  ASSERT_EQ(result.status, Flow_status::completed);
  EXPECT_EQ(std::count(stages.begin(), stages.end(), "mapping"), 0);
  EXPECT_EQ(std::count(stages.begin(), stages.end(), "area-recovery"), 2);
  ASSERT_TRUE(result.delay_qor);
  EXPECT_LE(result.delay_qor->first, plan.budget);
  EXPECT_TRUE(Abc_NtkIsMappedLogic(Abc_FrameReadNtk(frame)));
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

// Area recovery may spend the slack, never the budget: a remap that comes
// back without a mapping that meets it (here a LUT remap, which no cell timing
// can measure, standing in for one that overshoots past what sizing repairs)
// is discarded, and the mapping that met the budget is the result -- in the
// frame and reported, so the area candidate still gets its turn.
TEST_F(AbcFlow, AreaRecoveryThatMissesTheBudgetIsDiscarded) {
  plan.flow       = "strash; &get -n; &st; &nf; &put -o";
  plan.map_step   = "&if -K 2";
  plan.remap_post = "; &put";
  plan.ladder = plan.remappable = plan.area_candidate = true;
  plan.area_relax_pct                               = 200;
  auto* unmapped = Abc_NtkDup(Abc_FrameReadNtk(frame));
  ASSERT_EQ(Cmd_CommandExecute(frame, plan.flow.c_str()), 0);
  const auto met = physical_flow_qor(Abc_FrameReadNtk(frame));
  ASSERT_TRUE(met);
  Abc_FrameReplaceCurrentNetwork(frame, unmapped);  // the flow starts from the logic network again
  std::vector<std::string> stages;
  const auto               result = execute_flow(frame, plan, [&](auto stage) {
    stages.emplace_back(stage);
    return true;
  });
  ASSERT_EQ(result.status, Flow_status::completed);
  EXPECT_EQ(std::count(stages.begin(), stages.end(), "area-recovery"), 2);  // the remap ran
  EXPECT_GT(std::count(stages.begin(), stages.end(), "area-candidate"), 0);  // and so did the candidate
  ASSERT_TRUE(result.delay_qor);
  EXPECT_EQ(result.delay_qor, met);
  EXPECT_TRUE(Abc_NtkIsMappedLogic(Abc_FrameReadNtk(frame)));
  EXPECT_TRUE(physical_flow_qor(Abc_FrameReadNtk(frame)));
}
}  // namespace
}  // namespace livehd::abc

// --- the delay budget's OTHER half (area recovery) --------------------------
//
// `pass.abc` re-maps a region with `&nf -R <pct>` when the first mapping BEAT
// its delay target, because ABC's `&nf -D` is silently ignored by the mapper
// (giaNf.c reads `MapDelayTarget`, which `-D` never writes) and the flow would
// otherwise always chase minimum delay and pay area for it. The QoR that buys
// depends on the cell library; the DECISION does not, so it is pinned here.
TEST(AbcAreaRelax, MissedBudgetNeverRelaxes) {
  // At or over the target there is no slack to trade -- that is the `upsize`
  // path's job, and relaxing there would give away timing the design needs.
  EXPECT_EQ(livehd::abc::area_relax_percent(100.0f, 140.0f, 200), 0);
  EXPECT_EQ(livehd::abc::area_relax_percent(100.0f, 100.0f, 200), 0);
}

TEST(AbcAreaRelax, SmallSlackIsNotWorthASecondMapping) {
  EXPECT_EQ(livehd::abc::area_relax_percent(110.0f, 100.0f, 200), 0);   // 10%
  EXPECT_EQ(livehd::abc::area_relax_percent(124.0f, 100.0f, 200), 0);   // just under the floor
  EXPECT_EQ(livehd::abc::area_relax_percent(125.0f, 100.0f, 200), 25);  // at it
}

TEST(AbcAreaRelax, RelaxIsBoundedByBothSlackAndCap) {
  // A relaxed sky130 target (20 ns against ~1.2 ns mapped) has far more slack
  // than ABC will trade, so the cap decides; a tighter one is slack-bound.
  EXPECT_EQ(livehd::abc::area_relax_percent(20000.0f, 1200.0f, 200), 200);
  EXPECT_EQ(livehd::abc::area_relax_percent(200.0f, 100.0f, 200), 100);
  EXPECT_EQ(livehd::abc::area_relax_percent(200.0f, 100.0f, 50), 50);
}

TEST(AbcAreaRelax, DisabledAndDegenerateInputs) {
  EXPECT_EQ(livehd::abc::area_relax_percent(20000.0f, 1200.0f, 0), 0);  // area_relax=0
  EXPECT_EQ(livehd::abc::area_relax_percent(0.0f, 1200.0f, 200), 0);    // no target
  EXPECT_EQ(livehd::abc::area_relax_percent(20000.0f, 0.0f, 200), 0);   // untimed network
}
