// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "proof_prep.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <set>
#include <string_view>

#include "diag.hpp"
#include "inline_sub.hpp"
#include "latch_contract.hpp"
#include "node_util.hpp"
#include "pass_single_edge.hpp"
#include "str_tools.hpp"

namespace livehd::single_edge {

// find, and a def with no gate at all is not touched in any way.
// 2f-latch M9 — RECOGNIZE instantiated clock gates as `Clock_cell`, everywhere
// the hierarchical driver will encode. Runs BEFORE the inline+fold below, and
// takes precedence over it: what this recognizes, the fold never sees.
//
// NO `is_boxed` FILTER, AND THAT IS THE POINT. Inlining a TRUSTED def is
// unsound -- it pulls internals the user declared out of scope into the
// compared cone, which is why `inline_clock_gate_cells` takes the predicate.
// Recognition is different in kind: nothing of the def's STATE crosses the
// boundary (the enable latch is replaced by the cell's sampling contract), only
// a pure combinational function of nets the parent ALREADY drives and already
// compares. So trust is respected rather than fought -- and since the instance
// is then gone, the def is no longer instantiated at all and its trust entry
// becomes a no-op, which is what lets `prim_clk_gate` leave the trust list.
int materialize_clock_cells_all(hhds::Graph* top, const std::vector<hhds::Graph*>& defs) {
  int                               n = livehd::latch_contract::materialize_clock_cells(top, "pass.single_edge");
  absl::flat_hash_set<hhds::Graph*> seen{top};
  for (auto* d : defs) {
    if (d == nullptr || !seen.insert(d).second) {
      continue;  // ref and impl def lists share every --lib cell model: same Graph*
    }
    n += livehd::latch_contract::materialize_clock_cells(d, "pass.single_edge");
  }
  return n;
}

namespace {

// Mapping into a library without reset latches folds an asynchronous reset
// into D and the transparency control. Recover that pin only after checking
// both cofactors: reset opens the latch and forces a constant, while releasing
// it leaves a plain clock window. This preserves reset priority and lets both
// sides use the same closing-edge schedule.
void recover_latch_resets(hhds::Graph* graph) {
  namespace gu = livehd::graph_util;
  struct Value {
    hhds::Pin_class     pin;
    std::optional<bool> constant;
    bool                inverted = false;
  };
  const auto negate = [](Value value) {
    if (value.constant) {
      value.constant = !*value.constant;
    } else {
      value.inverted = !value.inverted;
    }
    return value;
  };
  const livehd::latch_contract::Design_clocks clocks(graph);
  std::vector<hhds::Pin_class>                resets;
  for (const auto& input : graph->get_io()->get_input_pin_decls()) {
    if (input.bits == 1 && str_tools::is_reset_like_name(input.name)) {
      resets.push_back(graph->get_input_pin(input.name));
    }
  }
  std::vector<hhds::Node_class> latches;
  for (auto node : graph->body().nodes()) {
    if (gu::type_op_of(node) == Ntype_op::Latch) {
      latches.push_back(node);
    }
  }
  for (auto latch : latches) {
    if (gu::type_op_of(latch) != Ntype_op::Latch || gu::bits_of(latch.get_driver_pin(0)) != 1
        || !gu::get_driver_of_sink_name(latch, "reset_pin").is_invalid()
        || !gu::get_driver_of_sink_name(latch, "initial").is_invalid()) {
      continue;
    }
    const auto en  = gu::get_driver_of_sink_name(latch, "enable");
    const auto din = gu::get_driver_of_sink_name(latch, "din");
    const auto pos = gu::get_driver_of_sink_name(latch, "posclk");
    if (en.is_invalid() || din.is_invalid() || (!pos.is_invalid() && !pos.is_const())) {
      continue;
    }
    for (auto reset : resets) {
      for (const bool high : {false, true}) {
        absl::flat_hash_map<hhds::Class_index, Value>   memo;
        bool                                            reset_level = high;
        std::function<Value(hhds::Pin_class, unsigned)> eval        = [&](hhds::Pin_class pin, unsigned depth) -> Value {
          Value opaque{pin, std::nullopt, false};
          if (pin.is_invalid() || depth > 128) {
            return opaque;
          }
          if (pin == reset) {
            return {{}, reset_level, false};
          }
          if (pin.is_const()) {
            const auto& value = gu::const_of(pin);
            if (!value.has_unknowns() && value.is_just_i64() && (value.to_just_i64() == 0 || value.to_just_i64() == 1)) {
              return {{}, value.to_just_i64() != 0, false};
            }
            return opaque;
          }
          if (auto it = memo.find(pin.get_class_index()); it != memo.end()) {
            return it->second;
          }
          memo.emplace(pin.get_class_index(), opaque);
          if (gu::is_graph_input_pin(pin) || gu::bits_of(pin) != 1) {
            return opaque;
          }
          auto       node = pin.get_master_node();
          const auto op   = gu::type_op_of(node);
          if (op != Ntype_op::Not && op != Ntype_op::Get_mask && op != Ntype_op::Sext && op != Ntype_op::Mux && op != Ntype_op::And
              && op != Ntype_op::Or && op != Ntype_op::EQ) {
            return opaque;
          }
          std::vector<Value> inputs;
          for (auto sink : node.inp_sorted_pins()) {
            for (auto driver : sink.get_driver_pins()) {
              inputs.push_back(eval(driver, depth + 1));
            }
          }
          Value result = opaque;
          if (op == Ntype_op::Not && inputs.size() == 1) {
            result = negate(inputs.front());
          } else if ((op == Ntype_op::Get_mask || op == Ntype_op::Sext) && inputs.size() >= 1) {
            auto source = gu::first_value_driver(node);
            if (gu::bits_of(source) == 1 && (op == Ntype_op::Sext || gu::bit_range(node) == std::make_optional(std::pair{0, 1}))) {
              result = eval(source, depth + 1);
            }
          } else if (op == Ntype_op::Mux && inputs.size() == 3 && inputs[0].constant) {
            result = inputs[*inputs[0].constant ? 2 : 1];
          } else if (op == Ntype_op::And || op == Ntype_op::Or) {
            const bool identity = op == Ntype_op::And;
            result              = {{}, identity, false};
            unsigned unknown    = 0;
            for (const auto& input : inputs) {
              if (input.constant) {
                if (*input.constant != identity) {
                  result  = {{}, !identity, false};
                  unknown = 0;
                  break;
                }
              } else if (++unknown == 1) {
                result = input;
              } else {
                result = opaque;
              }
            }
          } else if (op == Ntype_op::EQ && inputs.size() == 2 && std::ranges::all_of(inputs, [](const auto& input) {
                       return input.constant.has_value() || gu::bits_of(input.pin) == 1;
                     })) {
            if (inputs[0].constant && (!*inputs[0].constant || inputs[1].constant || gu::is_unsign(inputs[1].pin))) {
              result = *inputs[0].constant ? inputs[1] : negate(inputs[1]);
            } else if (inputs[1].constant && (!*inputs[1].constant || inputs[0].constant || gu::is_unsign(inputs[0].pin))) {
              result = *inputs[1].constant ? inputs[0] : negate(inputs[0]);
            }
          }
          memo[pin.get_class_index()] = result;
          return result;
        };
        auto active = eval(en, 0);
        if (pos.is_known_false()) {
          active = negate(active);
        }
        const auto data = eval(din, 0);
        if (!active.constant || !*active.constant || !data.constant) {
          continue;
        }
        reset_level = !high;
        memo.clear();
        auto inactive = eval(en, 0);
        if (pos.is_known_false()) {
          inactive = negate(inactive);
        }
        if (inactive.constant || inactive.pin.is_invalid() || gu::bits_of(inactive.pin) != 1 || !clocks.is_clock(inactive.pin)) {
          continue;
        }
        gu::find_sink_pin(latch, "enable").del_sink();
        inactive.pin.connect_sink(gu::setup_sink_by_name(latch, "enable"));
        if (!pos.is_invalid()) {
          gu::find_sink_pin(latch, "posclk").del_sink();
        }
        gu::create_const(*graph, *Dlop::create_integer(inactive.inverted ? 0 : 1))
            .connect_sink(gu::setup_sink_by_name(latch, "posclk"));
        reset.connect_sink(gu::setup_sink_by_name(latch, "reset_pin"));
        gu::create_const(*graph, *Dlop::create_integer(high ? 0 : 1)).connect_sink(gu::setup_sink_by_name(latch, "negreset"));
        gu::create_const(*graph, *Dlop::create_integer(*data.constant ? 1 : 0))
            .connect_sink(gu::setup_sink_by_name(latch, "initial"));
        break;
      }
      if (!gu::get_driver_of_sink_name(latch, "reset_pin").is_invalid()) {
        break;
      }
    }
  }
}

// An exported gated-clock waveform must retain its enable sampling state.
// Edge normalization can fold gates used only as clocks, but cannot encode an
// observable Clock_cell as ordinary data in its one-step time base. Expose the
// same latch and Boolean gate that cgen emits before planning that rewrite.
void expose_clock_outputs(hhds::Graph* graph) {
  namespace gu = livehd::graph_util;
  std::vector<hhds::Node_class> cells;
  for (auto node : graph->body().nodes()) {
    if (gu::type_op_of(node) == Ntype_op::Clock_cell) {
      cells.push_back(node);
    }
  }
  for (auto cell : cells) {
    auto                                   q = cell.get_driver_pin(0);
    std::vector<hhds::Pin_class>           work{q};
    absl::flat_hash_set<hhds::Class_index> seen;
    bool                                   observable = false;
    while (!work.empty() && !observable) {
      auto pin = work.back();
      work.pop_back();
      if (!seen.insert(pin.get_class_index()).second) {
        continue;
      }
      for (const auto& out : pin.out_edges()) {
        if (gu::is_graph_output_pin(out.sink)) {
          observable = true;
          break;
        }
        auto       consumer = out.sink.get_master_node();
        const auto op       = gu::type_op_of(consumer);
        if (gu::is_type_register(consumer) || op == Ntype_op::Memory || op == Ntype_op::Sub) {
          continue;
        }
        for (auto output : consumer.out_pins()) {
          work.push_back(output);
        }
      }
    }
    if (!observable) {
      continue;
    }
    auto clk = gu::get_driver_of_sink_name(cell, "clk_ref");
    auto div = gu::get_driver_of_sink_name(cell, "div");
    auto inv = gu::get_driver_of_sink_name(cell, "invert");
    if (clk.is_invalid()
        || (!div.is_invalid() && (!div.is_const() || !gu::const_of(div).is_just_i64() || gu::const_of(div).to_just_i64() != 1))
        || (!inv.is_invalid() && !inv.is_const())) {
      continue;  // unsupported flavours remain fail-closed in the encoder
    }
    const bool inverted = !inv.is_invalid() && !gu::const_of(inv).is_known_false();
    auto       en       = gu::get_driver_of_sink_name(cell, "en");
    if (en.is_invalid()) {
      en = gu::create_const(*graph, *Dlop::create_integer(1));
    }
    auto latch = gu::create_typed_node(*graph, Ntype_op::Latch);
    clk.connect_sink(gu::setup_sink_by_name(latch, "enable"));
    en.connect_sink(gu::setup_sink_by_name(latch, "din"));
    gu::create_const(*graph, *Dlop::create_integer(inverted ? 1 : 0)).connect_sink(gu::setup_sink_by_name(latch, "posclk"));
    auto held = latch.create_driver_pin(0);
    gu::set_bits(held, 1);
    gu::set_unsign(held);
    if (inverted) {
      auto neg = gu::create_typed_node(*graph, Ntype_op::Not);
      held.connect_sink(gu::setup_sink_pid(neg, 0));
      held = neg.create_driver_pin(0);
      gu::set_bits(held, 1);
      gu::set_unsign(held);
    }
    auto gate = gu::create_typed_node(*graph, inverted ? Ntype_op::Or : Ntype_op::And);
    clk.connect_sink(gu::setup_sink_pid(gate, 0));
    held.connect_sink(gu::setup_sink_pid(gate, 0));
    auto output = gate.create_driver_pin(0);
    gu::set_bits(output, 1);
    gu::set_unsign(output);
    const auto         edges = q.out_edges();
    const gu::Edge_vec consumers(edges.begin(), edges.end());
    for (const auto& out : consumers) {
      output.connect_sink(out.sink);
    }
    cell.del_node();
  }
}

// Where one instance of a `--lib` cell is spliced from.
//
// An instance whose definition the design's OWN library holds is spliced from
// THAT body, with no override: the inliner then binds it by port id through
// the one GraphIO it was elaborated with, correct by construction. This is the
// encoder's rule too (pass/lec/encode.cpp: an instance with a body is descended
// into, and `sub_lib` resolves only a body-less one), so a cell means the same
// thing whether this prep splices it or the encoder meets it later. Only a
// BODY-LESS instance (an lg: netlist that references the cell without holding
// it) takes the `--lib` model, bound by port name.
//
// Substituting the `--lib` model for a body the design carries compared the
// model instead of the design: an lg: netlist shipped with its own bypassed
// clock gate (`GCLK = CLK`) or its own `INVx1` spelled `Y = A` PROVED against
// the gated RTL, because the splice silently put the correct library cell in
// place of the design's broken one. On a Verilog side the own body IS the
// `--lib` model (lec elaborates the netlist against the emitted models), so
// nothing is lost there.
//
// NAMES are a separate matter from semantics. A correspondence key must not
// depend on which copy of the cell carried the body, so a spliced own body's
// one state element is named the way the `--lib` model names its own
// (Lib_splice::state_name). The copy a Verilog netlist is elaborated with came
// through cgen, which invents a name for the model's anonymous latch
// (`latch_16`); carrying that one made two netlists that differ only in their
// instance names (bit 0 held by an instance called `g[1]`) pair their latches
// by instance name, and a no-reset BMC started them equal and REFUTED an
// equivalent pair that the model's anonymous latch had let semdiff match.
struct Lib_splice {
  hhds::Graph*               body         = nullptr;  // what gets spliced (and classified): own body or --lib model
  hhds::Graph*               override_def = nullptr;  // the inliner `def` override: the --lib model, only when body-less
  std::optional<std::string> state_name;              // inliner `state_name`: the model's name for the one state element
};

// The one state element (flop or latch) of a cell body: its op and name.
// `count` > 1 (or a Memory) means the body has no single state element to name.
struct Cell_state {
  int                        count = 0;
  Ntype_op                   op    = Ntype_op::Invalid;
  std::optional<std::string> name;
};

Cell_state cell_state_of(hhds::Graph* g) {
  Cell_state s;
  for (auto n : g->body().nodes()) {
    const auto op = livehd::graph_util::type_op_of(n);
    if (op == Ntype_op::Memory) {
      s.count = 2;
      return s;
    }
    if (!livehd::graph_util::is_type_flop(n) && op != Ntype_op::Latch) {
      continue;
    }
    if (++s.count > 1) {
      return s;
    }
    s.op = op;
    if (livehd::graph_util::has_name(n)) {
      s.name = std::string{livehd::graph_util::node_name_of(n)};
    }
  }
  return s;
}

// The body an instance of a `--lib` cell is spliced (and classified) from.
hhds::Graph* lib_splice_body(const hhds::Node_class& inst, const Cell_models& sub_lib) {
  if (auto own = inst.get_subnode_graph(); own) {
    return own.get();
  }
  auto it = sub_lib.find(inst.get_subnode_gid());
  return it != sub_lib.end() ? it->second : nullptr;
}

Lib_splice lib_splice_source(const hhds::Node_class& inst, const Cell_models& sub_lib) {
  auto        it    = sub_lib.find(inst.get_subnode_gid());
  auto* const model = it != sub_lib.end() ? it->second : nullptr;
  if (auto own = inst.get_subnode_graph(); own) {
    Lib_splice src{own.get(), nullptr, std::nullopt};
    if (model != nullptr && model != own.get()) {
      // Same single state element on both copies: take the model's name (or
      // its anonymity). Anything else keeps the own body's names.
      const auto os = cell_state_of(own.get());
      const auto ms = cell_state_of(model);
      if (os.count == 1 && ms.count == 1 && os.op == ms.op) {
        src.state_name = ms.name.value_or(std::string{});
      }
    }
    return src;
  }
  if (model != nullptr) {
    return {model, model, std::nullopt};
  }
  return {};
}

// The refusal for a --lib splice the inliner declined. A model override binds
// by port NAME through the instance's own IO (graph/inline_sub.hpp), so a
// decline means the model does not describe this instance's ports (a name the
// model lacks, a width it states differently) or a shape the inliner cannot
// resolve. Leaving the instance for a later pass is NOT a safe fallback: it is
// how a model spliced by port id silently dropped a clock gate's GCLK and made
// a netlist with a swapped or inverted gate PROVE against the correct one.
std::string lib_splice_refusal(const hhds::Node_class& inst, const Lib_splice& src, std::string_view what) {
  const auto why = src.override_def != nullptr ? livehd::graph_util::sub_def_port_mismatch(inst, src.override_def) : std::string{};
  const auto sio = inst.get_subnode_io();
  return std::format("lec: cannot splice the {} --lib cell instance '{}' (cell '{}') from its {}: {}",
                     what,
                     livehd::graph_util::default_instance_name(inst),
                     sio != nullptr ? std::string{sio->get_name()} : std::string{src.body->get_name()},
                     src.override_def != nullptr ? "--lib model" : "own definition",
                     why.empty() ? std::string{"the inliner refused it (see the diagnostic above)"} : why);
}

}  // namespace

// Clock analysis runs before the encoder's ordinary combinational --lib
// expansion, so expose modeled gates on clock cones (and buffer/inverter chains
// on latch enables) before scheduling edges.
std::string inline_clock_lib_cells(const absl::flat_hash_map<hhds::Gid, hhds::Graph*>& sub_lib, hhds::Graph* graph) {
  if (graph == nullptr || sub_lib.empty()) {
    return {};
  }
  // Classified on the body that would be SPLICED (lib_splice_source): the
  // design's own definition when it has one, else the --lib model. Only a Sub
  // of a --lib cell is a candidate at all.
  absl::flat_hash_map<hhds::Graph*, bool> pure_memo;
  auto                                    combinational = [&](const hhds::Node_class& sub) {
    if (!sub_lib.contains(sub.get_subnode_gid())) {
      return false;
    }
    auto* body = lib_splice_body(sub, sub_lib);
    if (body == nullptr) {
      return false;
    }
    auto [it, fresh] = pure_memo.try_emplace(body, true);
    if (fresh) {
      for (auto node : body->body().nodes()) {
        const auto op = livehd::graph_util::type_op_of(node);
        if (livehd::graph_util::is_type_register(node) || op == Ntype_op::Memory || op == Ntype_op::Sub
            || op == Ntype_op::Clock_cell) {
          it->second = false;
          break;
        }
      }
    }
    return it->second;
  };
  std::vector<hhds::Pin_class> pending;
  std::vector<hhds::Pin_class> latch_enables;
  const bool                   has_reset_input = std::ranges::any_of(graph->get_io()->get_input_pin_decls(), [](const auto& input) {
    return input.bits == 1 && str_tools::is_reset_like_name(input.name);
  });
  for (auto node : graph->body().nodes()) {
    const auto op = livehd::graph_util::type_op_of(node);
    if (op == Ntype_op::Memory) {
      livehd::graph_util::for_each_memory_clock_driver(node, [&](auto pin) { pending.push_back(pin); });
    } else if (op == Ntype_op::Latch) {
      const auto enable = livehd::graph_util::get_driver_of_sink_name(node, "enable");
      if (has_reset_input) {
        // Expose both halves of a folded reset so recover_latch_resets can
        // check its priority and constant value before assigning a clock slot.
        pending.push_back(enable);
        pending.push_back(livehd::graph_util::get_driver_of_sink_name(node, "din"));
      } else {
        latch_enables.push_back(enable);
      }
    } else if (livehd::graph_util::is_type_register(node)) {
      pending.push_back(livehd::graph_util::get_driver_of_sink_name(node, "clock_pin"));
    }
  }
  absl::flat_hash_set<hhds::Class_index> seen;
  std::vector<hhds::Node_class>          cells;
  // A latch's gate IS its `enable` (its `clock_pin` is reserved and never
  // driven), so seeding only `clock_pin` left a mapped `INVx1(clk)` on an
  // active-low latch's enable an opaque Sub: the commit-class walk then saw a
  // DATA-gated latch on the netlist side against the source's `!clk`
  // CLOCK-gated one, slotted the two differently, and REFUTED an equivalent
  // pair (abc_latch_mix `b`/`p1` against a Liberty with no latch cell).
  //
  // Only the UNARY chain is exposed: buffer/inverter cells and the one-operand
  // shaping nodes between them, exactly what `control_root` peels. A latch's
  // enable is often a data cone (a folded reset, `rst | clk`), and inlining a
  // multi-input gate there re-spells it (NOR2 = `~a & ~b` reads as a gated
  // `~clk`) into a shape the source side never has, moving the latch to a
  // different commit class than its reference twin.
  absl::flat_hash_set<hhds::Class_index> queued;
  for (auto pin : latch_enables) {
    absl::flat_hash_set<hhds::Class_index> walked;
    while (!pin.is_invalid() && !pin.is_const() && !livehd::graph_util::is_graph_input_pin(pin)) {
      auto node = pin.get_master_node();
      if (!walked.insert(node.get_class_index()).second || livehd::graph_util::is_type_register(node)) {
        break;
      }
      const bool is_sub = livehd::graph_util::type_op_of(node) == Ntype_op::Sub;
      if (is_sub && !combinational(node)) {
        break;
      }
      hhds::Pin_class next;
      int             n_drivers = 0;
      for (auto sink : node.inp_sorted_pins()) {
        for (const auto& drv : sink.get_driver_pins()) {
          if (!drv.is_const()) {
            next = drv;
            ++n_drivers;
          }
        }
      }
      if (n_drivers != 1) {
        break;  // a multi-operand node is the cone's root, not a polarity step
      }
      if (is_sub && queued.insert(node.get_class_index()).second) {
        cells.push_back(node);
      }
      pin = next;
    }
  }
  while (!pending.empty()) {
    const auto pin = pending.back();
    pending.pop_back();
    if (pin.is_invalid() || pin.is_const() || livehd::graph_util::is_graph_input_pin(pin)) {
      continue;
    }
    auto node = pin.get_master_node();
    if (!seen.insert(node.get_class_index()).second || livehd::graph_util::is_type_register(node)) {
      continue;
    }
    if (livehd::graph_util::type_op_of(node) == Ntype_op::Sub) {
      if (!combinational(node)) {
        continue;
      }
      if (queued.insert(node.get_class_index()).second) {
        cells.push_back(node);
      }
    }
    for (auto sink : node.inp_sorted_pins()) {
      // PLURAL: a compact loop's carry-in sink holds two drivers
      // (pass/legalize/legalize.cpp:301).
      for (const auto& drv : sink.get_driver_pins()) {
        pending.push_back(drv);
      }
    }
  }
  for (const auto& cell : cells) {
    // A replicated Sub stands for several occurrences and cannot be spliced;
    // it stays an opaque clock driver, which the clock analysis refuses.
    if (cell.is_loop_subnode()) {
      continue;
    }
    // Inline only the clock cone so phase analysis sees buffer/inverter
    // polarity and gates: from the design's own cell body when it holds one,
    // else from the --lib model (which lives outside the design library).
    const auto src = lib_splice_source(cell, sub_lib);
    if (!livehd::graph_util::inline_sub_instance(graph, cell, "pass.lec", src.override_def)) {
      return lib_splice_refusal(cell, src, "clock-cone");
    }
  }
  return {};
}

// Stateful --lib models must contribute real state before the encoder cuts
// flops; an opaque instance would otherwise leave unrelated free symbols.
std::string inline_stateful_lib_cells(const absl::flat_hash_map<hhds::Gid, hhds::Graph*>& sub_lib, hhds::Graph* impl_g) {
  if (sub_lib.empty() || impl_g == nullptr) {
    return {};
  }
  // Classified on the body that would be SPLICED (lib_splice_source): the
  // design's own definition when it has one, else the --lib model.
  absl::flat_hash_map<hhds::Graph*, bool> stateful_memo;
  auto                                    stateful = [&](const hhds::Node_class& sub) {
    if (!sub_lib.contains(sub.get_subnode_gid())) {
      return false;
    }
    auto* body = lib_splice_body(sub, sub_lib);
    if (body == nullptr) {
      return false;
    }
    auto [it, fresh] = stateful_memo.try_emplace(body, false);
    if (fresh) {
      for (auto dn : body->body().nodes(hhds::Node_order::forward)) {
        const auto op = livehd::graph_util::type_op_of(dn);
        if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch || op == Ntype_op::Memory) {
          it->second = true;
          break;
        }
      }
    }
    return it->second;
  };
  std::set<std::string>         hit;    // sorted: the message must be deterministic
  std::vector<hhds::Node_class> insts;  // collect first: never mutate while walking
  for (auto sn : impl_g->body().nodes()) {
    if (livehd::graph_util::type_op_of(sn) != Ntype_op::Sub || !stateful(sn)) {
      continue;
    }
    insts.push_back(sn);
    if (auto sio = sn.get_subnode_io(); sio != nullptr) {
      hit.insert(std::string(sio->get_name()));
    }
  }
  const size_t n = insts.size();
  if (n == 0) {
    return {};
  }
  std::string names;
  for (const auto& s : hit) {
    names += names.empty() ? "" : ", ";
    names += s;
  }
  // INLINE them instead of blackboxing. encode.cpp's `--lib` inline path is
  // combinational-only (a stateful model falls through to the blackbox path,
  // where it contributes NO state and nothing can correspond to the ref's native
  // flop — both engines then answer unknown in milliseconds). Splicing the cell
  // body into the impl turns its internal Flop into an ordinary body flop, which
  // the existing flop-cut machinery cuts and names after the instance — and
  // pass/abc/abc_map.cpp already names each mapped DFF instance after its source
  // register bit. Same move `inline_clock_gate_cells` makes for an ICG cell; the
  // only reason it could not reach these is that a `--lib` model is not in the
  // impl's own graph library, hence the explicit-def overload -- used ONLY for
  // a body-less instance (lib_splice_source). An instance that carries its own
  // body is spliced from it, never from the model.
  size_t done = 0;
  for (const auto& inst : insts) {
    const auto src = lib_splice_source(inst, sub_lib);
    if (src.body == nullptr) {
      continue;
    }
    // A replicated Sub stands for several occurrences and cannot be spliced:
    // it stays a stateless blackbox, reported below (an honest UNKNOWN).
    if (inst.is_loop_subnode()) {
      continue;
    }
    // The cell's own instance name is the ONLY meaningful name the spliced state
    // can carry: a gensim cell model's internal flop has no `name` attr, so
    // Sub_inliner::carry_node_attrs leaves it unnamed and the flop cut ends up
    // keyed on a synthesized net name (`n1831`) that corresponds to nothing on
    // the ref side. Snapshot the existing flops, inline, then name whatever flop
    // appeared after the instance (`id_q_0`) — abc already named the instance
    // after the source register bit.
    // Only a SINGLE-flop model may take the instance name: stamping it on two
    // flops would fuse two distinct state cuts onto one key and silently drop a
    // compare point. A multi-flop cell keeps whatever the inliner produced.
    // Counted on the spliced BODY (a handful of nodes), and the naming itself
    // happens inside the inliner — re-walking the whole parent body once per
    // instance would be quadratic on a design with thousands of mapped cells.
    // An own body's flop usually arrives NAMED (`x[3].flop_16`); the inliner
    // names only an unnamed one, and core/bus_name reads the named shape.
    int model_flops = 0;
    for (auto dn : src.body->body().nodes(hhds::Node_order::forward)) {
      model_flops += livehd::graph_util::is_type_flop(dn) ? 1 : 0;
    }
    if (!livehd::graph_util::inline_sub_instance(impl_g,
                                                 inst,
                                                 "pass.lec",
                                                 src.override_def,
                                                 model_flops == 1,
                                                 /*prefix_instance=*/true,
                                                 /*inherit_color=*/false,
                                                 src.state_name)) {
      return lib_splice_refusal(inst, src, "stateful");
    }
    ++done;
  }
  if (done == n) {
    return {};  // fully inlined: the cells are ordinary logic + flops now
  }
  livehd::diag::warn("pass.lec", "stateful-lib-cell", "unsupported")
      .msg("the impl instantiates {} STATEFUL library cell(s) ({}) — lec could inline only {} of them", n, names, done)
      .hint(
          "a cell model that stays a blackbox contributes no state, so nothing corresponds to the ref's native flop "
          "and the run is INCONCLUSIVE no matter the budget; re-synthesize with `--set pass.abc.register=false` to "
          "keep registers native")
      .emit();
  return {};
}

