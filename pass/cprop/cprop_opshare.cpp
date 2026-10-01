// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cprop_opshare.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <vector>

#include "attr_carry.hpp"
#include "cprop.hpp"
#include "cprop_muxctx.hpp"
#include "cprop_value.hpp"

namespace {
namespace gu = livehd::graph_util;
namespace cv = livehd::cprop_value;
using Pin    = hhds::Pin_class;
using Node   = hhds::Node_class;

struct Operand {
  hhds::Port_id role;
  Pin           value;
};
struct Shape {
  Ntype_op             op;
  std::vector<Operand> operands;
};

bool spend(size_t& budget, size_t cost = 1) {
  if (cost > budget) {
    budget = 0;
    return false;
  }
  budget -= cost;
  return true;
}

bool private_arm(const Node& node, const Node& mux) {
  if (gu::is_builtin_node(node) || gu::has_color(node) || gu::has_runtime_check(node) || gu::has_name(node)
      || !gu::pin_name_of(node.get_driver_pin(0)).empty()) {
    return false;
  }
  auto edges = node.out_edges();
  auto it    = edges.begin();
  if (it == edges.end() || (*it).sink.get_master_node() != mux) {
    return false;
  }
  return ++it == edges.end();
}

// This is an explicit whitelist: a cell with an unmodelled attribute or an
// effect must never become shareable just because its operands happen to match.
std::optional<Shape> shape_of(const Node& node, size_t& budget) {
  const auto op = gu::type_op_of(node);
  switch (op) {
    case Ntype_op::Sum     :
    case Ntype_op::LT      :
    case Ntype_op::GT      :
    case Ntype_op::And     :
    case Ntype_op::Or      :
    case Ntype_op::Xor     :
    case Ntype_op::Mult    :
    case Ntype_op::EQ      :
    case Ntype_op::Ror     :
    case Ntype_op::Not     :
    case Ntype_op::SHL     :
    case Ntype_op::SRA     :
    case Ntype_op::Div     :
    case Ntype_op::Rem     :
    case Ntype_op::Sext    :
    case Ntype_op::Rxor    :
    case Ntype_op::Popcount:
    case Ntype_op::Get_mask:
    case Ntype_op::Set_mask:
    case Ntype_op::Concat  : break;
    default                : return {};
  }
  Shape result{op, {}};
  for (auto sink : node.inp_sorted_pins()) {
    if (!spend(budget)) {
      return {};
    }
    auto value = sink.get_driver_pin();
    if (value.is_invalid() || (value.is_const() && (!gu::const_of(value).is_numeric() || gu::const_of(value).has_unknowns()))
        || (!value.is_const() && gu::type_op_of(value.get_master_node()) == Ntype_op::Latch)) {
      return {};
    }
    result.operands.push_back({Ntype::sink_bank(op, sink.get_port_id()), value});
  }
  if (result.operands.empty()) {
    return {};
  }
  if (Ntype::sink_bank_count(op) != 0) {
    std::sort(result.operands.begin(), result.operands.end(), [](const auto& a, const auto& b) {
      return a.role != b.role ? a.role < b.role : a.value.get_class_index().value < b.value.get_class_index().value;
    });
    return result;
  }
  const auto   count    = result.operands.size();
  const size_t expected = op == Ntype_op::Not ? 1 : op == Ntype_op::Set_mask ? 3 : 2;
  if (op == Ntype_op::Concat ? count % 2 != 0 : count != expected) {
    return {};
  }
  for (size_t i = 0; i < count; ++i) {
    const auto& operand = result.operands[i];
    // Masks use sparse positional pids: a=0, mask=2, replacement=4.
    const auto  pid     = op == Ntype_op::Get_mask || op == Ntype_op::Set_mask ? 2 * i : i;
    if (operand.role != pid) {
      return {};
    }
    const bool parameter = ((op == Ntype_op::Sext || op == Ntype_op::Rxor || op == Ntype_op::Popcount) && i == 1)
                           || ((op == Ntype_op::Get_mask || op == Ntype_op::Set_mask) && operand.role == 2)
                           || (op == Ntype_op::Concat && i % 2 == 1);
    if (parameter && !operand.value.is_const()) {
      return {};
    }
  }
  return result;
}

// Retain common multiset operands first, then pair residuals in stable order.
// Pairing is per bank: neither multiplicity nor a Sum operand's sign changes.
bool align(const Shape& base, Shape& other) {
  if (base.op != other.op || base.operands.size() != other.operands.size()) {
    return false;
  }
  const auto count = base.operands.size();
  for (size_t i = 0; i < count; ++i) {
    if (base.operands[i].role != other.operands[i].role) {
      return false;
    }
  }
  if (Ntype::sink_bank_count(base.op) == 0) {
    for (size_t i = 0; i < count; ++i) {
      const auto role = base.operands[i].role;
      const bool parameter
          = ((base.op == Ntype_op::Sext || base.op == Ntype_op::Rxor || base.op == Ntype_op::Popcount) && role == 1)
            || ((base.op == Ntype_op::Get_mask || base.op == Ntype_op::Set_mask) && role == 2)
            || (base.op == Ntype_op::Concat && role % 2 == 1);
      if (parameter && base.operands[i].value != other.operands[i].value) {
        return false;
      }
    }
    return true;
  }
  std::vector<Operand> paired(count);
  for (size_t begin = 0; begin < count;) {
    size_t end = begin + 1;
    while (end < count && base.operands[end].role == base.operands[begin].role) {
      ++end;
    }
    std::vector<size_t>  slots;
    std::vector<Operand> residual;
    size_t               a = begin, b = begin;
    while (a < end && b < end) {
      if (base.operands[a].value == other.operands[b].value) {
        paired[a++] = other.operands[b++];
      } else if (base.operands[a].value.get_class_index().value < other.operands[b].value.get_class_index().value) {
        slots.push_back(a++);
      } else {
        residual.push_back(other.operands[b++]);
      }
    }
    while (a < end) {
      slots.push_back(a++);
    }
    while (b < end) {
      residual.push_back(other.operands[b++]);
    }
    I(slots.size() == residual.size());
    for (size_t i = 0; i < slots.size(); ++i) {
      paired[slots[i]] = residual[i];
    }
    begin = end;
  }
  other.operands = std::move(paired);
  return true;
}

// Smallest carrier {bits, unsigned} holding every value operand `j` can take,
// or none when some operand's realization is unknown (unstamped or negative
// constant). Mixed signs need one extra bit above an unsigned operand.
std::optional<std::pair<int32_t, bool>> union_carrier(const std::vector<Shape>& shapes, size_t j) {
  int32_t ubits = 0, sbits = 0;
  bool    any_signed = false;
  for (const auto& shape : shapes) {
    const auto& value = shape.operands[j].value;
    if (value.is_const()) {
      const int width = cv::unsigned_width(value);
      if (width < 0) {
        return {};
      }
      ubits = std::max(ubits, std::max(width, 1));
      continue;
    }
    const auto bits = gu::bits_of(value);
    if (bits <= 0) {
      return {};
    }
    if (gu::is_unsign(value)) {
      ubits = std::max(ubits, bits);
    } else {
      any_signed = true;
      sbits      = std::max(sbits, bits);
    }
  }
  if (!any_signed) {
    return std::pair<int32_t, bool>{ubits, true};
  }
  return std::pair<int32_t, bool>{std::max(sbits, ubits == 0 ? 0 : ubits + 1), false};
}
// Same shape buckets are disjoint, and each operator has exactly one use.
// Keep the outer controls/default, so grouping cannot change their obligation
// or the all-controls-zero behavior. Inner controls are exclusive subsets.
void share_hotmux_operators(hhds::Graph& graph, size_t& budget) {
  std::vector<Node> roots;
  for (auto n : graph.body().nodes()) {
    if (gu::type_op_of(n) == Ntype_op::Hotmux) {
      roots.push_back(n);
    }
  }
  for (auto root : roots) {
    if (!spend(budget) || gu::has_color(root) || !root.has_out_edges() || !livehd::muxctx::exclusive(root)) {
      continue;
    }
    const auto inputs = gu::hotmux_inputs(root);
    if (!spend(budget, inputs.arms.size() * 4 + 1)) {
      break;
    }
    struct Arm {
      Pin   sink;
      Pin   control;
      Node  node;
      Shape shape;
    };
    using Key = std::vector<uint64_t>;
    std::map<Key, std::vector<Arm>> buckets;
    const auto                      collect = [&](Pin sink, Pin control, Pin value) {
      if (value.is_const() || !private_arm(value.get_master_node(), root)) {
        return;
      }
      auto shape = shape_of(value.get_master_node(), budget);
      if (!shape) {
        return;
      }
      // Keep each operator's realized output boundary. This also avoids
      // inventing a common finite carrier for unstamped/differently stamped
      // operator results in clients that do not rerun bitwidth.
      Key key{static_cast<uint64_t>(shape->op), static_cast<uint64_t>(gu::bits_of(value)), gu::is_unsign(value)};
      for (const auto& operand : shape->operands) {
        const auto role      = operand.role;
        const auto op        = shape->op;
        const bool parameter = ((op == Ntype_op::Sext || op == Ntype_op::Rxor || op == Ntype_op::Popcount) && role == 1)
                               || ((op == Ntype_op::Get_mask || op == Ntype_op::Set_mask) && role == 2)
                               || (op == Ntype_op::Concat && role % 2 == 1);
        key.push_back(role);
        key.push_back(parameter ? operand.value.get_class_index().value : 0);
      }
      buckets[std::move(key)].push_back({sink, control, value.get_master_node(), std::move(*shape)});
    };
    for (size_t i = 0; i < inputs.arms.size(); ++i) {
      collect(root.get_sink_pin(2 * i + 1), inputs.arms[i].first, inputs.arms[i].second);
    }
    if (!inputs.fallback.is_invalid()) {
      collect(root.get_sink_pin(2 * inputs.arms.size()), {}, inputs.fallback);
    }
    for (auto& [key, group] : buckets) {
      (void)key;
      if (group.size() < 2) {
        continue;
      }
      std::vector<Shape> shapes;
      bool               valid = true;
      for (auto& arm : group) {
        if (!spend(budget, arm.shape.operands.size()) || (!shapes.empty() && !align(shapes.front(), arm.shape))) {
          valid = false;
          break;
        }
        shapes.push_back(arm.shape);
      }
      if (!valid) {
        continue;
      }
      const auto& base = shapes.front();
      for (size_t j = 0; j < base.operands.size(); ++j) {
        const auto value = base.operands[j].value;
        const bool same
            = std::all_of(shapes.begin() + 1, shapes.end(), [&](const auto& s) { return s.operands[j].value == value; });
        if (same) {
          continue;
        }
        if ((base.op == Ntype_op::SHL || base.op == Ntype_op::SRA) && base.operands[j].role == 1
            && std::any_of(shapes.begin(), shapes.end(), [&](const auto& s) { return s.operands[j].value.is_const(); })) {
          valid = false;
        }
        if (base.op == Ntype_op::Concat && j % 2 == 0) {
          const auto& width = gu::const_of(base.operands[j + 1].value);
          if (!width.is_just_i64() || width.to_just_i64() <= 0) {
            valid = false;
            break;
          }
          for (const auto& s : shapes) {
            const int bits  = cv::unsigned_width(s.operands[j].value);
            valid          &= bits >= 0 && bits <= width.to_just_i64();
          }
        }
      }
      if (!valid || !spend(budget, 8 * group.size() * base.operands.size())) {
        continue;
      }
      auto shared = cv::make_node(graph, base.op);
      gu::carry_srcid(group.front().node, shared);
      auto       output = shared.create_driver_pin(0);
      const auto old    = group.front().node.get_driver_pin(0);
      if (gu::bits_of(old) > 0) {
        if (gu::is_unsign(old)) {
          gu::set_ubits(output, gu::bits_of(old));
        } else {
          gu::set_sbits(output, gu::bits_of(old));
        }
      }
      for (size_t j = 0; j < base.operands.size(); ++j) {
        auto       value = base.operands[j].value;
        const bool same
            = std::all_of(shapes.begin() + 1, shapes.end(), [&](const auto& s) { return s.operands[j].value == value; });
        if (!same) {
          auto   mux      = cv::make_node(graph, Ntype_op::Hotmux);
          size_t pid      = 0;
          // A group without the original default uses one ORIGINAL operand
          // vector when inactive, not zeros that could introduce div-by-zero
          // or an invalid extension/shift. The outer Hotmux hides its value.
          Pin    fallback = value;
          for (size_t i = 0; i < group.size(); ++i) {
            if (group[i].control.is_invalid()) {
              fallback = shapes[i].operands[j].value;
            } else {
              gu::setup_sink_pid(mux, pid++).connect_driver(group[i].control);
              gu::setup_sink_pid(mux, pid++).connect_driver(shapes[i].operands[j].value);
            }
          }
          gu::setup_sink_pid(mux, pid).connect_driver(fallback);
          gu::set_proven(mux, gu::kFormalOnehot);
          value = mux.create_driver_pin(0);
          if (const auto carrier = union_carrier(shapes, j)) {
            if (carrier->second) {
              gu::set_ubits(value, carrier->first);
            } else {
              gu::set_sbits(value, carrier->first);
            }
          }
        }
        gu::setup_sink_pid(shared, base.operands[j].role).connect_driver(value);
      }
      if (base.op == Ntype_op::Concat) {
        gu::set_ubits(output, gu::concat_total_width(shared));
      }
      cv::forget(root.get_driver_pin(0));
      for (const auto& arm : group) {
        arm.sink.get_driver_pin().del_sink(arm.sink);
        output.connect_sink(arm.sink);
        I(!arm.node.has_out_edges());
        cv::retire(arm.node);
      }
    }
  }
}
}  // namespace

