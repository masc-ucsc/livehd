//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "ir_adapter.hpp"
#include "lgraph.hpp"
#include "lgedgeiter.hpp"

namespace upass {

class Lgraph_manager final : public IR_adapter {
public:
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

  // Summary for subtraction-specific folds on Sum nodes that have both A (addend)
  // and B (subtrahend) inputs.
  struct Fold_sub_summary {
    std::size_t sub_zero_simplified{0};  // a - 0  → a
    std::size_t sub_self_simplified{0};  // a - a  → 0
    std::size_t const_sub_folded{0};     // c1 - c2 → (c1-c2)
    std::size_t rewired_edges{0};
    std::size_t new_const_nodes{0};
    std::size_t deleted_nodes{0};
  };

  explicit Lgraph_manager(Lgraph *graph) : lg(graph) {}

  std::string_view kind() const override { return "lgraph"; }
  std::size_t      node_count() const override {
    if (lg == nullptr) {
      return 0;
    }
    std::size_t count = 0;
    for (const auto &node : lg->fast()) {
      (void)node;
      ++count;
    }
    return count;
  }
  std::size_t      const_count() const override {
    if (lg == nullptr) {
      return 0;
    }
    std::size_t count = 0;
    for (const auto &node : lg->fast()) {
      if (node.get_type_op() == Ntype_op::Const) {
        ++count;
      }
    }
    return count;
  }
  std::size_t      arithmetic_count() const override {
    if (lg == nullptr) {
      return 0;
    }
    std::size_t count = 0;
    for (const auto &node : lg->fast()) {
      if (is_foldable_op(node.get_type_op())) {
        ++count;
      }
    }
    return count;
  }
  std::size_t      fold_candidate_count() const override { return scan_fold_candidates().fold_candidate_nodes; }
  std::vector<Node_id> list_nodes() const override {
    std::vector<Node_id> nodes;
    if (lg == nullptr) {
      return nodes;
    }

    nodes.reserve(node_count());
    for (const auto &node : lg->fast()) {
      nodes.emplace_back(static_cast<Node_id>(node.get_nid()));
    }
    return nodes;
  }
  std::string_view op_name(Node_id node_id) const override {
    const auto node = get_node(node_id);
    if (node.is_invalid()) {
      return "invalid";
    }

    switch (node.get_type_op()) {
      case Ntype_op::Const: return "const";
      case Ntype_op::Sum: return "sum";
      case Ntype_op::Mult: return "mult";
      case Ntype_op::Div: return "div";
      case Ntype_op::And: return "and";
      case Ntype_op::Or: return "or";
      case Ntype_op::Xor: return "xor";
      case Ntype_op::SHL: return "shl";
      case Ntype_op::SRA: return "sra";
      default: return "other";
    }
  }
  std::vector<Node_id> inputs(Node_id node_id) const override {
    std::vector<Node_id> inps;
    const auto           node = get_node(node_id);
    if (node.is_invalid()) {
      return inps;
    }

    for (const auto &inp : node.inp_edges()) {
      inps.emplace_back(static_cast<Node_id>(inp.driver.get_node().get_nid()));
    }
    return inps;
  }
  bool is_const(Node_id node_id) const override {
    const auto node = get_node(node_id);
    if (node.is_invalid()) {
      return false;
    }
    return node.get_type_op() == Ntype_op::Const;
  }
  std::optional<std::int64_t> const_value(Node_id node_id) const override {
    const auto node = get_node(node_id);
    if (node.is_invalid() || node.get_type_op() != Ntype_op::Const) {
      return std::nullopt;
    }

    const auto c = node.get_type_const();
    if (!c.is_i()) {
      return std::nullopt;
    }
    return c.to_i();
  }
  Replace_effect estimate_replace_with_const(Node_id node_id) const override {
    Replace_effect effect;
    const auto     node = get_node(node_id);
    if (node.is_invalid() || node.is_type_io()) {
      return effect;
    }

    if (node.get_type_op() == Ntype_op::Const) {
      return effect;
    }

    for (const auto &out : node.out_edges()) {
      (void)out;
      ++effect.rewired_edges;
    }
    effect.new_const_nodes = 1;
    effect.deleted_nodes   = 1;
    return effect;
  }
  bool replace_with_const(Node_id node_id, std::int64_t value) override {
    auto node = get_node(node_id);
    if (node.is_invalid() || node.is_type_io()) {
      return false;
    }

    if (node.get_type_op() == Ntype_op::Const) {
      const auto cur = node.get_type_const();
      if (cur.is_i() && cur.to_i() == value) {
        return false;
      }
      node.set_type_const(Lconst(value));
      return true;
    }

    auto                 node_out = node.setup_driver_pin();
    std::vector<Node_pin> sinks;
    for (const auto &out : node.out_edges()) {
      sinks.emplace_back(out.sink);
    }

    auto new_c = lg->create_node_const(value);
    auto dpin  = new_c.setup_driver_pin();
    dpin.set_size(node_out);
    for (const auto &sink : sinks) {
      dpin.connect_sink(sink);
    }
    node.del_node();
    return true;
  }

