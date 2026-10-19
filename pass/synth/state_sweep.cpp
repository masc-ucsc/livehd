// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "state_sweep.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <format>
#include <print>
#include <deque>
#include <limits>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "attr_carry.hpp"
#include "bus_name.hpp"
#include "node_util.hpp"
#include "specialize.hpp"

namespace livehd::synth {

namespace {

namespace gu = livehd::graph_util;

// A set of bit positions. `tail_` means every position at or above 64 * words_.size()
// is a member too, so `all()` costs no storage and a wide unknown demand stays cheap.
// Every operation works a word at a time: a wide bus is a few hundred words.
class Mask {
public:
  static Mask all() {
    Mask m;
    m.tail_ = true;
    return m;
  }
  static Mask prefix(int64_t n) {
    Mask m;
    m.set_range(0, n);
    return m;
  }

  [[nodiscard]] bool any() const {
    return tail_ || std::ranges::any_of(words_, [](uint64_t w) { return w != 0; });
  }
  [[nodiscard]] bool unbounded() const { return tail_; }
  [[nodiscard]] bool test(int64_t i) const {
    if (i < 0) {
      return false;
    }
    const auto word = static_cast<size_t>(i >> 6);
    return word >= words_.size() ? tail_ : ((words_[word] >> (i & 63)) & 1) != 0;
  }
  // Highest member, -1 when empty; INT64_MAX for an unbounded set.
  [[nodiscard]] int64_t highest() const {
    if (tail_) {
      return std::numeric_limits<int64_t>::max();
    }
    for (size_t i = words_.size(); i-- > 0;) {
      if (words_[i] != 0) {
        return static_cast<int64_t>(i * 64 + 63 - static_cast<size_t>(std::countl_zero(words_[i])));
      }
    }
    return -1;
  }

  void set(int64_t i) { set_range(i, i + 1); }
  void set_range(int64_t lo, int64_t hi) {
    lo = std::max<int64_t>(lo, 0);
    if (hi <= lo) {
      return;
    }
    if (tail_ && static_cast<size_t>(lo >> 6) >= words_.size()) {
      return;  // already a member
    }
    const auto last = static_cast<size_t>((hi - 1) >> 6);
    if (last >= words_.size()) {
      if (tail_) {
        hi = static_cast<int64_t>(words_.size()) * 64;
        if (hi <= lo) {
          return;
        }
      } else {
        words_.resize(last + 1, 0);
      }
    }
    for (int64_t w = lo >> 6; w <= (hi - 1) >> 6; ++w) {
      const int64_t from = std::max<int64_t>(lo, w * 64) - w * 64;
      const int64_t to   = std::min<int64_t>(hi, w * 64 + 64) - w * 64;
      const uint64_t bits = (to - from == 64 ? ~uint64_t{0} : ((uint64_t{1} << (to - from)) - 1)) << from;
      words_[static_cast<size_t>(w)] |= bits;
    }
  }
  // True when this set grew.
  bool merge(const Mask& other) {
    bool changed = false;
    if (other.tail_ && !tail_) {
      tail_   = true;  // everything above the current storage becomes a member
      changed = true;
    }
    if (other.words_.size() > words_.size()) {
      words_.resize(other.words_.size(), tail_ && !other.tail_ ? ~uint64_t{0} : 0);
    }
    for (size_t i = 0; i < words_.size(); ++i) {
      const uint64_t theirs = i < other.words_.size() ? other.words_[i] : (other.tail_ ? ~uint64_t{0} : 0);
      if ((words_[i] | theirs) != words_[i]) {
        words_[i] |= theirs;
        changed = true;
      }
    }
    return changed;
  }