// Bring every INTEGRATED CLOCK GATE into a body the analyses can see, across
// the top AND each def the encoder will meet, and fold the defs that gained one.
// Returns {cells inlined, defs folded}.
//
// Inlining the top alone holds only for a design that instantiates its gate AT
// the top. A real one puts it further down (minion instantiates `prim_clk_gate`
// inside `minion_dcache_reduce`, `txfma_top`, `vpu_trans` and 8 more), and there
// the top body holds no cell at all -- so nothing was inlined, every gated flop
// kept an opaque `Sub` for a clock, and the encoder refused each of those defs.
//
// Folding is not optional once a def is inlined: the cell's enable latch lands
// in the def's body, and the def scan refuses ANY def holding a latch, so
// inlining alone would trade an encode refusal for a normalization refusal.
//
// P=1 IS THE WHOLE SAFETY ARGUMENT. A gate has ONE commit edge, so folding it
// into an enable is a pure retype -- no phase divider, no re-timing, hence none
// of the cross-module timing question that keeps the GENERAL per-def case (a
// genuine latch or a negedge flop, P>1) refusing. The dry run enforces exactly
// that: a def whose plan wants a divider is left untouched for the refusal to
std::pair<int, int> inline_clock_gates_and_fold(hhds::Graph* top, const std::vector<hhds::Graph*>& defs,
                                                       absl::flat_hash_set<hhds::Graph*>*             unfolded,
                                                       const std::function<bool(const hhds::Graph*)>& is_boxed) {
  int                               cells  = livehd::latch_contract::inline_clock_gate_cells(top, "pass.single_edge", is_boxed);
  int                               folded = 0;
  // Dedupe: the ref and impl def lists share every `--lib` cell model, and a def
  // reached twice is the same Graph*. Inlining is idempotent, but the COUNT
  // would double and read as twice the work.
  absl::flat_hash_set<hhds::Graph*> seen{top};
  for (auto* d : defs) {
    if (d == nullptr || !seen.insert(d).second) {
      continue;
    }
    // STRICTLY ADDITIVE: a def that already holds a latch or a negedge flop is
    // one the def scan refuses TODAY, and that refusal is load-bearing (it is
    // what keeps a latch def from being silently blackboxed). Leave it exactly
    // as it was and let the scan speak. We only ever touch defs that pass the
    // scan today, so nothing that passes now can start failing.
    if (const auto pre = livehd::latch_contract::needs_single_edge(d); pre.n_latches > 0 || pre.n_negedge_flops > 0) {
      continue;
    }
    // PREDICT the fold failure instead of discovering it after mutating. The
    // inline is DESTRUCTIVE and has no undo, so a def whose fold then fails is
    // handed back holding an enable Latch it did NOT have when we found it —
    // and while `unfolded` keeps it out of the def SCAN, the ENCODER still
    // refuses a Latch, so a def that used to encode cleanly (an opaque gate
    // cell whose gated clock only crosses into a child) regresses from PROVEN
    // to UNKNOWN purely because this ran. "Nothing that passes now can start
    // failing" only holds for defs whose fold succeeds.
    //
    // The dominant failure is the documented one: resolve_icg folds only in a
    // SINGLE-clock design (a gate on a second domain has no reference clock to
    // be relative to), after which the orphaned latch wants a divider. That is
    // decidable BEFORE touching anything.
    if (livehd::latch_contract::Design_clocks(d).n_clock_inputs() > 1) {
      continue;
    }
    const int nd = livehd::latch_contract::inline_clock_gate_cells(d, "pass.single_edge", is_boxed);
    if (nd <= 0) {
      continue;  // no gate here: leave the def byte-for-byte as it was
    }
    cells += nd;
    // An EMPTY allow-list: this call normalizes the def's OWN body only. A
    // latch deeper still is not this call's business -- the caller's top-level
    // scan walks the whole instance tree and refuses there, as before.
    livehd::single_edge::Options dp;
    dp.dry_run                        = true;
    dp.quiet                          = true;  // a def we then decline to fold must not print a refusal
    const auto                   plan = livehd::single_edge::normalize(d, {}, dp);
    livehd::single_edge::Options ao;
    ao.quiet = true;
    if (!plan.error && plan.applied && plan.slots == 1) {
      if (const auto done = livehd::single_edge::normalize(d, {}, ao); done.applied && !done.error) {
        ++folded;
        continue;
      }
    }
    // Could not fold (a second clock net, or a plan wanting a divider). The
    // gate's enable latch is now in this def's body, which the def scan would
    // refuse -- turning what is today a single UNKNOWN def into a refusal of
    // the WHOLE run. So hand the def back to the caller to keep OUT of that
    // scan: the encoder then meets it exactly as it does today and returns the
    // same honest per-def UNKNOWN (`sequential op 'latch' not supported yet`
    // rather than `derived clock` -- same verdict, different sentence), while
    // every def that did fold is a def that now proves.
    // Residual (not predicted above): the def is now mutated and there is no
    // rollback, so at minimum say so instead of leaving a silent regression.
    if (const auto post = livehd::latch_contract::needs_single_edge(d); post.n_latches > 0) {
      livehd::diag::warn("pass.single_edge", "icg-inline-not-folded", "unsupported")
          .msg(
              "def '{}' had its clock-gate cell inlined but the fold did not apply, so it now holds an enable latch it "
              "did not have before; it will encode as UNKNOWN rather than refuse",
              d->get_name())
          .hint("flatten the design, or trust this def, to get a verdict for it")
          .emit();
    }
    if (unfolded != nullptr) {
      unfolded->insert(d);
    }
  }
  return {cells, folded};
}

