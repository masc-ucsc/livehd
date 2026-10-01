// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "clock_gates.hpp"

#include <algorithm>
#include <format>
#include <functional>

#include "latch_contract.hpp"
#include "node_util.hpp"

namespace livehd::synth {
namespace gu = livehd::graph_util;

hhds::Pin_class clock_root(hhds::Pin_class p, const absl::flat_hash_set<hhds::Pin_class>& boundaries) {
  for (int guard = 0; guard < 64 && !p.is_invalid(); ++guard) {  // guard: cycle net, > any sane hierarchy depth
    if (boundaries.contains(p) || p.is_const()) {
      break;
    }
    auto m = p.get_master_node();
    if (gu::type_op_of(m) == Ntype_op::Sext && std::max(1, gu::real_width(p)) == 1) {
      auto a = gu::get_driver_of_sink_name(m, "a");
      if (a.is_invalid() || std::max(1, gu::real_width(a)) != 1) {
        break;
      }
      p = a;
      continue;
    }
    if (gu::type_op_of(m) != Ntype_op::Get_mask) {
      break;
    }
    auto a    = gu::get_driver_of_sink_name(m, "a");
    auto mask = gu::get_driver_of_sink_name(m, "mask");
    if (a.is_invalid() || std::max(1, gu::real_width(a)) != 1 || mask.is_invalid() || !mask.is_const()
        || !gu::const_of(mask).bit_test(0)) {
      break;
    }
    p = a;  // get_mask(bit0) of a 1-bit wire == the wire
  }
  return p;
}

Clock_gates analyze_clock_gates(const livehd::partition::Region_body& rb) {
  Clock_gates                           result;
  absl::flat_hash_set<hhds::Node_class> region(rb.nodes.begin(), rb.nodes.end());
  absl::flat_hash_set<hhds::Pin_class>  region_in_name;
  for (const auto& p : rb.inputs) {
    region_in_name.insert(p.src_driver);
  }
  auto&      icg_absorbed = result.absorbed;
  auto&      icg_of_gclk  = result.by_output;
  auto&      icg_rejected = result.rejected;
  const auto real_width   = [](hhds::Pin_class p) { return std::max(1, gu::real_width(p)); };
  const auto peel_clock   = [&](hhds::Pin_class p) { return clock_root(p, region_in_name); };
  // `root`'s consumers, through 1-bit identities (collected into `idents`),
  // are all `only`; false on anything else.
  auto consumers_only     = [&](const hhds::Pin_class& root, const hhds::Node_class& only, std::vector<hhds::Node_class>& idents) {
    std::vector<hhds::Pin_class> work{root};
    for (size_t i = 0; i < work.size(); ++i) {
      if (work.size() > 64) {
        return false;  // guard: an enable latch's Q does not fan through dozens of identities
      }
      for (const auto& e : work[i].out_edges()) {
        const auto sn = e.sink.get_master_node();
        if (sn == only) {
          continue;
        }
        const auto op = gu::type_op_of(sn);
        if (region.contains(sn) && (op == Ntype_op::Get_mask || op == Ntype_op::Sext)) {
          auto out = sn.get_driver_pin(0);
          if (!out.is_invalid() && peel_clock(out) == root) {
            idents.push_back(sn);
            for (const auto& o : sn.out_sorted_pins()) {
              work.push_back(o);
            }
            continue;
          }
        }
        return false;
      }
    }
    return true;
  };
  // gate output -> 1: being recognized (a cycle), 2: done (see icg_of_gclk /
  // icg_rejected for the answer).
  absl::flat_hash_map<hhds::Pin_class, int>          visiting;
  std::function<std::string(const hhds::Pin_class&)> recognize = [&](const hhds::Pin_class& gclk) -> std::string {
    if (icg_of_gclk.contains(gclk)) {
      return {};
    }
    if (auto it = icg_rejected.find(gclk); it != icg_rejected.end()) {
      return it->second;
    }
    if (visiting[gclk] == 1) {
      return "the clock gate is part of a gate cycle";
    }
    visiting[gclk] = 1;
    auto why       = [&]() -> std::string {
      auto g = gclk.get_master_node();
      if (gu::type_op_of(g) == Ntype_op::Or) {
        return "the clock is an OR (`clk | ~latch`, the active-low gate flavour, or other OR logic), which no "
               "latch_posedge integrated clock-gate cell implements";
      }
      if (gu::type_op_of(g) != Ntype_op::And || real_width(gclk) != 1) {
        return "the clock is computed by logic that is not a latch-based clock gate (`clk & latch`)";
      }
      std::vector<hhds::Pin_class> live;
      for (const auto& in_pin : g.inp_sorted_pins()) {
        for (auto drv : in_pin.get_driver_pins()) {
          if (drv.is_const()) {
            if (!gu::const_of(drv).bit_test(0)) {
              return "the clock gate is tied off by a constant operand";
            }
            continue;
          }
          live.push_back(drv);
        }
      }
      if (live.size() != 2) {
        return "the clock gate AND does not have exactly two operands (a clock and one latched enable)";
      }
      hhds::Pin_class clk_op, clk_src, latch_q;
      for (const auto& op : live) {
        const auto root = peel_clock(op);
        if (!root.is_invalid() && !root.is_const() && !region_in_name.contains(root)
            && gu::type_op_of(root.get_master_node()) == Ntype_op::Latch && root.get_port_id() == 0 && latch_q.is_invalid()) {
          latch_q = root;
        } else {
          clk_op  = op;
          clk_src = root;
        }
      }
      if (latch_q.is_invalid() || clk_src.is_invalid() || clk_src.is_const()) {
        return "the clock gate is not `<clock> & <latch>`";
      }
      int32_t parent = -1;
      if (!region_in_name.contains(clk_src)) {
        // A gate chain: the reference clock must itself be a recognized gate.
        if (auto pwhy = recognize(clk_src); !pwhy.empty()) {
          return std::format("the clock gate's reference clock is neither a region input nor a mappable clock gate ({})", pwhy);
        }
        parent = icg_of_gclk.at(clk_src);
      } else if (real_width(clk_src) != 1) {
        return "the clock gate's reference clock is wider than one bit";
      }
      const auto latch = latch_q.get_master_node();
      if (real_width(latch_q) != 1) {
        return "the clock gate's enable latch is wider than one bit";
      }
      for (std::string_view pin : {"reset_pin", "initial"}) {
        if (auto d = gu::get_driver_of_sink_name(latch, pin); !d.is_invalid()) {
          return "the clock gate's enable latch has a reset or power-on value an ICG cell does not have";
        }
      }
      const auto en  = gu::get_driver_of_sink_name(latch, "enable");
      const auto din = gu::get_driver_of_sink_name(latch, "din");
      if (en.is_invalid() || din.is_invalid()) {
        return "the clock gate's enable latch has no enable or data input";
      }
      bool active_low = false;
      if (auto pc = gu::get_driver_of_sink_name(latch, "posclk"); pc.is_const()) {
        active_low = gu::const_of(pc).is_known_false();
      } else if (!pc.is_invalid()) {
        return "the clock gate's enable latch has a non-constant enable polarity";
      }
      const auto er = livehd::latch_contract::control_root(en, /*stop_at_clock_cell=*/true);
      const auto cr = livehd::latch_contract::control_root(clk_op, /*stop_at_clock_cell=*/true);
      if (cr.net.is_invalid() || er.net.is_invalid() || !(er.net == cr.net)) {
        return "the clock gate's latch is not enabled by the gate's own clock";
      }
      if (cr.inverted) {
        return "the clock gate ANDs the INVERTED clock (a falling-edge gate)";
      }
      if (er.inverted == active_low) {
        return "the clock gate's latch is transparent while the clock is HIGH (an AND gate needs it transparent while "
               "LOW)";
      }
      // The latch Q feeds only this AND (through identities): no feedback
      // into its own D, no data use of the held enable.
      std::vector<hhds::Node_class> q_idents;
      if (!consumers_only(latch_q, g, q_idents)) {
        return "the clock gate's enable latch is also read by other logic";
      }
      icg_absorbed.insert(latch);
      icg_absorbed.insert(g);
      icg_absorbed.insert(q_idents.begin(), q_idents.end());
      Icg_gate gate;
      gate.latch   = latch;
      gate.gclk    = gclk;
      gate.clk_src = clk_src;
      gate.parent  = parent;
      gate.en_drv  = din;
      gate.name    = gu::wire_name(latch_q);
      if (gate.name.empty()) {
        gate.name = std::format("icg{}", latch.get_debug_nid());
      }
      icg_of_gclk.emplace(gclk, static_cast<int32_t>(result.gates.size()));
      result.gates.push_back(std::move(gate));
      return {};
    }();
    visiting[gclk] = 2;
    if (!why.empty()) {
      icg_rejected.emplace(gclk, why);
    }
    return why;
  };
  for (const auto& n : rb.nodes) {
    if (gu::type_op_of(n) == Ntype_op::Flop) {
      // Every register on a derived clock asks, so its rejection reason is
      // on record for the derived-clock-native report.
      const auto c = peel_clock(gu::get_driver_of_sink_name(n, "clock_pin"));
      if (!c.is_invalid() && !c.is_const() && !region_in_name.contains(c)) {
        (void)recognize(c);
      }
      continue;
    }
    // A gate clocking only memories, data latches or other gates maps too.
    if (gu::type_op_of(n) == Ntype_op::And && !icg_absorbed.contains(n)) {
      auto out = n.get_driver_pin(0);
      if (!out.is_invalid() && real_width(out) == 1) {
        bool latch_operand = false;
        for (const auto& in_pin : n.inp_sorted_pins()) {
          for (const auto& drv : in_pin.get_driver_pins()) {
            const auto r  = peel_clock(drv);
            latch_operand = latch_operand
                            || (!r.is_invalid() && !r.is_const() && !region_in_name.contains(r)
                                && gu::type_op_of(r.get_master_node()) == Ntype_op::Latch);
          }
        }
        if (latch_operand) {
          (void)recognize(out);
        }
      }
    }
  }
  return result;
}

}  // namespace livehd::synth