  // Members below `width`, as a bounded set.
  [[nodiscard]] Mask clipped(int64_t width) const {
    Mask out;
    if (width <= 0) {
      return out;
    }
    const auto count = static_cast<size_t>((width + 63) >> 6);
    out.words_.resize(count, 0);
    for (size_t i = 0; i < count; ++i) {
      out.words_[i] = word(i);
    }
    if ((width & 63) != 0) {
      out.words_.back() &= (uint64_t{1} << (width & 63)) - 1;
    }
    return out;
  }
  // Member i of the result is member i + lo of this set, for i in [0, hi - lo).
  [[nodiscard]] Mask window(int64_t lo, int64_t hi) const {
    Mask out;
    if (hi <= lo) {
      return out;
    }
    const int64_t width = hi - lo;
    const auto    count = static_cast<size_t>((width + 63) >> 6);
    out.words_.resize(count, 0);
    const auto first = static_cast<size_t>(lo >> 6);
    const int  shift = static_cast<int>(lo & 63);
    for (size_t i = 0; i < count; ++i) {
      uint64_t value = word(first + i) >> shift;
      if (shift != 0) {
        value |= word(first + i + 1) << (64 - shift);
      }
      out.words_[i] = value;
    }
    if ((width & 63) != 0) {
      out.words_.back() &= (uint64_t{1} << (width & 63)) - 1;
    }
    return out;
  }
  // Member i + by of the result is member i of this bounded set.
  [[nodiscard]] Mask shifted_up(int64_t by) const {
    Mask out;
    for (size_t w = 0; w < words_.size(); ++w) {
      if (words_[w] == 0) {
        continue;
      }
      const int64_t base = static_cast<int64_t>(w) * 64 + by;
      const int     lane = static_cast<int>(base & 63);
      const auto    at   = static_cast<size_t>(base >> 6);
      if (out.words_.size() < at + 2) {
        out.words_.resize(at + 2, 0);
      }
      out.words_[at] |= words_[w] << lane;
      if (lane != 0) {
        out.words_[at + 1] |= words_[w] >> (64 - lane);
      }
    }
    return out;
  }
  // This set without [lo, hi).
  [[nodiscard]] Mask without(int64_t lo, int64_t hi) const {
    Mask out = *this;
    lo       = std::max<int64_t>(lo, 0);
    for (int64_t w = lo >> 6; hi > lo && w <= (hi - 1) >> 6 && static_cast<size_t>(w) < out.words_.size(); ++w) {
      const int64_t  from = std::max<int64_t>(lo, w * 64) - w * 64;
      const int64_t  to   = std::min<int64_t>(hi, w * 64 + 64) - w * 64;
      const uint64_t bits = (to - from == 64 ? ~uint64_t{0} : ((uint64_t{1} << (to - from)) - 1)) << from;
      out.words_[static_cast<size_t>(w)] &= ~bits;
    }
    if (tail_ && hi > static_cast<int64_t>(words_.size()) * 64) {
      // Removing a range out of the unbounded tail: keep the storage explicit up to hi.
      const auto count = static_cast<size_t>((hi + 63) >> 6);
      const auto have  = out.words_.size();
      out.words_.resize(count, ~uint64_t{0});
      for (size_t w = have; w < count; ++w) {
        const int64_t from = std::max<int64_t>(lo, static_cast<int64_t>(w) * 64) - static_cast<int64_t>(w) * 64;
        const int64_t to   = std::min<int64_t>(hi, static_cast<int64_t>(w) * 64 + 64) - static_cast<int64_t>(w) * 64;
        if (to > from) {
          const uint64_t bits = (to - from == 64 ? ~uint64_t{0} : ((uint64_t{1} << (to - from)) - 1)) << from;
          out.words_[w] &= ~bits;
        }
      }
    }
    return out;
  }

private:
  [[nodiscard]] uint64_t word(size_t i) const { return i < words_.size() ? words_[i] : (tail_ ? ~uint64_t{0} : 0); }

  std::vector<uint64_t> words_;
  bool                  tail_ = false;
};

struct Def;

struct Instance {
  Def*             parent = nullptr;
  hhds::Node_class node;
};

struct Def {
  std::shared_ptr<hhds::Graph>                 graph;
  std::shared_ptr<hhds::GraphIO>               io;
  bool                                         skip = false;  // never swept, never narrows what it exposes
  absl::flat_hash_map<hhds::Class_index, Mask> demand;       // driver pin -> bits something reads
  absl::flat_hash_map<hhds::Port_id, Mask>     in_demand;    // input port -> bits the body reads
  absl::flat_hash_map<hhds::Port_id, Mask>     out_demand;   // output port -> bits the instances read
  std::vector<Instance>                        instances;
  absl::flat_hash_set<hhds::Class_index>       queued;
  bool                                         output_dirty = false;
};

bool keeps_unobserved(Ntype_op op) {
  // State or structure the sweep never removes: instances, memories, fluid
  // flops and attribute cells observe or carry more than a data value.
  return op == Ntype_op::Sub || op == Ntype_op::Memory || op == Ntype_op::Fflop || op == Ntype_op::AttrSet || op == Ntype_op::IO
         || op == Ntype_op::Invalid;
}

bool is_register(Ntype_op op) { return op == Ntype_op::Flop || op == Ntype_op::Latch; }

class Sweeper {
public:
  Sweeper(std::span<const std::shared_ptr<hhds::Graph>> definitions, std::string_view top, Preparation_budget* budget)
      : budget_(budget) {
    for (const auto& graph : definitions) {
      if (!graph) {
        continue;
      }
      const auto [it, added] = index_.emplace(graph->get_gid(), defs_.size());
      if (!added) {
        continue;
      }
      auto& def = defs_.emplace_back();
      def.graph = graph;
      def.io    = graph->get_io();
    }
    for (const auto& graph : definitions) {
      if (graph && !top.empty() && graph->get_name() == top) {
        root_gids_.insert(graph->get_gid());
      }
    }
  }

  std::optional<State_sweep_stats> run() {
    State_sweep_stats folded;
    if (!fold_constant_registers(folded)) {
      return std::nullopt;
    }
    auto stats = run_demand();
    if (stats) {
      stats->registers_constant = folded.registers_constant;
      stats->constant_bits      = folded.constant_bits;
    }
    return stats;
  }

private:
  std::optional<State_sweep_stats> run_demand() {
    if (!link()) {
      return std::nullopt;
    }
    seed();
    if (!solve()) {
      return std::nullopt;
    }
    if (std::getenv("LIVEHD_STATE_SWEEP_DEBUG") != nullptr) {
      for (auto& def : defs_) {
        std::string outs;
        for (const auto& [port, m] : def.out_demand) {
          outs += std::format(" o{}:{}", port, m.unbounded() ? std::string{"all"} : std::to_string(m.highest() + 1));
        }
        std::print(stderr, "state-sweep def {} skip={} instances={}{}\n", def.graph->get_name(), def.skip, def.instances.size(),
                   outs);
      }
    }
    return apply();
  }

