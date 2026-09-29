// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "abc_flow.hpp"

#include <algorithm>
#include <format>
#include <memory>

// clang-format off
extern "C" {
#include "base/abc/abc.h"
#include "base/main/main.h"
#include "base/cmd/cmd.h"
#include "map/scl/sclLib.h"
#include "map/scl/sclSize.h"
}
// clang-format on

namespace livehd::abc {
namespace {
struct Delete_network {
  void operator()(Abc_Ntk_t* network) const {
    if (network) {
      Abc_NtkDelete(network);
    }
  }
};
using Network = std::unique_ptr<Abc_Ntk_t, Delete_network>;
}  // namespace

// `read_lib` retains scalar timing arcs too, but a physical GENLIB needs actual
// slew/load surfaces. Its vTempls vector is not a capability flag: the
// Liberty reader consumes templates while constructing the per-pin surfaces,
// so a valid NLDM library such as ASAP7 can leave it empty. Inspect the parsed
// inverter timing surface instead.
//
// This is the ONE library-capability predicate for every SCL command the pass
// drives (the `buffer`/`dnsize` tail and the `stime`-shaped QoR
// timer). It is deliberately STRICTER than ABC's own `Abc_SclHasDelayInfo`,
// which is satisfied by a scalar-only arc: the SCL timer walks 2-D surfaces,
// so a scalar Liberty that happens to declare `lu_table_template` must NOT be
// accepted (the old vTempls proxy accepted exactly that, and rejected ASAP7).
// `Abc_SclFindInvertor` cannot be trusted to return NULL when the library has
// no inverter -- its `Vec_PtrForEachEntry` loop leaves the LAST cell class
// assigned when nothing matches -- so re-check that the cell really is a
// one-input inverter before indexing its timing arcs.
bool lib_has_nldm_timing(const void* opaque) {
  const auto* lib = static_cast<const SC_Lib*>(opaque);
  if (lib == nullptr) {
    return false;
  }
  auto* inv = Abc_SclFindInvertor(const_cast<SC_Lib*>(lib), 0);
  if (inv == nullptr || inv->n_inputs != 1) {
    return false;
  }
  auto* timing = Scl_CellPinTime(inv, 0);
  return timing != nullptr && Vec_FltSize(&timing->pCellRise.vIndex0) > 1 && Vec_FltSize(&timing->pCellRise.vIndex1) > 1;
}

bool frame_has_nldm_timing() { return lib_has_nldm_timing(Abc_FrameReadLibScl()); }

int area_relax_percent(float target, float achieved, uint32_t cap) {
  if (cap == 0 || target <= 0.0f || achieved <= 0.0f || achieved > target) {
    return 0;
  }
  const double slack_pct = (static_cast<double>(target) / achieved - 1.0) * 100.0;
  // Below this floor a remap cannot repay its extra mapping pass.
  return slack_pct < 25.0 ? 0 : static_cast<int>(std::min(slack_pct, static_cast<double>(cap)));
}

Flow_qor physical_flow_qor(void* opaque) {
  auto* mapped = static_cast<Abc_Ntk_t*>(opaque);
  if (!mapped || !Abc_NtkIsMappedLogic(mapped)) {
    return {};
  }
  // SCL propagates in object-id order: match stime's topology/dangling gate.
  Network copy(mapped->nBarBufs2 > 0 ? Abc_NtkDupDfsNoBarBufs(mapped) : nullptr);
  auto*   timing_net = copy ? copy.get() : mapped;
  if (!Abc_SclCheckNtk(timing_net, 0)) {
    return {};
  }
  auto*      timing = Abc_SclManStart(static_cast<SC_Lib*>(Abc_FrameReadLibScl()), timing_net, 0, 1, 0.0f, 0);
  const auto result = std::pair{timing->MaxDelay0, static_cast<double>(timing->SumArea0)};
  Abc_SclManFree(timing);
  return result;
}

Flow_result execute_flow(void* opaque, const Flow_plan& plan, const std::function<bool(std::string_view)>& admission) {
  auto*       frame = static_cast<Abc_Frame_t*>(opaque);
  Flow_result result;
  auto        admit = [&](std::string_view stage) {
    if (admission && !admission(stage)) {
      result.status = Flow_status::refused;
      result.stage  = stage;
      return false;
    }
    return true;
  };
  auto execute = [&](std::string_view command, std::string_view stage) {
    if (!admit(stage)) {
      return false;
    }
    if (Cmd_CommandExecute(frame, std::string(command).c_str()) != 0) {
      result.status  = Flow_status::failed;
      result.stage   = stage;
      result.command = command;
      return false;
    }
    ++result.commands_completed;
    return admit(stage);
  };
  auto measure = [&](Flow_qor& qor) {
    if (!admit("timing")) {
      return false;
    }
    qor = physical_flow_qor(Abc_FrameReadNtk(frame));
    return admit("timing");
  };
  if (!frame || !Abc_FrameReadNtk(frame)) {
    result.status = Flow_status::failed;
    result.stage  = "missing-network";
    return result;
  }
  if (!admit("entry")) {
    return result;
  }
  Network original(plan.area_candidate ? Abc_NtkDup(Abc_FrameReadNtk(frame)) : nullptr);
  if (!execute(plan.flow, "mapping")) {
    return result;
  }
  if (!plan.ladder) {
    return result;
  }
  Flow_qor delay;
  if (!measure(delay)) {
    return result;
  }
  if (delay && delay->first > plan.budget) {
    if (!execute(plan.size_to_budget, "budget-sizing") || !measure(delay)) {
      return result;
    }
  }
  if (delay && delay->first > plan.budget) {
    if (!execute("upsize; dnsize", "conditional-sizing") || !measure(delay)) {
      return result;
    }
  } else if (delay && plan.remappable) {
    const int relax = area_relax_percent(plan.budget, delay->first, plan.area_relax_pct);
    if (relax > 0) {
      const auto command = std::format("&undo; {} -R {}{}", plan.map_step, relax, plan.remap_post);
      if (!execute(command, "area-recovery") || !measure(delay)) {
        return result;
      }
      // SCL slack and the mapper's depth model can disagree. Repair a miss.
      if (delay && delay->first > plan.budget) {
        if (!execute(plan.size_to_budget, "sizing-repair") || !measure(delay)) {
          return result;
        }
      }
    }
  }
  result.delay_qor = delay;
  if (original && delay && delay->first <= plan.budget) {
    if (!admit("area-candidate")) {
      return result;
    }
    Network delay_network(Abc_NtkDup(Abc_FrameReadNtk(frame)));
    Abc_FrameReplaceCurrentNetwork(frame, original.release());
    if (!execute(plan.area_flow, "area-candidate") || !measure(result.area_qor)) {
      return result;
    }
    if (result.area_qor && result.area_qor->first <= plan.budget && result.area_qor->second < delay->second) {
      result.candidate = "area";
    } else {
      Abc_FrameReplaceCurrentNetwork(frame, delay_network.release());
      result.candidate = "delay";
    }
  }
  if (!admit("complete")) {
    return result;
  }
  return result;
}
}  // namespace livehd::abc
