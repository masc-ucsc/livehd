// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "literal_stats.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <optional>
#include <map>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "latch_contract.hpp"
#include "node_util.hpp"
#include "tmap.hpp"

namespace livehd::usyn {

namespace {

namespace gu = livehd::graph_util;

// Input classes, cumulative scenario masks over them.
enum Cls : uint8_t { c_flop = 1, c_port = 2, c_latch_low = 4, c_latch_other = 8, c_other = 16, c_stat = 32, c_stat_lat = 64 };
constexpr std::array<const char*, 4> kScenario{"reg", "reg_lat", "stat", "stat_lat"};
constexpr std::array<uint8_t, 4>     kEligible{c_flop | c_port,
                                           c_flop | c_port | c_latch_low,
                                           c_flop | c_port | c_stat,
                                           c_flop | c_port | c_latch_low | c_stat | c_stat_lat};
constexpr uint32_t                   kMaxInputs   = 8;
constexpr uint32_t                   kMaxDepth    = 3;
constexpr uint64_t                   kMatchLeaves = 200000;

// Does `value == want` imply that the raw design clock (a graph input) is LOW?
// Peels identities and inversions (control_root), and descends ANDs (want=1:
// any conjunct suffices) / ORs (want=0). A gated clock does not qualify: a
// gated-off `~gclk` enable is open while the raw clock is high.
bool implies_clock_low(hhds::Pin_class value, bool want, int depth = 0) {
  if (value.is_invalid() || value.is_const() || depth > 6) {
    return false;
  }
  const auto root = livehd::latch_contract::control_root(value, /*stop_at_clock_cell=*/true);
  if (root.net.is_invalid() || root.net.is_const()) {
    return false;
  }
  const bool w = want != root.inverted;  // the root must equal `w`
  if (gu::is_graph_input_pin(root.net)) {
    return !w && gu::bits_of(root.net) <= 1;  // root == 0 is the clock low (only a 1-bit raw port can be a clock)
  }
  const auto n  = root.net.get_master_node();
  const auto op = gu::type_op_of(n);
  if ((op == Ntype_op::And && w) || (op == Ntype_op::Or && !w)) {
    for (const auto& in : gu::inp_sink_drivers(n)) {
      if (implies_clock_low(in.driver, w, depth + 1)) {
        return true;
      }
    }
  }
  return false;
}

// Closed (stable) during the first half: transparent only while the raw clock is LOW.
bool latch_transparent_low(const hhds::Node_class& latch) {
  const auto en = gu::get_driver_of_sink_name(latch, "enable");
  if (en.is_invalid() || en.is_const()) {
    return false;
  }
  bool active_low = false;
  if (auto pc = gu::get_driver_of_sink_name(latch, "posclk"); pc.is_const()) {
    active_low = gu::const_of(pc).is_known_false();
  } else if (!pc.is_invalid()) {
    return false;
  }
  return implies_clock_low(en, /*want=*/!active_low);
}

// A driver's state class. A child-instance output is followed into the
// child's definition (Pyrope keeps flops/latch pairs in small modules).
uint8_t classify_driver(const hhds::Pin_class& d, int depth = 0) {
  if (d.is_invalid() || d.is_const()) {
    return c_port;  // a constant lane is trivially stable
  }
  if (gu::is_graph_input_pin(d)) {
    return c_port;
  }
  const auto n  = d.get_master_node();
  const auto op = gu::type_op_of(n);
  if (op == Ntype_op::Flop || op == Ntype_op::Fflop) {
    return c_flop;
  }
  if (op == Ntype_op::Latch) {
    return latch_transparent_low(n) ? c_latch_low : c_latch_other;
  }
  if ((op == Ntype_op::Get_mask || op == Ntype_op::Sext) && depth < 8) {
    return classify_driver(gu::first_value_driver(n), depth + 1);  // a bit/width select of a state value
  }
  if (op == Ntype_op::Sub && depth < 8) {
    const auto child = n.get_subnode_graph();
    const auto cio   = n.get_subnode_io();
    if (child && cio) {
      for (const auto& decl : cio->get_output_pin_decls()) {
        if (decl.port_id != d.get_port_id()) {
          continue;
        }
        const auto out = child->get_output_pin(decl.name);
        const auto drv = out.is_invalid() ? hhds::Pin_class{} : out.get_driver_pin();
        if (!drv.is_invalid() && gu::is_graph_input_pin(drv)) {
          return c_other;  // a pass-through: the parent wiring would decide
        }
        return drv.is_invalid() ? static_cast<uint8_t>(c_other) : classify_driver(drv, depth + 1);
      }
    }
  }
  return c_other;
}

// A region input driven by combinational logic in another region: trace its
// source-graph cone (bounded) and classify by support. All leaves registered
// (flop/port) -> c_stat; registered or first-half-stable latches -> c_stat_lat.
struct Cone_classifier {
  absl::flat_hash_map<hhds::Class_index, uint8_t> memo;
  uint64_t                                        budget = 0;
  uint8_t operator()(const hhds::Pin_class& d) {
    const auto direct = classify_driver(d);
    if (direct != c_other || d.is_invalid() || d.is_const()) {
      return direct;
    }
    const auto n  = d.get_master_node();
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Sub || op == Ntype_op::Memory || gu::is_type_register(n)) {
      return c_other;  // an opaque/unclassified instance or memory read
    }
    if (const auto it = memo.find(n.get_class_index()); it != memo.end()) {
      return it->second;
    }
    memo[n.get_class_index()] = c_other;  // cycles and budget refusals stay "other"
    if (budget == 0) {
      return c_other;
    }
    --budget;
    bool any_latch = false;
    for (const auto& in : gu::inp_sink_drivers(n)) {
      const auto c = (*this)(in.driver);
      if ((c & (c_flop | c_port | c_stat)) != 0) {
        continue;
      }
      if ((c & (c_latch_low | c_stat_lat)) != 0) {
        any_latch = true;
        continue;
      }
      return c_other;
    }
    const uint8_t result = any_latch ? c_stat_lat : c_stat;
    memo[n.get_class_index()] = result;
    return result;
  }
};

// Ternary formula evaluation: 0, 1, 2 = unknown.
uint8_t eval3(const Gate_formula& f, const std::array<uint8_t, 16>& var) {
  std::vector<uint8_t> v;
  v.reserve(f.nodes.size());
  for (const auto& n : f.nodes) {
    switch (n.kind) {
      case Gate_formula::Kind::constant: v.push_back(n.inverted ? 1 : 0); break;
      case Gate_formula::Kind::literal: {
        const auto x = var[n.variable];
        v.push_back(x == 2 ? 2 : static_cast<uint8_t>(x ^ (n.inverted ? 1 : 0)));
        break;
      }
      case Gate_formula::Kind::series: {
        const auto a = v[n.left], b = v[n.right];
        v.push_back((a == 0 || b == 0) ? 0 : (a == 1 && b == 1 ? 1 : 2));
        break;
      }
      case Gate_formula::Kind::parallel: {
        const auto a = v[n.left], b = v[n.right];
        v.push_back((a == 1 || b == 1) ? 1 : (a == 0 && b == 0 ? 0 : 2));
        break;
      }
    }
  }
  const auto out = v.back();
  return out == 2 ? 2 : static_cast<uint8_t>(out ^ (f.output_inverted ? 1 : 0));
}

// Slot choice: 0..2r-1 = remaining input (choice/2) with polarity (choice&1); 2r = const 0; 2r+1 = const 1.
struct Matcher {
  const Gate_formula&   t;
  std::vector<uint32_t> slots;  // T's support variables
  uint32_t              r;      // remaining inputs
  const Truth_table&    target;
  std::vector<uint32_t> choice;
  uint64_t              leaves = 0;
  bool                  exhausted = false;

