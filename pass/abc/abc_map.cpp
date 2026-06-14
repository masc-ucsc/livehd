// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Region body <-> ABC translation for pass.abc (task 2a-abc). Each colored
// region (handed over by pass.partition's decomposition seam) is bit-blasted
// into an ABC AIG netlist, optimized + technology-mapped by ABC against a
// Liberty library, and read back as a netlist of 1-bit blackbox Sub cells named
// after the Liberty cells. The bit-blast boundary (multi-bit module IO <-> 1-bit
// ABC PI/PO) is handled with Get_mask bit-selects on inputs and a Set_mask
// concat on outputs, exactly the modern equivalent of the old Pick/Join path.

#include "abc_map.hpp"

#include <algorithm>
#include <functional>
#include <print>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "cell.hpp"
#include "diag.hpp"
#include "dlop.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/graph.hpp"
#include "node_util.hpp"

extern "C" {
#include "base/abc/abc.h"       // brings abc_global.h (word, macros, ABC_NAMESPACE_*)
#include "base/main/abcapis.h"  // Abc_Frame_t
#include "base/main/main.h"
#include "base/cmd/cmd.h"
#include "aig/hop/hop.h"
#include "map/mio/mio.h"
#include "misc/extra/extra.h"
}

namespace gu = livehd::graph_util;

