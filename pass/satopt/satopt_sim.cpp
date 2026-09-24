// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "satopt_sim.hpp"

#include <algorithm>
#include <map>
#include <string>

#include "mask_eval.hpp"
#include "satopt_detail.hpp"

namespace livehd::satopt::detail {
namespace {
// Values held at once, over every column (16 bytes each, inline up to 64 bits).
constexpr uint64_t kMaxStored = 4'000'000;

uint64_t splitmix(uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

Dlop fit(const Dlop& v, const Pin& p) {
  if (p.is_const()) {
    return v;
  }
  auto low = v.and_op(Dlop::get_mask_value(width(p)));
  return gu::is_unsign(p) ? *low : *low->sext_op(Dlop::create_integer(width(p)));
}

// `w` bits, bit b = bit(b), as an unsigned value.
template <class Bit>
Dlop from_bits(int w, const Bit& bit) {
  if (w <= 62) {
    uint64_t v = 0;
    for (int b = 0; b < w; ++b) {
      v |= static_cast<uint64_t>(bit(b)) << b;
    }
    return *Dlop::create_integer(static_cast<int64_t>(v));
  }
  std::string text(static_cast<size_t>(w), '0');
  for (int b = 0; b < w; ++b) {
    text[static_cast<size_t>(w - 1 - b)] = bit(b) ? '1' : '0';  // MSB first
  }
  return *Dlop::from_binary(text, true);
}
}  // namespace

Word_sim::Word_sim(const Options& opts) : opts_(opts), columns_(std::max<uint32_t>(1, opts.samples)) {}

bool Word_sim::is_leaf(const Key& k) const { return gu::is_graph_input_pin(k.second) || cut(k.second); }

uint32_t Word_sim::child(uint32_t scope, const Node& inst) {
  const auto [it, fresh] = children_.try_emplace({scope, inst}, static_cast<uint32_t>(scopes_.size()));
  if (fresh) {
    scopes_.push_back(
        {scope, inst, scopes_[scope].depth + 1, splitmix(scopes_[scope].path ^ static_cast<uint64_t>(inst.get_debug_nid()))});
  }
  return it->second;
}

// Where p continues across an instance boundary when descending.
std::optional<std::pair<uint32_t, Pin>> Word_sim::cross(uint32_t scope, const Pin& p) {
  if (!opts_.descend) {
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
  return std::pair{child(scope, inst), driver};
}

std::optional<Dlop> Word_sim::leaf(const Key& k, uint32_t j) {
  const auto& p = k.second;
  if (auto m = models_.find(j); m != models_.end()) {
    if (auto it = m->second.find(k); it != m->second.end()) {
      return fit(it->second, p);
    }
  }
  const int w = width(p);
  if (j < kCorners) {
    // 0, all ones, 0101.., 1010.., signed min, signed max, 1: the carries,
    // wraps and sign boundaries a handful of random words rarely hit.
    const auto corner = [&](int b) {
      switch (j) {
        case 0: return false;
        case 1: return true;
        case 2: return (b & 1) == 0;
        case 3: return (b & 1) == 1;
        case 4: return b == w - 1;
        case 5: return b != w - 1;
        default: return b == 0;
      }
    };
    return fit(from_bits(w, corner), p);
  }
  const auto  n    = p.get_master_node();
  const auto  seed = splitmix(opts_.salt ^ scopes_[k.first].path ^ splitmix(static_cast<uint64_t>(n.get_debug_nid()))
                             ^ (static_cast<uint64_t>(p.get_port_id()) << 48) ^ (static_cast<uint64_t>(j) << 32));
  uint64_t    word = 0;
  const auto  bit  = [&](int b) {
    if (b % 64 == 0) {
      word = splitmix(seed + static_cast<uint64_t>(b / 64));
    }
    return ((word >> (b % 64)) & 1) != 0;
  };
  return fit(from_bits(w, bit), p);
}

bool Word_sim::deps(const Key& k, std::vector<Key>& out) {
  const auto& [scope, p] = k;
  if (p.is_invalid() || width(p) > 65536) {
    return false;
  }
  if (p.is_const()) {
    return !gu::const_of(p).has_unknowns();
  }
  if (const auto other = cross(scope, p)) {
    const Key from{other->first, other->second};
    copies_[k] = from;
    out.push_back(from);
    return true;
  }
  if (opts_.reject_unstamped && !gu::is_graph_input_pin(p) && gu::bits_of(p) <= 0
      && gu::type_op_of(p.get_master_node()) != Ntype_op::Sub) {
    return false;
  }
  if (is_leaf(k)) {
    return true;
  }
  for (const auto& in_pin : p.get_master_node().inp_sorted_pins()) {
    for (const auto& d : in_pin.get_driver_pins()) {
      out.push_back({scope, d});
    }
  }
  return true;
}

std::optional<Dlop> Word_sim::column(const Key& k, uint32_t j) {
  const auto& [scope, p] = k;
  if (p.is_const()) {
    return gu::const_of(p);
  }
  if (const auto c = copies_.find(k); c != copies_.end()) {
    if (c->second.second.is_const()) {
      return gu::const_of(c->second.second);  // a constant crossing the boundary
    }
    const auto from = memo_.find(c->second);
    if (from == memo_.end() || from->second.size() <= j || failed_.contains(c->second)) {
      return std::nullopt;
    }
    return from->second[j];
  }
  if (is_leaf(k)) {
    return leaf(k, j);
  }
  const auto n  = p.get_master_node();
  const auto op = gu::type_op_of(n);
  // Operands by BANK (a commutative cell spends one sink pid per operand), in
  // pid order; each operand's column j.
  std::map<int, std::vector<const Dlop*>> ins;
  const auto                              value_of = [&](const Pin& d) -> const Dlop* {
    if (d.is_const()) {
      return &gu::const_of(d);
    }
    if (overlay_ != nullptr) {
      if (const auto o = overlay_->find({scope, d}); o != overlay_->end()) {
        return o->second.size() <= j ? nullptr : &o->second[j];
      }
    }
    const auto it = memo_.find({scope, d});
    return it == memo_.end() || it->second.size() <= j || failed_.contains({scope, d}) ? nullptr : &it->second[j];
  };
  for (const auto& in_pin : n.inp_sorted_pins()) {
    for (const auto& d : in_pin.get_driver_pins()) {
      const auto* v = value_of(d);
      if (v == nullptr) {
        return std::nullopt;
      }
      ins[Ntype::sink_bank(op, in_pin.get_port_id())].push_back(v);
    }
  }
  // The one operand of a single-operand bank; a bank with several operands is
  // only meaningful for the commutative cells below.
  const auto arg = [&](int bank) -> const Dlop* {
    const auto it = ins.find(bank);
    return it == ins.end() || it->second.size() != 1 ? nullptr : it->second.front();
  };
  const auto pin_value = [&](const Pin& d) { return value_of(d); };

  Dlop v = *Dlop::create_integer(0);
  if (op == Ntype_op::Mux || op == Ntype_op::Hotmux) {
    const auto arms = arms_of(n);
    if (arms.values.empty()) {
      return std::nullopt;
    }
    size_t selected = arms.values.size();
    if (!arms.hot) {
      const auto* c = pin_value(arms.controls[0]);
      if (c == nullptr) {
        return std::nullopt;
      }
      selected = c->is_known_zero() ? 0 : 1;
    } else {
      for (size_t i = 0; i < arms.values.size(); ++i) {
        if (arms.controls[i].is_invalid()) {
          selected = i;
          break;
        }
        const auto* c = pin_value(arms.controls[i]);
        if (c == nullptr) {
          return std::nullopt;
        }
        if (!c->is_known_zero()) {
          selected = i;
          break;
        }
      }
    }
    if (selected < arms.values.size()) {
      const auto* s = pin_value(arms.values[selected]);
      if (s == nullptr) {
        return std::nullopt;
      }
      v = *s;
    }
  } else if (op == Ntype_op::Concat) {
    std::vector<Dlop::Concat_lane> lanes;
    for (const auto& l : gu::concat_lanes(n)) {
      const auto* lane = pin_value(l.value);
      if (lane == nullptr) {
        return std::nullopt;
      }
      lanes.push_back({lane, l.width});
    }
    v = *Dlop::concat_op(lanes);
  } else if (op == Ntype_op::Rxor || op == Ntype_op::Popcount) {
    const auto* a = arg(0);
    if (a == nullptr) {
      return std::nullopt;
    }
    const auto selected = a->get_mask_op_opt(0, gu::reduction_count(n));
    v                   = *(op == Ntype_op::Rxor ? selected->rxor_op() : selected->popcount_op());
  } else if (op == Ntype_op::Not) {
    const auto* a = arg(0);
    if (a == nullptr) {
      return std::nullopt;
    }
    v = *a->not_op();
  } else if (op == Ntype_op::Get_mask) {
    const auto *a = arg(0), *m = arg(2);
    if (a == nullptr || m == nullptr) {
      return std::nullopt;
    }
    v = livehd::eval_get_mask(*a, *m);
  } else if (op == Ntype_op::Set_mask) {
    const auto *a = arg(0), *m = arg(2), *s = arg(4);
    if (a == nullptr || m == nullptr || s == nullptr) {
      return std::nullopt;
    }
    v = livehd::eval_set_mask(*a, *m, *s);
  } else if (op == Ntype_op::Sext || op == Ntype_op::SHL || op == Ntype_op::SRA || op == Ntype_op::LT || op == Ntype_op::GT) {
    const auto *a = arg(0), *b = arg(1);
    if (a == nullptr || b == nullptr) {
      return std::nullopt;
    }
    switch (op) {
      case Ntype_op::Sext: v = *a->sext_op(*b); break;
      case Ntype_op::SHL: v = *a->shl_op(*b); break;
      case Ntype_op::SRA: v = *a->sra_op(*b); break;
      case Ntype_op::LT: v = *a->lt_op(*b); break;
      default: v = *a->gt_op(*b); break;
    }
  } else if (op == Ntype_op::Sum || op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Xor || op == Ntype_op::Mult
             || op == Ntype_op::EQ || op == Ntype_op::Ror) {
    bool first = true;
    Dlop equal_to;
    for (const auto& [bank, operands] : ins) {
      for (const auto* operand : operands) {
        const auto& x = *operand;
        if (op == Ntype_op::Sum) {
          v = *(bank == 1 ? v.sub_op(x) : v.add_op(x));
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
    return std::nullopt;
  }
  if (v.has_unknowns() || v.is_invalid()) {
    return std::nullopt;
  }
  return fit(v, p);
}

const std::vector<Dlop>* Word_sim::values(uint32_t scope, const Pin& start) {
  if (start.is_invalid()) {
    return nullptr;
  }
  const Key root{scope, start};
  if (failed_.contains(root)) {
    return nullptr;
  }
  if (auto it = memo_.find(root); it != memo_.end()) {
    return &it->second;
  }
  // Iterative post-order: a cone as deep as the design never recurses. A
  // frame is expanded once (its operands pushed above it) and computed when it
  // surfaces again; an operand still being expanded below it is a loop.
  struct Frame {
    Key  key;
    bool expanded = false;
  };
  std::vector<Frame>       stack{{root}};
  absl::flat_hash_set<Key> on_stack;
  std::vector<Key>         operands;
  while (!stack.empty()) {
    const auto key = stack.back().key;
    if (memo_.contains(key) || failed_.contains(key)) {
      stack.pop_back();
      continue;
    }
    if (!stack.back().expanded) {
      stack.back().expanded = true;
      operands.clear();
      if (!deps(key, operands)) {
        failed_.insert(key);
        stack.pop_back();
        continue;
      }
      on_stack.insert(key);
      bool loop = false;
      for (const auto& d : operands) {
        if (d.second.is_const() || memo_.contains(d) || failed_.contains(d)) {
          continue;
        }
        if (on_stack.contains(d)) {
          loop = true;
          break;
        }
        stack.push_back({d});
      }
      if (loop) {
        // A combinational loop has no value; frames already pushed above
        // evaluate (or fail) on their own.
        failed_.insert(key);
        on_stack.erase(key);
      }
      continue;
    }
    stack.pop_back();
    on_stack.erase(key);
    if (stored_ + columns_ > kMaxStored) {
      failed_.insert(key);
      continue;
    }
    std::vector<Dlop> v;
    v.reserve(columns_);
    bool ok = true;
    for (uint32_t j = 0; j < columns_ && ok; ++j) {
      auto c = column(key, j);
      ok     = c.has_value();
      if (ok) {
        v.push_back(std::move(*c));
      }
    }
    work_ += columns_;
    if (!ok) {
      failed_.insert(key);
      continue;
    }
    stored_ += v.size();
    memo_.emplace(key, std::move(v));
    order_.push_back(key);
  }
  if (auto it = memo_.find(root); it != memo_.end()) {
    return &it->second;
  }
  return nullptr;
}

bool Word_sim::add_model(const formal::Model& model) {
  if (models_.size() >= opts_.max_models) {
    return false;
  }
  const uint32_t j = columns_;
  auto&          m = models_[j];
  for (const auto& leaf : model) {
    uint32_t scope = 0;
    for (const auto& inst : leaf.path) {
      scope = child(scope, inst);
    }
    m.emplace(Key{scope, leaf.pin}, leaf.value);
  }
  ++columns_;
  // Extend every computed value, operands first. A value the new column makes
  // unavailable is marked failed: it keeps its old columns (a caller may hold
  // it) but is never handed out again, and its users fail with it.
  std::vector<Key> keep;
  keep.reserve(order_.size());
  for (const auto& key : order_) {
    auto it = memo_.find(key);
    auto c  = column(key, j);
    ++work_;
    if (!c) {
      failed_.insert(key);
      continue;
    }
    it->second.push_back(std::move(*c));
    ++stored_;
    keep.push_back(key);
  }
  order_ = std::move(keep);
  return true;
}

std::optional<bool> Word_sim::unchanged_under(const Pin& target, const Dlop& mask, const Dlop& value,
                                             const std::vector<Node>& window, const std::vector<Pin>& exits) {
  const auto* tv = values(target);
  if (tv == nullptr) {
    return std::nullopt;
  }
  absl::flat_hash_set<Pin> inside;
  for (const auto& node : window) {
    for (const auto& e : node.out_edges()) {
      inside.insert(e.driver);
    }
  }
  // Every other input of the window, as it is.
  for (const auto& node : window) {
    for (const auto& in_pin : node.inp_sorted_pins()) {
      for (const auto& d : in_pin.get_driver_pins()) {
        if (!d.is_const() && d != target && !inside.contains(d) && values(d) == nullptr) {
          return std::nullopt;
        }
      }
    }
  }
  std::vector<const std::vector<Dlop>*> before;
  for (const auto& e : exits) {
    const auto* v = values(e);
    if (v == nullptr) {
      return std::nullopt;
    }
    before.push_back(v);
  }
  const int                                   w    = width(target);
  const auto                                  keep = *Dlop::get_mask_value(w)->xor_op(mask);
  absl::flat_hash_map<Key, std::vector<Dlop>> overlay;
  auto&                                       t = overlay[{0, target}];
  for (const auto& x : *tv) {
    t.push_back(fit(*x.and_op(keep)->or_op(*value.and_op(mask)), target));
  }
  work_    += columns_;
  overlay_  = &overlay;
  bool ok   = true;
  for (const auto& node : window) {
    for (const auto& e : node.out_edges()) {
      const Key k{0, e.driver};
      if (!ok || overlay.contains(k)) {
        continue;
      }
      std::vector<Dlop> v;
      for (uint32_t j = 0; j < columns_ && ok; ++j) {
        auto c = column(k, j);
        ok     = c.has_value();
        if (ok) {
          v.push_back(std::move(*c));
        }
      }
      work_ += columns_;
      overlay.emplace(k, std::move(v));
    }
  }
  overlay_ = nullptr;
  if (!ok) {
    return std::nullopt;
  }
  for (size_t i = 0; i < exits.size(); ++i) {
    const auto it = overlay.find({0, exits[i]});
    if (it == overlay.end() || it->second.size() != before[i]->size()) {
      return std::nullopt;
    }
    for (size_t j = 0; j < it->second.size(); ++j) {
      if (!it->second[j].is_known_eq((*before[i])[j])) {
        return false;
      }
    }
  }
  return true;
}

void Word_sim::invalidate(const std::vector<Pin>& pins) {
  absl::flat_hash_set<Key> gone;
  for (const auto& p : pins) {
    gone.insert({0, p});
  }
  for (const auto& k : gone) {
    if (auto it = memo_.find(k); it != memo_.end()) {
      stored_ -= it->second.size();
      memo_.erase(it);
    }
    failed_.erase(k);
    copies_.erase(k);
  }
  if (!gone.empty()) {
    std::erase_if(order_, [&](const Key& k) { return gone.contains(k); });
  }
}

std::vector<hhds::Graph*> Word_sim::descended() const {
  std::vector<hhds::Graph*> out;
  for (size_t i = 1; i < scopes_.size(); ++i) {
    auto* g = scopes_[i].inst.get_subnode_graph().get();
    if (g != nullptr && std::find(out.begin(), out.end(), g) == out.end()) {
      out.push_back(g);
    }
  }
  return out;
}

std::vector<uint64_t> Word_sim::bit_signature(const Pin& p, int bit) {
  const auto* v = values(p);
  if (v == nullptr) {
    return {};
  }
  std::vector<uint64_t> sig((v->size() + 63) / 64, 0);
  for (size_t j = 0; j < v->size(); ++j) {
    if ((*v)[j].bit_test(bit)) {
      sig[j / 64] |= uint64_t{1} << (j % 64);
    }
  }
  return sig;
}

}  // namespace livehd::satopt::detail