  uint8_t value(uint32_t c, uint32_t m) const {
    if (c >= 2 * r) {
      return static_cast<uint8_t>(c - 2 * r);
    }
    return static_cast<uint8_t>(((m >> (c / 2)) & 1) ^ (c & 1));
  }
  bool consistent(size_t assigned) const {
    std::array<uint8_t, 16> var{};
    for (uint32_t m = 0; m < (1u << r); ++m) {
      var.fill(2);
      for (size_t s = 0; s < assigned; ++s) {
        var[slots[s]] = value(choice[s], m);
      }
      const auto out = eval3(t, var);
      if (out != 2 && (out == 1) != target.get(m)) {
        return false;
      }
    }
    return true;
  }
  bool dfs(size_t s) {
    if (++leaves > kMatchLeaves) {
      exhausted = true;
      return false;
    }
    if (!consistent(s)) {
      return false;
    }
    if (s == slots.size()) {
      return true;
    }
    for (uint32_t c = 0; c < 2 * r + 2; ++c) {
      choice[s] = c;
      if (dfs(s + 1) || exhausted) {
        return !exhausted;
      }
    }
    return false;
  }
};

Truth_table cofactor(const Truth_table& f, uint32_t k, const std::vector<uint32_t>& ctrl, uint32_t value) {
  std::vector<uint32_t> rest;
  for (uint32_t i = 0; i < k; ++i) {
    if (std::find(ctrl.begin(), ctrl.end(), i) == ctrl.end()) {
      rest.push_back(i);
    }
  }
  Truth_table out(static_cast<uint32_t>(rest.size()));
  for (uint32_t m = 0; m < (1u << rest.size()); ++m) {
    uint32_t a = 0;
    for (size_t j = 0; j < rest.size(); ++j) {
      a |= ((m >> j) & 1) << rest[j];
    }
    for (size_t j = 0; j < ctrl.size(); ++j) {
      a |= ((value >> j) & 1) << ctrl[j];
    }
    out.set(m, f.get(a));
  }
  return out;
}

struct Acc {
  uint64_t cells_with_controls = 0;  // >= depth eligible controls
  uint64_t lb_gain_cells       = 0;  // lower bound below the baseline (after mux cost)
  uint64_t tmpl_found          = 0;  // shared template found
  uint64_t tmpl_gain_cells     = 0;  // template (incl. muxes) below the baseline
  uint64_t tmpl_unknown        = 0;  // match search exhausted
  uint64_t base_cost           = 0;  // baseline cost of cells with controls
  uint64_t best_lb_cost        = 0;  // min over subsets of (lower bound + muxes), never above baseline
  uint64_t best_tmpl_cost      = 0;  // min over subsets of (template + muxes), never above baseline
  uint64_t base_stack = 0, lb_stack = 0, best_stack = 0;  // series stack sums
  uint64_t base_branches = 0, best_branches = 0;           // parallel branch sums
  uint64_t stack_gain_cells    = 0;  // template with a shorter stack
  uint64_t muxes               = 0;  // literal-network mux2 count of the chosen templates
};

struct Cone {
  const Logical_cell*                cell = nullptr;
  uint32_t                           state  = 0;
  bool                               native = false;  // the endpoint's DominoLatch with no phase-1 producer inputs
  std::vector<uint32_t>              ctrl;   // control slots (cell input indices)
  std::vector<uint32_t>              slots;  // template variables
  Gate_formula                       tmpl;   // over the remaining inputs
  std::vector<std::vector<uint32_t>> per;    // per control value: literal choice per slot
};

// One combinational network: a graph input per distinct cell input signal and
// one output per cone. `variant` spells literal network + template; otherwise
// the cell's own formula. Formula series = AND, parallel = OR.
// mode 0: the selected cells; 1: literal network + template; 2: the literal
// network alone (one output per non-trivial slot).
bool slot_varies(const Cone& c, size_t s) {
  for (const auto& choices : c.per) {
    if (choices[s] != c.per[0][s]) return true;
  }
  return false;
}
std::shared_ptr<hhds::Graph> build_cones(hhds::GraphLibrary& lib, const std::string& name, const std::vector<Cone>& cones,
                                         int mode) {
  const bool variant = mode != 0;
  auto io = lib.create_io(name);
  std::map<std::pair<Id, bool>, std::string> in_name;
  for (const auto& c : cones) {
    for (const auto& x : c.cell->inputs) {
      in_name.try_emplace({x.id, x.inverted}, std::format("i{}{}", x.id, x.inverted ? "n" : ""));
    }
  }
  hhds::Port_id pid = 0;
  for (const auto& [k, n] : in_name) {
    io->add_input(n, pid++);
    io->set_bits(n, 1);
  }
  size_t outputs = cones.size();
  if (mode == 2) {
    outputs = 0;
    for (const auto& c : cones) {
      for (size_t s = 0; s < c.slots.size(); ++s) outputs += slot_varies(c, s);
    }
  }
  for (size_t o = 0; o < outputs; ++o) {
    const auto n = std::format("o{}", o);
    io->add_output(n, pid++);
    io->set_bits(n, 1);
  }
  size_t next_out = 0;
  auto g = io->create_graph();
  for (const auto& [k, n] : in_name) {
    gu::set_bits(g->get_input_pin(n), 1);
  }
  const auto bit = [](hhds::Pin_class p) {
    gu::set_bits(p, 1);
    gu::set_unsign(p);
    return p;
  };
  const auto lnot = [&](hhds::Pin_class a) {
    auto n = gu::create_typed_node(*g, Ntype_op::Not);
    a.connect_sink(n.create_sink_pin(0));
    return bit(n.create_driver_pin(0));
  };
  const auto op2 = [&](Ntype_op op, hhds::Pin_class a, hhds::Pin_class b) {
    auto n = gu::create_typed_node(*g, op);
    a.connect_sink(n.create_sink_pin(0));
    b.connect_sink(n.create_sink_pin(1));
    return bit(n.create_driver_pin(0));
  };
  const auto konst = [&](bool v) { return gu::create_const(*g, *Dlop::create_integer(v ? 1 : 0)); };
  // Evaluate a formula whose variable v reads var_pin(v).
  const auto emit = [&](const Gate_formula& f, const std::function<hhds::Pin_class(uint32_t, bool)>& lit) {
    std::vector<hhds::Pin_class> v;
    for (const auto& n : f.nodes) {
      switch (n.kind) {
        case Gate_formula::Kind::constant: v.push_back(konst(n.inverted)); break;
        case Gate_formula::Kind::literal : v.push_back(lit(n.variable, n.inverted)); break;
        case Gate_formula::Kind::series  : v.push_back(op2(Ntype_op::And, v[n.left], v[n.right])); break;
        case Gate_formula::Kind::parallel: v.push_back(op2(Ntype_op::Or, v[n.left], v[n.right])); break;
      }
    }
    return f.output_inverted ? lnot(v.back()) : v.back();
  };
  for (size_t o = 0; o < cones.size(); ++o) {
    const auto& c   = cones[o];
    const auto  in  = [&](uint32_t i) { const auto& x = c.cell->inputs[i]; return g->get_input_pin(in_name.at({x.id, x.inverted})); };
    hhds::Pin_class out;
    if (!variant) {
      out = emit(c.cell->formula, [&](uint32_t var, bool inv) { return inv ? lnot(in(var)) : in(var); });
    } else {
      const uint32_t        k = static_cast<uint32_t>(c.cell->inputs.size()), r = k - static_cast<uint32_t>(c.ctrl.size());
      std::vector<uint32_t> rest;
      for (uint32_t i = 0; i < k; ++i) {
        if (std::find(c.ctrl.begin(), c.ctrl.end(), i) == c.ctrl.end()) rest.push_back(i);
      }
      const auto literal = [&](uint32_t choice) {
        if (choice >= 2 * r) return konst(choice == 2 * r + 1);
        const auto p = in(rest[choice / 2]);
        return (choice & 1) ? lnot(p) : p;
      };
      // Slot value: a balanced mux tree over the controls (ctrl[0] at the leaves).
      std::map<uint32_t, hhds::Pin_class> slot_pin;
      for (size_t s = 0; s < c.slots.size(); ++s) {
        std::vector<hhds::Pin_class> level;
        for (const auto& choices : c.per) level.push_back(literal(choices[s]));
        bool same = true;
        for (const auto& choices : c.per) same &= choices[s] == c.per[0][s];
        if (same) {
          slot_pin[c.slots[s]] = level[0];
          continue;
        }
        for (size_t q = 0; level.size() > 1; ++q) {
          std::vector<hhds::Pin_class> next;
          for (size_t i = 0; i + 1 < level.size(); i += 2) {
            auto m = gu::create_typed_node(*g, Ntype_op::Mux);
            in(c.ctrl[q]).connect_sink(m.create_sink_pin(0));
            level[i].connect_sink(m.create_sink_pin(1));
            level[i + 1].connect_sink(m.create_sink_pin(2));
            next.push_back(bit(m.create_driver_pin(0)));
          }
          level = std::move(next);
        }
        slot_pin[c.slots[s]] = level[0];
        if (mode == 2) {
          level[0].connect_sink(g->get_output_pin(std::format("o{}", next_out++)));
        }
      }
      if (mode == 2) {
        continue;
      }
      out = emit(c.tmpl, [&](uint32_t var, bool inv) {
        const auto p = slot_pin.at(var);
        return inv ? lnot(p) : p;
      });
    }
    out.connect_sink(g->get_output_pin(std::format("o{}", o)));
  }
  return g;
}

struct Mapped_qor {
  bool     ok = false;
  double   area = 0, delay = 0;
  uint64_t gates = 0;
};
Mapped_qor map_cones(const std::shared_ptr<hhds::Graph>& g, std::string_view provider, const synth::Tmap_options& options) {
  Mapped_qor q;
  auto       r = synth::technology_map(provider, g, options);
  if (r.status != synth::Tmap_status::mapped || !r.design) {
    return q;
  }
  q.ok = true;
  for (const auto& row : r.design->regions) {
    q.area += row.area;
    q.gates += static_cast<uint64_t>(std::max(row.gates, 0));
    q.delay = std::max<double>(q.delay, row.delay);
  }
  return q;
}

}  // namespace

