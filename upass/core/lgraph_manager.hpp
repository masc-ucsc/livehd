//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "const.hpp"
#include "hhds/graph.hpp"
#include "ir_adapter.hpp"
#include "node_util.hpp"

namespace upass {

// HHDS-graph manager used by the templated shared upass dataflow passes. Holds
// a shared_ptr<hhds::Graph>; mutation methods (fold_sum_const, fold_neutral,
// fold_sub_const, …) operate directly through HHDS APIs + graph_util helpers
// — no //lgraph dependency.
class Lgraph_manager final {
public:
  // Handles surfaced to the templated shared passes.
  using Node  = hhds::Node_class;
  using Input = hhds::Pin_class;

  struct Fold_scan_summary {
    std::size_t visited_nodes{0};
    std::size_t const_nodes{0};
    std::size_t arithmetic_nodes{0};
    std::size_t fold_candidate_nodes{0};
  };

  struct Fold_tag_summary {
    std::size_t tagged_nodes{0};
  };

  struct Fold_sum_const_summary {
    std::size_t folded_nodes{0};
    std::size_t rewired_edges{0};
    std::size_t new_const_nodes{0};
    std::size_t deleted_nodes{0};
  };

  struct Fold_neutral_summary {
    std::size_t simplified_to_driver{0};
    std::size_t simplified_to_const_zero{0};
    std::size_t simplified_to_const_one{0};
    std::size_t rewired_edges{0};
    std::size_t new_const_nodes{0};
    std::size_t deleted_nodes{0};
  };

  struct Fold_shift_div_summary {
    std::size_t simplified_to_driver{0};
    std::size_t simplified_to_const_zero{0};
    std::size_t simplified_to_const_one{0};
    std::size_t simplified_to_const_other{0};
    std::size_t rewired_edges{0};
    std::size_t new_const_nodes{0};
    std::size_t deleted_nodes{0};
  };

  // Summary for constant-multiplication folding on Mult nodes.
  struct Fold_mult_const_summary {
    std::size_t const_mult_folded{0};
    std::size_t rewired_edges{0};
    std::size_t new_const_nodes{0};
    std::size_t deleted_nodes{0};
  };

  // Summary for subtraction-specific folds on Sum nodes that have both A
  // (addend) and B (subtrahend) inputs.
  struct Fold_sub_summary {
    std::size_t sub_zero_simplified{0};  // a - 0  → a
    std::size_t sub_self_simplified{0};  // a - a  → 0
    std::size_t const_sub_folded{0};     // c1 - c2 → (c1-c2)
    std::size_t rewired_edges{0};
    std::size_t new_const_nodes{0};
    std::size_t deleted_nodes{0};
  };

  // Summary for dead-code elimination.
  // A node is dead if it has no output consumers (out_edges is empty) and is
  // not a graph IO / CONST builtin or a CompileErr marker.
  struct Fold_dce_summary {
    std::size_t dead_nodes_removed{0};
    std::size_t edges_freed{0};
  };

  explicit Lgraph_manager(std::shared_ptr<hhds::Graph> g) : graph_(std::move(g)) {}

  std::string_view kind() const { return "lgraph"; }
  bool             has_graph() const { return graph_ != nullptr; }

  std::size_t node_count() const {
    if (!graph_) {
      return 0;
    }
    std::size_t count = 0;
    for (const auto& n : graph_->fast_class()) {
      (void)n;
      ++count;
    }
    return count;
  }

  std::size_t const_count() const {
    if (!graph_) {
      return 0;
    }
    std::size_t count = 0;
    for (const auto& n : graph_->fast_class()) {
      if (livehd::graph_util::is_type_const(n) || livehd::graph_util::type_op_of(n) == Ntype_op::Nconst) {
        ++count;
      }
    }
    return count;
  }

  std::size_t arithmetic_count() const {
    if (!graph_) {
      return 0;
    }
    std::size_t count = 0;
    for (const auto& n : graph_->fast_class()) {
      if (is_foldable_op(livehd::graph_util::type_op_of(n))) {
        ++count;
      }
    }
    return count;
  }

