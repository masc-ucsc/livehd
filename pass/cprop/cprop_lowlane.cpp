//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Low-lane narrowing (Cprop::low_lane). A value whose low k bits are a known
// small part is spelled, width-free, as
//
//   Or(Shl(H, k), L)   with 0 <= L < 2^k     (value = H*2^k + L)
//
// a bare Shl(H, k) (L = 0), And(x, -2^k) (H = Sra(x, k), L = 0), or a
// two-lane Concat whose lanes fit their windows. An operation reading such an
// operand runs on the H parts only (k bits narrower) and re-attaches the low
// part: Sum(Shl(a,k), b) -> Or(Shl(Sum(a, Sra(b,k)), k), Get_mask(b,[0,k))).
// The result has the same shape, so the next operation narrows too, until a
// consumer that needs every bit (popcount, reductions, a runtime shift, a
// state input) reads the Or itself. Every identity holds in unlimited
// precision: no width or sign stamp is read or needed; bitwidth sizes the new
// cells afterwards.
#include <optional>
#include <vector>

#include "attr_carry.hpp"
#include "cprop.hpp"
#include "cprop_value.hpp"

namespace {
namespace gu = livehd::graph_util;
using Pin    = hhds::Pin_class;
using Node   = hhds::Node_class;

constexpr int kMaxLane = 1 << 20;

// A constant, numeric, fully known value.
std::optional<Dlop> known_const(const Pin& p) {
  if (p.is_invalid() || !p.is_const()) {
    return std::nullopt;
  }
  const auto& v = gu::const_of(p);
  if (!v.is_numeric() || v.has_unknowns()) {
    return std::nullopt;
  }
  return v;
}

// A constant shift/position amount in [0, kMaxLane], or -1.
int const_amount(const Pin& p) {
  const auto v = known_const(p);
  if (!v || v->is_negative() || !v->is_just_i64()) {
    return -1;
  }
  const auto a = v->to_just_i64();
  return a <= kMaxLane ? static_cast<int>(a) : -1;
}

// The one driver of sink `pid`, or invalid.
Pin driver(const Node& n, hhds::Port_id pid) {
  auto ds = n.get_sink_pin(pid).get_driver_pins();
  return ds.size() == 1 ? ds.front() : Pin{};
}

// `l` provably lies in [0, 2^k).
bool fits(const Pin& l, int k) {
  if (const auto c = known_const(l)) {
    return !c->is_negative() && (c->is_known_zero() || c->get_last_bit_set() < k);
  }
  const int w = livehd::cprop_value::unsigned_width(l);
  return w >= 0 && w <= k;
}

// value = H*2^k + L, 0 <= L < 2^k. `h` is H, or (h_sra) the x whose Sra by k
// is H. An invalid `l` is L = 0.
struct Form {
  int  k = 0;
  Pin  h;
  bool h_sra = false;
  Pin  l;
};

// The `-2^k` of And(x, -2^k), or 0.
int neg_mask_k(const Dlop& m) {
  if (!m.is_negative()) {
    return 0;
  }
  const int k = m.get_trailing_zeroes();
  return k > 0 && k <= kMaxLane && m.is_known_eq(*Dlop::get_neg_mask_value(k)) ? k : 0;
}

// A Shl(x, k) or And(x, -2^k) with k > 0.
std::optional<Form> shift_form(const Pin& p) {
  if (p.is_invalid() || p.is_const() || gu::is_graph_input_pin(p) || p.get_port_id() != 0) {
    return std::nullopt;
  }
  const auto n  = p.get_master_node();
  const auto op = gu::type_op_of(n);
  if (op == Ntype_op::SHL) {
    const int k = const_amount(driver(n, 1));
    const auto x = driver(n, 0);
    if (k > 0 && !x.is_invalid()) {
      return Form{k, x, false, {}};
    }
  } else if (op == Ntype_op::And) {
    std::vector<Pin> ops;
    for (auto s : n.inp_sorted_pins()) {
      for (auto d : s.get_driver_pins()) {
        ops.push_back(d);
      }
    }
    if (ops.size() == 2) {
      for (int i = 0; i < 2; ++i) {
        const auto c = known_const(ops[i]);
        const int  k = c ? neg_mask_k(*c) : 0;
        if (k > 0 && !ops[1 - i].is_const()) {
          return Form{k, ops[1 - i], true, {}};
        }
      }
    }
  }
  return std::nullopt;
}

// Any low-lane shape (see the file comment), or nullopt.
std::optional<Form> form_of(const Pin& p) {
  if (auto f = shift_form(p)) {
    return f;
  }
  if (p.is_invalid() || p.is_const() || gu::is_graph_input_pin(p) || p.get_port_id() != 0) {
    return std::nullopt;
  }
  const auto n  = p.get_master_node();
  const auto op = gu::type_op_of(n);
  if (op == Ntype_op::Or) {
    std::vector<Pin> ops;
    for (auto s : n.inp_sorted_pins()) {
      for (auto d : s.get_driver_pins()) {
        ops.push_back(d);
      }
    }
    if (ops.size() != 2) {
      return std::nullopt;
    }
    for (int i = 0; i < 2; ++i) {
      auto f = shift_form(ops[i]);
      if (f && fits(ops[1 - i], f->k)) {
        f->l = ops[1 - i];
        return f;
      }
    }
  } else if (op == Ntype_op::Concat) {
    const auto lanes = gu::concat_lanes(n);
    if (lanes.size() == 2 && lanes[1].offset == 0 && fits(lanes[0].value, lanes[0].width) && fits(lanes[1].value, lanes[1].width)
        && lanes[1].width <= kMaxLane) {
      return Form{lanes[1].width, lanes[0].value, false, lanes[1].value};
    }
  }
  return std::nullopt;
}

// One operand split at k: value = H*2^k + L.
struct Part {
  // Exactly one of hc/h: a constant H or the pin that is (or, with h_sra, is
  // Sra by h_shift of) H. `h_shl` > 0 means H = Shl(that, h_shl).
  std::optional<Dlop> hc;
  Pin                 h;
  int                 h_sra = 0;
  int                 h_shl = 0;
  // Exactly one of lc/l: a constant L (0 = none) or the pin of L. `l_mask`
  // means L = Get_mask(l, [0, k)).
  std::optional<Dlop> lc;
  Pin                 l;
  bool                l_mask    = false;
  bool                arbitrary = false;  // neither a constant nor a low-lane form
  bool                form      = false;  // a (non-constant) low-lane form
};

Part split(const Pin& p, int k) {
  Part part;
  if (const auto c = known_const(p)) {
    part.hc = *c->sra_op(*Dlop::create_integer(k));
    part.lc = *c->and_op(*Dlop::get_mask_value(k));
    return part;
  }
  if (const auto f = form_of(p); f && (f->k == k || (f->k > k && f->l.is_invalid()))) {
    part.form  = true;
    part.h     = f->h;
    part.h_sra = f->h_sra ? f->k : 0;
    part.h_shl = f->k - k;
    if (f->l.is_invalid()) {
      part.lc = *Dlop::create_integer(0);
    } else if (const auto lc = known_const(f->l)) {
      part.lc = *lc;
    } else {
      part.l = f->l;
    }
    return part;
  }
  part.arbitrary = true;
  part.h         = p;
  part.h_sra     = k;
  part.l         = p;
  part.l_mask    = true;
  return part;
}

bool zero_l(const Part& p) { return p.lc && p.lc->is_known_zero(); }

// Builds the replacement cells for one rewrite: every cell created is colored
// like the node it replaces and normalized once it is wired in.
class Low_lane_builder {
public:
  Low_lane_builder(hhds::Graph& g, const Node& origin) : g_(g), origin_(origin) {}

