// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "abc_lnet.hpp"

#include <format>
#include <string>
#include <vector>

// clang-format off
extern "C" {
#include "base/abc/abc.h"
#include "aig/hop/hop.h"
#include "misc/extra/extra.h"
#include "misc/mem/mem.h"
}
// clang-format on

namespace livehd::abc {
namespace {
using synth::Lid;
using synth::Lnet;

void named(Abc_Obj_t* obj, const std::string& name) { Abc_ObjAssignName(obj, const_cast<char*>(name.c_str()), nullptr); }

// The Hop function of LUT `id` (Hop variable i = fanin i).
Hop_Obj_t* lut_function(Hop_Man_t* man, const Lnet& net, Lid id) {
  const auto k  = net.fanin_count(id);
  const auto fn = k <= 6 ? net.fn(id) : 0;
  if (k == 1 && fn == Lnet::kNot) {
    return Hop_Not(Hop_IthVar(man, 0));
  }
  if (k == 2 && fn == Lnet::kAnd2) {
    return Hop_CreateAnd(man, 2);
  }
  if (k == 2 && fn == Lnet::kOr2) {
    return Hop_CreateOr(man, 2);
  }
  if (k == 2 && fn == Lnet::kXor2) {
    return Hop_CreateExor(man, 2);
  }
  // Shannon expansion on the highest variable over the rows [first, first + 2^vars).
  const auto expand = [&](auto& self, uint32_t first, uint32_t vars) -> Hop_Obj_t* {
    bool any = false, all = true;
    for (uint32_t x = first; x < first + (1U << vars); ++x) {
      const bool v = net.eval(id, x);
      any          = any || v;
      all          = all && v;
    }
    if (!any) {
      return Hop_Not(Hop_ManConst1(man));
    }
    if (all) {
      return Hop_ManConst1(man);
    }
    auto* f0 = self(self, first, vars - 1);
    auto* f1 = self(self, first + (1U << (vars - 1)), vars - 1);
    return Hop_Mux(man, Hop_IthVar(man, static_cast<int>(vars - 1)), f1, f0);
  };
  return expand(expand, 0, k);
}
}  // namespace

void* lnet_to_abc(const Lnet& net, std::string_view name, Lnet_names names) {
  auto* ntk      = Abc_NtkAlloc(ABC_NTK_NETLIST, ABC_FUNC_AIG, 1);
  ntk->pName     = Extra_UtilStrsav(const_cast<char*>(std::string(name).c_str()));
  auto* man      = static_cast<Hop_Man_t*>(ntk->pManFunc);
  auto  nets     = std::vector<Abc_Obj_t*>(net.size(), nullptr);
  auto  bis      = std::vector<Abc_Obj_t*>(net.latches().size(), nullptr);
  auto  make_net = [&](Abc_Obj_t* node) {
    auto* out = Abc_NtkCreateNet(ntk);
    Abc_ObjAddFanin(out, node);
    return out;
  };
  const auto constant = [&](bool one) {
    auto* node  = Abc_NtkCreateNode(ntk);
    node->pData = one ? Hop_ManConst1(man) : Hop_Not(Hop_ManConst1(man));
    return make_net(node);
  };
  // Outputs are replayed where they were recorded: before the node created
  // right after them.
  const auto& outputs     = net.outputs();
  size_t      next_output = 0;
  const auto  emit_outputs = [&](Lid upto) {
    for (; next_output < outputs.size() && outputs[next_output].created <= upto; ++next_output) {
      const auto& o = outputs[next_output];
      auto*       po = Abc_NtkCreatePo(ntk);
      if (names == Lnet_names::region) {
        auto* alias = Abc_NtkCreateNet(ntk);
        named(alias, o.name);
        Abc_ObjAddFanin(alias, Abc_ObjFanin0(nets[o.node]));
        Abc_ObjAddFanin(po, alias);
      } else {
        Abc_ObjAddFanin(po, nets[o.node]);
        named(po, o.name);
      }
    }
  };
  // Node 0 (the Lnet's own constant 0) exists only when something reads it; a
  // RAW network keeps its lazily created constants instead.
  if (net.fanout_count(Lnet::kConst0) > 0) {
    nets[Lnet::kConst0] = constant(false);
  }
  for (Lid id = 1; id < net.size(); ++id) {
    emit_outputs(id);
    switch (net.kind(id)) {
      case Lnet::Kind::constant: nets[id] = constant(net.fn(id) != 0); break;
      case Lnet::Kind::lut: {
        auto* node  = Abc_NtkCreateNode(ntk);
        node->pData = lut_function(man, net, id);
        for (const auto f : net.fanins(id)) {
          Abc_ObjAddFanin(node, nets[f]);
        }
        nets[id] = make_net(node);
        break;
      }
      case Lnet::Kind::source: {
        if (net.is_latch_source(id)) {
          const auto  k     = net.source_index(id);
          const auto& l     = net.latch(k);
          auto*       bo    = Abc_NtkCreateBo(ntk);
          auto*       latch = Abc_NtkCreateLatch(ntk);
          auto*       bi    = Abc_NtkCreateBi(ntk);
          Abc_ObjAddFanin(bo, latch);
          Abc_ObjAddFanin(latch, bi);
          if (l.init == '0') {
            Abc_LatchSetInit0(latch);
          } else if (l.init == '1') {
            Abc_LatchSetInit1(latch);
          } else {
            Abc_LatchSetInitDc(latch);
          }
          nets[id] = make_net(bo);
          named(nets[id], l.name);
          bis[k] = bi;
          break;
        }
        auto* pi  = Abc_NtkCreatePi(ntk);
        auto* out = Abc_NtkCreateNet(ntk);
        if (names == Lnet_names::proof) {
          named(out, std::format("i{}", Abc_ObjId(pi)));
        } else if (const auto& in = net.inputs()[net.source_index(id)]; !in.name.empty()) {
          named(out, in.name);
        }
        Abc_ObjAddFanin(out, pi);
        nets[id] = out;
        break;
      }
    }
  }
  emit_outputs(static_cast<Lid>(net.size()));
  for (size_t k = 0; k < net.latches().size(); ++k) {
    Abc_ObjAddFanin(bis[k], nets[net.latch(static_cast<uint32_t>(k)).d]);
  }
  return ntk;
}

namespace {
// The SOP text of a LUT without SOP side data: its on-set minterms, or a
// constant 0 over the fanins.
std::string minterm_sop(const Lnet& net, Lid id) {
  const auto  k = net.fanin_count(id);
  std::string sop;
  for (uint32_t x = 0; x < (1U << k); ++x) {
    if (!net.eval(id, x)) {
      continue;
    }
    for (uint32_t j = 0; j < k; ++j) {
      sop += ((x >> j) & 1) ? '1' : '0';
    }
    sop += " 1\n";
  }
  return sop.empty() ? std::string(k, '-') + " 0\n" : sop;
}
std::string sop_text(const Lnet::Sop& sop, uint32_t k) {
  std::string text;
  for (const auto& cube : sop.cubes) {
    for (uint32_t j = 0; j < k; ++j) {
      text += ((cube.care >> j) & 1) ? (((cube.ones >> j) & 1) ? '1' : '0') : '-';
    }
    text += sop.complemented ? " 0\n" : " 1\n";
  }
  return text;
}
}  // namespace

void* lnet_into_logic(const Lnet& net, void* opaque_skeleton) {
  auto* skeleton = static_cast<Abc_Ntk_t*>(opaque_skeleton);
  const auto sources = net.inputs().size() + net.latches().size();
  const auto cos     = synth::combinational_outputs(net);
  if (static_cast<size_t>(Abc_NtkCiNum(skeleton)) != sources || static_cast<size_t>(Abc_NtkCoNum(skeleton)) != cos.size()
      || static_cast<size_t>(Abc_NtkLatchNum(skeleton)) != net.latches().size()) {
    return nullptr;
  }
  auto*                   ntk     = Abc_NtkStartFrom(skeleton, ABC_NTK_LOGIC, ABC_FUNC_SOP);
  auto*                   manager = static_cast<Mem_Flex_t*>(ntk->pManFunc);
  std::vector<Abc_Obj_t*> objs(net.size(), nullptr);
  for (Lid id = 0; id < net.size(); ++id) {
    switch (net.kind(id)) {
      case Lnet::Kind::constant:
        if (net.fanout_count(id) > 0) {
          objs[id] = net.fn(id) != 0 ? Abc_NtkCreateNodeConst1(ntk) : Abc_NtkCreateNodeConst0(ntk);
        }
        break;
      case Lnet::Kind::source: {
        const auto ci = net.is_latch_source(id) ? net.inputs().size() + net.source_index(id) : net.source_index(id);
        objs[id]      = Abc_NtkCi(ntk, static_cast<int>(ci));
        break;
      }
      case Lnet::Kind::lut: {
        const auto k = net.fanin_count(id);
        if (k == 1 && net.fn(id) == Lnet::kVar0) {
          objs[id] = objs[net.fanin(id, 0)];
          break;
        }
        if (k == 1 && net.fn(id) == Lnet::kNot && net.sop(id) == nullptr) {
          objs[id] = Abc_NtkCreateNodeInv(ntk, objs[net.fanin(id, 0)]);
          break;
        }
        auto* node = Abc_NtkCreateNode(ntk);
        for (const auto f : net.fanins(id)) {
          Abc_ObjAddFanin(node, objs[f]);
        }
        const auto text = net.sop(id) != nullptr ? sop_text(*net.sop(id), k) : minterm_sop(net, id);
        node->pData     = Abc_SopRegister(manager, text.c_str());
        objs[id]        = node;
        break;
      }
    }
  }
  for (size_t j = 0; j < cos.size(); ++j) {
    Abc_ObjAddFanin(Abc_NtkCo(ntk, static_cast<int>(j)), objs[cos[j]]);
  }
  if (!Abc_NtkCheck(ntk)) {
    Abc_NtkDelete(ntk);
    return nullptr;
  }
  return ntk;
}

}  // namespace livehd::abc