  std::size_t fold_candidate_count() const { return scan_fold_candidates().fold_candidate_nodes; }

  // Streams every node through `fn` as an `hhds::Node_class`. Snapshots the
  // node set first so the callback is free to delete the current node (the
  // templated shared `replace_with_const` path does this).
  template <typename Fn>
  void for_each_node(Fn&& fn) const {
    if (!graph_) {
      return;
    }
    std::vector<hhds::Node_class> snapshot;
    for (const auto& n : graph_->forward_class()) {
      snapshot.push_back(n);
    }
    for (auto& n : snapshot) {
      if (n.is_invalid()) {
        continue;
      }
      std::forward<Fn>(fn)(n);
    }
  }

  template <typename Fn>
  void for_each_input(const Node& node, Fn&& fn) const {
    if (node.is_invalid()) {
      return;
    }
    for (const auto& e : node.inp_edges()) {
      std::forward<Fn>(fn)(e.driver);
    }
  }

  bool is_sum_op(const Node& node) const { return !node.is_invalid() && livehd::graph_util::type_op_of(node) == Ntype_op::Sum; }

  bool is_const(const Input& pin) const { return livehd::graph_util::is_const_pin(pin); }

  std::optional<Const> const_value(const Input& pin) const {
    if (!is_const(pin)) {
      return std::nullopt;
    }
    return livehd::graph_util::hydrate_const(pin);
  }

  Replace_effect estimate_replace_with_const(const Node& node) const {
    Replace_effect effect;
    if (node.is_invalid() || livehd::graph_util::is_builtin_node(node)) {
      return effect;
    }
    auto op = livehd::graph_util::type_op_of(node);
    if (op == Ntype_op::Nconst) {
      return effect;
    }
    for (const auto& out : node.out_edges()) {
      (void)out;
      ++effect.rewired_edges;
    }
    effect.new_const_nodes = 1;
    effect.deleted_nodes   = 1;
    return effect;
  }

  bool replace_with_const(const Node& node_in, const Const& value) {
    if (node_in.is_invalid() || !graph_ || livehd::graph_util::is_builtin_node(node_in)) {
      return false;
    }
    auto node = node_in;  // local mutable copy
    auto op   = livehd::graph_util::type_op_of(node);
    if (op == Ntype_op::Nconst) {
      auto cur = livehd::graph_util::hydrate_const(node);
      if (cur.same_repr(value)) {
        return false;
      }
      node.attr(livehd::attrs::const_value).set(value.serialize());
      return true;
    }
    rewire_to_const(node, value);
    return true;
  }

  const std::shared_ptr<hhds::Graph>& ref_graph() const { return graph_; }
  hhds::Graph*                        ref_graph_raw() const { return graph_.get(); }

  Fold_scan_summary scan_fold_candidates() const {
    Fold_scan_summary summary;
    if (!graph_) {
      return summary;
    }
    for (const auto& n : graph_->fast_class()) {
      ++summary.visited_nodes;
      auto op = livehd::graph_util::type_op_of(n);
      if (op == Ntype_op::Nconst) {
        ++summary.const_nodes;
        continue;
      }
      if (!is_foldable_op(op)) {
        continue;
      }
      ++summary.arithmetic_nodes;

      std::size_t input_count = 0;
      bool        all_const   = true;
      for (const auto& inp : n.inp_edges()) {
        ++input_count;
        if (!livehd::graph_util::is_const_pin(inp.driver)) {
          all_const = false;
          break;
        }
      }
      if (input_count > 0 && all_const) {
        ++summary.fold_candidate_nodes;
      }
    }
    return summary;
  }

