// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "abc_cells.hpp"

#include "diag.hpp"

// clang-format off
extern "C" {
#include "base/abc/abc.h"
#include "map/mio/mio.h"
}
// clang-format on

namespace livehd::abc {

void build_cell_library(void* mio_library, synth::Cell_library& lib, Gate_types& types) {
  auto* mio = static_cast<Mio_Library_t*>(mio_library);
  lib       = {};
  types.clear();
  if (mio == nullptr) {
    return;
  }
  Mio_Gate_t* g = nullptr;
  Mio_LibraryForEachGate(mio, g) {
    synth::Cell_type t;
    t.name   = Mio_GateReadName(g);
    t.output = Mio_GateReadOutName(g);
    for (auto* pin = Mio_GateReadPins(g); pin != nullptr; pin = Mio_PinReadNext(pin)) {
      t.inputs.emplace_back(Mio_PinReadName(pin));
    }
    t.area  = Mio_GateReadArea(g);
    // Mio truth tables are replicated 64-bit words (mioUtils.c); 0 past six pins.
    t.truth = Mio_GateReadPinNum(g) <= 6 ? static_cast<uint64_t>(Mio_GateReadTruth(g)) : 0;
    types.emplace(g, lib.add(std::move(t)));
  }
  if (auto* inv = Mio_LibraryReadInv(mio); inv != nullptr) {
    if (auto it = types.find(inv); it != types.end()) {
      lib.set_inverter(it->second);
    }
  }
}

std::optional<synth::Cell_netlist> abc_to_cells(void* netlist, const Gate_types& types, std::string_view region) {
  using Cells = synth::Cell_netlist;
  auto* ntk   = static_cast<Abc_Ntk_t*>(netlist);
  Cells out;
  // Driver object (PI, latch output BO, node) -> its signal. In a netlist every
  // net has exactly one driver, so a net resolves through its fanin.
  std::vector<uint32_t> signal_of(static_cast<size_t>(Abc_NtkObjNumMax(ntk)), Cells::kNone);
  const auto            add = [&](Abc_Obj_t* driver, Cells::Kind kind, uint32_t index) {
    const auto sig = static_cast<uint32_t>(out.signals.size());
    out.signals.push_back({kind, index});
    signal_of[static_cast<size_t>(Abc_ObjId(driver))] = sig;
    return sig;
  };
  const auto net_signal = [&](Abc_Obj_t* net) -> uint32_t {
    if (net == nullptr || Abc_ObjFaninNum(net) == 0) {
      return Cells::kNone;
    }
    const auto id = static_cast<size_t>(Abc_ObjId(Abc_ObjFanin0(net)));
    return id < signal_of.size() ? signal_of[id] : Cells::kNone;
  };
  Abc_Obj_t* obj = nullptr;
  int        i   = 0;
  Abc_NtkForEachPi(ntk, obj, i) { out.sources.push_back(add(obj, Cells::Kind::source, static_cast<uint32_t>(i))); }
  Abc_NtkForEachLatch(ntk, obj, i) {
    Cells::Latch l;
    l.q  = add(Abc_ObjFanout0(obj), Cells::Kind::latch, static_cast<uint32_t>(out.latches.size()));  // latch -> BO
    switch (Abc_LatchInit(obj)) {
      case ABC_INIT_ZERO: l.init = Cells::Init::zero; break;
      case ABC_INIT_ONE: l.init = Cells::Init::one; break;
      case ABC_INIT_DC: l.init = Cells::Init::dont_care; break;
      default: l.init = Cells::Init::none; break;
    }
    out.latches.push_back(l);
  }
  Abc_NtkForEachNode(ntk, obj, i) {
    auto* g = static_cast<Mio_Gate_t*>(obj->pData);
    auto  t = g == nullptr ? types.end() : types.find(g);
    if (t == types.end()) {
      // A mapped node without Mio data cannot be read back -- skipping it
      // would silently collapse its fanout cone to const0 (seen with
      // multi-output supergates before read_lib -s). Never miscompile.
      livehd::diag::err("pass.abc", "abc-readback", "internal")
          .msg("region '{}': mapped node {} carries no Mio gate — unreadable mapping (multi-output cell?)", region, Abc_ObjId(obj))
          .fatal();
      return std::nullopt;
    }
    Cells::Cell c;
    c.type   = t->second;
    c.output = add(obj, Cells::Kind::cell, static_cast<uint32_t>(out.cells.size()));
    out.cells.push_back(std::move(c));
  }
  // Fanins, outputs and latch inputs resolve once every driver has a signal.
  size_t c = 0;
  Abc_NtkForEachNode(ntk, obj, i) {
    Abc_Obj_t* fin = nullptr;
    int        k   = 0;
    Abc_ObjForEachFanin(obj, fin, k) { out.cells[c].fanins.push_back(net_signal(fin)); }
    ++c;
  }
  size_t l = 0;
  Abc_NtkForEachLatch(ntk, obj, i) {
    out.latches[l++].d = net_signal(Abc_ObjFanin0(Abc_ObjFanin0(obj)));  // latch <- BI <- D net
  }
  Abc_NtkForEachPo(ntk, obj, i) { out.outputs.push_back(net_signal(Abc_ObjFanin0(obj))); }
  return out;
}

}  // namespace livehd::abc