  // ---- constant registers -------------------------------------------------
  // A register whose D is the constant c holds c from its first load on, and holds nothing else when its reset or
  // power-on value is c as well (or it has none): every reader sees c. Yosys's opt_dff does this; here it is the
  // `enable ? 0 : q` registers of generated RTL (a size field set to 0 or a parameter) that enableopt left with a constant D.
  // The constant then folds into the readers, which can make more registers constant, so the definition is refolded and
  // the scan repeats.
  static std::optional<Dlop> constant_register_value(const hhds::Node_class& flop) {
    const auto q     = flop.get_driver_pin(0);
    const auto width = gu::bits_of(q);
    if (q.is_invalid() || width < 1) {
      return std::nullopt;
    }
    for (auto name : {"pipe_min", "pipe_max"}) {
      const auto depth = gu::get_driver_of_sink_name(flop, name);
      if (!depth.is_invalid() && const_i64(depth) != std::optional<int64_t>{1}) {
        return std::nullopt;
      }
    }
    const auto din = gu::get_driver_of_sink_name(flop, "din");
    if (din.is_invalid() || !din.is_const() || gu::const_of(din).has_unknowns() || !gu::const_of(din).is_just_i64()) {
      return std::nullopt;
    }
    const int64_t raw      = gu::const_of(din).to_just_i64();
    int64_t       value    = raw;
    int64_t       reduced  = raw;
    if (width < 63) {
      const int64_t mask = (int64_t{1} << width) - 1;
      reduced            = raw & mask;
      value              = !gu::is_unsign(q) && ((reduced >> (width - 1)) & 1) != 0 ? reduced - (int64_t{1} << width) : reduced;
    } else if (raw != 0) {
      return std::nullopt;
    }
    const auto initial   = gu::get_driver_of_sink_name(flop, "initial");
    const auto reset     = gu::get_driver_of_sink_name(flop, "reset_pin");
    int64_t    held      = 0;  // the reset value when there is a reset and no value: zero
    if (!initial.is_invalid()) {
      if (!initial.is_const() || gu::const_of(initial).has_unknowns() || !gu::const_of(initial).is_just_i64()) {
        return std::nullopt;
      }
      held = gu::const_of(initial).to_just_i64();
      if (width < 63) {
        held &= (int64_t{1} << width) - 1;
      }
    }
    if ((!initial.is_invalid() || !reset.is_invalid()) && held != reduced) {
      return std::nullopt;
    }
    return *Dlop::create_integer(value);
  }

  bool fold_constant_registers(State_sweep_stats& stats) {
    for (auto& def : defs_) {
      std::optional<absl::flat_hash_set<uint64_t>> existed;
      for (int round = 0; round < 8; ++round) {
        std::vector<std::pair<hhds::Node_class, Dlop>> constants;
        for (auto node : def.graph->body().nodes()) {
          if (!admit("state-sweep-constant")) {
            return false;
          }
          if (gu::type_op_of(node) == Ntype_op::Flop) {
            if (auto value = constant_register_value(node)) {
              constants.emplace_back(node, std::move(*value));
            }
          }
        }
        if (constants.empty()) {
          break;
        }
        if (!existed) {
          existed.emplace();
          for (auto node : def.graph->body().nodes()) {
            existed->insert(static_cast<uint64_t>(node.get_debug_nid()));
          }
        }
        for (const auto& [flop, value] : constants) {
          const auto q = flop.get_driver_pin(0);
          std::vector<hhds::Pin_class> readers;
          for (auto edge : q.out_edges()) {
            readers.push_back(edge.sink);
          }
          if (!readers.empty()) {
            const auto constant = gu::create_const(*def.graph, value);
            for (const auto& sink : readers) {
              sink.del_sink(q);
              constant.connect_sink(sink);
            }
          }
          ++stats.registers_constant;
          stats.constant_bits += static_cast<uint64_t>(gu::bits_of(q));
          gu::drop_drivers(flop);
          flop.del_node();
        }
        specialize::refold_private_body(def.graph);
        specialize::inherit_colors(*def.graph, &*existed);
      }
    }
    return true;
  }

private:
  Preparation_budget*                         budget_;
  std::deque<Def>                             defs_;
  absl::flat_hash_map<hhds::Gid, size_t>      index_;
  absl::flat_hash_set<hhds::Gid>              root_gids_;
  std::deque<std::pair<Def*, hhds::Node_class>> work_;
  std::deque<Def*>                            output_work_;

  Def* find(hhds::Gid gid) {
    const auto it = index_.find(gid);
    return it == index_.end() ? nullptr : &defs_[it->second];
  }

  bool admit(std::string_view stage, uint64_t work = 1) { return admit_preparation(budget_, stage, work); }

  // ---- widths and signs ---------------------------------------------------
  // The declared container of a driver pin; 0 when nothing is stamped.
  static int64_t pin_width(Def& def, const hhds::Pin_class& drv) {
    if (gu::is_graph_input_pin(drv)) {
      const auto name = drv.get_pin_name();
      return def.io && !name.empty() ? static_cast<int64_t>(def.io->get_bits(name)) : 0;
    }
    if (auto bits = gu::bits_of(drv); bits != 0) {
      return bits;
    }
    // An instance output without its own stamp reads the callee's declaration.
    const auto node = drv.get_master_node();
    if (gu::type_op_of(node) == Ntype_op::Sub) {
      if (const auto sio = node.get_subnode_io(); sio) {
        for (const auto& decl : sio->get_output_pin_decls()) {
          if (decl.port_id == drv.get_port_id()) {
            return static_cast<int64_t>(sio->get_bits(decl.name));
          }
        }
      }
    }
    return 0;
  }
  static bool pin_unsigned(Def& def, const hhds::Pin_class& drv) {
    if (gu::is_graph_input_pin(drv)) {
      const auto name = drv.get_pin_name();
      return def.io && !name.empty() ? def.io->is_unsign(name) : true;
    }
    return gu::is_unsign(drv);
  }