  Lgraph *ref_lgraph() const { return lg; }

  Fold_scan_summary scan_fold_candidates(bool visit_sub = false) const {
    Fold_scan_summary summary;
    if (lg == nullptr) {
      return summary;
    }

    for (const auto &node : lg->fast(visit_sub)) {
      ++summary.visited_nodes;

      auto op = node.get_type_op();
      if (op == Ntype_op::Const) {
        ++summary.const_nodes;
        continue;
      }

      if (!is_foldable_op(op)) {
        continue;
      }
      ++summary.arithmetic_nodes;

      std::size_t input_count = 0;
      bool        all_const   = true;
      for (const auto &inp : node.inp_edges()) {
        ++input_count;
        if (!inp.driver.is_type(Ntype_op::Const)) {
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

  // Counts Sum nodes that have a foldable subtraction pattern:
  //   • B-port input is const(0)      → a - 0  (neutral element)
  //   • A and B share the same driver → a - a  (self-cancellation)
  //   • Both A and B are constants    → c1 - c2 (constant fold)
  // Used as the guard predicate for fold_sub_const.
  std::size_t count_sub_candidates() const {
    if (lg == nullptr) {
      return 0;
    }

    static constexpr Port_ID k_port_b = 1;
    std::size_t              count    = 0;

    for (const auto &node : lg->fast()) {
      if (node.get_type_op() != Ntype_op::Sum) {
        continue;
      }

      std::vector<XEdge> a_ins, b_ins;
      for (const auto &inp : node.inp_edges()) {
        if (inp.sink.get_pid() == k_port_b) {
          b_ins.emplace_back(inp);
        } else {
          a_ins.emplace_back(inp);
        }
      }

      if (a_ins.size() != 1 || b_ins.size() != 1) {
        continue;
      }

      const auto &a_drv = a_ins[0].driver;
      const auto &b_drv = b_ins[0].driver;

      // a - 0
      if (b_drv.is_type(Ntype_op::Const) && b_drv.get_type_const() == 0) {
        ++count;
        continue;
      }
      // a - a
      if (a_drv.get_compact_class_driver() == b_drv.get_compact_class_driver()) {
        ++count;
        continue;
      }
      // c1 - c2
      if (a_drv.is_type(Ntype_op::Const) && b_drv.is_type(Ntype_op::Const)) {
        ++count;
      }
    }

    return count;
  }

  Fold_tag_summary tag_fold_candidates(int color = 7, bool visit_sub = false) const {
    Fold_tag_summary summary;
    if (lg == nullptr) {
      return summary;
    }

    for (auto node : lg->fast(visit_sub)) {
      auto op = node.get_type_op();
      if (!is_foldable_op(op)) {
        continue;
      }

      std::size_t input_count = 0;
      bool        all_const   = true;
      for (const auto &inp : node.inp_edges()) {
        ++input_count;
        if (!inp.driver.is_type(Ntype_op::Const)) {
          all_const = false;
          break;
        }
      }

      if (input_count == 0 || !all_const) {
        continue;
      }

      if (!node.has_color() || node.get_color() != color) {
        node.set_color(color);
        ++summary.tagged_nodes;
      }
    }

    return summary;
  }

  Fold_sum_const_summary fold_sum_const(bool visit_sub = false, bool dry_run = false) const {
    Fold_sum_const_summary summary;
    if (lg == nullptr) {
      return summary;
    }

    std::vector<Node> candidates;
    for (const auto &node : lg->fast(visit_sub)) {
      if (node.get_type_op() == Ntype_op::Sum) {
        candidates.emplace_back(node);
      }
    }

    for (auto &node : candidates) {
      auto node_out = node.setup_driver_pin();
      std::vector<XEdge> inputs;
      for (const auto &inp : node.inp_edges()) {
        inputs.emplace_back(inp);
      }
      if (inputs.size() != 2) {
        continue;
      }
      if (!inputs[0].driver.is_type(Ntype_op::Const) || !inputs[1].driver.is_type(Ntype_op::Const)) {
        continue;
      }

      const auto c0 = inputs[0].driver.get_type_const();
      const auto c1 = inputs[1].driver.get_type_const();
      if (!c0.is_i() || !c1.is_i()) {
        continue;
      }

      std::vector<Node_pin> sinks;
      for (const auto &out : node.out_edges()) {
        sinks.emplace_back(out.sink);
      }

      ++summary.new_const_nodes;
      if (!dry_run) {
        auto new_c = lg->create_node_const(c0 + c1);
        auto dpin  = new_c.setup_driver_pin();
        dpin.set_size(node_out);
        for (const auto &sink : sinks) {
          dpin.connect_sink(sink);
          ++summary.rewired_edges;
        }
      } else {
        summary.rewired_edges += sinks.size();
      }

      if (!dry_run) {
        node.del_node();
      }
      ++summary.folded_nodes;
      ++summary.deleted_nodes;
    }

    return summary;
  }

  Fold_neutral_summary fold_neutral_const(bool visit_sub = false, bool dry_run = false) const {
    Fold_neutral_summary summary;
    if (lg == nullptr) {
      return summary;
    }

    std::vector<Node> candidates;
    for (const auto &node : lg->fast(visit_sub)) {
      auto op = node.get_type_op();
      if (op == Ntype_op::Sum || op == Ntype_op::Or || op == Ntype_op::Xor || op == Ntype_op::And || op == Ntype_op::Mult) {
        candidates.emplace_back(node);
      }
    }

    for (auto &node : candidates) {
      auto node_out = node.setup_driver_pin();
      std::vector<XEdge> inputs;
      for (const auto &inp : node.inp_edges()) {
        inputs.emplace_back(inp);
      }
      if (inputs.size() != 2) {
        continue;
      }

      std::vector<Node_pin> sinks;
      for (const auto &out : node.out_edges()) {
        sinks.emplace_back(out.sink);
      }

      bool rewritten = false;
      const auto other_driver_is_1bit = [&](int const_pos) {
        const auto &other = inputs[1 - const_pos].driver;
        return other.get_bits() == 1;
      };

      int const_zero_pos = -1;
      for (int i = 0; i < 2; ++i) {
        if (inputs[i].driver.is_type(Ntype_op::Const) && inputs[i].driver.get_type_const() == 0) {
          const_zero_pos = i;
          break;
        }
      }
      if (const_zero_pos >= 0) {
        if (node.get_type_op() == Ntype_op::And) {
          ++summary.new_const_nodes;
          if (!dry_run) {
            auto c0   = lg->create_node_const(0);
            auto dpin = c0.setup_driver_pin();
            dpin.set_size(node_out);
            for (const auto &sink : sinks) {
              dpin.connect_sink(sink);
              ++summary.rewired_edges;
            }
          } else {
            summary.rewired_edges += sinks.size();
          }
          ++summary.simplified_to_const_zero;
          rewritten = true;
        } else {
          const auto &driver_keep = inputs[1 - const_zero_pos].driver;
          if (!dry_run) {
            for (const auto &sink : sinks) {
              driver_keep.connect_sink(sink);
              ++summary.rewired_edges;
            }
          } else {
            summary.rewired_edges += sinks.size();
          }
          ++summary.simplified_to_driver;
          rewritten = true;
        }
      }

      if (!rewritten && node.get_type_op() == Ntype_op::Mult) {
        int const_zero_pos_mul = -1;
        for (int i = 0; i < 2; ++i) {
          if (inputs[i].driver.is_type(Ntype_op::Const) && inputs[i].driver.get_type_const() == 0) {
            const_zero_pos_mul = i;
            break;
          }
        }
        if (const_zero_pos_mul >= 0) {
          ++summary.new_const_nodes;
          if (!dry_run) {
            auto c0   = lg->create_node_const(0);
            auto dpin = c0.setup_driver_pin();
            dpin.set_size(node_out);
            for (const auto &sink : sinks) {
              dpin.connect_sink(sink);
              ++summary.rewired_edges;
            }
          } else {
            summary.rewired_edges += sinks.size();
          }
          ++summary.simplified_to_const_zero;
          rewritten = true;
        }
      }

      if (!rewritten && node.get_type_op() == Ntype_op::Mult) {
        int const_one_pos = -1;
        for (int i = 0; i < 2; ++i) {
          if (inputs[i].driver.is_type(Ntype_op::Const) && inputs[i].driver.get_type_const() == 1) {
            const_one_pos = i;
            break;
          }
        }
        if (const_one_pos >= 0) {
          const auto &driver_keep = inputs[1 - const_one_pos].driver;
          if (!dry_run) {
            for (const auto &sink : sinks) {
              driver_keep.connect_sink(sink);
              ++summary.rewired_edges;
            }
          } else {
            summary.rewired_edges += sinks.size();
          }
          ++summary.simplified_to_driver;
          rewritten = true;
        }
      }

      if (!rewritten && node.get_type_op() == Ntype_op::And) {
        int const_one_pos = -1;
        for (int i = 0; i < 2; ++i) {
          if (inputs[i].driver.is_type(Ntype_op::Const) && inputs[i].driver.get_type_const() == 1) {
            const_one_pos = i;
            break;
          }
        }
        if (const_one_pos >= 0 && other_driver_is_1bit(const_one_pos)) {
          const auto &driver_keep = inputs[1 - const_one_pos].driver;
          if (!dry_run) {
            for (const auto &sink : sinks) {
              driver_keep.connect_sink(sink);
              ++summary.rewired_edges;
            }
          } else {
            summary.rewired_edges += sinks.size();
          }
          ++summary.simplified_to_driver;
          rewritten = true;
        }
      }

      if (!rewritten && node.get_type_op() == Ntype_op::And) {
        bool same_driver = inputs[0].driver.get_compact_class_driver() == inputs[1].driver.get_compact_class_driver();
        if (same_driver) {
          const auto &driver_keep = inputs[0].driver;
          if (!dry_run) {
            for (const auto &sink : sinks) {
              driver_keep.connect_sink(sink);
              ++summary.rewired_edges;
            }
          } else {
            summary.rewired_edges += sinks.size();
          }
          ++summary.simplified_to_driver;
          rewritten = true;
        }
      }

      if (!rewritten && (node.get_type_op() == Ntype_op::Or || node.get_type_op() == Ntype_op::Xor)) {
        if (node.get_type_op() == Ntype_op::Or) {
          int const_one_pos = -1;
          for (int i = 0; i < 2; ++i) {
            if (inputs[i].driver.is_type(Ntype_op::Const) && inputs[i].driver.get_type_const() == 1) {
              const_one_pos = i;
              break;
            }
          }
          if (const_one_pos >= 0 && other_driver_is_1bit(const_one_pos)) {
            ++summary.new_const_nodes;
            if (!dry_run) {
              auto c1   = lg->create_node_const(1);
              auto dpin = c1.setup_driver_pin();
              dpin.set_size(node_out);
              for (const auto &sink : sinks) {
                dpin.connect_sink(sink);
                ++summary.rewired_edges;
              }
            } else {
              summary.rewired_edges += sinks.size();
            }
            ++summary.simplified_to_const_one;
            rewritten = true;
          }
        }
      }

      if (!rewritten && (node.get_type_op() == Ntype_op::Or || node.get_type_op() == Ntype_op::Xor)) {
        bool same_driver = inputs[0].driver.get_compact_class_driver() == inputs[1].driver.get_compact_class_driver();
        if (same_driver) {
          if (node.get_type_op() == Ntype_op::Or) {
            const auto &driver_keep = inputs[0].driver;
            if (!dry_run) {
              for (const auto &sink : sinks) {
                driver_keep.connect_sink(sink);
                ++summary.rewired_edges;
              }
            } else {
              summary.rewired_edges += sinks.size();
            }
            ++summary.simplified_to_driver;
          } else {
            ++summary.new_const_nodes;
            if (!dry_run) {
              auto c0   = lg->create_node_const(0);
              auto dpin = c0.setup_driver_pin();
              dpin.set_size(node_out);
              for (const auto &sink : sinks) {
                dpin.connect_sink(sink);
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

  Fold_shift_div_summary fold_shift_div_const(bool visit_sub = false, bool dry_run = false) const {
    Fold_shift_div_summary summary;
    if (lg == nullptr) {
      return summary;
    }

    std::vector<Node> candidates;
    for (const auto &node : lg->fast(visit_sub)) {
      auto op = node.get_type_op();
      if (op == Ntype_op::Div || op == Ntype_op::SHL || op == Ntype_op::SRA) {
        candidates.emplace_back(node);
      }
    }

    for (auto &node : candidates) {
      auto node_out = node.setup_driver_pin();
      std::vector<XEdge> inputs;
      for (const auto &inp : node.inp_edges()) {
        inputs.emplace_back(inp);
      }
      if (inputs.size() != 2) {
        continue;
      }

      const auto &lhs = inputs[0].driver;
      const auto &rhs = inputs[1].driver;

      std::vector<Node_pin> sinks;
      for (const auto &out : node.out_edges()) {
        sinks.emplace_back(out.sink);
      }

      bool rewritten = false;

      if ((node.get_type_op() == Ntype_op::Div || node.get_type_op() == Ntype_op::SHL || node.get_type_op() == Ntype_op::SRA)
          && rhs.is_type(Ntype_op::Const) && rhs.get_type_const() == 0) {
        if (node.get_type_op() == Ntype_op::Div) {
          // Do not rewrite x/0
        } else {
          if (!dry_run) {
            for (const auto &sink : sinks) {
              lhs.connect_sink(sink);
              ++summary.rewired_edges;
            }
          } else {
            summary.rewired_edges += sinks.size();
          }
          ++summary.simplified_to_driver;
          rewritten = true;
        }
      }

      if (!rewritten && node.get_type_op() == Ntype_op::Div && rhs.is_type(Ntype_op::Const) && rhs.get_type_const() == 1) {
        if (!dry_run) {
          for (const auto &sink : sinks) {
            lhs.connect_sink(sink);
            ++summary.rewired_edges;
          }
        } else {
          summary.rewired_edges += sinks.size();
        }
        ++summary.simplified_to_driver;
        rewritten = true;
      }

      if (!rewritten && node.get_type_op() == Ntype_op::Div && lhs.is_type(Ntype_op::Const) && lhs.get_type_const() == 0
          && rhs.is_type(Ntype_op::Const) && rhs.get_type_const() != 0) {
        ++summary.new_const_nodes;
        if (!dry_run) {
          auto c0   = lg->create_node_const(0);
          auto dpin = c0.setup_driver_pin();
          dpin.set_size(node_out);
          for (const auto &sink : sinks) {
            dpin.connect_sink(sink);
            ++summary.rewired_edges;
          }
        } else {
          summary.rewired_edges += sinks.size();
        }
        ++summary.simplified_to_const_zero;
        rewritten = true;
      }

      if (!rewritten && node.get_type_op() == Ntype_op::Div && lhs.is_type(Ntype_op::Const) && rhs.is_type(Ntype_op::Const)
          && lhs.get_type_const() == rhs.get_type_const() && rhs.get_type_const() != 0) {
        ++summary.new_const_nodes;
        if (!dry_run) {
          auto c1   = lg->create_node_const(1);
          auto dpin = c1.setup_driver_pin();
          dpin.set_size(node_out);
          for (const auto &sink : sinks) {
            dpin.connect_sink(sink);
            ++summary.rewired_edges;
          }
        } else {
          summary.rewired_edges += sinks.size();
        }
        ++summary.simplified_to_const_one;
        rewritten = true;
      }

      if (!rewritten && node.get_type_op() == Ntype_op::Div && lhs.is_type(Ntype_op::Const) && rhs.is_type(Ntype_op::Const)
          && rhs.get_type_const() != 0) {
        const auto c_lhs = lhs.get_type_const();
        const auto c_rhs = rhs.get_type_const();
        if (c_lhs.is_i() && c_rhs.is_i()) {
          const auto c_res = Lconst(c_lhs.to_i() / c_rhs.to_i());
          ++summary.new_const_nodes;
          if (!dry_run) {
            auto cnew = lg->create_node_const(c_res);
            auto dpin = cnew.setup_driver_pin();
            dpin.set_size(node_out);
            for (const auto &sink : sinks) {
              dpin.connect_sink(sink);
              ++summary.rewired_edges;
            }
          } else {
            summary.rewired_edges += sinks.size();
          }

          if (c_res == 0) {
            ++summary.simplified_to_const_zero;
          } else if (c_res == 1) {
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

  // Folds Sum nodes that have mixed A (addend, pid=0) / B (subtrahend, pid=1) inputs.
  // Three patterns are handled:
  //   1. a - 0      → a          (neutral element: subtracting zero is identity)
  //   2. a - a      → 0          (self-cancellation)
  //   3. c1 - c2    → (c1-c2)    (fully-constant subtraction)
  // Only 1-A / 1-B nodes are targeted; multi-input Sum nodes are left for later.
  Fold_sub_summary fold_sub_const(bool visit_sub = false, bool dry_run = false) const {
    Fold_sub_summary summary;
    if (lg == nullptr) {
      return summary;
    }

    // Port A (addend/positive) has sink pid 0; port B (subtrahend/negative) has pid 1.
    static constexpr Port_ID k_port_b = 1;

    std::vector<Node> candidates;
    for (const auto &node : lg->fast(visit_sub)) {
      if (node.get_type_op() == Ntype_op::Sum) {
        candidates.emplace_back(node);
      }
    }

    for (auto &node : candidates) {
      auto node_out = node.setup_driver_pin();

      std::vector<XEdge> a_inputs, b_inputs;
      for (const auto &inp : node.inp_edges()) {
        if (inp.sink.get_pid() == k_port_b) {
          b_inputs.emplace_back(inp);
        } else {
          a_inputs.emplace_back(inp);
        }
      }

      // Only handle the simple 1-A / 1-B case; skip pure-addition nodes (no B)
      // and multi-input nodes.
      if (a_inputs.size() != 1 || b_inputs.size() != 1) {
        continue;
      }

      const auto &a_drv = a_inputs[0].driver;
      const auto &b_drv = b_inputs[0].driver;

      std::vector<Node_pin> sinks;
      for (const auto &out : node.out_edges()) {
        sinks.emplace_back(out.sink);
      }

      bool rewritten = false;

      // Case 1: a - 0 → a  (B input is the constant zero)
      if (b_drv.is_type(Ntype_op::Const) && b_drv.get_type_const() == 0) {
        if (!dry_run) {
          for (const auto &sink : sinks) {
            a_drv.connect_sink(sink);
            ++summary.rewired_edges;
          }
        } else {
          summary.rewired_edges += sinks.size();
        }
        ++summary.sub_zero_simplified;
        rewritten = true;
      }

      // Case 2: a - a → 0  (same driver feeds both A and B)
      if (!rewritten
          && a_drv.get_compact_class_driver() == b_drv.get_compact_class_driver()) {
        ++summary.new_const_nodes;
        if (!dry_run) {
          auto c0   = lg->create_node_const(0);
          auto dpin = c0.setup_driver_pin();
          dpin.set_size(node_out);
          for (const auto &sink : sinks) {
            dpin.connect_sink(sink);
            ++summary.rewired_edges;
          }
        } else {
          summary.rewired_edges += sinks.size();
        }
        ++summary.sub_self_simplified;
        rewritten = true;
      }

      // Case 3: const_a - const_b → result  (both inputs are compile-time constants)
      if (!rewritten && a_drv.is_type(Ntype_op::Const) && b_drv.is_type(Ntype_op::Const)) {
        const auto ca = a_drv.get_type_const();
        const auto cb = b_drv.get_type_const();
        if (ca.is_i() && cb.is_i()) {
          const auto result = Lconst(ca.to_i() - cb.to_i());
          ++summary.new_const_nodes;
          if (!dry_run) {
            auto cnew = lg->create_node_const(result);
            auto dpin = cnew.setup_driver_pin();
            dpin.set_size(node_out);
            for (const auto &sink : sinks) {
              dpin.connect_sink(sink);
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

private:
  Node get_node(Node_id node_id) const {
    if (lg == nullptr) {
      return {};
    }

    const auto nid = static_cast<Index_id>(node_id);
    if (nid == 0) {
      return {};
    }

    return Node(lg, Hierarchy::non_hierarchical(), nid);
  }

  static bool is_foldable_op(Ntype_op op) {
    switch (op) {
      case Ntype_op::Sum:
      case Ntype_op::Mult:
      case Ntype_op::Div:
      case Ntype_op::And:
      case Ntype_op::Or:
      case Ntype_op::Xor:
      case Ntype_op::SHL:
      case Ntype_op::SRA: return true;
      default: return false;
    }
  }

  Lgraph *lg;
};

}  // namespace upass