std::string literal_stats_region(const partition::Region_body& rb, const synth::Region_blast& blast, const Logical_region& sel,
                                 const Endpoint_options& options, const synth::Tmap_options* tmap, std::string_view provider,
                                 std::vector<Literal_rewrite>* plan, uint32_t plan_depth) {
  const auto& g = sel.logic.graph;
  std::string tmap_lines;
  // 1. Classify sources.
  std::vector<uint8_t> cls(g.size(), c_other);
  Cone_classifier      cone_class;
  cone_class.budget = 200000;  // source nodes visited per region
  cls[0] = c_port;  // constant
  for (size_t i = 0; i < sel.logic.inputs.size(); ++i) {
    uint8_t c = c_other;
    if (i < blast.all_pi_order.size()) {
      const auto& o = blast.all_pi_order[i];
      if (o.kind == synth::Pi_kind::region_input && o.index < blast.pi_order.size()) {
        const auto port = blast.pi_order[o.index].first;
        if (port < rb.inputs.size()) {
          c = cone_class(rb.inputs[port].src_driver);
        }
      } else if (o.kind == synth::Pi_kind::bbox_output && o.index < blast.bbox_pi.size()) {
        const auto bx = std::get<0>(blast.bbox_pi[o.index]);
        if (bx >= 0 && static_cast<size_t>(bx) < blast.bboxes.size()) {
          const auto& b = blast.bboxes[bx];
          if (b.op == Ntype_op::Latch) {
            c = latch_transparent_low(b.node) ? c_latch_low : c_latch_other;
          } else if (b.op == Ntype_op::Flop || b.op == Ntype_op::Fflop) {
            c = c_flop;
          }
        }
      }
    }
    cls[sel.logic.inputs[i].id] = c;
  }
  for (const auto& s : sel.logic.state) {
    cls[s.q.id] = c_flop;
  }
  // 2. Interior nodes whose whole support is registered (nodes are topological).
  for (Id id = 1; id < g.size(); ++id) {
    const auto& n = g.node(id);
    if (n.kind != Xag::Kind::and_gate && n.kind != Xag::Kind::xor_gate) {
      continue;
    }
    const auto a = cls[n.inputs[0].id], b = cls[n.inputs[1].id];
    const auto reg = [](uint8_t c) { return (c & (c_flop | c_port | c_stat)) != 0; };
    const auto lat = [&](uint8_t c) { return reg(c) || (c & (c_latch_low | c_stat_lat)) != 0; };
    if (reg(a) && reg(b)) {
      cls[id] = c_stat;
    } else if (lat(a) && lat(b)) {
      cls[id] = c_stat_lat;
    }
  }

  // 3. Per cell.
  std::array<std::array<Acc, kMaxDepth + 1>, kScenario.size()>               acc{};
  std::array<std::array<std::vector<Cone>, kMaxDepth + 1>, kScenario.size()> cones{};
  uint64_t cells = 0, wide = 0, analyzed = 0, total_base = 0, verify_failures = 0;
  for (const auto& e : sel.endpoints) {
    for (const auto& cell : e.cells) {
      ++cells;
      const auto k = static_cast<uint32_t>(cell.inputs.size());
      const uint64_t base = cell.metrics.transistors + (cell.latch ? options.cost.domino_latch : options.cost.domino);
      total_base += base;
      if (k > kMaxInputs || k < 2) {
        wide += k > kMaxInputs;
        continue;
      }
      const bool native_latch
          = cell.latch && &cell == &e.cells.back() && std::all_of(cell.producers.begin(), cell.producers.end(), [](int32_t p) {
              return p < 0;
            });
      Budget work{20000000};
      auto   table = cell.formula.evaluate_table(k, work);
      if (!table) {
        continue;
      }
      ++analyzed;
      // Cache cofactor syntheses and matches per control subset (shared by scenarios).
      struct Subset_result {
        bool     ok = false;
        uint64_t lb = 0, lb_stack = 0, tmpl = 0, stack = 0, branches = 0, muxes = 0;
        bool     tmpl_ok = false, unknown = false;
        Cone     cone;  // the realization (for the tmap experiment)
      };
      std::map<std::vector<uint32_t>, Subset_result> memo;
      const auto eval_subset = [&](const std::vector<uint32_t>& ctrl) -> Subset_result {
        if (auto it = memo.find(ctrl); it != memo.end()) {
          return it->second;
        }
        Subset_result res;
        const uint32_t n = static_cast<uint32_t>(ctrl.size()), r = k - n;
        std::vector<Truth_table>    cof;
        std::vector<Gate_candidate> gate;
        uint64_t                    lb = 0, lb_stack = 0;
        for (uint32_t v = 0; v < (1u << n); ++v) {
          cof.push_back(cofactor(*table, k, ctrl, v));
          Budget gw{4000000};
          gate.push_back(synthesize_gate(cof.back(), options.gates, gw, options.functions));
          if (!gate.back().formula) {
            memo[ctrl] = res;
            return res;
          }
          lb       = std::max<uint64_t>(lb, gate.back().cost.transistors);
          lb_stack = std::max<uint64_t>(lb_stack, gate.back().cost.stack);
        }
        const uint64_t overhead = cell.latch ? options.cost.domino_latch : options.cost.domino;
        res.ok = true;
        res.lb       = lb + overhead;
        res.lb_stack = lb_stack;
        // Template search: each cofactor's gate, cheapest first.
        std::vector<uint32_t> order(1u << n);
        for (uint32_t v = 0; v < order.size(); ++v) order[v] = v;
        std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return gate[a].cost.transistors < gate[b].cost.transistors; });
        uint64_t best = UINT64_MAX;
        for (const auto j : order) {
          const auto&           t = *gate[j].formula;
          std::vector<uint32_t> slots;
          for (uint32_t v = 0; v < 16; ++v) {
            if ((gate[j].cost.support >> v) & 1) slots.push_back(v);
          }
          // choices per cofactor; the template's own cofactor is the identity.
          std::vector<std::vector<uint32_t>> per(1u << n);
          bool                               all = true;
          for (uint32_t i = 0; i < (1u << n) && all; ++i) {
            if (i == j) {
              for (const auto s : slots) per[i].push_back(2 * s);
              continue;
            }
            Matcher mt{t, slots, r, cof[i], std::vector<uint32_t>(slots.size(), 0)};
            if (!mt.dfs(0)) {
              all = false;
              res.unknown |= mt.exhausted;
            } else {
              per[i] = mt.choice;
            }
          }
          if (!all) {
            continue;
          }
          // Self-check: control values pick a cofactor's literal choices, the
          // template evaluates them; the composition must be the cell function.
          {
            std::vector<uint32_t> rest;
            for (uint32_t i = 0; i < k; ++i) {
              if (std::find(ctrl.begin(), ctrl.end(), i) == ctrl.end()) rest.push_back(i);
            }
            for (uint32_t a = 0; a < (1u << k); ++a) {
              uint32_t v = 0, m = 0;
              for (size_t q = 0; q < ctrl.size(); ++q) v |= ((a >> ctrl[q]) & 1) << q;
              for (size_t q = 0; q < rest.size(); ++q) m |= ((a >> rest[q]) & 1) << q;
              std::array<uint8_t, 16> var{};
              for (size_t q = 0; q < slots.size(); ++q) {
                const auto c = per[v][q];
                var[slots[q]] = c >= 2 * r ? static_cast<uint8_t>(c - 2 * r) : static_cast<uint8_t>(((m >> (c / 2)) & 1) ^ (c & 1));
              }
              if ((eval3(t, var) == 1) != table->get(a)) {
                ++verify_failures;
                all = false;
                break;
              }
            }
            if (!all) {
              continue;
            }
          }
          uint64_t muxes = 0;
          for (size_t s = 0; s < slots.size(); ++s) {
            bool differ = false;
            for (uint32_t i = 1; i < per.size(); ++i) differ |= per[i][s] != per[0][s];
            muxes += differ ? (1u << n) - 1 : 0;
          }
          // Rank by the DOMINO gate alone (the literal network settles in the
          // free first half and is reported separately), then by fewer muxes.
          const uint64_t cost = (gate[j].cost.transistors + overhead) * 1024 + muxes;
          if (cost < best) {
            best         = cost;
            res.tmpl_ok  = true;
            res.tmpl     = gate[j].cost.transistors + overhead;
            res.stack    = gate[j].cost.stack;
            res.branches = gate[j].cost.branches;
            res.muxes    = muxes;
            res.cone     = Cone{&cell, e.state_index, native_latch, ctrl, slots, t, per};
          }
        }
        memo[ctrl] = res;
        return res;
      };
      for (size_t sc = 0; sc < kScenario.size(); ++sc) {
        std::vector<uint32_t> elig;
        for (uint32_t i = 0; i < k; ++i) {
          if (cell.producers[i] < 0 && (cls[cell.inputs[i].id] & kEligible[sc]) != 0) elig.push_back(i);
        }
        for (uint32_t n = 1; n <= kMaxDepth; ++n) {
          if (elig.size() < n || k - n < 1) {
            continue;
          }
          auto& a = acc[sc][n];
          ++a.cells_with_controls;
          a.base_cost += base;
          a.base_stack += cell.metrics.stack;
          a.base_branches += cell.metrics.branches;
          uint64_t best_lb = base, best_lb_stack = cell.metrics.stack, best_t = base, best_stack = cell.metrics.stack,
                   best_branches = cell.metrics.branches, best_mux = 0;
          std::optional<Cone> best_cone;
          bool     found = false, unknown = false;
          // All n-subsets of the eligible slots.
          std::vector<uint32_t> idx(n);
          for (uint32_t i = 0; i < n; ++i) idx[i] = i;
          while (true) {
            std::vector<uint32_t> ctrl(n);
            for (uint32_t i = 0; i < n; ++i) ctrl[i] = elig[idx[i]];
            const auto res = eval_subset(ctrl);
            if (res.ok) {
              best_lb       = std::min(best_lb, res.lb);
              best_lb_stack = std::min(best_lb_stack, res.lb_stack);
              if (res.tmpl_ok) {
                found = true;
                if (res.tmpl < best_t || (res.tmpl == best_t && res.stack < best_stack)) {
                  best_t        = res.tmpl;
                  best_stack    = res.stack;
                  best_branches = res.branches;
                  best_mux      = res.muxes;
                  best_cone     = res.cone;
                }
              }
              unknown |= res.unknown;
            }
            int p = static_cast<int>(n) - 1;
            while (p >= 0 && idx[p] == elig.size() - n + p) --p;
            if (p < 0) break;
            ++idx[p];
            for (uint32_t i = p + 1; i < n; ++i) idx[i] = idx[i - 1] + 1;
          }
          a.best_lb_cost   += best_lb;
          a.best_tmpl_cost += best_t;
          a.best_stack     += best_stack;
          a.lb_stack       += best_lb_stack;
          a.best_branches  += best_branches;
          a.stack_gain_cells += best_stack < cell.metrics.stack;
          if (best_cone && best_t < base) {
            cones[sc][n].push_back(*best_cone);
          }
          a.muxes          += best_mux;
          a.lb_gain_cells  += best_lb < base;
          a.tmpl_found     += found;
          a.tmpl_gain_cells += best_t < base;
          a.tmpl_unknown   += !found && unknown;
        }
      }
    }
  }
  std::string j = std::format("{{\"region\":\"{}\",\"cells\":{},\"analyzed\":{},\"wide\":{},\"base_cost\":{},\"verify_failures\":{},"
                              "\"scenarios\":{{",
                              rb.module_name, cells, analyzed, wide, total_base, verify_failures);
  for (size_t sc = 0; sc < kScenario.size(); ++sc) {
    j += std::format("{}\"{}\":[", sc ? "," : "", kScenario[sc]);
    for (uint32_t n = 1; n <= kMaxDepth; ++n) {
      const auto& a = acc[sc][n];
      j += std::format(
          "{}{{\"depth\":{},\"cells\":{},\"base\":{},\"lb\":{},\"tmpl\":{},\"lb_gain_cells\":{},\"tmpl_found\":{},"
          "\"tmpl_gain_cells\":{},\"tmpl_unknown\":{},\"base_stack\":{},\"lb_stack\":{},\"tmpl_stack\":{},"
          "\"stack_gain_cells\":{},\"base_branches\":{},\"tmpl_branches\":{},\"muxes\":{}}}",
          n > 1 ? "," : "", n, a.cells_with_controls, a.base_cost, a.best_lb_cost, a.best_tmpl_cost, a.lb_gain_cells, a.tmpl_found,
          a.tmpl_gain_cells, a.tmpl_unknown, a.base_stack, a.lb_stack, a.best_stack, a.stack_gain_cells, a.base_branches,
          a.best_branches, a.muxes);
    }
    j += "]";
  }
  // Input-class census over the cells' native inputs.
  std::array<uint64_t, 7> census{};
  for (const auto& e : sel.endpoints) {
    for (const auto& cell : e.cells) {
      for (size_t i = 0; i < cell.inputs.size(); ++i) {
        if (cell.producers[i] >= 0) continue;
        const auto c = cls[cell.inputs[i].id];
        for (int b = 0; b < 7; ++b) census[b] += (c >> b) & 1;
      }
    }
  }
  if (plan != nullptr && plan_depth >= 1 && plan_depth <= kMaxDepth) {
    // Scenario `reg` (registered controls): the default extraction policy.
    for (const auto& c : cones[0][plan_depth]) {
      if (c.native) {
        plan->push_back(Literal_rewrite{c.state, c.cell->inputs, c.ctrl, c.slots, c.tmpl, c.per});
      }
    }
  }
  if (tmap != nullptr && !provider.empty()) {
    for (size_t sc = 0; sc < kScenario.size(); sc += 2) {  // reg and stat (the latch twins only differ on latched designs)
      for (uint32_t n = 1; n <= kMaxDepth; ++n) {
        const auto& set = cones[sc][n];
        if (set.empty()) {
          continue;
        }
        hhds::GraphLibrary lib;
        const auto         base = map_cones(build_cones(lib, "lit_base", set, 0), provider, *tmap);
        const auto         var  = map_cones(build_cones(lib, "lit_variant", set, 1), provider, *tmap);
        const auto         net  = map_cones(build_cones(lib, "lit_network", set, 2), provider, *tmap);
        if (!base.ok || !var.ok) {
          continue;
        }
        tmap_lines += std::format(
            "\n{{\"kind\":\"tmap\",\"region\":\"{}\",\"scenario\":\"{}\",\"depth\":{},\"cones\":{},\"base_area\":{:.4f},"
            "\"base_gates\":{},\"base_delay\":{:.3f},\"variant_area\":{:.4f},\"variant_gates\":{},\"variant_delay\":{:.3f},"
            "\"network_area\":{:.4f},\"network_gates\":{},\"network_delay\":{:.3f}}}",
            rb.module_name, kScenario[sc], n, set.size(), base.area, base.gates, base.delay, var.area, var.gates, var.delay,
            net.area, net.gates, net.delay);
      }
    }
  }
  j += std::format("}},\"inputs\":{{\"flop\":{},\"port\":{},\"latch_low\":{},\"latch_other\":{},\"other\":{},\"stat\":{},\"stat_lat\":{}}}}}",
                   census[0], census[1], census[2], census[3], census[4], census[5], census[6]);
  return j + tmap_lines;
}