  // Counts Sum nodes with a foldable subtraction pattern.
  std::size_t count_sub_candidates() const {
    if (!graph_) {
      return 0;
    }
    static constexpr hhds::Port_id k_port_b = 1;
    std::size_t                    count    = 0;
    for (const auto& n : graph_->fast_class()) {
      if (livehd::graph_util::type_op_of(n) != Ntype_op::Sum) {
        continue;
      }
      std::vector<hhds::Edge_class> a_ins, b_ins;
      for (const auto& inp : n.inp_edges()) {
        if (inp.sink.get_port_id() == k_port_b) {
          b_ins.push_back(inp);
        } else {
          a_ins.push_back(inp);
        }
      }
      if (a_ins.size() != 1 || b_ins.size() != 1) {
        continue;
      }
      const auto& a_drv = a_ins[0].driver;
      const auto& b_drv = b_ins[0].driver;
      if (livehd::graph_util::is_const_pin(b_drv) && livehd::graph_util::hydrate_const(b_drv).is_known_zero()) {
        ++count;
        continue;
      }
      if (same_driver(a_drv, b_drv)) {
        ++count;
        continue;
      }
      if (livehd::graph_util::is_const_pin(a_drv) && livehd::graph_util::is_const_pin(b_drv)) {
        ++count;
      }
    }
    return count;
  }

  Fold_tag_summary tag_fold_candidates(int color = 7) const {
    Fold_tag_summary summary;
    if (!graph_) {
      return summary;
    }
    for (const auto& n : graph_->fast_class()) {
      auto op = livehd::graph_util::type_op_of(n);
      if (!is_foldable_op(op)) {
        continue;
      }
      std::size_t input_count = 0;
      bool        all_const   = true;
      for (const auto& inp : n.inp_edges()) {
        ++input_count;
        if (!livehd::graph_util::is_const_pin(inp.driver)) {
          all_const = false;
          break;
        }
      }
      if (input_count == 0 || !all_const) {
        continue;
      }
      if (!livehd::graph_util::has_color(n) || livehd::graph_util::color_of(n) != color) {
        livehd::graph_util::set_color(n, color);
        ++summary.tagged_nodes;
      }
    }
    return summary;
  }

  Fold_sum_const_summary fold_sum_const(bool dry_run = false) const {
    Fold_sum_const_summary summary;
    if (!graph_) {
      return summary;
    }
    std::vector<hhds::Node_class> candidates;
    for (const auto& n : graph_->fast_class()) {
      if (livehd::graph_util::type_op_of(n) == Ntype_op::Sum) {
        candidates.push_back(n);
      }
    }
    for (auto& node : candidates) {
      std::vector<hhds::Edge_class> inputs;
      for (const auto& e : node.inp_edges()) {
        inputs.push_back(e);
      }
      if (inputs.empty()) {
        continue;
      }
      bool all_const = true;
      for (const auto& e : inputs) {
        if (!livehd::graph_util::is_const_pin(e.driver)) {
          all_const = false;
          break;
        }
      }
      if (!all_const) {
        continue;
      }
      Const result = livehd::graph_util::hydrate_const(inputs[0].driver);
      if (!result.is_i()) {
        continue;
      }
      bool ok = true;
      for (std::size_t i = 1; i < inputs.size(); ++i) {
        auto c = livehd::graph_util::hydrate_const(inputs[i].driver);
        if (!c.is_i()) {
          ok = false;
          break;
        }
        result = result.add_op(c);
      }
      if (!ok) {
        continue;
      }
      auto sinks    = collect_sinks(node);
      auto node_out = node.create_driver_pin(0);

      ++summary.new_const_nodes;
      if (!dry_run) {
        auto dpin = livehd::graph_util::create_const(*graph_, result);
        livehd::graph_util::set_bits(dpin, livehd::graph_util::bits_of(node_out));
        for (const auto& sink : sinks) {
          dpin.connect_sink(sink);
          ++summary.rewired_edges;
        }
        node.del_node();
      } else {
        summary.rewired_edges += sinks.size();
      }
      ++summary.folded_nodes;
      ++summary.deleted_nodes;
    }
    return summary;
  }