// One side dropped part of the other's hierarchy: inline, into each definition
// the other side still has, every instance whose definition the other library
// no longer holds, so both sides expose comparable machine state.
//
// Two producers do this to a netlist. `pass color flat` fuses the WHOLE
// hierarchy into ONE abc region, so the impl is a single graph while the ref
// keeps every child; `pass color synth` (and `lhd synth`) colors a flat view
// and partitions its logic into regions that can span source definitions. In
// both shapes the ref top owns only its own flops (dino: pc, cycleCount) while
// the impl top owns `pipeA_if_id.reg_0` and friends, so those are impl-only
// unpaired state, no flop bijection exists, the flop-cut inductive miter is
// never built, and the def degrades to a whole-design BMC that times out and
// falls to the flat retry (dino: ~8 s wasted before the cone pass proves it).
//
// Inlining is semantics-preserving and gives each spliced flop its hierarchical
// name (`pipeA_if_id.reg_0`), which is exactly what the netlist calls it, so
// tier-1 name pairing resolves them and the collapsed hierarchical proof goes
// through with the kept defs still boxed. An instance whose def the impl DOES
// keep is left alone: that pair proves def by def, and flattening it would
// throw away the decomposition that makes the proof tractable.
//
// This is deliberately source-format agnostic. It covers mapped netlists, but
// also the ordinary Verilog-vs-Pyrope shape where one front-end retains helper
// modules and the other has already flattened them. A hierarchy boundary is
// not part of the equivalence contract and must never manufacture a REFUTED
// verdict. `sub_lib` definitions are real mapped-cell vocabulary and are never
// treated as absorbed design hierarchy. Returns how many instances were
// spliced on `side`.
size_t inline_instances_missing_from_other_side(const absl::flat_hash_map<hhds::Gid, hhds::Graph*>& sub_lib,
                                                       const std::vector<std::shared_ptr<hhds::Graph>>&    side_graphs,
                                                       const std::vector<std::shared_ptr<hhds::Graph>>&    other_graphs,
                                                       hhds::Graph*                                        side_g,
                                                       absl::flat_hash_set<std::string>*                   absorbed) {
  // The defs the other side still has, by full name AND by entity tail (a
  // Pyrope graph keeps `file.entity`; the tail covers a flat Verilog side).
  absl::flat_hash_set<std::string> other_defs;
  for (const auto& sp : other_graphs) {
    if (sp) {
      const std::string full{sp->get_name()};
      other_defs.insert(full);
      other_defs.insert(str_tools::canonical_entity_name(full));
    }
  }
  auto other_has_def = [&](std::string_view def_name) {
    const std::string full{def_name};
    return other_defs.contains(full) || other_defs.contains(str_tools::canonical_entity_name(full));
  };
  // Every definition the other side still has gets the treatment, the top
  // included: an absorbed def is inlined at ALL its sites, so a kept child may
  // hold absorbed grandchildren too.
  std::vector<hhds::Graph*> hosts;
  if (side_g != nullptr) {
    hosts.push_back(side_g);
  }
  for (const auto& sp : side_graphs) {
    if (sp && sp.get() != side_g && other_has_def(sp->get_name()) && sub_lib.find(sp->get_gid()) == sub_lib.end()) {
      hosts.push_back(sp.get());
    }
  }
  // RUNAWAY GUARD ONLY -- it is not the termination argument. Termination comes
  // from the `spliced == 0` break below: a pass either inlines something or
  // stops. This exists solely so a hierarchy that somehow regenerates instances
  // fails loudly instead of spinning, so it must be UNREACHABLE for any real
  // design. It is NOT a bound on the instance count: a def instantiated N times
  // contributes N copies of its whole subtree, so the transitive total is a
  // PRODUCT down the hierarchy, not a sum. A first attempt used
  // (Sub count x 2) and fired at 292 splices on a legitimate `loop_roll_carry`
  // run that needed exactly that many -- turning a PROVEN into a hard error.
  size_t side_nodes = 0;
  for (const auto& sp : side_graphs) {
    if (!sp) {
      continue;
    }
    for ([[maybe_unused]] auto n : sp->body().nodes()) {
      ++side_nodes;
    }
  }
  const size_t splice_budget = 10000 + side_nodes * 100;

  // Cell classification depends on the definition, not its instance. Keep the
  // definitions alive while caching so a released graph cannot reuse its key.
  absl::flat_hash_map<hhds::Graph*, bool>   icg_defs;
  std::vector<std::shared_ptr<hhds::Graph>> icg_defs_keepalive;
  size_t done = 0;
  for (auto* host : hosts) {
    bool collect_all_candidates = false;
    // ONE SPLICE PER COLLECTION, then re-collect.
    //
    // `inline_sub_instance` MUTATES `host`, and every other handle in the
    // collected vector points into that same host. Splicing the whole batch
    // reused handles that the first splice had already invalidated, which
    // silently corrupted the model: with >=2 absorbed instances LEC reported a
    // counterexample that DOES NOT REPRODUCE IN SIMULATION. Swapping two
    // instantiation lines in the reference flipped REFUTED<->PROVEN with the
    // implementation byte-identical, while 20,052 random vectors showed zero
    // mismatch between source Verilog, ref netlist and impl netlist.
    // Collecting first is necessary (never mutate while walking) but NOT
    // sufficient -- the handles have to be re-derived after each mutation.
    //
    // Progress-bounded rather than a fixed round cap: the loop only continues
    // while a splice actually happened, so it cannot spin, and the old magic 64
    // silently truncated any host with more absorbed instances than that.
    while (true) {
      std::vector<hhds::Node_class> insts;  // collect first: never mutate while walking
      for (auto n : host->body().nodes()) {
        if (livehd::graph_util::type_op_of(n) != Ntype_op::Sub || sub_lib.find(n.get_subnode_gid()) != sub_lib.end()) {
          continue;  // a Liberty cell is the impl's vocabulary, never an absorbed def
        }
        auto sio = n.get_subnode_io();
        if (sio == nullptr || other_has_def(sio->get_name())) {
          continue;  // the other side kept this def: it pairs def by def
        }
        // A genuine BLACKBOX -- an assertion/diagnostic marker instance, a
        // memory macro, any def whose body this side does not hold either --
        // has nothing to splice, and inline_sub_instance answers a bodyless or
        // replicated Sub with a FATAL internal error that kills the whole run.
        // This used to be unreachable because the caller only ran on a `--lib`
        // mapped-netlist comparison; now that ANY design reaches here, leave
        // such an instance a boundary (it is a boundary on both sides anyway).
        auto def_g = n.get_subnode_graph();
        if (n.is_loop_subnode() || !def_g) {
          continue;
        }
        // An instantiated CLOCK GATE is not absorbed design hierarchy either: it
        // is the one recognized clock operator, and materialize_clock_cells (run
        // further down) is what turns it into the `Clock_cell` the encoder can
        // model. Dissolving it here leaves a plain derived-clock cone, the
        // encoder REFUSES the def, and a real difference downstream of the gate
        // comes back UNKNOWN instead of REFUTED (clock_cell_test case 6b).
        auto [icg, inserted] = icg_defs.try_emplace(def_g.get(), false);
        if (inserted) {
          icg->second = livehd::latch_contract::match_icg_def(def_g.get()).has_value();
          icg_defs_keepalive.push_back(def_g);
        }
        if (icg->second) {
          continue;
        }
        insts.push_back(n);
        // Most candidates splice immediately. Do not collect the entire host
        // only to discard all but its first handle after that mutation.
        if (!collect_all_candidates) {
          break;
        }
      }
      if (insts.empty()) {
        break;
      }
      if (done >= splice_budget) {
        livehd::diag::err("pass.lec", "inline-runaway", "internal")
            .msg("inlining absorbed defs into '{}' exceeded {} splices; refusing to continue",
                 std::string{host->get_name()},
                 splice_budget)
            .emit();
        break;
      }
      size_t spliced = 0;
      // Try candidates in order and STOP AT THE FIRST SUCCESS: that splice
      // invalidates every remaining handle, so the rest of `insts` is discarded
      // and re-derived on the next pass. A candidate that returns false did not
      // mutate anything (a real blackbox), so it is safe to try the next one --
      // otherwise one un-inlinable instance would mask every later one, which is
      // what the batch loop got right and a naive one-shot fix would lose.
      for (const auto& inst : insts) {
        if (spliced != 0) {
          break;
        }
        // Inlining prefixes spliced names with the instance's LOGICAL prefix
        // (graph_util::logical_instance_prefix). An unnamed instance -- such as
        // pass.color's `<host>__c<N>` region wrapper or any other generated
        // wrapper -- and a `__flat___*` instance are hierarchy-transparent: they
        // add no component, matching HHDS get_hier_name, so every preserved
        // register name keeps its flop correspondence after collapse. A named
        // instance keeps its `inst.` component. Two transparent occurrences of
        // one stateful def therefore share logical state names, and the encoder
        // refuses them as ambiguous rather than merging them (graph/README.md,
        // "Transparent hierarchy wrappers").
        const std::string def_name{inst.get_subnode_io()->get_name()};  // `inst` is gone after the splice
        if (livehd::graph_util::inline_sub_instance(host, inst, "pass.lec")) {
          ++spliced;
          if (absorbed != nullptr) {
            absorbed->insert(def_name);
          }
        }
      }
      if (spliced == 0) {
        if (!collect_all_candidates) {
          // A refused candidate must not hide a later inlinable instance.
          // Nothing mutated: retry with the original full collection.
          collect_all_candidates = true;
          continue;
        }
        break;  // nothing inlinable left (a real blackbox): stop rather than spin
      }
      icg_defs.erase(host);  // this definition's body changed
      done += spliced;
    }
  }
  return done;
}