  // ---- linking ------------------------------------------------------------
  bool link() {
    for (auto& def : defs_) {
      for (auto node : def.graph->body().nodes()) {
        if (!admit("state-sweep-link")) {
          return false;
        }
        if (gu::type_op_of(node) != Ntype_op::Sub) {
          continue;
        }
        auto* child = node.get_subnode_graph() ? find(node.get_subnode_graph()->get_gid()) : nullptr;
        if (child == nullptr) {
          continue;
        }
        if (node.is_loop_subnode()) {
          child->skip = true;  // compact loops keep their carries, ordinals and cut bits
          continue;
        }
        child->instances.push_back({&def, node});
      }
    }
    // A loop body is neither swept nor narrowed, and neither is whatever it holds.
    for (bool again = true; again;) {
      again = false;
      for (auto& def : defs_) {
        if (!def.skip) {
          continue;
        }
        for (auto node : def.graph->body().nodes()) {
          if (gu::type_op_of(node) == Ntype_op::Sub) {
            if (auto* child = node.get_subnode_graph() ? find(node.get_subnode_graph()->get_gid()) : nullptr;
                child != nullptr && !child->skip) {
              child->skip = true;
              again       = true;
            }
          }
        }
      }
    }
    return true;
  }

  // ---- demand -------------------------------------------------------------
  void push(Def& def, const hhds::Node_class& node) {
    if (node.is_invalid() || gu::is_builtin_node(node)) {
      return;
    }
    if (def.queued.insert(node.get_class_index()).second) {
      work_.emplace_back(&def, node);
    }
  }

  void note_output_dirty(Def& def) {
    if (!def.output_dirty) {
      def.output_dirty = true;
      output_work_.push_back(&def);
    }
  }

  // Positions `wanted` of driver `drv` are read. Positions beyond the pin's declared
  // container read its sign (signed) or zero (unsigned) extension.
  void add_demand(Def& def, const hhds::Pin_class& drv, const Mask& wanted) {
    if (drv.is_invalid() || drv.is_const() || !wanted.any()) {
      return;
    }
    const int64_t width = pin_width(def, drv);
    Mask          eff;
    if (width <= 0) {
      eff = Mask::all();
    } else {
      eff = wanted.clipped(width);
      if (wanted.highest() >= width && !pin_unsigned(def, drv)) {
        eff.set(width - 1);
      }
    }
    if (!eff.any()) {
      return;
    }
    if (gu::is_graph_input_pin(drv)) {
      if (def.in_demand[drv.get_port_id()].merge(eff)) {
        for (const auto& inst : def.instances) {
          push(*inst.parent, inst.node);
        }
      }
      return;
    }
    if (def.demand[drv.get_class_index()].merge(eff)) {
      push(def, drv.get_master_node());
    }
  }

  void add_all(Def& def, const hhds::Pin_class& drv) { add_demand(def, drv, Mask::all()); }

  void demand_all_inputs(Def& def, const hhds::Node_class& node) {
    for (auto sink : node.inp_sorted_pins()) {
      for (auto drv : sink.get_driver_pins()) {
        add_all(def, drv);
      }
    }
  }

  Mask output_demand(Def& def, const hhds::Node_class& node) {
    Mask out;
    for (auto pin : node.out_sorted_pins()) {
      if (auto it = def.demand.find(pin.get_class_index()); it != def.demand.end()) {
        out.merge(it->second);
      }
    }
    return out;
  }

  static bool single_output(const hhds::Node_class& node) {
    for (auto pin : node.out_sorted_pins()) {
      if (pin.get_port_id() != 0) {
        return false;
      }
    }
    return true;
  }

  static std::optional<int64_t> const_i64(const hhds::Pin_class& pin) {
    if (pin.is_invalid() || !pin.is_const()) {
      return std::nullopt;
    }
    const auto& c = gu::const_of(pin);
    if (c.has_unknowns() || !c.is_just_i64()) {
      return std::nullopt;
    }
    return c.to_just_i64();
  }