  // Folds Mult nodes where all inputs are compile-time constants.
  Fold_mult_const_summary fold_mult_const(bool dry_run = false) const {
    Fold_mult_const_summary summary;
    if (!graph_) {
      return summary;
    }
    std::vector<hhds::Node_class> candidates;
    for (const auto& n : graph_->fast_class()) {
      if (livehd::graph_util::type_op_of(n) == Ntype_op::Mult) {
        candidates.push_back(n);
      }
    }
    for (auto& node : candidates) {
      std::vector<hhds::Edge_class> inputs;
      for (const auto& e : node.inp_edges()) {
        inputs.push_back(e);
      }
      if (inputs.empty()) {
        continue;
      }
      bool all_const = true;
      for (const auto& e : inputs) {
        if (!livehd::graph_util::is_const_pin(e.driver)) {
          all_const = false;
          break;
        }
      }
      if (!all_const) {
        continue;
      }
      Const result = livehd::graph_util::hydrate_const(inputs[0].driver);
      if (!result.is_i()) {
        continue;
      }
      bool ok = true;
      for (std::size_t i = 1; i < inputs.size(); ++i) {
        auto c = livehd::graph_util::hydrate_const(inputs[i].driver);
        if (!c.is_i()) {
          ok = false;
          break;
        }
        result = result.mult_op(c);
      }
      if (!ok) {
        continue;
      }
      auto sinks    = collect_sinks(node);
      auto node_out = node.create_driver_pin(0);

      ++summary.new_const_nodes;
      if (!dry_run) {
        auto dpin = livehd::graph_util::create_const(*graph_, result);
        livehd::graph_util::set_bits(dpin, livehd::graph_util::bits_of(node_out));
        for (const auto& sink : sinks) {
          dpin.connect_sink(sink);
          ++summary.rewired_edges;
        }
        node.del_node();
      } else {
        summary.rewired_edges += sinks.size();
      }
      ++summary.const_mult_folded;
      ++summary.deleted_nodes;
    }
    return summary;
  }