void Cprop::mux_op_share_pass() {
  auto&      facts      = *cv::active;
  const auto generation = [&](const Node& node) {
    auto it = facts.generations.find(node.get_class_index());
    return it == facts.generations.end() ? uint64_t{0} : it->second;
  };
  struct Pending {
    Node     node;
    uint64_t generation;
  };
  std::vector<Pending> pending;
  size_t               budget = 0;
  for (auto node : current_graph->body().nodes()) {
    budget += 16;
    for ([[maybe_unused]] auto sink : node.inp_sorted_pins()) {
      budget += 16;
    }
    if (gu::type_op_of(node) == Ntype_op::Mux) {
      pending.push_back({node, generation(node)});
    }
  }
  share_hotmux_operators(*current_graph, budget);
  for (size_t next = 0; next < pending.size() && spend(budget); ++next) {
    auto root = pending[next].node;
    if (root.is_invalid() || generation(root) != pending[next].generation || gu::type_op_of(root) != Ntype_op::Mux
        || !root.has_out_edges() || gu::has_color(root) || gu::has_runtime_check(root)) {
      continue;
    }
    std::vector<Pin> pins;
    bool             valid = true;
    for (auto sink : root.inp_sorted_pins()) {
      if (!spend(budget) || sink.get_port_id() != pins.size()) {
        valid = false;
        break;
      }
      auto pin = sink.get_driver_pin();
      if (pin.is_invalid() || (pin.is_const() && gu::const_of(pin).has_unknowns())) {
        valid = false;
        break;
      }
      pins.push_back(pin);
    }
    if (!valid || pins.size() < 3) {
      continue;
    }
    if (pins.size() > 3) {
      // The runtime rejects an out-of-range index whereas today's LEC model
      // uses the last arm and scalar cprop uses zero. Until that boundary is
      // unified, factor only selectors whose explicit structure covers no
      // out-of-range value. Width annotations are not evidence for this test.
      const int width = cv::unsigned_width(pins[0]);
      if (width < 0 || width >= std::numeric_limits<size_t>::digits || (size_t{1} << width) > pins.size() - 1) {
        continue;
      }
    }
    std::vector<Node>  arms;
    std::vector<Shape> shapes;
    for (size_t i = 1; i < pins.size(); ++i) {
      auto arm = pins[i].get_master_node();
      if (pins[i].is_const() || !private_arm(arm, root)) {
        valid = false;
        break;
      }
      auto shape = shape_of(arm, budget);
      if (!shape || (!shapes.empty() && !align(shapes.front(), *shape))) {
        valid = false;
        break;
      }
      arms.push_back(arm);
      shapes.push_back(std::move(*shape));
    }
    if (!valid) {
      continue;
    }
    const auto& base = shapes.front();
    // A shift by a constant is wiring, not an operator: sharing `a << 32` and
    // `b << 65` as `mux(a,b) << mux(32,65)` saves nothing and manufactures a
    // barrel shifter over the whole carrier (satopt's hotmux stage exposed
    // this in dino's ALU sign-extension, +40% on that shift). Share a shift
    // only when its amounts agree or none of them is a constant.
    if (base.op == Ntype_op::SHL || base.op == Ntype_op::SRA) {
      for (size_t j = 0; j < base.operands.size(); ++j) {
        if (base.operands[j].role == 0) {
          continue;  // the shifted value
        }
        const auto value = base.operands[j].value;
        const bool same  = std::all_of(shapes.begin() + 1, shapes.end(), [&](const auto& s) {
          const auto& other = s.operands[j].value;
          return other == value || (other.is_const() && value.is_const() && gu::const_of(other).is_known_eq(gu::const_of(value)));
        });
        const bool any_cst
            = std::any_of(shapes.begin(), shapes.end(), [&](const auto& s) { return s.operands[j].value.is_const(); });
        valid = valid && (same || !any_cst);
      }
      if (!valid) {
        continue;
      }
    }
    // An UNSTAMPED Mux result means "as wide as the selected arm" (the LEC
    // encoder models it that way), but an unstamped result of any other op is
    // read as one bit by the encoder and cgen. The root is re-typed in place,
    // so it must carry the arms' realization: when every arm has the same
    // stamp (bits+sign, possibly none), the shared op at that stamp equals the
    // selected arm. Differing arm stamps have no single equivalent here.
    // Concat stamps its own total width below.
    const bool stamp_root = base.op != Ntype_op::Concat && gu::bits_of(root.get_driver_pin(0)) == 0;
    if (stamp_root) {
      const auto arm_bits = gu::bits_of(pins[1]);
      const auto arm_uns  = gu::is_unsign(pins[1]);
      if (!std::all_of(pins.begin() + 2, pins.end(), [&](const Pin& p) {
            return gu::bits_of(p) == arm_bits && (arm_bits == 0 || gu::is_unsign(p) == arm_uns);
          })) {
        continue;
      }
    }
    const auto root_bits = gu::bits_of(pins[1]);
    const auto root_uns  = gu::is_unsign(pins[1]);
    if (base.op == Ntype_op::Concat) {
      // A mixed-sign mux may need a wider carrier than either input. Concat
      // requires each carrier to fit its declared lane window. Without a
      // structural unsigned bound an explicit mask would be needed, consuming
      // the binary rewrite's operator saving. Leave such candidates intact.
      for (size_t j = 0; j < base.operands.size(); j += 2) {
        const auto value = base.operands[j].value;
        if (std::all_of(shapes.begin() + 1, shapes.end(), [&](const auto& s) { return s.operands[j].value == value; })) {
          continue;
        }
        const auto& width = gu::const_of(base.operands[j + 1].value);
        if (!width.is_just_i64() || width.to_just_i64() <= 0) {
          valid = false;
          break;
        }
        for (const auto& shape : shapes) {
          const int bits = cv::unsigned_width(shape.operands[j].value);
          if (bits < 0 || bits > width.to_just_i64()) {
            valid = false;
            break;
          }
        }
        if (!valid) {
          break;
        }
      }
      if (!valid) {
        continue;
      }
    }
    // Reserve both wiring traffic and local normalization work before mutation.
    // At most one new mux per operand, regardless of the width of that operand.
    if (!spend(budget, 4 * pins.size() * base.operands.size())) {
      break;
    }
    std::vector<Pin>  operands;
    std::vector<Node> created;
    for (size_t j = 0; j < base.operands.size(); ++j) {
      const auto value = base.operands[j].value;
      const bool same  = std::all_of(shapes.begin() + 1, shapes.end(), [&](const auto& s) { return s.operands[j].value == value; });
      if (same) {
        operands.push_back(value);
        continue;
      }
      // No result/arm width is copied: these are unlimited integer operands,
      // and the next bitwidth pass unions their ranges, including mixed signs.
      auto mux = cv::make_node(*current_graph, Ntype_op::Mux);
      gu::setup_sink_pid(mux, 0).connect_driver(pins[0]);
      for (size_t i = 0; i < shapes.size(); ++i) {
        gu::setup_sink_pid(mux, i + 1).connect_driver(shapes[i].operands[j].value);
      }
      auto out = mux.create_driver_pin(0);
      // Not every consumer re-runs bitwidth (LEC re-simplifies inlined
      // hierarchy with cprop alone, and cgen declares an unstamped net as one
      // bit). When every operand's realization is known, stamp the LOSSLESS
      // union carrier -- never the narrower result/arm hint -- which is what
      // bitwidth would infer anyway.
      if (const auto carrier = union_carrier(shapes, j)) {
        if (carrier->second) {
          gu::set_ubits(out, carrier->first);
        } else {
          gu::set_sbits(out, carrier->first);
        }
      }
      operands.push_back(out);
      created.push_back(mux);
    }
    cv::forget(root.get_driver_pin(0));
    for (auto sink : root.inp_pins_snapshot()) {
      sink.del_sink();
    }
    gu::clear_proven(root);
    gu::set_type_op(root, base.op);
    for (size_t j = 0; j < operands.size(); ++j) {
      gu::setup_sink_pid(root, base.operands[j].role).connect_driver(operands[j]);
    }
    if (base.op == Ntype_op::Concat) {
      gu::set_ubits(root.get_driver_pin(0), gu::concat_total_width(root));
    } else if (stamp_root && root_bits != 0) {
      if (root_uns) {
        gu::set_ubits(root.get_driver_pin(0), root_bits);
      } else {
        gu::set_sbits(root.get_driver_pin(0), root_bits);
      }
    }
    // Inputs have already been transferred. Retiring the private arms cannot
    // delete anything queued other than those exact generation-tagged nodes.
    for (auto arm : arms) {
      I(!arm.has_out_edges());
      cv::retire(arm);
    }
    for (auto mux : created) {
      const auto gen = generation(mux);
      // Fold constants/equal values without manufacturing Boolean gates.
      // Turning each new operand mux into an And/Or here could consume the
      // non-mux operator saving that justified this rewrite.
      Inp_pins   inputs;
      for (auto sink : mux.inp_sorted_pins()) {
        inputs.push_back(sink);
      }
      if (!try_constant_prop(mux, inputs) && !mux.is_invalid()) {
        try_collapse_forward(mux, inputs);
      }
      if (!mux.is_invalid() && generation(mux) == gen && gu::type_op_of(mux) == Ntype_op::Mux) {
        remember_node(mux);
        pending.push_back({mux, gen});
      }
    }
    remember_node(root);
  }
}