  Pin constant(const Dlop& v) { return gu::create_const(g_, v); }

  Node make(Ntype_op op) {
    auto n = livehd::cprop_value::make_node(g_, op);
    if (gu::has_color(origin_)) {
      gu::set_color(n, gu::color_of(origin_));
    }
    livehd::graph_util::carry_srcid(origin_, n);
    made_.push_back(n);
    return n;
  }
  Pin shift(Ntype_op op, const Pin& x, int amount) {
    if (amount == 0) {
      return x;
    }
    auto n = make(op);
    gu::setup_sink_by_name(n, "a").connect_driver(x);
    gu::setup_sink_by_name(n, "b").connect_driver(constant(*Dlop::create_integer(amount)));
    return n.create_driver_pin(0);
  }
  Pin low_mask(const Pin& x, int lo, int hi) {
    auto n = livehd::cprop_value::make_get_mask(g_, x, constant(*Dlop::get_mask_value(hi - 1, lo)));
    if (gu::has_color(origin_)) {
      gu::set_color(n, gu::color_of(origin_));
    }
    livehd::graph_util::carry_srcid(origin_, n);
    made_.push_back(n);
    return n.create_driver_pin(0);
  }
  Pin h_of(const Part& p) {
    if (p.hc) {
      return constant(*p.hc);
    }
    return shift(Ntype_op::SHL, shift(Ntype_op::SRA, p.h, p.h_sra), p.h_shl);
  }
  // L as a pin, or invalid for a zero constant.
  Pin l_of(const Part& p, int k) {
    if (p.lc) {
      return p.lc->is_known_zero() ? Pin{} : constant(*p.lc);
    }
    return p.l_mask ? low_mask(p.l, 0, k) : p.l;
  }
  // Or(Shl(h, k), l); just the Shl when l is invalid.
  Pin join(const Pin& h, int k, const Pin& l) {
    auto hi = shift(Ntype_op::SHL, h, k);
    if (l.is_invalid()) {
      return hi;
    }
    auto n = make(Ntype_op::Or);
    gu::setup_sink_by_name(n, "as").connect_driver(hi);
    gu::setup_sink_by_name(n, "as").connect_driver(l);
    return n.create_driver_pin(0);
  }
  const std::vector<Node>& made() const { return made_; }

private:
  hhds::Graph&      g_;
  Node              origin_;
  std::vector<Node> made_;
};

}  // namespace