  Fold_neutral_summary fold_neutral_const(bool dry_run = false) const {
    Fold_neutral_summary summary;
    if (!graph_) {
      return summary;
    }
    std::vector<hhds::Node_class> candidates;
    for (const auto& n : graph_->fast_class()) {
      auto op = livehd::graph_util::type_op_of(n);
      if (op == Ntype_op::Sum || op == Ntype_op::Or || op == Ntype_op::Xor || op == Ntype_op::And || op == Ntype_op::Mult) {
        candidates.push_back(n);
      }
    }
    for (auto& node : candidates) {
      std::vector<hhds::Edge_class> inputs;
      for (const auto& e : node.inp_edges()) {
        inputs.push_back(e);
      }
      if (inputs.size() != 2) {
        continue;
      }
      auto sinks    = collect_sinks(node);
      auto node_out = node.create_driver_pin(0);
      auto op       = livehd::graph_util::type_op_of(node);

      const auto other_driver_is_1bit
          = [&](int const_pos) { return livehd::graph_util::bits_of(inputs[1 - const_pos].driver) == 1; };

      bool rewritten = false;

      int const_zero_pos = -1;
      for (int i = 0; i < 2; ++i) {
        if (livehd::graph_util::is_const_pin(inputs[i].driver)
            && livehd::graph_util::hydrate_const(inputs[i].driver).is_known_zero()) {
          const_zero_pos = i;
          break;
        }
      }
      if (const_zero_pos >= 0) {
        if (op == Ntype_op::And) {
          ++summary.new_const_nodes;
          if (!dry_run) {
            auto dpin = livehd::graph_util::create_const(*graph_, *Dlop::create_integer(0));
            livehd::graph_util::set_bits(dpin, livehd::graph_util::bits_of(node_out));
            for (const auto& s : sinks) {
              dpin.connect_sink(s);
              ++summary.rewired_edges;
            }
          } else {
            summary.rewired_edges += sinks.size();
          }
          ++summary.simplified_to_const_zero;
          rewritten = true;
        } else {
          const auto& driver_keep = inputs[1 - const_zero_pos].driver;
          if (!dry_run) {
            for (const auto& s : sinks) {
              driver_keep.connect_sink(s);
              ++summary.rewired_edges;
            }
          } else {
            summary.rewired_edges += sinks.size();
          }
          ++summary.simplified_to_driver;
          rewritten = true;
        }
      }

      if (!rewritten && op == Ntype_op::Mult) {
        int zpos = -1;
        for (int i = 0; i < 2; ++i) {
          if (livehd::graph_util::is_const_pin(inputs[i].driver)
              && livehd::graph_util::hydrate_const(inputs[i].driver).is_known_zero()) {
            zpos = i;
            break;
          }
        }
        if (zpos >= 0) {
          ++summary.new_const_nodes;
          if (!dry_run) {
            auto dpin = livehd::graph_util::create_const(*graph_, *Dlop::create_integer(0));
            livehd::graph_util::set_bits(dpin, livehd::graph_util::bits_of(node_out));
            for (const auto& s : sinks) {
              dpin.connect_sink(s);
              ++summary.rewired_edges;
            }
          } else {
            summary.rewired_edges += sinks.size();
          }
          ++summary.simplified_to_const_zero;
          rewritten = true;
        }
      }

      if (!rewritten && op == Ntype_op::Mult) {
        int one_pos = -1;
        for (int i = 0; i < 2; ++i) {
          if (livehd::graph_util::is_const_pin(inputs[i].driver)) {
            auto c = livehd::graph_util::hydrate_const(inputs[i].driver);
            if (c.is_i() && c.to_i() == 1) {
              one_pos = i;
              break;
            }
          }
        }
        if (one_pos >= 0) {
          const auto& driver_keep = inputs[1 - one_pos].driver;
          if (!dry_run) {
            for (const auto& s : sinks) {
              driver_keep.connect_sink(s);
              ++summary.rewired_edges;
            }
          } else {
            summary.rewired_edges += sinks.size();
          }
          ++summary.simplified_to_driver;
          rewritten = true;
        }
      }

      if (!rewritten && op == Ntype_op::And) {
        int one_pos = -1;
        for (int i = 0; i < 2; ++i) {
          if (livehd::graph_util::is_const_pin(inputs[i].driver)) {
            auto c = livehd::graph_util::hydrate_const(inputs[i].driver);
            if (c.is_i() && c.to_i() == 1) {
              one_pos = i;
              break;
            }
          }
        }
        if (one_pos >= 0 && other_driver_is_1bit(one_pos)) {
          const auto& driver_keep = inputs[1 - one_pos].driver;
          if (!dry_run) {
            for (const auto& s : sinks) {
              driver_keep.connect_sink(s);
              ++summary.rewired_edges;
            }
          } else {
            summary.rewired_edges += sinks.size();
          }
          ++summary.simplified_to_driver;
          rewritten = true;
        }
      }

      if (!rewritten && op == Ntype_op::And) {
        if (same_driver(inputs[0].driver, inputs[1].driver)) {
          const auto& driver_keep = inputs[0].driver;
          if (!dry_run) {
            for (const auto& s : sinks) {
              driver_keep.connect_sink(s);
              ++summary.rewired_edges;
            }
          } else {
            summary.rewired_edges += sinks.size();
          }
          ++summary.simplified_to_driver;
          rewritten = true;
        }
      }

      if (!rewritten && op == Ntype_op::Or) {
        int one_pos = -1;
        for (int i = 0; i < 2; ++i) {
          if (livehd::graph_util::is_const_pin(inputs[i].driver)) {
            auto c = livehd::graph_util::hydrate_const(inputs[i].driver);
            if (c.is_i() && c.to_i() == 1) {
              one_pos = i;
              break;
            }
          }
        }
        if (one_pos >= 0 && other_driver_is_1bit(one_pos)) {
          ++summary.new_const_nodes;
          if (!dry_run) {
            auto dpin = livehd::graph_util::create_const(*graph_, *Dlop::create_integer(1));
            livehd::graph_util::set_bits(dpin, livehd::graph_util::bits_of(node_out));
            for (const auto& s : sinks) {
              dpin.connect_sink(s);
              ++summary.rewired_edges;
            }
          } else {
            summary.rewired_edges += sinks.size();
          }
          ++summary.simplified_to_const_one;
          rewritten = true;
        }
      }

      if (!rewritten && (op == Ntype_op::Or || op == Ntype_op::Xor)) {
        if (same_driver(inputs[0].driver, inputs[1].driver)) {
          if (op == Ntype_op::Or) {
            const auto& driver_keep = inputs[0].driver;
            if (!dry_run) {
              for (const auto& s : sinks) {
                driver_keep.connect_sink(s);
                ++summary.rewired_edges;
              }
            } else {
              summary.rewired_edges += sinks.size();
            }
            ++summary.simplified_to_driver;
          } else {
            ++summary.new_const_nodes;
            if (!dry_run) {
              auto dpin = livehd::graph_util::create_const(*graph_, *Dlop::create_integer(0));
              livehd::graph_util::set_bits(dpin, livehd::graph_util::bits_of(node_out));
              for (const auto& s : sinks) {
                dpin.connect_sink(s);
                ++summary.rewired_edges;
              }
            } else {
              summary.rewired_edges += sinks.size();
            }
            ++summary.simplified_to_const_zero;
          }
          rewritten = true;
        }
      }

      if (!rewritten) {
        continue;
      }
      if (!dry_run) {
        node.del_node();
      }
      ++summary.deleted_nodes;
    }
    return summary;
  }