namespace {

// Flatten `host`: splice every instance under it (a mapped DFF cell kept as an
// ordinary module, the INV cell that makes its clock a negedge, a
// register-holding child), so the phase divider needs no port threading and
// every clock cone is visible in the body. Semantics-preserving. Skips a boxed
// (collapsed/trusted) def, a bodyless blackbox, a compact loop and a clock-gate
// cell (recognized as a Clock_cell already). One splice per collection: a
// splice invalidates every other handle into `host`. Returns the number of
// instances spliced. Only run when a P>1 time base cannot be built otherwise.
size_t inline_instances_for_divider(hhds::Graph* host, const std::function<bool(const hhds::Graph*)>& is_boxed) {
  if (host == nullptr) {
    return 0;
  }
  size_t done = 0;
  while (done < 100000) {
    bool spliced = false;
    for (auto n : host->body().nodes()) {
      if (livehd::graph_util::type_op_of(n) != Ntype_op::Sub || n.is_loop_subnode()) {
        continue;
      }
      auto def_g = n.get_subnode_graph();
      if (!def_g || (is_boxed && is_boxed(def_g.get())) || livehd::latch_contract::match_icg_def(def_g.get())) {
        continue;
      }
      if (livehd::graph_util::inline_sub_instance(host, n, "pass.lec")) {
        spliced = true;
        ++done;
        break;  // `n` and the walk are stale now: re-collect
      }
    }
    if (!spliced) {
      break;
    }
  }
  return done;
}

}  // namespace