bool Cprop::low_lane(hhds::Node_class& node) {
  if (!low_lanes_ || node.is_invalid() || !node.has_out_edges()) {
    return false;
  }
  const auto op = gu::type_op_of(node);
  switch (op) {
    case Ntype_op::Sum:
    case Ntype_op::Mult:
    case Ntype_op::And:
    case Ntype_op::Or:
    case Ntype_op::Xor:
    case Ntype_op::Mux:
    case Ntype_op::EQ:
    case Ntype_op::LT:
    case Ntype_op::GT:
    case Ntype_op::SRA:
    case Ntype_op::Get_mask:
    case Ntype_op::Sext:
    case Ntype_op::Not: break;
    default: return false;
  }
  // Operands in pid order, with their pid.
  std::vector<std::pair<hhds::Port_id, Pin>> ops;
  for (auto s : node.inp_sorted_pins()) {
    auto ds = s.get_driver_pins();
    if (ds.size() != 1) {
      return false;
    }
    ops.emplace_back(s.get_port_id(), ds.front());
  }
  if (ops.empty()) {
    return false;
  }

  Low_lane_builder b{*current_graph, node};
  Pin              result;

  // The common k: the smallest k of a non-constant low-lane operand (data
  // operands only; a Mux selector is not one).
  const size_t first = op == Ntype_op::Mux ? 1 : 0;
  int          k     = 0;
  for (size_t i = first; i < ops.size(); ++i) {
    if (const auto f = form_of(ops[i].second)) {
      k = k == 0 ? f->k : std::min(k, f->k);
    }
  }

  const auto parts_at = [&](int at) {
    std::vector<Part> parts;
    for (size_t i = first; i < ops.size(); ++i) {
      parts.push_back(split(ops[i].second, at));
    }
    return parts;
  };
  const auto count = [](const std::vector<Part>& parts, auto pred) {
    int n = 0;
    for (const auto& p : parts) {
      n += pred(p) ? 1 : 0;
    }
    return n;
  };

  if (op == Ntype_op::Sum) {
    if (k == 0 || ops.size() < 2) {
      return false;
    }
    auto parts = parts_at(k);
    // Low parts: at most one runtime L, added (never subtracted); constants
    // fold into one value whose carry moves into the H sum.
    Dlop cl       = *Dlop::create_integer(0);
    int  runtime  = -1;
    int  n_arbit  = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
      const bool minus = Ntype::sink_bank(op, ops[i].first) == 1;
      n_arbit += parts[i].arbitrary ? 1 : 0;
      if (parts[i].lc) {
        cl = minus ? *cl.sub_op(*parts[i].lc) : *cl.add_op(*parts[i].lc);
      } else if (runtime >= 0 || minus) {
        return false;
      } else {
        runtime = static_cast<int>(i);
      }
    }
    if (n_arbit > 1 || (runtime >= 0 && !cl.is_known_zero())) {
      return false;
    }
    // One runtime operand plus constants narrows nothing a mapper does not
    // already see (a constant's zero low bits are wires), and folding the
    // constant's carry into H widens an affine word-select index:
    // `(3 - sel)*32 + 32` became `(4 - sel)*32`, a 3-bit index over a 2-bit
    // select that doubled the select mux (dynamic_word_select).
    if (count(parts, [](const Part& p) { return !p.hc; }) < 2) {
      return false;
    }
    auto sum = b.make(Ntype_op::Sum);
    for (size_t i = 0; i < parts.size(); ++i) {
      const bool minus = Ntype::sink_bank(op, ops[i].first) == 1;
      gu::setup_sink_by_name(sum, minus ? "bs" : "as").connect_driver(b.h_of(parts[i]));
    }
    Pin low;
    if (runtime >= 0) {
      low = b.l_of(parts[runtime], k);
    } else {
      const auto carry = *cl.sra_op(*Dlop::create_integer(k));
      if (!carry.is_known_zero()) {
        gu::setup_sink_by_name(sum, "as").connect_driver(b.constant(carry));
      }
      const auto rest = *cl.and_op(*Dlop::get_mask_value(k));
      low             = rest.is_known_zero() ? Pin{} : b.constant(rest);
    }
    result = b.join(sum.create_driver_pin(0), k, low);
  } else if (op == Ntype_op::Mult) {
    // Each operand contributes its own trailing zeros: product of the H
    // parts, shifted by the total.
    int              total = 0;
    bool             any   = false;
    std::vector<Pin> hs;
    for (const auto& [pid, p] : ops) {
      if (const auto c = known_const(p)) {
        if (c->is_known_zero()) {
          return false;
        }
        const int tz = c->get_trailing_zeroes();
        total += tz;
        hs.push_back(b.constant(*c->sra_op(*Dlop::create_integer(tz))));
      } else if (const auto f = form_of(p); f && f->l.is_invalid()) {
        any    = true;
        total += f->k;
        hs.push_back(b.shift(Ntype_op::SRA, f->h, f->h_sra ? f->k : 0));
      } else {
        hs.push_back(p);
      }
    }
    if (!any || total > kMaxLane) {
      return false;
    }
    auto mult = b.make(Ntype_op::Mult);
    for (const auto& h : hs) {
      gu::setup_sink_by_name(mult, "as").connect_driver(h);
    }
    result = b.shift(Ntype_op::SHL, mult.create_driver_pin(0), total);
  } else if (op == Ntype_op::And) {
    // One operand with a zero low part zeroes the whole low part.
    if (k == 0) {
      return false;
    }
    auto parts = parts_at(k);
    if (count(parts, [](const Part& p) { return p.form && zero_l(p); }) == 0) {
      return false;
    }
    auto n = b.make(Ntype_op::And);
    for (const auto& p : parts) {
      gu::setup_sink_by_name(n, "as").connect_driver(b.h_of(p));
    }
    result = b.shift(Ntype_op::SHL, n.create_driver_pin(0), k);
  } else if (op == Ntype_op::Or || op == Ntype_op::Xor) {
    if (k == 0) {
      return false;
    }
    auto parts = parts_at(k);
    // Or merges two or more low-lane operands (an Or of one form and a small
    // value IS the form); Xor also takes one arbitrary operand.
    const int forms   = count(parts, [](const Part& p) { return p.form; });
    const int arbit   = count(parts, [](const Part& p) { return p.arbitrary; });
    const int runtime = count(parts, [](const Part& p) { return !p.lc; });
    if (op == Ntype_op::Or ? (forms < 2 || arbit > 0) : (arbit > 1 || runtime > 1)) {
      return false;
    }
    Dlop             cl = *Dlop::create_integer(0);
    std::vector<Pin> ls;
    auto             n = b.make(op);
    for (const auto& p : parts) {
      gu::setup_sink_by_name(n, "as").connect_driver(b.h_of(p));
      if (p.lc) {
        cl = op == Ntype_op::Or ? *cl.or_op(*p.lc) : *cl.xor_op(*p.lc);
      } else {
        ls.push_back(b.l_of(p, k));
      }
    }
    if (!cl.is_known_zero()) {
      ls.push_back(b.constant(cl));
    }
    Pin low;
    if (ls.size() == 1) {
      low = ls.front();
    } else if (ls.size() > 1) {
      auto ln = b.make(op);
      for (const auto& l : ls) {
        gu::setup_sink_by_name(ln, "as").connect_driver(l);
      }
      low = ln.create_driver_pin(0);
    }
    result = b.join(n.create_driver_pin(0), k, low);
  } else if (op == Ntype_op::Mux) {
    // Every arm a constant or a low-lane form: one narrower Mux of the H
    // parts, and the low parts (one value, or their own Mux).
    if (k == 0 || ops.size() < 3 || ops[0].first != 0) {
      return false;
    }
    auto parts = parts_at(k);
    if (count(parts, [](const Part& p) { return p.arbitrary; }) > 0) {
      return false;
    }
    auto n = b.make(Ntype_op::Mux);
    gu::setup_sink_pid(n, 0).connect_driver(ops[0].second);
    bool same_low = true;
    for (size_t i = 0; i < parts.size(); ++i) {
      gu::setup_sink_pid(n, ops[i + 1].first).connect_driver(b.h_of(parts[i]));
      const auto& a = parts[i];
      const auto& z = parts[0];
      same_low      = same_low && (a.lc && z.lc ? a.lc->is_known_eq(*z.lc) : (!a.lc && !z.lc && a.l == z.l));
    }
    Pin low;
    if (same_low) {
      low = b.l_of(parts[0], k);
    } else {
      auto ln = b.make(Ntype_op::Mux);
      gu::setup_sink_pid(ln, 0).connect_driver(ops[0].second);
      for (size_t i = 0; i < parts.size(); ++i) {
        auto l = b.l_of(parts[i], k);
        gu::setup_sink_pid(ln, ops[i + 1].first).connect_driver(l.is_invalid() ? b.constant(*Dlop::create_integer(0)) : l);
      }
      low = ln.create_driver_pin(0);
    }
    result = b.join(n.create_driver_pin(0), k, low);
  } else if (op == Ntype_op::EQ || op == Ntype_op::LT || op == Ntype_op::GT) {
    // a == b  <=>  Ha == Hb and La == Lb. a < b with equal constant low
    // parts <=> Ha < Hb.
    if (k == 0 || ops.size() != 2) {
      return false;
    }
    if (op != Ntype_op::EQ && (ops[0].first != 0 || ops[1].first != 1)) {
      return false;
    }
    auto       parts = parts_at(k);
    const int  arbit = count(parts, [](const Part& p) { return p.arbitrary; });
    const bool const_low = parts[0].lc && parts[1].lc;
    if (arbit > (op == Ntype_op::EQ ? 1 : 0) || (op != Ntype_op::EQ && !(const_low && parts[0].lc->is_known_eq(*parts[1].lc)))) {
      return false;
    }
    if (const_low && !parts[0].lc->is_known_eq(*parts[1].lc)) {
      result = b.constant(*Dlop::create_integer(0));  // EQ only (checked above)
    } else {
      auto cmp = b.make(op);
      gu::setup_sink_pid(cmp, ops[0].first).connect_driver(b.h_of(parts[0]));
      gu::setup_sink_pid(cmp, ops[1].first).connect_driver(b.h_of(parts[1]));
      result = cmp.create_driver_pin(0);
      if (!const_low) {
        auto la = b.l_of(parts[0], k), lb = b.l_of(parts[1], k);
        auto eq = b.make(Ntype_op::EQ);
        gu::setup_sink_pid(eq, 0).connect_driver(la.is_invalid() ? b.constant(*Dlop::create_integer(0)) : la);
        gu::setup_sink_pid(eq, 0).connect_driver(lb.is_invalid() ? b.constant(*Dlop::create_integer(0)) : lb);
        auto both = b.make(Ntype_op::And);
        gu::setup_sink_by_name(both, "as").connect_driver(result);
        gu::setup_sink_by_name(both, "as").connect_driver(eq.create_driver_pin(0));
        result = both.create_driver_pin(0);
      }
    }
  } else {
    // Unary readers of one low-lane operand.
    const auto f = form_of(ops[0].second);
    if (!f) {
      return false;
    }
    const int  fk = f->k;
    const auto hb = [&]() { return b.shift(Ntype_op::SRA, f->h, f->h_sra ? fk : 0); };
    if (op == Ntype_op::SRA) {
      // floor((H*2^k + L) / 2^j): the low part only survives below k.
      const int j = ops.size() == 2 ? const_amount(ops[1].second) : -1;
      if (j < 0 || (f->l.is_invalid() && !f->h_sra)) {
        return false;  // a plain Sra(Shl) is scalar_shift's
      }
      if (j >= fk) {
        result = f->h_sra ? b.shift(Ntype_op::SRA, f->h, j) : b.shift(Ntype_op::SRA, f->h, j - fk);
      } else {
        result = b.join(hb(), fk - j, f->l.is_invalid() ? Pin{} : b.shift(Ntype_op::SRA, f->l, j));
      }
    } else if (op == Ntype_op::Get_mask) {
      const auto mask = ops.size() == 2 ? known_const(ops[1].second) : std::nullopt;
      if (!mask || mask->is_negative()) {
        return false;
      }
      const auto [lo, hi] = mask->get_mask_range();  // [lo, hi)
      if (lo < 0 || hi <= lo) {
        return false;
      }
      if (lo >= fk) {
        result = b.low_mask(hb(), lo - fk, hi - fk);
      } else if (hi <= fk) {
        result = f->l.is_invalid() ? b.constant(*Dlop::create_integer(0)) : b.low_mask(f->l, lo, hi);
      } else {
        return false;
      }
    } else if (op == Ntype_op::Sext) {
      // Sign extension from bit b-1 above the low part leaves L alone.
      const int bits = ops.size() == 2 ? const_amount(ops[1].second) : -1;
      if (bits <= fk) {
        return false;
      }
      auto n = b.make(Ntype_op::Sext);
      gu::setup_sink_by_name(n, "a").connect_driver(hb());
      gu::setup_sink_by_name(n, "b").connect_driver(b.constant(*Dlop::create_integer(bits - fk)));
      result = b.join(n.create_driver_pin(0), fk, f->l);
    } else if (op == Ntype_op::Not) {
      // ~(H*2^k + L) = (~H)*2^k + (2^k-1-L).
      auto n = b.make(Ntype_op::Not);
      gu::setup_sink_by_name(n, "a").connect_driver(hb());
      const auto ones = *Dlop::get_mask_value(fk);
      Pin        low;
      if (f->l.is_invalid()) {
        low = b.constant(ones);
      } else if (const auto lc = known_const(f->l)) {
        const auto v = *lc->xor_op(ones);
        low          = v.is_known_zero() ? Pin{} : b.constant(v);
      } else {
        auto x = b.make(Ntype_op::Xor);
        gu::setup_sink_by_name(x, "as").connect_driver(f->l);
        gu::setup_sink_by_name(x, "as").connect_driver(b.constant(ones));
        low = x.create_driver_pin(0);
      }
      result = b.join(n.create_driver_pin(0), fk, low);
    } else {
      return false;
    }
  }

  if (result.is_invalid()) {
    return false;
  }
  const auto out  = node.get_driver_pin(0);
  const auto name = std::string{gu::pin_name_of(out)};
  if (!collapse_forward_for_pin(node, result)) {
    for (auto n : b.made()) {
      if (!n.is_invalid() && !n.has_out_edges()) {
        bwd_del_node(n);
      }
    }
    return false;
  }
  // The value keeps its name when the rewrite built its new driver.
  if (!name.empty() && !result.is_const() && gu::pin_name_of(result).empty()) {
    const auto rn = result.get_master_node();
    for (const auto& n : b.made()) {
      if (n == rn) {
        gu::set_pin_name(result, name);
        break;
      }
    }
  }
  for (auto n : b.made()) {
    normalize_emitted(n);
  }
  return true;
}

int livehd::low_lane_bits(const hhds::Pin_class& pin) {
  const auto f = form_of(pin);
  return f ? f->k : 0;
}