  Fold_shift_div_summary fold_shift_div_const(bool dry_run = false) const {
    Fold_shift_div_summary summary;
    if (!graph_) {
      return summary;
    }
    std::vector<hhds::Node_class> candidates;
    for (const auto& n : graph_->fast_class()) {
      auto op = livehd::graph_util::type_op_of(n);
      if (op == Ntype_op::Div || op == Ntype_op::SHL || op == Ntype_op::SRA) {
        candidates.push_back(n);
      }
    }
    for (auto& node : candidates) {
      std::vector<hhds::Edge_class> inputs;
      for (const auto& e : node.inp_edges()) {
        inputs.push_back(e);
      }
      if (inputs.size() != 2) {
        continue;
      }
      const auto& lhs      = inputs[0].driver;
      const auto& rhs      = inputs[1].driver;
      auto        sinks    = collect_sinks(node);
      auto        node_out = node.create_driver_pin(0);
      auto        op       = livehd::graph_util::type_op_of(node);

      bool rewritten = false;

      if (livehd::graph_util::is_const_pin(rhs) && livehd::graph_util::hydrate_const(rhs).is_known_zero()) {
        if (op == Ntype_op::Div) {
          // don't fold x/0
        } else {
          if (!dry_run) {
            for (const auto& s : sinks) {
              lhs.connect_sink(s);
              ++summary.rewired_edges;
            }
          } else {
            summary.rewired_edges += sinks.size();
          }
          ++summary.simplified_to_driver;
          rewritten = true;
        }
      }

      if (!rewritten && op == Ntype_op::Div && livehd::graph_util::is_const_pin(rhs)) {
        auto crhs = livehd::graph_util::hydrate_const(rhs);
        if (crhs.is_i() && crhs.to_i() == 1) {
          if (!dry_run) {
            for (const auto& s : sinks) {
              lhs.connect_sink(s);
              ++summary.rewired_edges;
            }
          } else {
            summary.rewired_edges += sinks.size();
          }
          ++summary.simplified_to_driver;
          rewritten = true;
        }
      }

      if (!rewritten && op == Ntype_op::Div && livehd::graph_util::is_const_pin(lhs) && livehd::graph_util::is_const_pin(rhs)) {
        auto clhs = livehd::graph_util::hydrate_const(lhs);
        auto crhs = livehd::graph_util::hydrate_const(rhs);
        if (clhs.is_known_zero() && !crhs.is_known_zero()) {
          ++summary.new_const_nodes;
          if (!dry_run) {
            auto dpin = livehd::graph_util::create_const(*graph_, *Dlop::create_integer(0));
            livehd::graph_util::set_bits(dpin, livehd::graph_util::bits_of(node_out));
            for (const auto& s : sinks) {
              dpin.connect_sink(s);
              ++summary.rewired_edges;
            }
          } else {
            summary.rewired_edges += sinks.size();
          }
          ++summary.simplified_to_const_zero;
          rewritten = true;
        } else if (clhs.same_repr(crhs) && !crhs.is_known_zero()) {
          ++summary.new_const_nodes;
          if (!dry_run) {
            auto dpin = livehd::graph_util::create_const(*graph_, *Dlop::create_integer(1));
            livehd::graph_util::set_bits(dpin, livehd::graph_util::bits_of(node_out));
            for (const auto& s : sinks) {
              dpin.connect_sink(s);
              ++summary.rewired_edges;
            }
          } else {
            summary.rewired_edges += sinks.size();
          }
          ++summary.simplified_to_const_one;
          rewritten = true;
        } else if (clhs.is_i() && crhs.is_i() && !crhs.is_known_zero()) {
          auto res = Dlop::create_integer(clhs.to_i() / crhs.to_i());
          ++summary.new_const_nodes;
          if (!dry_run) {
            auto dpin = livehd::graph_util::create_const(*graph_, *res);
            livehd::graph_util::set_bits(dpin, livehd::graph_util::bits_of(node_out));
            for (const auto& s : sinks) {
              dpin.connect_sink(s);
              ++summary.rewired_edges;
            }
          } else {
            summary.rewired_edges += sinks.size();
          }
          if (res->is_known_zero()) {
            ++summary.simplified_to_const_zero;
          } else if (res->is_i() && res->to_i() == 1) {
            ++summary.simplified_to_const_one;
          } else {
            ++summary.simplified_to_const_other;
          }
          rewritten = true;
        }
      }

      if (!rewritten) {
        continue;
      }
      if (!dry_run) {
        node.del_node();
      }
      ++summary.deleted_nodes;
    }
    return summary;
  }