bool livehd::share_exclusive_operators(hhds::Graph& graph, Node first, Node second, Pin selector, bool first_when_true,
                                       size_t& budget, bool commit) {
  if (first == second || first.is_invalid() || second.is_invalid() || gu::has_color(first) || gu::has_color(second)
      || gu::has_runtime_check(first) || gu::has_runtime_check(second) || gu::has_name(first) || gu::has_name(second)) {
    return false;
  }
  auto a = first.get_driver_pin(0), b = second.get_driver_pin(0);
  if (!gu::pin_name_of(a).empty() || !gu::pin_name_of(b).empty() || gu::bits_of(a) <= 0 || gu::bits_of(a) != gu::bits_of(b)
      || gu::is_unsign(a) != gu::is_unsign(b)) {
    return false;
  }
  auto one_use = [](Node n) -> Pin {
    auto edges = n.out_edges();
    auto it    = edges.begin();
    if (it == edges.end()) {
      return {};
    }
    const auto sink = (*it).sink;
    return ++it == edges.end() ? sink : Pin{};
  };
  const auto sink_a = one_use(first), sink_b = one_use(second);
  if (sink_a.is_invalid() || sink_b.is_invalid()) {
    return false;
  }
  auto shape_a = shape_of(first, budget), shape_b = shape_of(second, budget);
  if (!shape_a || !shape_b || !align(*shape_a, *shape_b)) {
    return false;
  }
  const auto op = shape_a->op;
  if (op != Ntype_op::Mult && op != Ntype_op::Div && op != Ntype_op::Rem && op != Ntype_op::SHL && op != Ntype_op::SRA) {
    return false;
  }
  if (op == Ntype_op::SHL || op == Ntype_op::SRA) {
    const auto x = shape_a->operands[1].value, y = shape_b->operands[1].value;
    if (x != y && (x.is_const() || y.is_const())) {
      return false;
    }
  }
  std::vector<Shape> shapes{*shape_a, *shape_b};
  for (size_t i = 0; i < shape_a->operands.size(); ++i) {
    if (shape_a->operands[i].value != shape_b->operands[i].value && !union_carrier(shapes, i)) {
      return false;
    }
  }
  if (!spend(budget, 16 * shape_a->operands.size())) {
    return false;
  }
  if (!commit) {
    return true;
  }
  std::vector<Pin> operands;
  for (size_t i = 0; i < shape_a->operands.size(); ++i) {
    const auto x = shape_a->operands[i].value, y = shape_b->operands[i].value;
    if (x == y) {
      operands.push_back(x);
      continue;
    }
    auto mux = cv::make_node(graph, Ntype_op::Mux);
    gu::setup_sink_pid(mux, 0).connect_driver(selector);
    gu::setup_sink_pid(mux, 1).connect_driver(first_when_true ? y : x);
    gu::setup_sink_pid(mux, 2).connect_driver(first_when_true ? x : y);
    const auto carrier = *union_carrier(shapes, i);
    auto       output  = mux.create_driver_pin(0);
    if (carrier.second) {
      gu::set_ubits(output, carrier.first);
    } else {
      gu::set_sbits(output, carrier.first);
    }
    operands.push_back(output);
  }
  cv::forget(a);
  for (auto sink : first.inp_pins_snapshot()) {
    sink.del_sink();
  }
  gu::clear_proven(first);
  for (size_t i = 0; i < operands.size(); ++i) {
    gu::setup_sink_pid(first, shape_a->operands[i].role).connect_driver(operands[i]);
  }
  b.del_sink(sink_b);
  a.connect_sink(sink_b);
  cv::retire(second);
  return true;
}