  void propagate(Def& def, const hhds::Node_class& node) {
    const auto op = gu::type_op_of(node);
    if (op == Ntype_op::Sub) {
      propagate_instance(def, node);
      return;
    }
    if (keeps_unobserved(op) || !single_output(node)) {
      demand_all_inputs(def, node);
      return;
    }
    const Mask m = output_demand(def, node);
    if (!m.any()) {
      return;
    }
    const int64_t top = m.highest();
    switch (op) {
      case Ntype_op::And:
      case Ntype_op::Or:
      case Ntype_op::Xor:
      case Ntype_op::Not:
        for (auto sink : node.inp_sorted_pins()) {
          for (auto drv : sink.get_driver_pins()) {
            add_demand(def, drv, m);
          }
        }
        return;
      case Ntype_op::Sum:
      case Ntype_op::Mult: {
        // Bit i of a sum or product reads bits 0..i of every operand.
        const Mask low = top == std::numeric_limits<int64_t>::max() ? Mask::all() : Mask::prefix(top + 1);
        for (auto sink : node.inp_sorted_pins()) {
          for (auto drv : sink.get_driver_pins()) {
            add_demand(def, drv, low);
          }
        }
        return;
      }
      case Ntype_op::Mux:
      case Ntype_op::Hotmux: {
        const auto control_end = op == Ntype_op::Hotmux ? gu::hotmux_control_end(node) : size_t{1};
        for (auto sink : node.inp_sorted_pins()) {
          const bool control
              = op == Ntype_op::Hotmux ? gu::is_hotmux_control(sink.get_port_id(), control_end) : sink.get_port_id() == 0;
          for (auto drv : sink.get_driver_pins()) {
            if (control) {
              add_all(def, drv);
            } else {
              add_demand(def, drv, m);
            }
          }
        }
        return;
      }
      case Ntype_op::Get_mask: {
        const auto lo = const_i64(gu::get_driver_of_sink_name(node, "lo"));
        const auto hi = const_i64(gu::get_driver_of_sink_name(node, "hi"));
        const auto a  = gu::get_driver_of_sink_name(node, "a");
        if (!lo || !hi || *lo < 0 || *hi <= *lo) {
          demand_all_inputs(def, node);
          return;
        }
        // Result bit i is operand bit lo + i for i below hi - lo; nothing above.
        add_demand(def, a, m.window(0, *hi - *lo).shifted_up(*lo));
        for (auto name : {"lo", "hi"}) {
          add_all(def, gu::get_driver_of_sink_name(node, name));
        }
        return;
      }
      case Ntype_op::Set_mask: {
        const auto lo = const_i64(gu::get_driver_of_sink_name(node, "lo"));
        const auto hi = const_i64(gu::get_driver_of_sink_name(node, "hi"));
        if (!lo || !hi || *lo < 0 || *hi <= *lo) {
          demand_all_inputs(def, node);
          return;
        }
        // Inside [lo, hi) the result is the replacement's low bits; elsewhere the base.
        add_demand(def, gu::get_driver_of_sink_name(node, "a"), m.without(*lo, *hi));
        add_demand(def, gu::get_driver_of_sink_name(node, "value"), m.window(*lo, *hi));
        for (auto name : {"lo", "hi"}) {
          add_all(def, gu::get_driver_of_sink_name(node, name));
        }
        return;
      }
      case Ntype_op::Concat: {
        const auto lanes = gu::concat_lanes(node);
        if (lanes.empty()) {
          demand_all_inputs(def, node);
          return;
        }
        for (const auto& lane : lanes) {
          add_demand(def, lane.value, m.window(lane.offset, static_cast<int64_t>(lane.offset) + lane.width));
        }
        // The width operands are comptime constants; nothing to demand.
        return;
      }
      case Ntype_op::SHL: {
        // Only a constant left shift is followed: result bit i is operand bit i - amount.
        const auto amount = const_i64(gu::get_driver_of_sink_name(node, "b"));
        if (amount && *amount >= 0 && top != std::numeric_limits<int64_t>::max()) {
          add_demand(def, gu::get_driver_of_sink_name(node, "a"), m.window(*amount, top + 1));
        } else {
          demand_all_inputs(def, node);
        }
        return;
      }
      case Ntype_op::Sext: {
        // Sext(a, b) keeps a's low b bits and repeats bit b-1 above them.
        const auto keep = const_i64(gu::get_driver_of_sink_name(node, "b"));
        if (!keep || *keep < 1) {
          demand_all_inputs(def, node);
          return;
        }
        Mask wanted = m.window(0, *keep);
        if (top >= *keep) {
          wanted.set(*keep - 1);
        }
        add_demand(def, gu::get_driver_of_sink_name(node, "a"), wanted);
        add_all(def, gu::get_driver_of_sink_name(node, "b"));
        return;
      }
      case Ntype_op::SRA: {
        // Result bit i is operand bit i + amount (the sign above the operand), for a constant amount.
        const auto amount = const_i64(gu::get_driver_of_sink_name(node, "b"));
        if (amount && *amount >= 0 && top != std::numeric_limits<int64_t>::max()) {
          add_demand(def, gu::get_driver_of_sink_name(node, "a"), m.clipped(top + 1).shifted_up(*amount));
          add_all(def, gu::get_driver_of_sink_name(node, "b"));
        } else {
          demand_all_inputs(def, node);
        }
        return;
      }
      case Ntype_op::Rxor:
      case Ntype_op::Popcount: {
        // Only the low b bits of a take part.
        const auto count = const_i64(gu::get_driver_of_sink_name(node, "b"));
        if (count && *count >= 0) {
          add_demand(def, gu::get_driver_of_sink_name(node, "a"), Mask::prefix(*count));
          add_all(def, gu::get_driver_of_sink_name(node, "b"));
        } else {
          demand_all_inputs(def, node);
        }
        return;
      }
      case Ntype_op::Flop:
      case Ntype_op::Latch: {
        // Bit i of the next value is bit i of D (or of the reset value); every
        // control is a one-bit signal.
        const auto din_pid     = static_cast<hhds::Port_id>(Ntype::get_sink_pid(op, "din"));
        const auto initial_pid = static_cast<hhds::Port_id>(Ntype::get_sink_pid(op, "initial"));
        for (auto sink : node.inp_sorted_pins()) {
          const bool bitwise = sink.get_port_id() == din_pid || sink.get_port_id() == initial_pid;
          for (auto drv : sink.get_driver_pins()) {
            if (bitwise) {
              add_demand(def, drv, m);
            } else {
              add_all(def, drv);
            }
          }
        }
        return;
      }
      default: demand_all_inputs(def, node); return;
    }
  }