namespace livehd::abc {

namespace {

// Built-in combinational flow (task default). {D}/{L} substituted from opts.
constexpr std::string_view kCombFlow = "strash; &get -n; &fraig -x; &put; &get -n; &dch -f; &nf {D}; &put";

std::string subst(std::string s, std::string_view tok, std::string_view val) {
  for (auto pos = s.find(tok); pos != std::string::npos; pos = s.find(tok, pos)) {
    s.replace(pos, tok.size(), val);
  }
  return s;
}

}  // namespace

std::string Mapper::comb_flow() const {
  std::string f = opts_.flow.empty() ? std::string{kCombFlow} : opts_.flow;
  f             = subst(std::move(f), "{D}", opts_.delay);
  f             = subst(std::move(f), "{L}", opts_.load);
  return f;
}

bool Mapper::start() {
  Abc_Start();
  pabc_ = Abc_FrameGetGlobalFrame();
  if (pabc_ == nullptr) {
    livehd::diag::err("pass.abc", "abc-frame", "internal").msg("could not initialize the ABC frame").fatal();
    return false;
  }
  auto* frame = static_cast<Abc_Frame_t*>(pabc_);
  auto  cmd   = std::string{"read_lib "} + opts_.library;
  if (Cmd_CommandExecute(frame, cmd.c_str()) != 0) {
    livehd::diag::err("pass.abc", "read-lib", "unsupported")
        .msg("ABC could not read the Liberty library '{}'", opts_.library)
        .fatal();
    return false;
  }
  lib_loaded_ = true;
  return true;
}

void Mapper::stop() {
  if (pabc_ != nullptr) {
    Abc_Stop();
    pabc_ = nullptr;
  }
}

void Mapper::map_region(const livehd::partition::Region_body& rb) {
  auto*       manNtk  = Abc_NtkAlloc(ABC_NTK_NETLIST, ABC_FUNC_AIG, 1);
  manNtk->pName       = Extra_UtilStrsav(const_cast<char*>(rb.module_name.c_str()));
  auto* manFunc       = static_cast<Hop_Man_t*>(manNtk->pManFunc);

  // bit i of an original driver pin -> the ABC net carrying it.
  absl::flat_hash_map<hhds::Pin_class, absl::flat_hash_map<int, Abc_Obj_t*>> bitnet;
  // Region node membership (handles into rb.src).
  absl::flat_hash_set<hhds::Node_class> region;
  for (const auto& n : rb.nodes) {
    region.insert(n);
  }

  // --- ABC gate constructors (each returns the new gate's output net) ---
  auto new_net = [&](Abc_Obj_t* node) {
    auto* net = Abc_NtkCreateNet(manNtk);
    Abc_ObjAddFanin(net, node);
    return net;
  };
  Abc_Obj_t* const1 = nullptr;
  auto       abc_const1 = [&]() {
    if (const1 == nullptr) {
      auto* node  = Abc_NtkCreateNode(manNtk);
      node->pData = Hop_ManConst1(manFunc);
      const1      = new_net(node);
    }
    return const1;
  };
  auto abc_not = [&](Abc_Obj_t* a) {
    auto* node  = Abc_NtkCreateNode(manNtk);
    node->pData = Hop_Not(Hop_IthVar(manFunc, 0));
    Abc_ObjAddFanin(node, a);
    return new_net(node);
  };
  auto abc_bin = [&](Abc_Obj_t* a, Abc_Obj_t* b, char kind) {
    auto* node = Abc_NtkCreateNode(manNtk);
    node->pData = kind == '&'   ? Hop_CreateAnd(manFunc, 2)
                  : kind == '|' ? Hop_CreateOr(manFunc, 2)
                                : Hop_CreateExor(manFunc, 2);
    Abc_ObjAddFanin(node, a);
    Abc_ObjAddFanin(node, b);
    return new_net(node);
  };
  auto abc_const_bit = [&](bool v) { return v ? abc_const1() : abc_not(abc_const1()); };

  // --- bit i of an original driver pin, with sign/zero extension past width ---
  std::function<Abc_Obj_t*(const hhds::Pin_class&, int)> abc_bit = [&](const hhds::Pin_class& drv, int i) -> Abc_Obj_t* {
    if (drv.is_invalid()) {
      return abc_const_bit(false);
    }
    int  w    = gu::bits_of(drv);
    bool sign = !gu::is_unsign(drv);
    int  eff  = i;
    if (w != 0 && i >= w) {
      eff = sign ? w - 1 : -1;  // -1 => constant 0 above an unsigned width
    }
    if (eff < 0) {
      return abc_const_bit(false);
    }
    auto& slots = bitnet[drv];
    if (auto it = slots.find(eff); it != slots.end()) {
      return it->second;
    }
    if (gu::is_const_pin(drv)) {
      auto val = gu::hydrate_const(drv);
      auto* net = abc_const_bit(val.bit_test(eff));
      slots[eff] = net;
      return net;
    }
    // A region-internal node not yet materialized (should not happen in topo
    // order) or an unexpected boundary: emit a constant 0 so the netlist stays
    // structurally valid; correctness is guarded by the unsupported-cell diag.
    auto* net  = abc_const_bit(false);
    slots[eff] = net;
    return net;
  };

  // --- region inputs -> per-bit ABC PIs (creation order == readback order) ---
  std::vector<std::pair<size_t, int>> pi_order;  // PI index -> (input port, bit)
  for (size_t pi = 0; pi < rb.inputs.size(); ++pi) {
    const auto& port = rb.inputs[pi];
    int         w    = port.bits == 0 ? 1 : port.bits;
    for (int b = 0; b < w; ++b) {
      auto* obj = Abc_NtkCreatePi(manNtk);
      auto* net = Abc_NtkCreateNet(manNtk);
      auto  nm  = std::format("{}_b{}", port.name, b);
      Abc_ObjAssignName(net, const_cast<char*>(nm.c_str()), nullptr);
      Abc_ObjAddFanin(net, obj);
      bitnet[port.src_driver][b] = net;
      pi_order.emplace_back(pi, b);
    }
  }

  // --- bit-blast each region node in topological order ---
  bool unsupported = false;
  for (auto n : rb.src->forward_class()) {
    if (!region.contains(n)) {
      continue;
    }
    auto op       = gu::type_op_of(n);
    auto out_pin  = n.create_driver_pin(0);
    int  out_bits = gu::bits_of(out_pin);
    if (out_bits == 0) {
      out_bits = 1;
    }
    auto& slots = bitnet[out_pin];

    if (op == Ntype_op::Not) {
      hhds::Pin_class a;
      for (const auto& e : n.inp_edges()) {
        a = e.driver;
      }
      for (int b = 0; b < out_bits; ++b) {
        slots[b] = abc_not(abc_bit(a, b));
      }
    } else if (op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Xor) {
      char kind = op == Ntype_op::And ? '&' : (op == Ntype_op::Or ? '|' : '^');
      std::vector<hhds::Pin_class> ins;
      for (const auto& e : n.inp_edges()) {
        ins.push_back(e.driver);
      }
      for (int b = 0; b < out_bits; ++b) {
        Abc_Obj_t* acc = nullptr;
        for (const auto& d : ins) {
          auto* bit = abc_bit(d, b);
          acc       = acc == nullptr ? bit : abc_bin(acc, bit, kind);
        }
        slots[b] = acc == nullptr ? abc_const_bit(false) : acc;
      }
    } else if (op == Ntype_op::Mux || op == Ntype_op::Hotmux) {
      hhds::Pin_class                       sel;
      absl::flat_hash_map<int, hhds::Pin_class> data;  // pid-1 (value) -> driver
      int                                   max_v = -1;
      for (const auto& e : n.inp_edges()) {
        auto pid = e.sink.get_port_id();
        if (pid == 0) {
          sel = e.driver;
        } else {
          data[static_cast<int>(pid) - 1] = e.driver;
          max_v                           = std::max(max_v, static_cast<int>(pid) - 1);
        }
      }
      int sel_bits = gu::bits_of(sel);
      if (sel_bits == 0) {
        sel_bits = 1;
      }
      for (int b = 0; b < out_bits; ++b) {
        Abc_Obj_t* acc = nullptr;
        for (const auto& [v, drv] : data) {
          Abc_Obj_t* term = abc_bit(drv, b);  // data_v[b]
          Abc_Obj_t* hit  = nullptr;          // selector matches value v
          if (op == Ntype_op::Hotmux) {
            hit = abc_bit(sel, v);  // one-hot: bit v of selector
          } else {
            for (int sb = 0; sb < sel_bits; ++sb) {
              auto* sbit = abc_bit(sel, sb);
              auto* lit  = ((v >> sb) & 1) ? sbit : abc_not(sbit);
              hit        = hit == nullptr ? lit : abc_bin(hit, lit, '&');
            }
            if (hit == nullptr) {
              hit = abc_const_bit(true);
            }
          }
          auto* prod = abc_bin(term, hit, '&');
          acc        = acc == nullptr ? prod : abc_bin(acc, prod, '|');
        }
        slots[b] = acc == nullptr ? abc_const_bit(false) : acc;
      }
    } else if (op == Ntype_op::Get_mask) {
      // out[j] = a[positions[j]] where positions = mask-selected source bits.
      auto a_drv = gu::get_driver_of_sink_name(n, "a");
      auto m_drv = gu::get_driver_of_sink_name(n, "mask");
      if (!gu::is_const_pin(m_drv)) {
        livehd::diag::err("pass.abc", "unsupported-cell", "unsupported")
            .msg("pass.abc: get_mask with a non-constant mask in region '{}' is not supported", rb.module_name)
            .emit();
        unsupported = true;
      } else {
        auto mask   = gu::hydrate_const(m_drv);
        bool neg    = mask.is_negative();
        int  mb     = mask.get_bits();
        int  pmb    = neg ? mb - 1 : mb;
        int  a_bits = gu::bits_of(a_drv);
        std::vector<int> pos;
        for (int k = 0; k < pmb; ++k) {
          bool sel = neg ? !mask.bit_test(k) : mask.bit_test(k);
          if (sel) {
            pos.push_back(k);
          }
        }
        if (neg) {
          for (int k = pmb; k < a_bits; ++k) {
            pos.push_back(k);
          }
        }
        for (int b = 0; b < out_bits; ++b) {
          slots[b] = b < static_cast<int>(pos.size()) ? abc_bit(a_drv, pos[b]) : abc_const_bit(false);
        }
      }
    } else if (op == Ntype_op::Set_mask) {
      // out[i] = mask-selected ? value[next] : a[i].
      auto a_drv = gu::get_driver_of_sink_name(n, "a");
      auto m_drv = gu::get_driver_of_sink_name(n, "mask");
      auto v_drv = gu::get_driver_of_sink_name(n, "value");
      if (!gu::is_const_pin(m_drv)) {
        livehd::diag::err("pass.abc", "unsupported-cell", "unsupported")
            .msg("pass.abc: set_mask with a non-constant mask in region '{}' is not supported", rb.module_name)
            .emit();
        unsupported = true;
      } else {
        auto mask      = gu::hydrate_const(m_drv);
        bool neg       = mask.is_negative();
        int  mb        = mask.get_bits();
        int  pmb       = neg ? mb - 1 : mb;
        int  value_pos = 0;
        for (int b = 0; b < out_bits; ++b) {
          bool from_value;
          if (b < pmb) {
            bool mbit  = mask.bit_test(b);
            from_value = neg ? !mbit : mbit;
          } else {
            from_value = neg;
          }
          slots[b] = from_value ? abc_bit(v_drv, value_pos++) : abc_bit(a_drv, b);
        }
      }
    } else if (op == Ntype_op::Sext) {
      // out[i] = a[min(i, from_bit)] (sign bit at from_bit replicated above).
      auto a_drv = gu::get_driver_of_sink_name(n, "a");
      auto b_drv = gu::get_driver_of_sink_name(n, "b");
      if (!gu::is_const_pin(b_drv)) {
        livehd::diag::err("pass.abc", "unsupported-cell", "unsupported")
            .msg("pass.abc: sext with a non-constant bit position in region '{}' is not supported", rb.module_name)
            .emit();
        unsupported = true;
      } else {
        int from_bit = static_cast<int>(gu::hydrate_const(b_drv).to_just_i64());
        for (int b = 0; b < out_bits; ++b) {
          slots[b] = abc_bit(a_drv, std::min(b, from_bit));
        }
      }
    } else {
      livehd::diag::err("pass.abc", "unsupported-cell", "unsupported")
          .msg("pass.abc: cell '{}' in region '{}' has no combinational bit-blast yet "
               "(supported: and/or/xor/not/mux/hotmux/const)",
               Ntype::get_name(op),
               rb.module_name)
          .emit();
      unsupported = true;
    }
  }
  if (unsupported) {
    Abc_NtkDelete(manNtk);
    return;
  }

  // --- region outputs -> per-bit ABC POs ---
  std::vector<std::pair<size_t, int>> po_order;  // PO index -> (output port, bit)
  for (size_t po = 0; po < rb.outputs.size(); ++po) {
    const auto& port = rb.outputs[po];
    int         w    = port.bits == 0 ? 1 : port.bits;
    for (int b = 0; b < w; ++b) {
      auto* value = abc_bit(port.src_driver, b);
      auto* buf   = Abc_NtkCreateNode(manNtk);
      buf->pData  = Hop_IthVar(manFunc, 0);
      Abc_ObjAddFanin(buf, value);
      auto* onet = new_net(buf);
      auto  nm   = std::format("{}_b{}", port.name, b);
      Abc_ObjAssignName(onet, const_cast<char*>(nm.c_str()), nullptr);
      auto* obj = Abc_NtkCreatePo(manNtk);
      Abc_ObjAddFanin(obj, onet);
      po_order.emplace_back(po, b);
    }
  }

  Abc_NtkFinalizeRead(manNtk);
  if (!Abc_NtkCheck(manNtk)) {
    livehd::diag::err("pass.abc", "abc-check", "internal")
        .msg("ABC netlist check failed for region '{}'", rb.module_name)
        .fatal();
    Abc_NtkDelete(manNtk);
    return;
  }

  // --- run the flow: logic -> optimize -> map ---
  auto* frame  = static_cast<Abc_Frame_t*>(pabc_);
  auto* pLogic = Abc_NtkToLogic(manNtk);
  Abc_NtkDelete(manNtk);
  Abc_FrameClearVerifStatus(frame);
  Abc_FrameSetCurrentNetwork(frame, pLogic);
  auto flow = comb_flow();
  if (Cmd_CommandExecute(frame, flow.c_str()) != 0) {
    livehd::diag::err("pass.abc", "abc-flow", "internal")
        .msg("ABC flow failed for region '{}': {}", rb.module_name, flow)
        .fatal();
    return;
  }
  auto* mapped = Abc_NtkToNetlist(Abc_FrameReadNtk(frame));
  if (mapped == nullptr || !Abc_NtkHasMapping(mapped)) {
    livehd::diag::err("pass.abc", "abc-unmapped", "internal")
        .msg("ABC produced no mapped netlist for region '{}' (check the Liberty library)", rb.module_name)
        .fatal();
    if (mapped != nullptr) {
      Abc_NtkDelete(mapped);
    }
    return;
  }

  // --- read back: each mapped gate -> a 1-bit blackbox Sub in the body ---
  auto* body = rb.body;

  // find-or-declare a 1-bit blackbox cell def (Liberty pins) in the out library
  auto blackbox_io = [&](Mio_Gate_t* g) -> std::shared_ptr<hhds::GraphIO> {
    std::string cell{Mio_GateReadName(g)};
    if (auto existing = outlib_->find_io(cell)) {
      return existing;
    }
    auto io = outlib_->create_io(cell);
    hhds::Port_id pid = 1;
    for (auto* pin = Mio_GateReadPins(g); pin != nullptr; pin = Mio_PinReadNext(pin)) {
      io->add_input(Mio_PinReadName(pin), pid++);
      io->set_bits(Mio_PinReadName(pin), 1);
    }
    io->add_output(Mio_GateReadOutName(g), pid++);
    io->set_bits(Mio_GateReadOutName(g), 1);
    return io;
  };

  // lazily build bit b of a body input pin (Get_mask bit-select; pin itself if 1-bit)
  std::vector<std::vector<hhds::Pin_class>> in_bit(rb.inputs.size());
  auto input_bit = [&](size_t port_idx, int b) -> hhds::Pin_class {
    auto&       cache = in_bit[port_idx];
    const auto& port  = rb.inputs[port_idx];
    int         w     = port.bits == 0 ? 1 : port.bits;
    if (static_cast<int>(cache.size()) < w) {
      cache.resize(w);
    }
    if (!cache[b].is_invalid()) {
      return cache[b];
    }
    auto ipin = body->get_input_pin(port.name);
    if (w == 1) {
      cache[b] = ipin;
      return ipin;
    }
    auto gm = gu::create_typed_node(*body, Ntype_op::Get_mask);
    ipin.connect_sink(gu::setup_sink_by_name(gm, "a"));
    gu::create_const(*body, *Dlop::create_integer(int64_t{1} << b)).connect_sink(gu::setup_sink_by_name(gm, "mask"));
    auto d = gm.create_driver_pin(0);
    gu::set_bits(d, 1);
    gu::set_unsign(d);
    cache[b] = d;
    return d;
  };

  absl::flat_hash_map<Abc_Obj_t*, hhds::Pin_class> net2drv;
  int  i = 0;
  Abc_Obj_t* pObj = nullptr;

  // pass 1a: PI nets -> body input bit drivers (match by creation order — ABC
  // preserves CI/CO order across the flow, more robust than name parsing).
  Abc_NtkForEachPi(mapped, pObj, i) {
    if (i < static_cast<int>(pi_order.size())) {
      net2drv[Abc_ObjFanout0(pObj)] = input_bit(pi_order[i].first, pi_order[i].second);
    }
  }

  // pass 1b: each mapped gate -> a Sub; map its output net -> Sub output pin
  std::vector<std::pair<hhds::Node_class, Abc_Obj_t*>> gates;
  Abc_NtkForEachNode(mapped, pObj, i) {
    auto* g = static_cast<Mio_Gate_t*>(pObj->pData);
    if (g == nullptr) {
      continue;
    }
    auto io  = blackbox_io(g);
    auto sub = gu::create_typed_node(*body, Ntype_op::Sub);
    sub.set_subnode(io);
    sub.attr(hhds::attrs::name).set(std::format("g{}_{}", Abc_ObjId(pObj), Mio_GateReadName(g)));
    auto outpin = sub.create_driver_pin(Mio_GateReadOutName(g));
    gu::set_bits(outpin, 1);
    gu::set_unsign(outpin);
    net2drv[Abc_ObjFanout0(pObj)] = outpin;
    gates.emplace_back(sub, pObj);
  }

  // pass 2: wire each Sub's fanins (fanin k <-> Liberty pin k)
  auto const0_pin = [&]() {
    return gu::create_const(*body, *Dlop::create_integer(0));
  };
  for (auto& [sub, obj] : gates) {
    auto* g = static_cast<Mio_Gate_t*>(obj->pData);
    std::vector<std::string> pins;
    for (auto* pin = Mio_GateReadPins(g); pin != nullptr; pin = Mio_PinReadNext(pin)) {
      pins.emplace_back(Mio_PinReadName(pin));
    }
    int k = 0;
    Abc_Obj_t* fin = nullptr;
    Abc_ObjForEachFanin(obj, fin, k) {
      if (k >= static_cast<int>(pins.size())) {
        break;
      }
      auto spin = sub.create_sink_pin(pins[k]);
      gu::set_bits(spin, 1);
      auto it = net2drv.find(fin);
      if (it != net2drv.end()) {
        it->second.connect_sink(spin);
      } else {
        const0_pin().connect_sink(spin);  // structurally complete; should not occur
      }
    }
  }

  // pass 3: POs -> reassemble multi-bit outputs (Set_mask concat). Match by
  // creation order (po_order), consistent with the PI readback.
  std::vector<std::vector<hhds::Pin_class>> out_bits(rb.outputs.size());
  for (size_t po = 0; po < rb.outputs.size(); ++po) {
    int w = rb.outputs[po].bits == 0 ? 1 : rb.outputs[po].bits;
    out_bits[po].resize(w);
  }
  Abc_NtkForEachPo(mapped, pObj, i) {
    if (i >= static_cast<int>(po_order.size())) {
      continue;
    }
    auto* net = Abc_ObjFanin0(pObj);
    auto  dit = net2drv.find(net);
    out_bits[po_order[i].first][po_order[i].second] = dit != net2drv.end() ? dit->second : const0_pin();
  }

  for (size_t po = 0; po < rb.outputs.size(); ++po) {
    const auto& port = rb.outputs[po];
    int         w    = port.bits == 0 ? 1 : port.bits;
    auto        opin = body->get_output_pin(port.name);
    auto&       bits = out_bits[po];
    for (int b = 0; b < w; ++b) {
      if (bits[b].is_invalid()) {
        bits[b] = const0_pin();
      }
    }
    if (w == 1) {
      bits[0].connect_sink(opin);
      continue;
    }
    // acc = const0; acc = Set_mask(acc, 1<<b, bit_b)  for each bit
    hhds::Pin_class acc = gu::create_const(*body, *Dlop::create_integer(0));
    for (int b = 0; b < w; ++b) {
      auto sm = gu::create_typed_node(*body, Ntype_op::Set_mask);
      acc.connect_sink(gu::setup_sink_by_name(sm, "a"));
      gu::create_const(*body, *Dlop::create_integer(int64_t{1} << b)).connect_sink(gu::setup_sink_by_name(sm, "mask"));
      bits[b].connect_sink(gu::setup_sink_by_name(sm, "value"));
      acc = sm.create_driver_pin(0);
      gu::set_bits(acc, b + 1);
      gu::set_unsign(acc);
    }
    gu::set_bits(acc, w);
    acc.connect_sink(opin);
  }

  if (opts_.verbose) {
    std::print("[pass.abc] region '{}' mapped: {} gates\n", rb.module_name, gates.size());
  }
  Abc_NtkDelete(mapped);
}

void report_stats(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string_view top, const Map_options& opts) {
  (void)graphs;
  std::print("pass.abc stats: top='{}' library='{}' seq={}\n", top, opts.library, opts.seq);
  std::print("  (run with --emit-dir lg:DIR to produce the mapped netlist library)\n");
}

}  // namespace livehd::abc