  // Folds Sum nodes that have mixed A (addend, pid=0) / B (subtrahend, pid=1)
  // inputs in the simple 1-A / 1-B shape.
  Fold_sub_summary fold_sub_const(bool dry_run = false) const {
    Fold_sub_summary summary;
    if (!graph_) {
      return summary;
    }
    static constexpr hhds::Port_id k_port_b = 1;
    std::vector<hhds::Node_class>  candidates;
    for (const auto& n : graph_->fast_class()) {
      if (livehd::graph_util::type_op_of(n) == Ntype_op::Sum) {
        candidates.push_back(n);
      }
    }
    for (auto& node : candidates) {
      std::vector<hhds::Edge_class> a_in, b_in;
      for (const auto& inp : node.inp_edges()) {
        if (inp.sink.get_port_id() == k_port_b) {
          b_in.push_back(inp);
        } else {
          a_in.push_back(inp);
        }
      }
      if (a_in.size() != 1 || b_in.size() != 1) {
        continue;
      }
      const auto& a_drv    = a_in[0].driver;
      const auto& b_drv    = b_in[0].driver;
      auto        sinks    = collect_sinks(node);
      auto        node_out = node.create_driver_pin(0);

      bool rewritten = false;

      if (livehd::graph_util::is_const_pin(b_drv) && livehd::graph_util::hydrate_const(b_drv).is_known_zero()) {
        if (!dry_run) {
          for (const auto& s : sinks) {
            a_drv.connect_sink(s);
            ++summary.rewired_edges;
          }
        } else {
          summary.rewired_edges += sinks.size();
        }
        ++summary.sub_zero_simplified;
        rewritten = true;
      }

      if (!rewritten && same_driver(a_drv, b_drv)) {
        ++summary.new_const_nodes;
        if (!dry_run) {
          auto dpin = livehd::graph_util::create_const(*graph_, *Dlop::create_integer(0));
          livehd::graph_util::set_bits(dpin, livehd::graph_util::bits_of(node_out));
          for (const auto& s : sinks) {
            dpin.connect_sink(s);
            ++summary.rewired_edges;
          }
        } else {
          summary.rewired_edges += sinks.size();
        }
        ++summary.sub_self_simplified;
        rewritten = true;
      }

      if (!rewritten && livehd::graph_util::is_const_pin(a_drv) && livehd::graph_util::is_const_pin(b_drv)) {
        auto ca = livehd::graph_util::hydrate_const(a_drv);
        auto cb = livehd::graph_util::hydrate_const(b_drv);
        if (ca.is_i() && cb.is_i()) {
          auto res = Dlop::create_integer(ca.to_i() - cb.to_i());
          ++summary.new_const_nodes;
          if (!dry_run) {
            auto dpin = livehd::graph_util::create_const(*graph_, *res);
            livehd::graph_util::set_bits(dpin, livehd::graph_util::bits_of(node_out));
            for (const auto& s : sinks) {
              dpin.connect_sink(s);
              ++summary.rewired_edges;
            }
          } else {
            summary.rewired_edges += sinks.size();
          }
          ++summary.const_sub_folded;
          rewritten = true;
        }
      }

      if (!rewritten) {
        continue;
      }
      if (!dry_run) {
        node.del_node();
      }
      ++summary.deleted_nodes;
    }
    return summary;
  }