  void propagate_instance(Def& def, const hhds::Node_class& node) {
    auto* child = node.get_subnode_graph() ? find(node.get_subnode_graph()->get_gid()) : nullptr;
    if (child == nullptr || child->skip || node.is_loop_subnode()) {
      demand_all_inputs(def, node);
      return;
    }
    for (auto pin : node.out_sorted_pins()) {
      const auto it = def.demand.find(pin.get_class_index());
      if (it != def.demand.end() && child->out_demand[pin.get_port_id()].merge(it->second)) {
        note_output_dirty(*child);
      }
    }
    for (auto sink : node.inp_sorted_pins()) {
      const auto it = child->in_demand.find(sink.get_port_id());
      if (it == child->in_demand.end()) {
        continue;
      }
      for (auto drv : sink.get_driver_pins()) {
        add_demand(def, drv, it->second);
      }
    }
  }

  void propagate_output_node(Def& def) {
    def.output_dirty = false;
    for (auto sink : def.graph->get_output_node().inp_sorted_pins()) {
      const auto it = def.out_demand.find(sink.get_port_id());
      if (it == def.out_demand.end()) {
        continue;
      }
      for (auto drv : sink.get_driver_pins()) {
        add_demand(def, drv, it->second);
      }
    }
  }

  void seed() {
    for (auto& def : defs_) {
      const bool is_root = def.skip || def.instances.empty() || root_gids_.contains(def.graph->get_gid());
      if (is_root && def.io) {
        for (const auto& decl : def.io->get_output_pin_decls()) {
          def.out_demand[decl.port_id] = Mask::all();
        }
      }
      note_output_dirty(def);
      for (auto node : def.graph->body().nodes()) {
        push(def, node);
      }
    }
  }

  bool solve() {
    while (!work_.empty() || !output_work_.empty()) {
      if (!admit("state-sweep-solve")) {
        return false;
      }
      if (!output_work_.empty()) {
        auto* def = output_work_.front();
        output_work_.pop_front();
        propagate_output_node(*def);
        continue;
      }
      auto [def, node] = work_.front();
      work_.pop_front();
      def->queued.erase(node.get_class_index());
      if (node.is_invalid()) {
        continue;
      }
      propagate(*def, node);
    }
    return true;
  }

  // ---- rewriting ----------------------------------------------------------
  std::optional<State_sweep_stats> apply() {
    State_sweep_stats stats;
    for (auto& def : defs_) {
      if (def.skip) {
        continue;
      }
      std::vector<hhds::Node_class> dead;
      std::vector<hhds::Node_class> registers;
      for (auto node : def.graph->body().nodes()) {
        if (!admit("state-sweep-scan")) {
          return std::nullopt;
        }
        const auto op = gu::type_op_of(node);
        trace_node(def, node);
        if (gu::is_builtin_node(node) || keeps_unobserved(op)) {
          continue;
        }
        if (!single_output(node) || output_demand(def, node).any()) {
          if (is_register(op)) {
            registers.push_back(node);
          }
          continue;
        }
        dead.push_back(node);
      }
      // Rewriting a register adds nodes: never while the body is being walked.
      for (const auto& node : registers) {
        trim(def, node, stats);
      }
      if (dead.empty()) {
        continue;
      }
      absl::flat_hash_set<hhds::Class_index> doomed;
      for (const auto& node : dead) {
        doomed.insert(node.get_class_index());
      }
      for (const auto& node : dead) {
        // A reader that stays but demands nothing of this cell (a replaced base,
        // an unread instance input) is tied low so no sink is left undriven.
        for (auto pin : node.out_pins_snapshot()) {
          std::vector<hhds::Pin_class> readers;
          for (auto edge : pin.out_edges()) {
            if (!doomed.contains(edge.sink.get_master_node().get_class_index())) {
              readers.push_back(edge.sink);
            }
          }
          for (const auto& sink : readers) {
            sink.del_sink(pin);
            gu::create_const(*def.graph, *Dlop::create_integer(0)).connect_sink(sink);
          }
        }
      }
      for (const auto& node : dead) {
        const auto op = gu::type_op_of(node);
        if (is_register(op)) {
          ++stats.registers_removed;
          stats.register_bits += static_cast<uint64_t>(std::max<int64_t>(1, gu::bits_of(node.create_driver_pin(0))));
        } else {
          ++stats.cells_removed;
        }
        gu::drop_drivers(node);
      }
      for (const auto& node : dead) {
        node.del_node();
      }
    }
    return stats;
  }

  static std::string ranges(const Mask& mask, int64_t limit) {
    std::string out;
    for (int64_t i = 0; i < limit; ++i) {
      if (mask.test(i)) {
        int64_t j = i;
        while (j + 1 < limit && mask.test(j + 1)) {
          ++j;
        }
        out += std::format("[{}:{}]", j, i);
        i = j;
      }
    }
    return out;
  }