uint32_t apply_literal_rewrites(Xag& g, std::vector<Xsignal>& state_d, std::span<const Literal_rewrite> plan) {
  uint32_t done = 0;
  for (const auto& rw : plan) {
    if (rw.state >= state_d.size()) {
      continue;
    }
    const uint32_t        k = static_cast<uint32_t>(rw.inputs.size()), r = k - static_cast<uint32_t>(rw.ctrl.size());
    std::vector<uint32_t> rest;
    for (uint32_t i = 0; i < k; ++i) {
      if (std::find(rw.ctrl.begin(), rw.ctrl.end(), i) == rw.ctrl.end()) rest.push_back(i);
    }
    const auto literal = [&](uint32_t choice) {
      if (choice >= 2 * r) return g.constant(choice == 2 * r + 1);
      const auto x = rw.inputs[rest[choice / 2]];
      return (choice & 1) ? ~x : x;
    };
    std::array<Xsignal, 16> slot{};
    for (size_t s = 0; s < rw.slots.size(); ++s) {
      std::vector<Xsignal> level;
      for (const auto& choices : rw.per) level.push_back(literal(choices[s]));
      // Balanced tree: control q selects at level q (value bit q).
      for (size_t q = 0; level.size() > 1; ++q) {
        std::vector<Xsignal> next;
        for (size_t i = 0; i + 1 < level.size(); i += 2) {
          next.push_back(level[i] == level[i + 1] ? level[i] : g.mux(rw.inputs[rw.ctrl[q]], level[i + 1], level[i]));
        }
        level = std::move(next);
      }
      slot[rw.slots[s]] = level[0];
    }
    std::vector<Xsignal> v;
    for (const auto& n : rw.tmpl.nodes) {
      switch (n.kind) {
        case Gate_formula::Kind::constant: v.push_back(g.constant(n.inverted)); break;
        case Gate_formula::Kind::literal : v.push_back(n.inverted ? ~slot[n.variable] : slot[n.variable]); break;
        case Gate_formula::Kind::series  : v.push_back(g.land(v[n.left], v[n.right])); break;
        case Gate_formula::Kind::parallel: v.push_back(g.lor(v[n.left], v[n.right])); break;
      }
    }
    state_d[rw.state] = rw.tmpl.output_inverted ? ~v.back() : v.back();
    ++done;
  }
  return done;
}

}  // namespace livehd::usyn
