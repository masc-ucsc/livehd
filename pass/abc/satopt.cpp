// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "satopt.hpp"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>

#include "abc_blast.hpp"
#include "abc_salt.hpp"
#include "attr_carry.hpp"
#include "absl/container/node_hash_map.h"
#include "hhds/attrs/name.hpp"
#include "json_util.hpp"
#include "mask_eval.hpp"
#include "node_util.hpp"
#include "prove.hpp"
#include "rapidjson/document.h"

// clang-format off
extern "C" {
#include "base/abc/abc.h"
#include "base/main/main.h"
#include "base/cmd/cmd.h"
#include "aig/hop/hop.h"
}
// clang-format on

namespace livehd::abc {
namespace {
namespace gu = livehd::graph_util;
using Pin    = hhds::Pin_class;
using Node   = hhds::Node_class;
struct Unsupported {};

bool cut(const Pin& p) {
  if (gu::is_graph_input_pin(p)) {
    return true;
  }
  auto op = gu::type_op_of(p.get_master_node());
  return op == Ntype_op::Flop || op == Ntype_op::Memory || op == Ntype_op::Sub || op == Ntype_op::Fflop || op == Ntype_op::Latch;
}
int width(const Pin& p) {
  return p.is_const() ? std::max(1, static_cast<int>(gu::const_of(p).get_signed_bits())) : std::max(1, gu::bits_of(p));
}

struct Arms {
  std::vector<Pin> controls, values;
  bool             hot = false;
};
Arms arms_of(const Node& n) {
  Arms a;
  a.hot = gu::type_op_of(n) == Ntype_op::Hotmux;
  if (a.hot) {
    auto h = gu::hotmux_inputs(n);
    for (auto [c, v] : h.arms) {
      a.controls.push_back(c);
      a.values.push_back(v);
    }
    if (!h.fallback.is_invalid()) {
      a.controls.emplace_back();
      a.values.push_back(h.fallback);
    }
  } else {
    std::map<int, Pin> inputs;
    for (const auto& in_pin : n.inp_sorted_pins()) {
      const auto in_drv            = in_pin.get_driver_pin();
      inputs[in_pin.get_port_id()] = in_drv;
    }
    // Two-arm Mux uses a nonzero condition; larger indexed muxes are left to ABC.
    if (inputs.size() == 3 && inputs.contains(0) && inputs.contains(1) && inputs.contains(2)) {
      a.controls = {inputs.at(0), inputs.at(0)};
      a.values   = {inputs.at(1), inputs.at(2)};
    }
  }
  return a;
}

// Eight deterministic, word-level input vectors. This is only a rejection
// filter: unsupported operations can reject an opportunity, never prove one.
// A descending filter walks the same virtual-flat view as a descending
// formal::Prover, so a value fixed inside a callee is not sampled as free.
class Seeds {
  using Values = std::array<Dlop, 8>;
  using Key    = std::pair<uint32_t, Pin>;  // (instance scope, pin); scope 0 = the graph
  struct Scope {
    uint32_t parent = 0;
    Node     inst;
    int      depth = 0;
  };
  absl::node_hash_map<Key, Values>                         values_;
  absl::flat_hash_set<Key>                                 visiting_;
  std::vector<Scope>                                       scopes_{Scope{}};
  absl::flat_hash_map<std::pair<uint32_t, Node>, uint32_t> children_;
  bool                                                     descend_ = false;
  std::mt19937_64                                          random_{0x7361746f7074ULL};
  static Dlop                                              fit(const Dlop& v, const Pin& p) {
    if (p.is_const()) {
      return v;
    }
    auto low = v.and_op(Dlop::get_mask_value(width(p)));
    return gu::is_unsign(p) ? *low : *low->sext_op(Dlop::create_integer(width(p)));
  }
  // Where p continues across an instance boundary when descending.
  std::optional<std::pair<uint32_t, Pin>> cross(uint32_t scope, const Pin& p) {
    if (!descend_) {
      return std::nullopt;
    }
    if (gu::is_graph_input_pin(p)) {
      const auto driver = scope == 0 ? Pin{} : formal::sub_input_driver(scopes_[scope].inst, p);
      if (driver.is_invalid()) {
        return std::nullopt;
      }
      return std::pair{scopes_[scope].parent, driver};
    }
    const auto inst = p.get_master_node();
    if (gu::type_op_of(inst) != Ntype_op::Sub || scopes_[scope].depth >= 32) {
      return std::nullopt;
    }
    const auto driver = formal::sub_body_driver(p);
    if (driver.is_invalid()) {
      return std::nullopt;
    }
    const auto [it, fresh] = children_.try_emplace({scope, inst}, static_cast<uint32_t>(scopes_.size()));
    if (fresh) {
      scopes_.push_back({scope, inst, scopes_[scope].depth + 1});
    }
    return std::pair{it->second, driver};
  }

public:
  explicit Seeds(bool descend = false) : descend_(descend) {}
  const Values& get(const Pin& p) { return get(0, p); }
  const Values& get(uint32_t scope, const Pin& p) {
    if (p.is_invalid()) {
      throw Unsupported{};
    }
    const Key key{scope, p};
    if (auto it = values_.find(key); it != values_.end()) {
      return it->second;
    }
    if (values_.size() > 50000 || width(p) > 65536 || !visiting_.insert(key).second) {
      throw Unsupported{};
    }
    Values out;
    if (p.is_const()) {
      if (gu::const_of(p).has_unknowns()) {
        throw Unsupported{};
      }
      out.fill(gu::const_of(p));
    } else if (const auto other = cross(scope, p)) {
      out = get(other->first, other->second);
    } else if (cut(p)) {
      for (auto& v : out) {
        std::string bits(static_cast<size_t>(width(p)), '0');
        uint64_t    r = 0;
        for (size_t b = 0; b < bits.size(); ++b) {
          if (b % 64 == 0) {
            r = random_();
          }
          bits[b] = ((r >> (b % 64)) & 1) ? '1' : '0';
        }
        v = fit(*Dlop::from_binary(bits, true), p);
      }
    } else {
      const auto                                n  = p.get_master_node();
      const auto                                op = gu::type_op_of(n);
      std::map<int, std::vector<const Values*>> ins;
      for (const auto& in_pin : n.inp_sorted_pins()) {
        const auto in_drv = in_pin.get_driver_pin();
        // Bucket by operand BANK: a commutative cell spends one sink pid per
        // operand (graph/cell.hpp's ONE DRIVER PER SINK PIN block), and every
        // reader below (`arg(0)`, `arg(1)`, the Sum sign test) names the ROLE.
        ins[Ntype::sink_bank(op, in_pin.get_port_id())].push_back(&get(scope, in_drv));
      }
      for (int seed = 0; seed < 8; ++seed) {
        const auto arg = [&](int pid) -> const Dlop& {
          if (!ins.contains(pid) || ins.at(pid).empty()) {
            throw Unsupported{};
          }
          return (*ins.at(pid).front())[seed];
        };
        Dlop v = *Dlop::create_integer(0);
        if (op == Ntype_op::Mux || op == Ntype_op::Hotmux) {
          const auto arms = arms_of(n);
          if (arms.values.empty()) {
            throw Unsupported{};
          }
          size_t selected = 0;
          if (!arms.hot) {
            selected = get(scope, arms.controls[0])[seed].is_known_zero() ? 0 : 1;
          } else {
            selected = arms.values.size();
            for (size_t i = 0; i < arms.values.size(); ++i) {
              if (arms.controls[i].is_invalid() || !get(scope, arms.controls[i])[seed].is_known_zero()) {
                selected = i;
                break;
              }
            }
          }
          if (selected < arms.values.size()) {
            v = get(scope, arms.values[selected])[seed];
          }
        } else if (op == Ntype_op::Concat) {
          std::vector<Dlop::Concat_lane> lanes;
          for (const auto& l : gu::concat_lanes(n)) {
            lanes.push_back({&get(scope, l.value)[seed], l.width});
          }
          v = *Dlop::concat_op(lanes);
        } else if (op == Ntype_op::Rxor || op == Ntype_op::Popcount) {
          const auto selected = arg(0).get_mask_op_opt(0, gu::reduction_count(n));
          v                   = *(op == Ntype_op::Rxor ? selected->rxor_op() : selected->popcount_op());
        } else if (op == Ntype_op::Not) {
          v = *arg(0).not_op();
        } else if (op == Ntype_op::Get_mask) {
          v = livehd::eval_get_mask(arg(0), arg(2));
        } else if (op == Ntype_op::Set_mask) {
          v = livehd::eval_set_mask(arg(0), arg(2), arg(4));
        } else if (op == Ntype_op::Sext) {
          v = *arg(0).sext_op(arg(1));
        } else if (op == Ntype_op::SHL) {
          v = *arg(0).shl_op(arg(1));
        } else if (op == Ntype_op::SRA) {
          v = *arg(0).sra_op(arg(1));
        } else if (op == Ntype_op::LT) {
          v = *arg(0).lt_op(arg(1));
        } else if (op == Ntype_op::GT) {
          v = *arg(0).gt_op(arg(1));
        } else if (op == Ntype_op::Sum || op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Xor || op == Ntype_op::Mult
                   || op == Ntype_op::EQ || op == Ntype_op::Ror) {
          bool first = true;
          Dlop equal_to;
          for (const auto& [pid, operands] : ins) {
            for (const auto* operand : operands) {
              const auto& x = (*operand)[seed];
              if (op == Ntype_op::Sum) {
                v = *(pid == 1 ? v.sub_op(x) : v.add_op(x));
              } else if (op == Ntype_op::Ror) {
                v = *v.ror_op(x);
              } else if (op == Ntype_op::EQ) {
                if (first) {
                  equal_to = x;
                  v        = *Dlop::create_integer(1);
                } else {
                  v = *v.and_op(equal_to.eq_op(x));
                }
              } else if (first) {
                v = x;
              } else if (op == Ntype_op::And) {
                v = *v.and_op(x);
              } else if (op == Ntype_op::Or) {
                v = *v.or_op(x);
              } else if (op == Ntype_op::Xor) {
                v = *v.xor_op(x);
              } else {
                v = *v.mult_op(x);
              }
              first = false;
            }
          }
        } else {
          throw Unsupported{};
        }
        if (v.has_unknowns() || v.is_invalid()) {
          throw Unsupported{};
        }
        out[seed] = fit(v, p);
      }
    }
    visiting_.erase(key);
    return values_.emplace(key, std::move(out)).first->second;
  }
  bool survives(const Arms& arms, const Mux_fact& f) {
    for (int seed = 0; seed < 8; ++seed) {
      bool selected = false;
      if (!arms.hot) {
        selected = get(arms.controls[0])[seed].is_known_zero() == (f.arm == 0);
      } else if (!arms.controls[f.arm].is_invalid()) {
        selected = !get(arms.controls[f.arm])[seed].is_known_zero();
      } else {
        selected = true;
        for (const auto& c : arms.controls) {
          if (!c.is_invalid() && !get(c)[seed].is_known_zero()) {
            selected = false;
          }
        }
      }
      if (!selected) {
        continue;
      }
      const bool bit = get(arms.values[f.arm])[seed].bit_test(f.bit);
      const bool expected
          = f.kind == Mux_fact::Kind::one
            || (f.other >= 0 && (get(arms.values[f.other])[seed].bit_test(f.bit) != (f.kind == Mux_fact::Kind::complement)));
      if (bit != expected) {
        return false;
      }
    }
    return true;
  }
};

struct Gates {
  Abc_Ntk_t* ntk = Abc_NtkAlloc(ABC_NTK_NETLIST, ABC_FUNC_AIG, 1);
  Abc_Obj_t* constants[2]{};
  uint64_t   count = 0;
  Gates() { ntk->pName = Abc_UtilStrsav(const_cast<char*>("satopt")); }
  ~Gates() {
    if (ntk) {
      Abc_NtkDelete(ntk);
    }
  }
  Abc_Obj_t* net(Abc_Obj_t* node) {
    if (++count > 1000000) {
      throw Unsupported{};
    }
    auto* out = Abc_NtkCreateNet(ntk);
    Abc_ObjAddFanin(out, node);
    return out;
  }
  Abc_Obj_t* konst(bool v) {
    auto*& c = constants[v];
    if (!c) {
      auto* n  = Abc_NtkCreateNode(ntk);
      n->pData = Hop_NotCond(Hop_ManConst1(static_cast<Hop_Man_t*>(ntk->pManFunc)), !v);
      c        = net(n);
    }
    return c;
  }
  Abc_Obj_t* zero() { return konst(false); }
  Abc_Obj_t* one() { return konst(true); }
  Abc_Obj_t* inv(Abc_Obj_t* a) {
    if (a == zero()) {
      return one();
    }
    if (a == one()) {
      return zero();
    }
    auto* n  = Abc_NtkCreateNode(ntk);
    n->pData = Hop_Not(Hop_IthVar(static_cast<Hop_Man_t*>(ntk->pManFunc), 0));
    Abc_ObjAddFanin(n, a);
    return net(n);
  }
  Abc_Obj_t* bin(Abc_Obj_t* a, Abc_Obj_t* b, char op) {
    if (op == '&') {
      if (a == zero() || b == zero()) {
        return zero();
      }
      if (a == one()) {
        return b;
      }
      if (b == one() || a == b) {
        return a;
      }
    }
    if (op == '|') {
      if (a == one() || b == one()) {
        return one();
      }
      if (a == zero()) {
        return b;
      }
      if (b == zero() || a == b) {
        return a;
      }
    }
    if (op == '^') {
      if (a == b) {
        return zero();
      }
      if (a == zero()) {
        return b;
      }
      if (b == zero()) {
        return a;
      }
    }
    auto* n  = Abc_NtkCreateNode(ntk);
    auto* h  = static_cast<Hop_Man_t*>(ntk->pManFunc);
    n->pData = op == '&' ? Hop_CreateAnd(h, 2) : op == '|' ? Hop_CreateOr(h, 2) : Hop_CreateExor(h, 2);
    Abc_ObjAddFanin(n, a);
    Abc_ObjAddFanin(n, b);
    return net(n);
  }
  Abc_Obj_t* and_(Abc_Obj_t* a, Abc_Obj_t* b) { return bin(a, b, '&'); }
  Abc_Obj_t* or_(Abc_Obj_t* a, Abc_Obj_t* b) { return bin(a, b, '|'); }
  Abc_Obj_t* xor_(Abc_Obj_t* a, Abc_Obj_t* b) { return bin(a, b, '^'); }
};

class Cone {
  Wiring_blaster<Abc_Obj_t*>                          wiring_;
  Gates&                                              ops_;
  absl::node_hash_map<Pin, std::map<int, Abc_Obj_t*>> bits_;
  absl::flat_hash_set<Pin>                            visiting_;
  absl::flat_hash_set<Node>                           region_;
  partition::Region_body                              rb_;
  Map_options                                         options_;

public:
  explicit Cone(Gates& gates) : ops_(gates) {}
  Abc_Obj_t* bit(const Pin& p, int b) {
    if (p.is_invalid() || b < 0 || b > 65536) {
      throw Unsupported{};
    }
    const int stamped_width = gu::bits_of(p);
    if (stamped_width > 0 && b >= stamped_width) {
      return gu::is_unsign(p) ? ops_.zero() : bit(p, stamped_width - 1);
    }
    auto& slots = bits_[p];
    if (auto it = slots.find(b); it != slots.end()) {
      return it->second;
    }
    if (p.is_const()) {
      if (gu::const_of(p).has_unknowns()) {
        throw Unsupported{};
      }
      return slots[b] = ops_.konst(gu::const_of(p).bit_test(b));
    }
    if (cut(p)) {
      auto* pi   = Abc_NtkCreatePi(ops_.ntk);
      auto  name = std::format("i{}", Abc_ObjId(pi));
      auto* net  = ops_.net(pi);
      Abc_ObjAssignName(net, const_cast<char*>(name.c_str()), nullptr);
      return slots[b] = net;
    }
    if (bits_.size() > 50000 || width(p) > 65536 || !visiting_.insert(p).second) {
      throw Unsupported{};
    }
    auto n  = p.get_master_node();
    auto op = gu::type_op_of(n);
    if (op == Ntype_op::Concat || op == Ntype_op::Set_mask) {
      // Wiring detects cycles per bit, so disjoint slices of a packed state
      // bus remain usable even when the source node has another active bit.
      visiting_.erase(p);
      slots[b] = wiring_.bit(
          p,
          b,
          [&](const Pin& q, int i) { return bit(q, i); },
          [&] { return ops_.zero(); },
          [](std::string_view) { throw Unsupported{}; });
    } else {
      // The proof uses priority Hotmux semantics. It never assumes exclusivity
      // of unconstrained state, so every resulting theorem is whole-design safe.
      if (op == Ntype_op::Hotmux) {
        auto  a     = arms_of(n);
        auto* value = ops_.zero();
        for (int i = static_cast<int>(a.values.size()) - 1; i >= 0; --i) {
          if (a.controls[i].is_invalid()) {
            value = bit(a.values[i], b);
          } else {
            auto* c = condition(a.controls[i]);
            value   = ops_.or_(ops_.and_(c, bit(a.values[i], b)), ops_.and_(ops_.inv(c), value));
          }
        }
        slots[b] = value;
      } else {
        const auto fail       = [](const Node&,
                                   std::string_view,
                                   std::string_view,
                                   std::string_view,
                                   std::string_view = {},
                                   const Pin&       = Pin{},
                                   std::string_view = {}) { throw Unsupported{}; };
        const auto fail_shift = [](const Node&, std::string_view, const Dlop&, const Pin&) { throw Unsupported{}; };
        blast_comb(
            n,
            width(p),
            slots,
            ops_,
            [&](const Pin& q, int i) { return bit(q, i); },
            options_,
            rb_,
            region_,
            fail,
            fail_shift);
      }
    }
    visiting_.erase(p);
    return slots.at(b);
  }
  Abc_Obj_t* condition(const Pin& p) {
    auto* c = ops_.zero();
    for (int b = 0; b < width(p); ++b) {
      c = ops_.or_(c, bit(p, b));
    }
    return c;
  }
  Abc_Obj_t* condition(const Arms& a, int arm) {
    if (!a.hot) {
      auto* c = condition(a.controls[0]);
      return arm == 0 ? ops_.inv(c) : c;
    }
    if (!a.controls[arm].is_invalid()) {
      return condition(a.controls[arm]);
    }
    auto* c = ops_.zero();
    for (const auto& p : a.controls) {
      if (!p.is_invalid()) {
        c = ops_.or_(c, condition(p));
      }
    }
    return ops_.inv(c);
  }
};

// The combinational fan-in of `roots` (cut like the proofs) in forward
// topological order. Evaluating it in this order before the roots keeps the
// memoized Seeds/Cone recursion one level deep: a pass must not recurse on
// design depth.
std::vector<Pin> fanin_forward(hhds::Graph* graph, std::vector<Pin> pending) {
  absl::flat_hash_set<Node> cone;
  while (!pending.empty()) {
    const auto p = pending.back();
    pending.pop_back();
    if (p.is_invalid() || p.is_const() || cut(p)) {
      continue;
    }
    const auto n = p.get_master_node();
    if (!cone.insert(n).second) {
      continue;
    }
    for (const auto& in_pin : n.inp_sorted_pins()) {
      for (const auto& d : in_pin.get_driver_pins()) {
        pending.push_back(d);
      }
    }
  }
  std::vector<Pin> order;
  for (const auto n : graph->body().nodes(hhds::Node_order::forward)) {
    if (cone.contains(n)) {
      for (const auto& p : n.out_sorted_pins()) {
        order.push_back(p);
      }
    }
  }
  return order;
}

// One ABC sweep over every candidate in a private frame. `build` returns one
// refutation per candidate (nonzero exactly when its claim fails), or nullptr
// for an unsupported one; only an output swept to constant zero is proven.
// nullopt when no frame is available: nothing was attempted.
std::optional<std::vector<bool>> prove_zero(const std::function<std::vector<Abc_Obj_t*>(Gates&, Cone&)>& build) {
  auto* previous = Abc_FrameReadGlobalFrame();
  auto* frame    = Abc_FrameCreate();
  if (!frame) {
    return std::nullopt;
  }
  Abc_FrameEnter(frame);
  std::vector<bool> proven;
  {
    Gates               gates;
    Cone                cone(gates);
    const auto          refutes = build(gates, cone);
    std::vector<size_t> outputs;
    proven.assign(refutes.size(), false);
    for (size_t i = 0; i < refutes.size(); ++i) {
      if (refutes[i] == nullptr) {
        continue;
      }
      auto* po = Abc_NtkCreatePo(gates.ntk);
      Abc_ObjAddFanin(po, refutes[i]);
      auto name = std::format("f{}", outputs.size());
      Abc_ObjAssignName(po, const_cast<char*>(name.c_str()), nullptr);
      outputs.push_back(i);
    }
    if (!outputs.empty()) {
      Abc_NtkAddDummyPiNames(gates.ntk);
      Abc_NtkAddDummyPoNames(gates.ntk);
      Abc_Ntk_t* logic = Abc_NtkToLogic(gates.ntk);
      if (logic) {
        Abc_FrameReplaceCurrentNetwork(frame, logic);
        if (Cmd_CommandExecute(frame, "strash; &get -n; &fraig -x -C 500; &put; strash") == 0) {
          auto* swept = Abc_FrameReadNtk(frame);
          if (Abc_NtkPoNum(swept) == static_cast<int>(outputs.size())) {
            for (size_t i = 0; i < outputs.size(); ++i) {
              auto* po = Abc_NtkPo(swept, static_cast<int>(i));
              if (Abc_ObjFanin0(po) == Abc_AigConst1(swept) && Abc_ObjFaninC0(po)) {
                proven[outputs[i]] = true;
              }
            }
          }
        }
      }
    }
  }
  Abc_FrameLeave(previous);
  Abc_FrameDestroy(frame);
  return proven;
}

bool crosses(const Node& n, const Arms& arms) {
  absl::flat_hash_set<Pin> seen;
  std::vector<Pin>         pending = arms.values;
  for (const auto& c : arms.controls) {
    if (!c.is_invalid()) {
      pending.push_back(c);
    }
  }
  while (!pending.empty()) {
    const auto p = pending.back();
    pending.pop_back();
    if (p.is_const() || !seen.insert(p).second || gu::is_graph_input_pin(p)) {
      continue;
    }
    const auto producer = p.get_master_node();
    if (gu::color_of(producer) != gu::color_of(n)) {
      return true;
    }
    if (cut(p)) {
      continue;
    }
    if (seen.size() > 50000) {
      return false;
    }
    for (const auto& in_pin : producer.inp_sorted_pins()) {
      const auto in_drv = in_pin.get_driver_pin();
      pending.push_back(in_drv);
    }
  }
  return false;
}

// The exact translation input, not a digest oracle. IDs are intentionally part
// of this descriptor: a harmless renumbering misses rather than attaching a
// theorem to a different node. State and opaque outputs remain independent cuts.
std::string source_key(hhds::Graph* graph, bool colors) {
  std::vector<std::string> rows;
  for (const auto n : graph->body().nodes()) {
    auto row = std::format("{}:{}", n.get_debug_nid(), static_cast<int>(gu::type_op_of(n)));
    if (colors) {
      row += std::format(":c{}", gu::color_of(n));
    }
    std::vector<std::string> edges;
    const auto               pin = [](const Pin& p) {
      std::string value;
      if (p.is_const()) {
        value = satopt_constant_key(gu::const_of(p));
      }
      return std::format("{}:{}:{}:{}:{}:{}",
                         p.get_master_node().get_debug_nid(),
                         p.get_port_id(),
                         gu::bits_of(p),
                         gu::is_unsign(p),
                         value.size(),
                         value);
    };
    for (const auto& in_pin : n.inp_sorted_pins()) {
      for (auto in_drv : in_pin.get_driver_pins()) {
        edges.push_back(std::format("{}={}", in_pin.get_port_id(), pin(in_drv)));
      }
    }
    for (const auto& e : n.out_edges()) {
      edges.push_back("o=" + pin(e.driver));
    }
    // Exact proof-cache identity: the caller compares the full source key.
    // A commutative digest without this representation cannot authorize reuse.
    std::sort(edges.begin(), edges.end());
    for (const auto& edge : edges) {
      row += std::format("|{}:{}", edge.size(), edge);
    }
    rows.push_back(std::move(row));
  }
  std::sort(rows.begin(), rows.end());
  std::string key = std::format("{}:{}:{}", kAbcSrcSalt, graph->get_name().size(), graph->get_name());
  for (const auto& row : rows) {
    key += std::format("\n{}:{}", row.size(), row);
  }
  return key;
}
struct Cached {
  std::string                    source, colors;
  bool                           all = false;
  std::shared_ptr<Satopt_result> result;
};
std::map<std::string, Cached> saved;
std::mutex                    saved_mutex;
std::string                   cache_path(std::string_view dir, std::string_view name, std::string_view suffix = {}) {
  if (dir.empty()) {
    return {};
  }
  uint64_t h = 14695981039346656037ULL;
  for (unsigned char c : name) {
    h = (h ^ c) * 1099511628211ULL;
  }
  return std::format("{}/{:016x}{}.json", dir, h, suffix);
}
void write_atomic(const std::string& path, const std::string& text) {
  if (path.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
  if (ec) {
    return;
  }
  // Separate temporary files prevent concurrent invocations from interleaving
  // a definition descriptor from one proof run with another run's facts.
  auto      temporary = path + ".tmp.XXXXXX";
  const int fd        = mkstemp(temporary.data());
  if (fd < 0) {
    return;
  }
  close(fd);
  std::ofstream out(temporary);
  out << text;
  out.close();
  if (out) {
    std::filesystem::rename(temporary, path, ec);
  }
  std::filesystem::remove(temporary, ec);
}
Cached read_cache(const std::string& path) {
  Cached row;
  if (path.empty()) {
    return row;
  }
  std::ifstream       input(path);
  std::string         text((std::istreambuf_iterator<char>(input)), {});
  rapidjson::Document doc;
  doc.Parse(text.c_str());
  if (!doc.IsObject() || !doc.HasMember("source") || !doc["source"].IsString() || !doc.HasMember("colors")
      || !doc["colors"].IsString() || !doc.HasMember("all") || !doc["all"].IsBool() || !doc.HasMember("facts")
      || !doc["facts"].IsArray()) {
    return row;
  }
  row.source = doc["source"].GetString();
  row.colors = doc["colors"].GetString();
  row.all    = doc["all"].GetBool();
  row.result = std::make_shared<Satopt_result>();
  for (const auto& f : doc["facts"].GetArray()) {
    if (!f.IsArray() || f.Size() != 5 || !f[0].IsUint64() || !f[1].IsInt() || !f[2].IsInt() || !f[3].IsInt() || !f[4].IsInt()
        || f[1].GetInt() < 0 || f[2].GetInt() < 0 || f[3].GetInt() < 0 || f[3].GetInt() > 3) {
      return {};
    }
    Mux_fact fact{f[0].GetUint64(), f[1].GetInt(), f[2].GetInt(), static_cast<Mux_fact::Kind>(f[3].GetInt()), f[4].GetInt()};
    row.result->mux[fact.node].push_back(fact);
    ++row.result->proven;
  }
  return row;
}
void write_cache(const std::string& path, const Cached& row) {
  if (path.empty()) {
    return;
  }
  auto text  = std::format("{{\"source\":\"{}\",\"colors\":\"{}\",\"all\":{},\"facts\":[",
                          json_util::escape(row.source),
                          json_util::escape(row.colors),
                          row.all ? "true" : "false");
  bool comma = false;
  for (const auto& [node, facts] : row.result->mux) {
    for (const auto& f : facts) {
      text  += std::format("{}[{},{},{},{},{}]", comma ? "," : "", node, f.arm, f.bit, static_cast<int>(f.kind), f.other);
      comma  = true;
    }
  }
  write_atomic(path, text + "]}\n");
}

// Selectors of one cell, indexed like Select_fact::control. A Hotmux fallback
// has no control of its own. Only a one-bit Flop enable is a candidate.
std::vector<Pin> selectors(const Node& n) {
  const auto op = gu::type_op_of(n);
  if (op == Ntype_op::Flop) {
    const auto en = gu::get_driver_of_sink_name(n, "enable");
    if (en.is_invalid() || gu::bits_of(en) != 1) {
      return {};
    }
    return {en};
  }
  if (op != Ntype_op::Mux && op != Ntype_op::Hotmux) {
    return {};
  }
  auto arms = arms_of(n);
  if (!arms.hot) {
    return arms.controls.empty() ? std::vector<Pin>{} : std::vector<Pin>{arms.controls[0]};
  }
  if (!arms.controls.empty() && arms.controls.back().is_invalid()) {
    arms.controls.pop_back();
  }
  return arms.controls;
}
// `children` pins every definition the proofs descended into: a callee edit
// changes the facts although the definition itself did not change.
struct Select_cached {
  std::string                                      source;
  std::vector<Select_fact>                         facts;
  std::vector<std::pair<std::string, std::string>> children;  // (name, exact source key)
};
std::map<std::string, Select_cached> saved_selects;
Select_cached                        read_selects(const std::string& path) {
  Select_cached row;
  if (path.empty()) {
    return row;
  }
  std::ifstream       input(path);
  std::string         text((std::istreambuf_iterator<char>(input)), {});
  rapidjson::Document doc;
  doc.Parse(text.c_str());
  if (!doc.IsObject() || !doc.HasMember("source") || !doc["source"].IsString() || !doc.HasMember("facts")
      || !doc["facts"].IsArray()) {
    return row;
  }
  for (const auto& f : doc["facts"].GetArray()) {
    if (!f.IsArray() || f.Size() != 3 || !f[0].IsUint64() || !f[1].IsInt() || f[1].GetInt() < 0 || !f[2].IsBool()) {
      return {};
    }
    row.facts.push_back({f[0].GetUint64(), f[1].GetInt(), f[2].GetBool()});
  }
  if (!doc.HasMember("children") || !doc["children"].IsArray()) {
    return {};
  }
  for (const auto& c : doc["children"].GetArray()) {
    if (!c.IsArray() || c.Size() != 2 || !c[0].IsString() || !c[1].IsString()) {
      return {};
    }
    row.children.emplace_back(c[0].GetString(), c[1].GetString());
  }
  row.source = doc["source"].GetString();
  return row;
}
bool children_match(hhds::Graph* graph, const Select_cached& row) {
  auto* lib = graph->get_io() ? graph->get_io()->get_library() : nullptr;
  for (const auto& [name, key] : row.children) {
    const auto io  = lib ? lib->find_io(name) : nullptr;
    const auto def = io ? io->get_graph() : nullptr;
    if (!def || source_key(def.get(), false) != key) {
      return false;
    }
  }
  return true;
}
void write_selects(const std::string& path, const Select_cached& row) {
  if (path.empty()) {
    return;
  }
  auto text = std::format("{{\"source\":\"{}\",\"facts\":[", json_util::escape(row.source));
  for (size_t i = 0; i < row.facts.size(); ++i) {
    const auto& f  = row.facts[i];
    text          += std::format("{}[{},{},{}]", i ? "," : "", f.node, f.control, f.value ? "true" : "false");
  }
  text += "],\"children\":[";
  for (size_t i = 0; i < row.children.size(); ++i) {
    text += std::format("{}[\"{}\",\"{}\"]",
                        i ? "," : "",
                        json_util::escape(row.children[i].first),
                        json_util::escape(row.children[i].second));
  }
  write_atomic(path, text + "]}\n");
}

// Rewrites against a proven selector. Tying a pin to the constant it always
// carries is exact; zeroing an arm is exact because no input selects it.
// Every replaced driver is recorded so its cone can be swept afterwards.
struct Select_rewrite {
  hhds::Graph&     g;
  std::vector<Pin> released;
  void             tie(const Node& n, hhds::Port_id pid, int64_t value) {
    const auto sink = n.create_sink_pin(pid);
    for (const auto& d : sink.get_driver_pins()) {
      if (d.is_const() && gu::const_of(d).is_known_eq(*Dlop::create_integer(value))) {
        return;
      }
      released.push_back(d);
    }
    gu::drop_drivers(sink);
    gu::create_const(g, *Dlop::create_integer(value)).connect_sink(sink);
  }
  // Rewire one sink to an equivalent replacement (an arm rebuilt from proven
  // per-bit facts). The replaced driver is released for the sweep.
  void replace(const Node& n, hhds::Port_id pid, const Pin& replacement) {
    const auto sink = n.create_sink_pin(pid);
    for (const auto& d : sink.get_driver_pins()) {
      if (d == replacement) {
        return;
      }
      released.push_back(d);
    }
    gu::drop_drivers(sink);
    replacement.connect_sink(sink);
  }
  // Only logic that lost its last consumer HERE is deleted: a cell that was
  // already dangling is not this pass's business.
  void sweep() {
    absl::flat_hash_set<Node> deleted;
    while (!released.empty()) {
      const auto p = released.back();
      released.pop_back();
      if (p.is_invalid() || p.is_const() || gu::is_graph_input_pin(p) || gu::is_graph_output_pin(p)) {
        continue;
      }
      const auto n = p.get_master_node();
      if (deleted.contains(n) || n.is_invalid() || gu::is_builtin_node(n)) {
        continue;
      }
      const auto op = gu::type_op_of(n);
      if (!Ntype::is_comb(op) || op == Ntype_op::Clock_cell || n.has_out_edges()) {
        continue;
      }
      for (const auto& in_pin : n.inp_sorted_pins()) {
        for (const auto& d : in_pin.get_driver_pins()) {
          released.push_back(d);
        }
      }
      deleted.insert(n);
      n.del_node();
    }
  }
};
// A per-bit arm fact holds only while its arm is selected, so it may rewrite
// that arm's INPUT and nothing else: never the arm's driver (it has other
// consumers) and never the mux output. equal/complement read the OTHER arm's
// ORIGINAL driver, captured before any sink of this mux is rewired.
//
// Each arm is rebuilt from maximal runs of bits with the same choice
// (unchanged, 0, 1, = other arm, ~other arm). One run over the whole width is
// a single pin (a constant, the other arm's driver, or its complement); mixed
// runs become a Concat of slices, most significant lane first.
struct Arm_builder {
  hhds::Graph& g;
  Node         mux;
  Node         node(Ntype_op op) {
    auto n = gu::create_typed_node(g, op);
    if (gu::has_color(mux)) {
      gu::set_color(n, gu::color_of(mux));
    }
    gu::carry_srcid(mux, n);
    return n;
  }
  Pin output(Node n, int width) {
    auto p = n.create_driver_pin(0);
    gu::set_ubits(p, width);
    return p;
  }
  Pin ones(int width) { return gu::create_const(g, *Dlop::get_mask_value(width)); }
  Pin slice(const Pin& value, int lo, int hi) {
    auto n = gu::create_get_mask(g, value, lo, hi);
    if (gu::has_color(mux)) {
      gu::set_color(n, gu::color_of(mux));
    }
    gu::carry_srcid(mux, n);
    return output(n, hi - lo);
  }
  Pin complement(const Pin& value, int lo, int hi) {
    auto n = node(Ntype_op::Xor);
    slice(value, lo, hi).connect_sink(gu::setup_sink_pid(n, 0));  // a banked op: each call appends an operand
    ones(hi - lo).connect_sink(gu::setup_sink_pid(n, 0));
    return output(n, hi - lo);
  }
  // lanes are least significant first: (value, width).
  Pin concat(const std::vector<std::pair<Pin, int>>& lanes, int width) {
    auto n = node(Ntype_op::Concat);
    for (size_t i = 0; i < lanes.size(); ++i) {
      const auto& [value, w] = lanes[lanes.size() - 1 - i];
      value.connect_sink(gu::setup_sink_pid(n, static_cast<hhds::Port_id>(2 * i)));
      gu::create_const(g, *Dlop::create_integer(w)).connect_sink(gu::setup_sink_pid(n, static_cast<hhds::Port_id>(2 * i + 1)));
    }
    return output(n, width);
  }
};

// What bit `bit` of `pin` reads inside the mux's own region, through the
// wiring a rewrite builds (Get_mask windows, Concat lanes, Xor with a
// constant, Not, constants): a constant, or a (driver, bit, complemented)
// triple. A fact whose arm bit already reads its target this way was applied
// before. The proof cone sees across regions, so a relation that is
// structural there (an arm computed as ~x in another region) is still new to
// the mux's region, which sees two unrelated inputs.
struct Local_bit {
  Pin                 pin;
  int                 bit      = 0;
  bool                inverted = false;
  std::optional<bool> value;
};
Local_bit local_bit(Pin pin, int bit, const Node& mux) {
  bool inverted = false;
  for (int guard = 0; guard < 256 && !pin.is_invalid(); ++guard) {
    if (pin.is_const()) {
      const auto& v = gu::const_of(pin);
      if (v.has_unknowns()) {
        break;
      }
      return {.value = v.bit_test(bit) != inverted};
    }
    const int w = gu::bits_of(pin);
    const auto n = pin.get_master_node();
    if ((w > 0 && bit >= w) || !gu::has_color(mux) || !gu::has_color(n) || gu::color_of(n) != gu::color_of(mux)) {
      break;
    }
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Get_mask) {
      const auto mask   = gu::get_driver_of_sink_name(n, "mask");
      const auto window = mask.is_const() ? gu::mask_window_of(gu::const_of(mask)) : std::nullopt;
      if (!window) {
        break;
      }
      pin = gu::get_driver_of_sink_name(n, "a");
      bit += window->first;
      continue;
    }
    if (op == Ntype_op::Concat) {
      const auto lanes = gu::concat_lanes(n);
      const auto lane  = std::ranges::find_if(lanes, [&](const auto& l) { return bit >= l.offset && bit < l.offset + l.width; });
      if (lane == lanes.end()) {
        break;
      }
      pin = lane->value;
      bit -= lane->offset;
      continue;
    }
    std::vector<Pin> ins;
    for (const auto& sink : n.inp_sorted_pins()) {
      ins.push_back(sink.get_driver_pin());
    }
    if (op == Ntype_op::Not && ins.size() == 1) {
      pin      = ins.front();
      inverted = !inverted;
      continue;
    }
    if (op == Ntype_op::Xor && ins.size() == 2 && ins[0].is_const() != ins[1].is_const()) {
      const auto& k = gu::const_of(ins[0].is_const() ? ins[0] : ins[1]);
      if (k.has_unknowns()) {
        break;
      }
      inverted ^= k.bit_test(bit);
      pin = ins[0].is_const() ? ins[1] : ins[0];
      continue;
    }
    break;
  }
  return {pin, bit, inverted, std::nullopt};
}

// True when arm `f.arm` bit `f.bit` already reads what the fact states.
bool applied(const Mux_fact& f, const Arms& arms, const Node& mux) {
  const auto self = local_bit(arms.values[f.arm], f.bit, mux);
  if (f.other < 0) {
    return self.value && *self.value == (f.kind == Mux_fact::Kind::one);
  }
  const auto other      = local_bit(arms.values[f.other], f.bit, mux);
  const bool complement = f.kind == Mux_fact::Kind::complement;
  if (self.value || other.value) {
    return self.value && other.value && (*self.value != *other.value) == complement;
  }
  return self.pin == other.pin && self.bit == other.bit && (self.inverted != other.inverted) == complement;
}

void apply_mux_facts(Select_rewrite& rw, const Node& n, const std::vector<Mux_fact>& facts, Mux_satopt& stats) {
  const auto arms  = arms_of(n);
  const int  count = static_cast<int>(arms.values.size());
  const int  width = gu::bits_of(n.create_driver_pin(0));
  if (count == 0 || width <= 0) {
    return;  // an unstamped output has no bit range to rewrite
  }
  // First fact per (arm, bit) wins, as the blaster applied them.
  std::vector<std::vector<int>> choice(count, std::vector<int>(width, -1));
  for (size_t i = 0; i < facts.size(); ++i) {
    const auto& f = facts[i];
    if (f.arm < 0 || f.arm >= count || f.bit < 0 || f.bit >= width || f.other >= count) {
      continue;
    }
    if (choice[f.arm][f.bit] < 0 && applied(f, arms, n)) {
      choice[f.arm][f.bit] = -2;  // already read that way: nothing to rewrite
    }
    if (choice[f.arm][f.bit] == -1) {
      choice[f.arm][f.bit] = static_cast<int>(i);
    }
  }
  // An arm bit may read another arm's bit only while that bit stays as is:
  // the rewrite reads the ORIGINAL arm drivers, so reading a bit that is
  // itself rewritten would no longer match the two arms. Scanning in arm
  // order drops the lower arm of a mutually equal pair (the higher arm then
  // reads the lower) and any read of a bit proven constant.
  for (int a = 0; a < count; ++a) {
    for (int b = 0; b < width; ++b) {
      const int c = choice[a][b];
      if (c >= 0 && facts[c].other >= 0 && choice[facts[c].other][b] >= 0) {
        choice[a][b] = -1;
      }
    }
  }
  const int explicit_arms = arms.hot && !arms.controls.empty() && arms.controls.back().is_invalid() ? count - 1 : count;
  Arm_builder build{rw.g, n};
  bool        changed = false;
  for (int a = 0; a < count; ++a) {
    // A constant arm is rewritten too: reading the other arm's bit makes that
    // mux bit independent of the select.
    // Maximal runs of the same (kind, other); kind -1 = unchanged.
    struct Run {
      int lo, hi, kind, other;
    };
    std::vector<Run> runs;
    int              rewritten = 0;
    for (int b = 0; b < width; ++b) {
      const int c     = choice[a][b];
      const int kind  = c < 0 ? -1 : static_cast<int>(facts[c].kind);
      const int other = c < 0 ? -1 : facts[c].other;
      rewritten += c >= 0;
      if (!runs.empty() && runs.back().kind == kind && runs.back().other == other && runs.back().hi == b) {
        runs.back().hi = b + 1;
      } else {
        runs.push_back({b, b + 1, kind, other});
      }
    }
    if (rewritten == 0) {
      continue;
    }
    const auto run_pin = [&](const Run& r) -> Pin {
      switch (static_cast<Mux_fact::Kind>(r.kind)) {
        case Mux_fact::Kind::zero: return gu::create_const(rw.g, *Dlop::create_integer(0));
        case Mux_fact::Kind::one: return build.ones(r.hi - r.lo);
        case Mux_fact::Kind::equal:
          return r.lo == 0 && r.hi == width ? arms.values[r.other] : build.slice(arms.values[r.other], r.lo, r.hi);
        case Mux_fact::Kind::complement: return build.complement(arms.values[r.other], r.lo, r.hi);
      }
      return {};
    };
    Pin replacement;
    if (runs.size() == 1) {
      replacement = run_pin(runs.front());
    } else {
      std::vector<std::pair<Pin, int>> lanes;
      for (const auto& r : runs) {
        lanes.emplace_back(r.kind < 0 ? build.slice(arms.values[a], r.lo, r.hi) : run_pin(r), r.hi - r.lo);
      }
      replacement = build.concat(lanes, width);
    }
    const auto pid = !arms.hot ? static_cast<hhds::Port_id>(a + 1)
                     : a < explicit_arms ? static_cast<hhds::Port_id>(2 * a + 1)
                                         : static_cast<hhds::Port_id>(2 * explicit_arms);
    rw.replace(n, pid, replacement);
    ++stats.arms;
    stats.bits += static_cast<uint64_t>(rewritten);
    changed = true;
  }
  stats.muxes += changed;
}

void apply_selects(Select_rewrite& rw, const Node& n, const std::map<int, bool>& known, Select_satopt& stats) {
  const auto op = gu::type_op_of(n);
  if (op == Ntype_op::Flop) {
    // A tied enable, not a removed one: a driverless sink is a malformed body.
    rw.tie(n, 4, known.at(0));
    ++stats.enables;
    return;
  }
  if (op == Ntype_op::Mux) {
    const bool value = known.at(0);
    rw.tie(n, 0, value);
    rw.tie(n, value ? 1 : 2, 0);
    ++stats.muxes;
    return;
  }
  // Priority semantics (the reference on an overlap): the first always-on
  // control wins, so every later arm and the fallback are never selected.
  const auto inputs = gu::hotmux_inputs(n);
  const int  arms   = static_cast<int>(inputs.arms.size());
  int        first  = arms;
  for (const auto& [control, value] : known) {
    if (value && control < first) {
      first = control;
    }
  }
  for (int i = 0; i < arms; ++i) {
    const auto found = known.find(i);
    if (i == first) {
      rw.tie(n, static_cast<hhds::Port_id>(2 * i), 1);
      ++stats.hotmux_arms;
    } else if (i > first || (found != known.end() && !found->second)) {
      rw.tie(n, static_cast<hhds::Port_id>(2 * i), 0);
      rw.tie(n, static_cast<hhds::Port_id>(2 * i + 1), 0);
      ++stats.hotmux_arms;
    }
  }
  if (first < arms && !inputs.fallback.is_invalid()) {
    rw.tie(n, static_cast<hhds::Port_id>(2 * arms), 0);
  }
}
}  // namespace

std::string satopt_constant_key(const Dlop& value) {
  constexpr char hex[] = "0123456789abcdef";
  std::string    key;
  for (unsigned char byte : value.serialize()) {
    key += hex[byte >> 4];
    key += hex[byte & 15];
  }
  return key;
}

std::string satopt_source_key(hhds::Graph* graph) { return source_key(graph, false); }

struct Satopt_seeds::Impl {
  Seeds seeds;
};
Satopt_seeds::Satopt_seeds() : impl_(std::make_unique<Impl>()) {}
Satopt_seeds::~Satopt_seeds() = default;
std::optional<std::array<Dlop, 8>> Satopt_seeds::sample(const Pin& pin) {
  try {
    return impl_->seeds.get(pin);
  } catch (const Unsupported&) {
    return std::nullopt;
  }
}

std::shared_ptr<const Satopt_result> satopt(hhds::Graph* graph, std::string_view cache_dir, bool all_regions) {
  std::lock_guard lock(saved_mutex);
  const auto      source = source_key(graph, false);
  const auto      colors = source_key(graph, true);
  const auto      path   = cache_path(cache_dir, graph->get_name());
  auto&           row    = saved[std::string(graph->get_name())];
  if (!row.result) {
    row = read_cache(path);
  }
  if (row.result && row.source == source && (row.all || (!all_regions && row.colors == colors))) {
    auto reused    = std::make_shared<Satopt_result>(*row.result);
    reused->reused = true;
    std::print("[pass.satopt] {}: reused {} proven mux facts (exact definition match)\n", graph->get_name(), reused->proven);
    return reused;
  }
  auto                                                          result = std::make_shared<Satopt_result>();
  Seeds                                                         seeds;
  std::vector<std::pair<Mux_fact, std::shared_ptr<const Arms>>> candidates;
  for (const auto n : graph->body().nodes()) {
    const auto op = gu::type_op_of(n);
    if (op != Ntype_op::Mux && op != Ntype_op::Hotmux) {
      continue;
    }
    const auto arms = arms_of(n);
    if (arms.values.empty() || (!all_regions && !crosses(n, arms))) {
      continue;
    }
    const auto shared_arms = std::make_shared<const Arms>(arms);
    const auto add         = [&](int arm, int bit, Mux_fact::Kind kind, int other) {
      if (result->candidates >= 1000000 || candidates.size() >= 100000) {
        return;
      }
      Mux_fact f{static_cast<uint64_t>(n.get_debug_nid()), arm, bit, kind, other};
      ++result->candidates;
      try {
        if (seeds.survives(arms, f)) {
          candidates.emplace_back(f, shared_arms);
        }
      } catch (const Unsupported&) {
      }
    };
    for (int bit = 0;
         bit < std::min(65536, width(n.create_driver_pin(0))) && result->candidates < 1000000 && candidates.size() < 100000;
         ++bit) {
      for (int arm = 0; arm < static_cast<int>(arms.values.size()) && result->candidates < 1000000 && candidates.size() < 100000;
           ++arm) {
        if (!arms.values[arm].is_const()) {
          add(arm, bit, Mux_fact::Kind::zero, -1);
          add(arm, bit, Mux_fact::Kind::one, -1);
        }
        for (int other = 0;
             other < static_cast<int>(arms.values.size()) && result->candidates < 1000000 && candidates.size() < 100000;
             ++other) {
          if (other != arm) {
            add(arm, bit, Mux_fact::Kind::equal, other);
            add(arm, bit, Mux_fact::Kind::complement, other);
          }
        }
      }
    }
  }
  result->survivors = candidates.size();
  const auto save   = [&] {
    row = {source, colors, all_regions, result};
    write_cache(path, row);
  };
  if (candidates.empty()) {
    save();
    return result;
  }
  const auto proven = prove_zero([&](Gates& gates, Cone& cone) {
    std::vector<Abc_Obj_t*> refutes;
    for (const auto& [fact, shared_arms] : candidates) {
      const auto& arms = *shared_arms;
      try {
        auto* c        = cone.condition(arms, fact.arm);
        auto* a        = cone.bit(arms.values[fact.arm], fact.bit);
        auto* expected = fact.kind == Mux_fact::Kind::one ? gates.one() : gates.zero();
        if (fact.other >= 0) {
          expected = cone.bit(arms.values[fact.other], fact.bit);
          if (fact.kind == Mux_fact::Kind::complement) {
            expected = gates.inv(expected);
          }
        }
        refutes.push_back(gates.and_(c, gates.xor_(a, expected)));
      } catch (const Unsupported&) {
        refutes.push_back(nullptr);
      }
    }
    return refutes;
  });
  if (!proven) {
    return result;
  }
  for (size_t i = 0; i < proven->size(); ++i) {
    if ((*proven)[i]) {
      result->mux[candidates[i].first.node].push_back(candidates[i].first);
      ++result->proven;
    }
  }
  std::print("[pass.satopt] {}: {} candidates, {} seed survivors, {} proven mux facts\n",
             graph->get_name(),
             result->candidates,
             result->survivors,
             result->proven);
  save();
  return result;
}
Select_satopt optimize_selects(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string_view cache_dir) {
  std::lock_guard lock(saved_mutex);
  Select_satopt   stats;
  for (const auto& graph : graphs) {
    if (!graph) {
      continue;
    }
    std::map<uint64_t, std::pair<Node, std::vector<Pin>>> cells;
    for (const auto n : graph->body().nodes()) {
      if (auto controls = selectors(n); !controls.empty()) {
        cells.emplace(static_cast<uint64_t>(n.get_debug_nid()), std::make_pair(n, std::move(controls)));
      }
    }
    if (cells.empty()) {
      continue;
    }
    const auto source = source_key(graph.get(), false);
    const auto path   = cache_path(cache_dir, graph->get_name(), "-select");
    auto&      row    = saved_selects[std::string(graph->get_name())];
    if (row.source != source || !children_match(graph.get(), row)) {
      row = read_selects(path);
    }
    if (row.source == source && children_match(graph.get(), row)) {
      stats.reused += row.facts.size();
    } else {
      Seeds                                    seeds(true);
      std::vector<std::pair<Select_fact, Pin>> candidates;
      std::vector<Pin>                         roots;
      for (const auto& [nid, cell] : cells) {
        roots.insert(roots.end(), cell.second.begin(), cell.second.end());
      }
      for (const auto& p : fanin_forward(graph.get(), std::move(roots))) {
        try {
          seeds.get(p);
        } catch (const Unsupported&) {
        }
      }
      for (const auto& [nid, cell] : cells) {
        const auto& controls = cell.second;
        for (size_t i = 0; i < controls.size(); ++i) {
          // An unstamped width would make the condition read only bit 0.
          const auto& c = controls[i];
          if (c.is_invalid() || c.is_const() || gu::bits_of(c) <= 0) {
            continue;
          }
          ++stats.candidates;
          try {
            const auto& values = seeds.get(c);
            const bool  value  = !values[0].is_known_zero();
            if (std::all_of(values.begin(), values.end(), [&](const Dlop& v) { return !v.is_known_zero() == value; })) {
              candidates.push_back({{nid, static_cast<int>(i), value}, c});
            }
          } catch (const Unsupported&) {
          }
        }
      }
      stats.survivors += candidates.size();
      // cvc5 answers true/false at word level, through the same encoder whole-
      // design LEC checks this rewrite with, and descends into called
      // submodules (virtual flat). Only cvc5: ABC maps every region anyway, so
      // a satopt proof pays off where it goes beyond what ABC finds. Unknown (a
      // body-less Sub in the cone, an unsupported cell, a budget-out) leaves the
      // selector alone.
      formal::Prover prover(graph.get(),
                            {.budget_k                 = 256,
                             .cone_max                 = 50000,
                             .memory_as_symbols        = true,
                             .reject_unknown_constants = true,
                             .descend_subs             = true});
      row = {source, {}, {}};
      for (const auto& [fact, control] : candidates) {
        if ((fact.value ? prover.is_true(control) : prover.is_false(control)).verdict == formal::Verdict::Proven) {
          row.facts.push_back(fact);
        }
      }
      for (auto* def : prover.descended()) {
        row.children.emplace_back(std::string(def->get_name()), source_key(def, false));
      }
      write_selects(path, row);
    }
    stats.proven += row.facts.size();
    std::map<uint64_t, std::map<int, bool>> known;
    for (const auto& f : row.facts) {
      known[f.node][f.control] = f.value;
    }
    Select_rewrite rw{*graph, {}};
    for (const auto& [nid, controls] : known) {
      if (auto it = cells.find(nid); it != cells.end()) {
        apply_selects(rw, it->second.first, controls, stats);
      }
    }
    rw.sweep();
  }
  std::print(
      "[pass.satopt] select: {} candidates, {} seed survivors, {} proven constant ({} reused): {} mux, {} hotmux arm, {} flop "
      "enable\n",
      stats.candidates,
      stats.survivors,
      stats.proven,
      stats.reused,
      stats.muxes,
      stats.hotmux_arms,
      stats.enables);
  return stats;
}
uint64_t drop_dead_logic(hhds::Graph* graph) {
  if (graph == nullptr) {
    return 0;
  }
  Select_rewrite rw{*graph, {}};
  uint64_t       before = 0;
  for (const auto n : graph->body().nodes()) {
    ++before;
    const auto op = gu::type_op_of(n);
    if (Ntype::is_comb(op) && op != Ntype_op::Clock_cell && !n.has_out_edges()) {
      rw.released.push_back(n.create_driver_pin(0));
    }
  }
  if (rw.released.empty()) {
    return 0;
  }
  rw.sweep();
  uint64_t after = 0;
  for (const auto n : graph->body().nodes()) {
    (void)n;
    ++after;
  }
  return before - after;
}

Mux_satopt optimize_muxes(hhds::Graph* graph, std::string_view cache_dir, bool all_regions) {
  Mux_satopt stats;
  const auto result = satopt(graph, cache_dir, all_regions);
  stats.candidates  = result->candidates;
  stats.survivors   = result->survivors;
  stats.proven      = result->proven;
  stats.reused      = result->reused;
  if (result->mux.empty()) {
    return stats;
  }
  // Resolve every subject before mutating: the rewrite adds nodes. Facts are
  // keyed by node id in a std::map, so the application order (and the ids of
  // the new nodes) is deterministic.
  // A reused all-regions proof (explicit pass.satopt) still applies only to
  // the muxes this call analyzes: inside one region a fact does not change
  // the region's function, so it is left to the mapper.
  absl::flat_hash_map<uint64_t, Node> subjects;
  for (const auto n : graph->body().nodes()) {
    if (result->mux.contains(static_cast<uint64_t>(n.get_debug_nid())) && (all_regions || crosses(n, arms_of(n)))) {
      subjects.emplace(static_cast<uint64_t>(n.get_debug_nid()), n);
    }
  }
  Select_rewrite rw{*graph, {}};
  for (const auto& [nid, facts] : result->mux) {
    if (auto it = subjects.find(nid); it != subjects.end()) {
      apply_mux_facts(rw, it->second, facts, stats);
    }
  }
  rw.sweep();
  std::print("[pass.satopt] {}: rewrote {} arm(s) of {} mux(es), {} bit(s) from proven mux facts\n",
             graph->get_name(),
             stats.arms,
             stats.muxes,
             stats.bits);
  return stats;
}
}  // namespace livehd::abc