  // Why is this cell observed? Print the readers that keep it alive, depth first.
  void explain(Def& def, const hhds::Node_class& node, int depth, absl::flat_hash_set<hhds::Class_index>& seen) {
    if (depth > 12 || !seen.insert(node.get_class_index()).second) {
      return;
    }
    const auto  op = gu::type_op_of(node);
    const Mask  m  = output_demand(def, node);
    const auto  q  = node.get_driver_pin(0);
    std::print(stderr,
               "{:{}}{} '{}' bits={} demand={}\n",
               "",
               depth * 2,
               Ntype::get_name(op),
               gu::node_name_of(node),
               gu::bits_of(q),
               ranges(m, std::max<int64_t>(gu::bits_of(q), 1)));
    if (keeps_unobserved(op) && depth > 0) {
      return;
    }
    int shown = 0;
    for (auto pin : node.out_sorted_pins()) {
      if (op == Ntype_op::Sub) {
        const auto it = def.demand.find(pin.get_class_index());
        std::print(stderr, "{:{}}output {} demand={}\n", "", depth * 2 + 1, pin.get_port_id(),
                   it == def.demand.end() ? std::string{} : ranges(it->second, std::max<int64_t>(gu::bits_of(pin), 1)));
      }
      for (auto edge : pin.out_edges()) {
        const auto reader = edge.sink.get_master_node();
        if (reader.is_invalid() || gu::is_builtin_node(reader)) {
          if (gu::is_graph_output_pin(edge.sink)) {
            std::print(stderr, "{:{}}-> output port {}\n", "", depth * 2 + 2, edge.sink.get_port_id());
          }
          continue;
        }
        if (!output_demand(def, reader).any() && !keeps_unobserved(gu::type_op_of(reader))) {
          continue;
        }
        if (++shown > 4) {
          std::print(stderr, "{:{}}...\n", "", depth * 2 + 2);
          return;
        }
        explain(def, reader, depth + 1, seen);
      }
    }
  }

  // LIVEHD_STATE_SWEEP_TRACE=<part of a cell or register name>, or =<whole name>: why do the matching cells stay observed?
  void trace_node(Def& def, const hhds::Node_class& node) {
    static const char* want = std::getenv("LIVEHD_STATE_SWEEP_TRACE");
    if (want == nullptr) {
      return;
    }
    const std::string_view pattern{want};  // "=name" matches the whole name, otherwise any part of it
    const auto             name = gu::node_name_of(node);
    if (pattern.starts_with('=') ? name != pattern.substr(1) : name.find(pattern) == std::string_view::npos) {
      return;
    }
    absl::flat_hash_set<hhds::Class_index> seen;
    explain(def, node, 0, seen);
  }