  // Dead-code elimination: remove nodes with no out-edges (skip builtins
  // INPUT_NODE / OUTPUT_NODE / CONST_NODE and CompileErr markers).
  Fold_dce_summary fold_dce(bool dry_run = false) const {
    Fold_dce_summary summary;
    if (!graph_) {
      return summary;
    }
    std::vector<hhds::Node_class> dead;
    for (const auto& n : graph_->fast_class()) {
      if (livehd::graph_util::is_builtin_node(n)) {
        continue;
      }
      if (!n.has_out_edges()) {
        dead.push_back(n);
      }
    }
    for (auto& node : dead) {
      for (const auto& inp : node.inp_edges()) {
        (void)inp;
        ++summary.edges_freed;
      }
      ++summary.dead_nodes_removed;
      if (!dry_run) {
        node.del_node();
      }
    }
    return summary;
  }

private:
  static bool is_foldable_op(Ntype_op op) {
    switch (op) {
      case Ntype_op::Sum:
      case Ntype_op::Mult:
      case Ntype_op::Div:
      case Ntype_op::And:
      case Ntype_op::Or:
      case Ntype_op::Xor:
      case Ntype_op::SHL:
      case Ntype_op::SRA : return true;
      default            : return false;
    }
  }

  // Same driver identity check (master node + port id).
  static bool same_driver(const hhds::Pin_class& a, const hhds::Pin_class& b) {
    if (a.is_invalid() || b.is_invalid()) {
      return false;
    }
    return a.get_master_node().get_debug_nid() == b.get_master_node().get_debug_nid() && a.get_port_id() == b.get_port_id();
  }

  static std::vector<hhds::Pin_class> collect_sinks(const hhds::Node_class& node) {
    std::vector<hhds::Pin_class> sinks;
    for (const auto& out : node.out_edges()) {
      sinks.push_back(out.sink);
    }
    return sinks;
  }

  // Replace a non-const node with a fresh CONST_NODE pin carrying `value`;
  // rewires all out-edges and deletes the old node.
  void rewire_to_const(hhds::Node_class& node, const Const& value) {
    auto sinks    = collect_sinks(node);
    auto node_out = node.create_driver_pin(0);
    auto dpin     = livehd::graph_util::create_const(*graph_, value);
    livehd::graph_util::set_bits(dpin, livehd::graph_util::bits_of(node_out));
    for (const auto& s : sinks) {
      dpin.connect_sink(s);
    }
    node.del_node();
  }

  std::shared_ptr<hhds::Graph> graph_;
};

}  // namespace upass
