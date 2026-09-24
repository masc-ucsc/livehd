// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "lnet.hpp"

#include <algorithm>

#include "absl/container/flat_hash_map.h"
#include "iassert.hpp"

namespace livehd::synth {

Lnet::Lnet() { add_node(Kind::constant, {}, 0, 0); }  // node 0: the constant 0

Lid Lnet::add_node(Kind kind, std::span<const Lid> fanins, uint64_t fn, uint32_t aux) {
  I(fanins.size() <= kMaxFanin);
  const auto               id = static_cast<Lid>(kind_.size());
  std::array<Lid, kMaxFanin> slots;
  slots.fill(kConst0);  // unused slots hold the constant-0 id
  uint32_t level = 0;
  for (size_t i = 0; i < fanins.size(); ++i) {
    I(fanins[i] < id);
    slots[i] = fanins[i];
    ++fanout_[fanins[i]];
    level = std::max(level, level_[fanins[i]] + 1);
  }
  kind_.push_back(kind);
  count_.push_back(static_cast<uint8_t>(fanins.size()));
  fanin_.push_back(slots);
  fn_.push_back(fn);
  fanout_.push_back(0);
  level_.push_back(level);
  aux_.push_back(aux);
  return id;
}

Lid Lnet::add_constant(bool value) { return add_node(Kind::constant, {}, value ? ~uint64_t{0} : 0, 0); }

namespace {
// `fn`'s low 2^k bits, replicated over 64.
uint64_t replicate(uint64_t fn, size_t k) {
  for (size_t bits = size_t{1} << k; bits < 64; bits *= 2) {
    fn = (fn & ((uint64_t{1} << bits) - 1)) | (fn << bits);
  }
  return fn;
}
}  // namespace

Lid Lnet::add_lut(std::span<const Lid> fanins, uint64_t fn) {
  I(fanins.size() <= 6);  // wider tables go through the word overload
  return add_node(Kind::lut, fanins, replicate(fn, fanins.size()), 0);
}

Lid Lnet::add_lut(std::span<const Lid> fanins, std::span<const uint64_t> table) {
  I(!table.empty());
  if (fanins.size() <= 6) {
    return add_lut(fanins, table[0]);
  }
  I(fanins.size() <= kMaxFanin && table.size() >= (fanins.size() == 7 ? 2u : 4u));
  std::array<uint64_t, 4> words{};
  for (size_t w = 0; w < 4; ++w) {
    words[w] = table[fanins.size() == 7 ? w % 2 : w];
  }
  wide_.push_back(words);
  return add_node(Kind::lut, fanins, wide_.size() - 1, 0);
}

void Lnet::set_sop(Lid n, Sop sop) {
  I(n < size() && kind_[n] == Kind::lut);
  if (sop_of_.size() < size()) {
    sop_of_.resize(size(), kNone);
  }
  if (sop_of_[n] == kNone) {
    sop_of_[n] = static_cast<uint32_t>(sops_.size());
    sops_.push_back(std::move(sop));
  } else {
    sops_[sop_of_[n]] = std::move(sop);
  }
}

Lid Lnet::add_input(std::string name) {
  const auto node = add_node(Kind::source, {}, 0, static_cast<uint32_t>(inputs_.size()));
  inputs_.push_back({node, std::move(name)});
  return node;
}

uint32_t Lnet::add_latch(std::string name, char init) {
  const auto k = static_cast<uint32_t>(latches_.size());
  latches_.push_back({std::move(name), init, kNone, kNone});
  latches_.back().q = add_node(Kind::source, {}, 0, k | kLatchBit);
  return k;
}

void Lnet::set_latch_input(uint32_t latch, Lid d) {
  I(latch < latches_.size() && d < size());
  if (latches_[latch].d != kNone) {
    --fanout_[latches_[latch].d];
  }
  latches_[latch].d = d;
  ++fanout_[d];
}

void Lnet::add_output(Lid bit, std::string name) {
  I(bit < size());
  ++fanout_[bit];
  outputs_.push_back({bit, std::move(name), static_cast<Lid>(size())});
}

std::vector<Lid> combinational_outputs(const Lnet& net) {
  std::vector<Lid> cos;
  cos.reserve(net.outputs().size() + net.latches().size());
  for (const auto& o : net.outputs()) {
    cos.push_back(o.node);
  }
  for (const auto& l : net.latches()) {
    cos.push_back(l.d);
  }
  return cos;
}

std::optional<Lnet> strash(const Lnet& raw, const Strash_options& options) {
  // A literal is (node << 1) | complemented, over a work network whose node 0
  // is the constant 0. Structural hashing, constant folding and complement
  // absorption: a complemented fanin folds into its consumer's table, and only
  // a complemented output gets an explicit inverter.
  using Lit = uint64_t;
  struct Gate {
    Lid      a, b;  // work nodes
    uint64_t fn;
  };
  constexpr Lit zero = 0, one = 1;
  const auto    sources = raw.inputs().size() + raw.latches().size();
  std::vector<Gate> gates;  // work node sources + 1 + i
  absl::flat_hash_map<Lit, Lid> and_nodes, xor_nodes;
  const auto key  = [](Lit a, Lit b) { return (a << 32) | b; };
  const auto gate = [&](Lit a, Lit b, bool is_xor) -> Lit {
    auto& nodes = is_xor ? xor_nodes : and_nodes;
    if (auto it = nodes.find(key(a, b)); it != nodes.end()) {
      return Lit{it->second} << 1;
    }
    uint64_t fn = 0;
    for (uint32_t x = 0; x < 4; ++x) {
      const bool va = ((x & 1) != 0) != ((a & 1) != 0);
      const bool vb = ((x & 2) != 0) != ((b & 1) != 0);
      if (is_xor ? va != vb : va && vb) {
        fn |= uint64_t{1} << x;
      }
    }
    const auto id = static_cast<Lid>(sources + 1 + gates.size());
    gates.push_back({static_cast<Lid>(a >> 1), static_cast<Lid>(b >> 1), fn});
    nodes.emplace(key(a, b), id);
    return Lit{id} << 1;
  };
  const auto and_ = [&](Lit a, Lit b) -> Lit {
    if (a == zero || b == zero || a == (b ^ 1)) {
      return zero;
    }
    if (a == one || a == b) {
      return b;
    }
    if (b == one) {
      return a;
    }
    return gate(std::min(a, b), std::max(a, b), false);
  };
  const auto xor_ = [&](Lit a, Lit b) -> Lit {
    const Lit phase = (a ^ b) & 1;
    a &= ~Lit{1};
    b &= ~Lit{1};
    if (a == zero) {
      return b ^ phase;
    }
    if (b == zero) {
      return a ^ phase;
    }
    if (a == b) {
      return zero ^ phase;
    }
    if (a > b) {
      std::swap(a, b);
    }
    if (options.xor_nodes) {
      return gate(a, b, true) ^ phase;
    }
    return (and_(and_(a, b ^ 1) ^ 1, and_(a ^ 1, b) ^ 1) ^ 1) ^ phase;
  };
  // Work nodes: 0 the constant, 1..sources the sources in CI order.
  std::vector<Lit> lit(raw.size(), zero);
  for (Lid i = 1; i < raw.size(); ++i) {
    if (i % 1024 == 0 && options.admit && !options.admit()) {
      return std::nullopt;
    }
    switch (raw.kind(i)) {
      case Lnet::Kind::constant: lit[i] = raw.fn(i) != 0 ? one : zero; break;
      case Lnet::Kind::source: {
        const auto index = raw.is_latch_source(i) ? raw.inputs().size() + raw.source_index(i) : raw.source_index(i);
        lit[i]           = Lit{1 + index} << 1;
        break;
      }
      case Lnet::Kind::lut: {
        const auto fn = raw.fn(i);
        const auto k  = raw.fanin_count(i);
        if (k == 1 && fn == Lnet::kNot) {
          lit[i] = lit[raw.fanin(i, 0)] ^ 1;
        } else if (k == 2 && fn == Lnet::kAnd2) {
          lit[i] = and_(lit[raw.fanin(i, 0)], lit[raw.fanin(i, 1)]);
        } else if (k == 2 && fn == Lnet::kOr2) {
          lit[i] = and_(lit[raw.fanin(i, 0)] ^ 1, lit[raw.fanin(i, 1)] ^ 1) ^ 1;
        } else if (k == 2 && fn == Lnet::kXor2) {
          lit[i] = xor_(lit[raw.fanin(i, 0)], lit[raw.fanin(i, 1)]);
        } else {
          return std::nullopt;
        }
        break;
      }
    }
  }
  std::vector<Lit> outs;
  for (const auto co : combinational_outputs(raw)) {
    if (co == Lnet::kNone) {
      return std::nullopt;
    }
    outs.push_back(lit[co]);
  }
  // Drop gates no output reaches.
  const auto        first_gate = sources + 1;
  std::vector<bool> live(first_gate + gates.size(), false);
  for (const auto out : outs) {
    live[out >> 1] = true;
  }
  for (size_t i = live.size(); i-- > first_gate;) {
    if (live[i]) {
      live[gates[i - first_gate].a] = true;
      live[gates[i - first_gate].b] = true;
    }
  }
  Lnet             net;
  std::vector<Lid> ids(live.size(), Lnet::kNone);
  ids[0] = Lnet::kConst0;
  for (const auto& in : raw.inputs()) {
    const auto work = net.size();  // the sources keep their work numbering
    ids[work]       = net.add_input(in.name);
  }
  for (const auto& l : raw.latches()) {
    const auto work = net.size();
    ids[work]       = net.latch(net.add_latch(l.name, l.init)).q;
  }
  for (size_t i = first_gate; i < live.size(); ++i) {
    if (live[i]) {
      const auto& g = gates[i - first_gate];
      ids[i]        = net.add_lut({ids[g.a], ids[g.b]}, g.fn);
    }
  }
  absl::flat_hash_map<Lid, Lid> inverters;
  const auto                    driver = [&](Lit out) {
    auto id = ids[out >> 1];
    if (out & 1) {
      auto [it, fresh] = inverters.try_emplace(id, Lnet::kNone);
      if (fresh) {
        it->second = net.add_lut({id}, Lnet::kNot);
      }
      id = it->second;
    }
    return id;
  };
  for (size_t j = 0; j < raw.outputs().size(); ++j) {
    net.add_output(driver(outs[j]), raw.outputs()[j].name);
  }
  for (size_t k = 0; k < raw.latches().size(); ++k) {
    net.set_latch_input(static_cast<uint32_t>(k), driver(outs[raw.outputs().size() + k]));
  }
  return net;
}

}  // namespace livehd::synth