  // A register whose observed bits are few and scattered (a shift register read at
  // its far end) becomes one single-bit register per observed bit, each named
  // `reg[i]` as the bus-expansion standard spells the bit of a bus, plus a Concat
  // that reassembles the narrow value the readers saw. Returns false when the
  // register is not a plain one (pipeline stages, a non-constant or partly
  // unknown reset value, a signed value whose sign bit is observed).
  bool split_register(Def& def, const hhds::Node_class& node, const Mask& m, int64_t width, State_sweep_stats& stats) {
    constexpr int64_t kMaxBits  = 1024;  // single-bit registers one register may become
    constexpr int64_t kMinSaved = 8;     // register bits the split must drop to be worth the extra cells
    const int64_t     hi        = m.highest() + 1;
    int64_t           count     = 0;
    for (int64_t i = 0; i < hi; ++i) {
      count += m.test(i) ? 1 : 0;
    }
    // Holes below the highest observed bit are what narrowing cannot drop.
    if (hi > width || count > kMaxBits || hi - count < kMinSaved) {
      return false;
    }
    auto q = node.create_driver_pin(0);
    if (!gu::is_unsign(q) && m.test(width - 1)) {
      return false;
    }
    for (auto name : {"pipe_min", "pipe_max"}) {
      const auto depth = gu::get_driver_of_sink_name(node, name);
      if (!depth.is_invalid() && const_i64(depth) != std::optional<int64_t>{1}) {
        return false;
      }
    }
    const auto din     = gu::get_driver_of_sink_name(node, "din");
    const auto initial = gu::get_driver_of_sink_name(node, "initial");
    if (din.is_invalid() || (!initial.is_invalid() && (!initial.is_const() || gu::const_of(initial).has_unknowns()))
        || (din.is_const() && gu::const_of(din).has_unknowns())) {
      return false;
    }
    auto&             graph = *def.graph;
    const std::string base  = gu::wire_name(q);
    const auto        din_pid     = static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, "din"));
    const auto        initial_pid = static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, "initial"));
    std::vector<hhds::Pin_class> bit_q(static_cast<size_t>(hi));
    for (int64_t i = 0; i < hi; ++i) {
      if (!m.test(i)) {
        continue;
      }
      auto bit = gu::create_typed_node(graph, Ntype_op::Flop);
      gu::carry_node_attrs(node, bit);
      gu::carry_srcid(node, bit);
      const auto name = livehd::bus_name::bit(base, i);
      bit.attr(hhds::attrs::name).set(name);
      for (auto sink : node.inp_sorted_pins()) {
        if (sink.get_port_id() == din_pid || sink.get_port_id() == initial_pid) {
          continue;
        }
        for (auto drv : sink.get_driver_pins()) {
          gu::setup_sink_pid(bit, sink.get_port_id()).connect_driver(drv);
        }
      }
      hhds::Pin_class d;
      if (din.is_const()) {
        d = gu::create_const(graph, *Dlop::create_integer(gu::const_of(din).bit_test(static_cast<int>(i)) ? 1 : 0));
      } else {
        auto get = gu::create_get_mask(graph, din, static_cast<int>(i), static_cast<int>(i) + 1);
        carry_color(node, get);
        d = get.create_driver_pin(0);
        gu::set_ubits(d, 1);
      }
      gu::setup_sink_pid(bit, din_pid).connect_driver(d);
      if (!initial.is_invalid()) {
        gu::setup_sink_pid(bit, initial_pid)
            .connect_driver(
                gu::create_const(graph, *Dlop::create_integer(gu::const_of(initial).bit_test(static_cast<int>(i)) ? 1 : 0)));
      }
      auto bq = bit.create_driver_pin(0);
      gu::carry_pin_attrs(q, bq);
      gu::set_ubits(bq, 1);
      gu::set_pin_name(bq, name);
      bit_q[static_cast<size_t>(i)] = bq;
    }
    // MSB-first lanes: a run of unobserved bits is one constant-zero lane.
    auto concat = gu::create_typed_node(graph, Ntype_op::Concat);
    carry_color(node, concat);
    size_t lane = 0;
    auto   put  = [&](const hhds::Pin_class& value, int64_t w) {
      gu::setup_sink_pid(concat, static_cast<hhds::Port_id>(2 * lane)).connect_driver(value);
      gu::setup_sink_pid(concat, static_cast<hhds::Port_id>(2 * lane + 1))
          .connect_driver(gu::create_const(graph, *Dlop::create_integer(w)));
      ++lane;
    };
    for (int64_t i = hi - 1; i >= 0;) {
      if (m.test(i)) {
        put(bit_q[static_cast<size_t>(i)], 1);
        --i;
        continue;
      }
      int64_t run = 0;
      while (i - run >= 0 && !m.test(i - run)) {
        ++run;
      }
      put(gu::create_const(graph, *Dlop::create_integer(0)), run);
      i -= run;
    }
    auto whole = concat.create_driver_pin(0);
    gu::set_ubits(whole, static_cast<int32_t>(hi));
    std::vector<hhds::Pin_class> readers;
    for (auto edge : q.out_edges()) {
      readers.push_back(edge.sink);
    }
    for (const auto& sink : readers) {
      sink.del_sink(q);
      whole.connect_sink(sink);
    }
    gu::drop_drivers(node);
    node.del_node();
    ++stats.registers_split;
    stats.split_bits += static_cast<uint64_t>(count);
    stats.trimmed_bits += static_cast<uint64_t>(width - count);
    return true;
  }

  static void carry_color(const hhds::Node_class& from, const hhds::Node_class& to) {
    if (gu::has_color(from)) {
      gu::set_color(to, gu::color_of(from));
    }
  }

  void trim(Def& def, const hhds::Node_class& node, State_sweep_stats& stats) {
    auto          q     = node.create_driver_pin(0);
    const int64_t width = gu::bits_of(q);
    if (width <= 1) {
      return;
    }
    const Mask m = output_demand(def, node);
    if (m.unbounded()) {
      return;
    }
    const int64_t keep = std::min(m.highest() + 1, value_width(def, node, width));
    if (gu::type_op_of(node) == Ntype_op::Flop && keep == m.highest() + 1 && split_register(def, node, m, width, stats)) {
      return;
    }
    if (keep >= width) {
      return;
    }
    gu::set_ubits(q, static_cast<int32_t>(keep));
    ++stats.registers_trimmed;
    stats.trimmed_bits += static_cast<uint64_t>(width - keep);
  }

  // The bits a register can ever hold: its D is an unsigned value of k bits (zero-extended into
  // the wider register, a status or size field assigned from a narrow wire) and its reset value
  // fits too, so every bit above them is always zero. `width` when nothing is known.
  int64_t value_width(Def& def, const hhds::Node_class& node, int64_t width) {
    if (gu::type_op_of(node) != Ntype_op::Flop) {
      return width;
    }
    const auto din = gu::get_driver_of_sink_name(node, "din");
    if (din.is_invalid()) {
      return width;
    }
    auto constant_bits = [](const hhds::Pin_class& pin) -> int64_t {
      if (!pin.is_const()) {
        return -1;
      }
      const auto& c = gu::const_of(pin);
      if (c.has_unknowns() || !c.is_just_i64() || c.to_just_i64() < 0) {
        return -1;
      }
      return std::max<int64_t>(1, std::bit_width(static_cast<uint64_t>(c.to_just_i64())));
    };
    int64_t bits = -1;
    if (din.is_const()) {
      bits = constant_bits(din);
    } else if (pin_unsigned(def, din)) {
      bits = pin_width(def, din);
    }
    if (bits <= 0 || bits >= width) {
      return width;
    }
    if (const auto initial = gu::get_driver_of_sink_name(node, "initial"); !initial.is_invalid()) {
      const auto init_bits = constant_bits(initial);
      if (init_bits < 0) {
        return width;
      }
      bits = std::max(bits, init_bits);
    }
    return std::min(bits, width);
  }
};

}  // namespace

std::optional<State_sweep_stats> sweep_unobserved_state(std::span<const std::shared_ptr<hhds::Graph>> definitions,
                                                        std::string_view top, Preparation_budget* budget) {
  Sweeper sweeper(definitions, top, budget);
  return sweeper.run();
}

}  // namespace livehd::synth