Time_base prepare_time_base(const Cell_models& sub_lib, hhds::Graph* ref, std::vector<hhds::Graph*>& ref_defs, hhds::Graph* impl,
                            std::vector<hhds::Graph*>& impl_defs, bool quiet_decline,
                            const std::function<bool(const hhds::Graph*)>& is_boxed) {
  Time_base tb;

  // Either input may be a mapped netlist. Expand explicit state/clock models
  // symmetrically before collecting cuts, including cells inside retained defs.
  // A splice the inliner refuses (the model does not bind to the instance by
  // port name and width) stops the run: nothing may be queried on a side whose
  // cell semantics were not resolved.
  auto inline_lib_cells = [&](hhds::Graph* top, const std::vector<hhds::Graph*>& defs) -> std::string {
    auto one = [&](hhds::Graph* g) -> std::string {
      if (auto e = inline_stateful_lib_cells(sub_lib, g); !e.empty()) {
        return e;
      }
      return inline_clock_lib_cells(sub_lib, g);
    };
    if (auto e = one(top); !e.empty()) {
      return e;
    }
    for (auto* d : defs) {
      if (d != nullptr && d != top && sub_lib.find(d->get_gid()) == sub_lib.end()) {
        if (auto e = one(d); !e.empty()) {
          return e;
        }
      }
    }
    return {};
  };
  auto lib_error = inline_lib_cells(ref, ref_defs);
  if (lib_error.empty()) {
    lib_error = inline_lib_cells(impl, impl_defs);
  }
  if (!lib_error.empty()) {
    tb.error      = std::move(lib_error);
    tb.error_hint = "a --lib model must declare every port of the instance by name and width; nothing was compared";
    return tb;
  }
  // CLOCK-GATE CELLS first. A real design instantiates its ICG
  // (`prim_clk_gate u_cg(.clk_i(clk), .en_i(en), .clk_o(gclk));`), so the gate
  // sits one module level away and the flop's clock_pin is an opaque Sub output
  // that nothing can recognize. Inlining just those cells brings the gate into
  // the body, where the M8 fold turns it into a flop enable. Semantics-
  // preserving on its own and idempotent, so it runs on BOTH sides before either
  // is probed -- symmetry matters here as much as anywhere.
  absl::flat_hash_set<hhds::Graph*> unfolded;
  auto                              note_gates = [&tb](std::string_view which, std::pair<int, int> r) {
    if (r.first > 0) {
      tb.recipe_steps.emplace_back(
          std::format("pass.single_edge inlined {} {} clock-gate cell(s), folded {} def(s)", r.first, which, r.second));
    }
  };
  // M9 recognition runs FIRST and on BOTH sides, since a gate recognized on one
  // side only would compare a Clock_cell against a Sub.
  if (const int mr = materialize_clock_cells_all(ref, ref_defs); mr > 0) {
    tb.recipe_steps.emplace_back(std::format("pass.single_edge recognized {} ref clock gate(s) as Clock_cell", mr));
  }
  if (const int mi = materialize_clock_cells_all(impl, impl_defs); mi > 0) {
    tb.recipe_steps.emplace_back(std::format("pass.single_edge recognized {} impl clock gate(s) as Clock_cell", mi));
  }
  note_gates("ref", inline_clock_gates_and_fold(ref, ref_defs, &unfolded, is_boxed));
  note_gates("impl", inline_clock_gates_and_fold(impl, impl_defs, &unfolded, is_boxed));
  absl::flat_hash_set<hhds::Graph*> exposed;
  for (auto* top : {ref, impl}) {
    if (exposed.insert(top).second) {
      expose_clock_outputs(top);
      recover_latch_resets(top);
    }
  }
  for (const auto* defs : {&ref_defs, &impl_defs}) {
    for (auto* def : *defs) {
      if (def != nullptr && (!is_boxed || !is_boxed(def)) && exposed.insert(def).second) {
        expose_clock_outputs(def);
        recover_latch_resets(def);
      }
    }
  }
  if (!unfolded.empty()) {
    auto drop = [&unfolded](std::vector<hhds::Graph*>& v) {
      std::erase_if(v, [&unfolded](hhds::Graph* d) { return unfolded.contains(d); });
    };
    drop(ref_defs);
    drop(impl_defs);
  }
  // The rewrite is PREFERRED when it applies: it also normalizes a sync reset
  // into the enable/din shape on BOTH sides, which is what lets state pairing
  // match a flop whose reset the other front-end spells in the body. BOTH sides
  // or NEITHER: a one-sided lowering compares two designs in different time
  // bases, which is the failure a decline exists to prevent.
  auto probe_side = [&](hhds::Graph* side, const std::vector<hhds::Graph*>& defs) {
    Options po;
    po.dry_run = true;
    po.quiet   = quiet_decline;
    return normalize(side, defs, po);
  };
  const auto pr = probe_side(ref, ref_defs);
  const auto pi = probe_side(impl, impl_defs);
  if (pr.error || pi.error) {
    tb.declined       = true;
    tb.declined_ref   = pr.error;
    tb.decline_reason = pr.error ? pr.reason : pi.reason;
    if (quiet_decline) {
      tb.recipe_steps.emplace_back(
          std::format("pass.lec phase_sched: 4-microstep schedule (edge normalization declined: {})", tb.decline_reason));
    }
    return tb;
  }
  if (!pr.applied && !pi.applied) {
    return tb;  // nothing to lower on either side: already one time base
  }
  // ONE time base for both sides: the max P either side needs. A side with
  // nothing of its own to lower still gets the divider and slot 0 -- its P=1
  // behavior embedded in the P-slot time base -- which keeps an all-posedge ref
  // comparable against a negedge impl instead of the two counting time
  // differently.
  Options ao;
  ao.force_slots        = std::max(pr.slots, pi.slots);
  // PROBE AGAIN AT THE SHARED P before rewriting anything. Each side was
  // planned at its OWN P above, and a side that needs nothing (P=1) never ran
  // the checks that only bite under a phase divider -- chiefly a STATEFUL
  // INSTANCE, which the divider cannot be threaded into. Forcing P=2 onto such a
  // side used to fail only AFTER the other side had been rewritten, and the run
  // stopped at "edge normalization failed" with no verdict: a negedge register
  // (behind an ICG) compared against its mapped netlist, whose DFF cells stay
  // ordinary module instances on the Verilog path.
  //
  // Such a side is flattened instead: inlining is semantics-preserving and puts
  // its flops -- and the inverter cells in their clock cones -- in the body,
  // where the divider and the commit-class walk reach them. Anything the
  // forced plan still refuses stays the hard error it was (the read-only phase
  // schedule is NOT a safe fallback here: it misreads a negedge register that
  // lives in a child clocked by an inverted parent net).
  if (ao.force_slots > 1) {
    Options fo = ao;
    fo.dry_run = true;
    fo.quiet   = true;
    auto probe_forced = [&](hhds::Graph* side, std::vector<hhds::Graph*>& defs, const char* which) {
      auto res = normalize(side, defs, fo);
      if (res.error) {
        if (const auto n = inline_instances_for_divider(side, is_boxed); n > 0) {
          tb.recipe_steps.emplace_back(std::format("pass.single_edge flattened {} {} instance(s) for P={}",
                                                   n,
                                                   which,
                                                   ao.force_slots));
          res = normalize(side, defs, fo);
        }
      }
      return res;
    };
    const auto fr = probe_forced(ref, ref_defs, "ref");
    const auto fi = ref == impl ? fr : probe_forced(impl, impl_defs, "impl");
    if (fr.error || fi.error) {
      tb.error = std::format("lec: edge normalization cannot put both sides in one P={} time base ({})",
                             ao.force_slots,
                             fr.error ? fr.reason : fi.reason);
      tb.error_hint = "the verdict would need a phase divider threaded through a module boundary; nothing was compared";
      return tb;
    }
  }
  const auto rn         = normalize(ref, ref_defs, ao);
  // SAME GRAPH OBJECT on both sides (`--impl X --ref X`, the vacuity-guard
  // idiom): normalizing again would find nothing left to lower and report
  // "skipped" with no slot count and no reference clock, which the agreement
  // checks below would misread as a disagreement.
  const bool same_graph = ref == impl;
  const auto in         = same_graph ? rn : normalize(impl, impl_defs, ao);
  if (rn.error || in.error) {
    tb.error = std::format("lec: edge normalization failed after planning ({})", rn.error ? rn.reason : in.reason);
    return tb;
  }
  // Both agreement checks apply only when BOTH sides actually normalized; a side
  // that legitimately had nothing to lower reports slots=1 and no reference
  // clock, and force_slots already put the two in one time base.
  if (rn.applied && in.applied && rn.slots != in.slots) {
    tb.error      = std::format("lec: edge normalization produced P={} on the ref side and P={} on the impl side", rn.slots, in.slots);
    tb.error_hint = "the two designs mix clock edges differently; compare like against like";
    return tb;
  }
  if (rn.applied && in.applied && rn.ref_clock != in.ref_clock) {
    // Slots are RELATIVE to a reference clock, so two sides normalized against
    // different clocks are in different time bases. The encoder models a single
    // clock as "commits every step" with no notion of clock IDENTITY, so a latch
    // gated by `clk` and one gated by `clk2` would encode identically and come
    // back falsely PROVEN.
    tb.error      = std::format("lec: the ref side normalizes against clock '{}' but the impl side against '{}'",
                           rn.ref_clock.empty() ? "<none>" : rn.ref_clock,
                           in.ref_clock.empty() ? "<none>" : in.ref_clock);
    tb.error_hint = "the two designs are clocked by different nets, so their slots do not denote the same instants";
    return tb;
  }
  tb.applied      = true;
  tb.slots        = rn.slots;
  tb.ref_latches  = rn.latches_retyped;
  tb.impl_latches = in.latches_retyped;
  return tb;
}

}  // namespace livehd::single_edge
