// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "abc_timing.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

// clang-format off
extern "C" {
#include "base/abc/abc.h"
#include "base/main/main.h"
#include "map/mio/mio.h"
#include "map/scl/sclLib.h"
#include "map/scl/sclSize.h"
}
// clang-format on

namespace livehd::synth {
bool timing_better(const Timing_qor& candidate, const Timing_qor& incumbent, double budget_ps, bool prefer_tie,
                   double delay_tolerance) {
  const auto valid = [](const Timing_qor& q) {
    return q.valid && std::isfinite(q.area) && q.area >= 0 && std::isfinite(q.delay_ps) && q.delay_ps >= 0;
  };
  if (!valid(candidate) || !valid(incumbent) || !std::isfinite(budget_ps) || !std::isfinite(delay_tolerance)
      || delay_tolerance < 0) {
    return false;
  }
  // Area decides inside the tie band; exact delay, then `prefer_tie`, only on
  // an exact area tie. With a zero band this is the plain lexicographic order.
  const double band        = delay_tolerance * incumbent.delay_ps;
  const bool   delay_tied  = std::abs(candidate.delay_ps - incumbent.delay_ps) <= band;
  const auto   area_decide = [&] {
    return candidate.area < incumbent.area
           || (candidate.area == incumbent.area
               && (candidate.delay_ps < incumbent.delay_ps || (prefer_tie && candidate.delay_ps == incumbent.delay_ps)));
  };
  if (budget_ps > 0) {
    const bool meets = candidate.delay_ps <= budget_ps, old_meets = incumbent.delay_ps <= budget_ps;
    if (meets != old_meets) {
      return meets;
    }
    if (!meets && !delay_tied) {
      return candidate.delay_ps < incumbent.delay_ps;  // both miss: a real delay difference decides
    }
    if (!meets && candidate.delay_ps == incumbent.delay_ps) {
      return candidate.area < incumbent.area || (prefer_tie && candidate.area == incumbent.area);
    }
    return area_decide();
  }
  // No budget: Pareto, with delays inside the band counted as equal.
  const bool delay_ok = candidate.delay_ps <= incumbent.delay_ps || delay_tied;
  return candidate.area <= incumbent.area && delay_ok
         && (candidate.area < incumbent.area || candidate.delay_ps < incumbent.delay_ps || prefer_tie);
}

bool abc_has_timing() {
  auto* library = static_cast<SC_Lib*>(Abc_FrameReadLibScl());
  if (!library) {
    return false;
  }
  auto* inv = Abc_SclFindInvertor(library, 0);
  if (!inv || inv->n_inputs != 1) {
    return false;
  }
  auto* timing = Scl_CellPinTime(inv, 0);
  return timing && Vec_FltSize(&timing->pCellRise.vIndex0) > 1 && Vec_FltSize(&timing->pCellRise.vIndex1) > 1;
}

Network_environment abc_boundary_environment(void* opaque) {
  auto*               network  = static_cast<Abc_Ntk_t*>(opaque);
  auto*               boundary = static_cast<SC_Bnd*>(Abc_FrameReadBnd());
  Network_environment result;
  result.inputs.resize(Abc_NtkCiNum(network));
  result.outputs.resize(Abc_NtkCoNum(network));
  for (int i = 0; i < Abc_NtkCiNum(network); ++i) {
    auto& input = result.inputs[i];
    input.name  = Abc_ObjName(Abc_NtkCi(network, i));
    if (const auto* driving_cell = Abc_FrameReadDrivingCell()) {
      input.driving_cell = driving_cell;
    }
    if (!boundary || i >= Abc_NtkPiNum(network)) {
      continue;
    }
    if (boundary->vPiArr && i < Vec_FltSize(boundary->vPiArr)) {
      input.arrival_ps = std::max(0.0f, Vec_FltEntry(boundary->vPiArr, i));
    }
    if (boundary->vPiCell && i < Vec_PtrSize(boundary->vPiCell)) {
      auto* cell = static_cast<SC_Cell*>(Vec_PtrEntry(boundary->vPiCell, i));
      if (cell) {
        input.driving_cell = cell->pName;
      }
    }
  }
  for (int i = 0; i < Abc_NtkCoNum(network); ++i) {
    if (i < Abc_NtkPoNum(network)) {
      result.outputs[i].load_ff = Abc_FrameReadMaxLoad();
    }
    if (!boundary || i >= Abc_NtkPoNum(network)) {
      continue;
    }
    if (boundary->vPoLoad && i < Vec_FltSize(boundary->vPoLoad)) {
      const auto load = Vec_FltEntry(boundary->vPoLoad, i);
      if (load >= 0) {
        result.outputs[i].load_ff = load;
      }
    }
    if (boundary->vPoDelay && i < Vec_FltSize(boundary->vPoDelay)) {
      result.outputs[i].downstream_ps = std::max(0.0f, Vec_FltEntry(boundary->vPoDelay, i));
    }
  }
  return result;
}

Timing_qor abc_timing_qor(void* opaque) {
  auto* network = static_cast<Abc_Ntk_t*>(opaque);
  if (!abc_has_timing() || !Abc_NtkIsMappedLogic(network)) {
    return {};
  }
  std::unique_ptr<Abc_Ntk_t, decltype(&Abc_NtkDelete)> unbuffered(
      network->nBarBufs2 > 0 ? Abc_NtkDupDfsNoBarBufs(network) : nullptr,
      &Abc_NtkDelete);
  if (unbuffered) {
    network = unbuffered.get();
  }
  if (!Abc_SclCheckNtk(network, 0)) {
    return {};
  }
  auto*      man = Abc_SclManStart(static_cast<SC_Lib*>(Abc_FrameReadLibScl()), network, 0, 1, 0.0f, 0);
  Timing_qor result{true, static_cast<double>(man->SumArea0), static_cast<double>(man->MaxDelay0)};
  Abc_SclManFree(man);
  result.valid = std::isfinite(result.area) && result.area >= 0 && std::isfinite(result.delay_ps) && result.delay_ps >= 0;
  return result;
}

bool abc_fragment_timing(void* opaque, Mapped_fragment& fragment) {
  auto* network = static_cast<Abc_Ntk_t*>(opaque);
  if (!abc_has_timing() || !Abc_NtkIsMappedLogic(network) || Abc_NtkPoNum(network) != 1 || !Abc_SclCheckNtk(network, 0)) {
    return false;
  }
  auto* man = Abc_SclManStart(static_cast<SC_Lib*>(Abc_FrameReadLibScl()), network, 0, 1, 0.0f, 0);
  fragment.input_loads_ff.clear();
  for (int i = 0; i < Abc_NtkPiNum(network); ++i) {
    const auto* load = Abc_SclObjLoad(man, Abc_NtkPi(network, i));
    fragment.input_loads_ff.push_back(std::max(load->rise, load->fall));
  }
  auto* driver               = Abc_ObjFanin0(Abc_NtkPo(network, 0));
  fragment.output_arrival_ps = Abc_SclObjTimeMax(man, driver);
  if (Abc_ObjIsNode(driver)) {
    auto* gate                               = static_cast<Mio_Gate_t*>(driver->pData);
    fragment.output_environment.driving_cell = Mio_GateReadName(gate);
  } else {
    fragment.output_environment.arrival_ps = fragment.output_arrival_ps;
  }
  Abc_SclManFree(man);
  // SCL's driver model adds the load-dependent delay DELTA, not the entire
  // cell arc. Supply the zero-load output arrival to avoid counting that delta
  // twice. Slew/driver estimates guide local sizing; full stitched timing is
  // still authoritative and uses the actual pin arcs and total fanout.
  auto* boundary = static_cast<SC_Bnd*>(Abc_FrameReadBnd());
  if (boundary && boundary->vPoLoad && Vec_FltSize(boundary->vPoLoad)) {
    const float load = Vec_FltEntry(boundary->vPoLoad, 0);
    Vec_FltWriteEntry(boundary->vPoLoad, 0, 0);
    man                                    = Abc_SclManStart(static_cast<SC_Lib*>(Abc_FrameReadLibScl()), network, 0, 1, 0.0f, 0);
    fragment.output_environment.arrival_ps = Abc_SclObjTimeMax(man, driver);
    Abc_SclManFree(man);
    Vec_FltWriteEntry(boundary->vPoLoad, 0, load);
  }
  return std::isfinite(fragment.output_arrival_ps) && fragment.output_arrival_ps >= 0;
}
}  // namespace livehd::synth
