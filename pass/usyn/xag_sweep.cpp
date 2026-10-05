// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "xag_sweep.hpp"

#include <algorithm>
#include <array>
#include <unordered_map>

namespace livehd::usyn {
namespace {
struct Function {
  std::array<uint32_t, 16> support{};
  std::vector<uint64_t>    bits{};
  uint8_t                  count                             = 0;
  bool                     valid                             = false;
  bool                     operator==(const Function&) const = default;
};
struct Hash {
  size_t operator()(const Function& f) const {
    size_t h = 0;
    for (const auto word : f.bits) {
      h ^= word + 0x9e3779b9U + (h << 6) + (h >> 2);
    }
    for (uint32_t i = 0; i < f.count; ++i) {
      h ^= f.support[i] + 0x9e3779b9U + (h << 6) + (h >> 2);
    }
    return h ^ f.count;
  }
};
bool value(const Function& function, uint32_t x) { return (function.bits[x / 64] >> (x % 64)) & 1; }
void complement(Function& function) {
  for (auto& word : function.bits) {
    word = ~word;
  }
  if (function.count < 6) {
    function.bits.back() &= (uint64_t{1} << (1U << function.count)) - 1;
  }
}
// Remove functionally unused variables, rather than merging different ordered
// bases because they happen to have the same numeric table word.
void minimize(Function& f) {
  for (uint32_t i = 0; i < f.count;) {
    bool dependent = false;
    for (uint32_t x = 0; x < (1U << f.count); ++x) {
      if (!(x & (1U << i)) && value(f, x) != value(f, x | (1U << i))) {
        dependent = true;
        break;
      }
    }
    if (dependent) {
      ++i;
      continue;
    }
    std::vector<uint64_t> reduced(((1U << (f.count - 1)) + 63) / 64);
    for (uint32_t x = 0; x < (1U << (f.count - 1)); ++x) {
      const auto low   = x & ((1U << i) - 1);
      const auto old   = low | ((x ^ low) << 1);
      reduced[x / 64] |= uint64_t{value(f, old)} << (x % 64);
    }
    f.bits = std::move(reduced);
    std::move(f.support.begin() + i + 1, f.support.begin() + f.count, f.support.begin() + i);
    f.support[--f.count] = 0;
  }
}
Function combine(const Function& a, const Function& b, const Xag::Node& node, uint32_t limit, Budget& work) {
  Function f;
  if (!a.valid || !b.valid) {
    return f;
  }
  std::array<uint32_t, 32> support{};
  const auto               end   = std::set_union(a.support.begin(),
                                                  a.support.begin() + a.count,
                                                  b.support.begin(),
                                                  b.support.begin() + b.count,
                                                  support.begin());
  const auto               count = static_cast<uint32_t>(end - support.begin());
  if (count > limit || !work.spend((1U << count) * (count + 3) * (count + 1))) {
    return f;
  }
  f.count = static_cast<uint8_t>(count);
  f.valid = true;
  f.bits.resize(((1U << count) + 63) / 64);
  std::copy_n(support.begin(), count, f.support.begin());
  std::array<uint32_t, 16> a_order{}, b_order{};
  for (uint32_t i = 0; i < a.count; ++i) {
    a_order[i] = static_cast<uint32_t>(std::lower_bound(support.begin(), end, a.support[i]) - support.begin());
  }
  for (uint32_t i = 0; i < b.count; ++i) {
    b_order[i] = static_cast<uint32_t>(std::lower_bound(support.begin(), end, b.support[i]) - support.begin());
  }
  for (uint32_t x = 0; x < (1U << count); ++x) {
    uint32_t ax = 0, bx = 0;
    for (uint32_t i = 0; i < a.count; ++i) {
      ax |= ((x >> a_order[i]) & 1) << i;
    }
    for (uint32_t i = 0; i < b.count; ++i) {
      bx |= ((x >> b_order[i]) & 1) << i;
    }
    const bool av   = value(a, ax) != node.inputs[0].inverted;
    const bool bv   = value(b, bx) != node.inputs[1].inverted;
    const bool bit  = node.kind == Xag::Kind::and_gate ? av && bv : av != bv;
    f.bits[x / 64] |= uint64_t{bit} << (x % 64);
  }
  minimize(f);
  return f;
}
}  // namespace

Sweep_result sweep_xag(const Xag& source, std::span<const Xsignal> outputs, Budget& work, uint32_t max_nodes,
                       uint32_t support_limit, uint64_t table_words_limit) {
  Sweep_result result;
  if (!max_nodes || !support_limit || support_limit > 16 || table_words_limit < 2) {
    return result;
  }
  if (source.size() > max_nodes || outputs.size() > max_nodes || !work.spend(4 * source.size() + outputs.size())) {
    result.status = Status::search_exhausted;
    return result;
  }
  std::vector<bool> live(source.size());
  for (auto output : outputs) {
    if (output.id >= source.size()) {
      return result;
    }
    live[output.id] = true;
  }
  for (size_t end = source.size(); end > 0; --end) {
    const auto  id   = static_cast<Id>(end - 1);
    const auto& node = source.node(id);
    if (live[id] && (node.kind == Xag::Kind::and_gate || node.kind == Xag::Kind::xor_gate)) {
      live[node.inputs[0].id] = live[node.inputs[1].id] = true;
    }
  }
  Xag                   candidate;
  std::vector<Xsignal>  map(source.size());
  std::vector<Function> functions(source.size());
  functions[0].valid = true;
  functions[0].bits  = {0};
  result.table_words = 2;
  std::unordered_map<Function, Xsignal, Hash> representatives;
  representatives.emplace(functions[0], candidate.constant(false));
  const auto translated = [&](Xsignal s) { return s.inverted ? ~map[s.id] : map[s.id]; };
  for (Id id = 1; id < source.size(); ++id) {
    if (!work.spend() || candidate.size() >= max_nodes) {
      result.status = Status::search_exhausted;
      return result;
    }
    const auto& node = source.node(id);
    auto&       f    = functions[id];
    if (node.kind == Xag::Kind::source) {
      map[id]      = candidate.input(source.input_names()[node.source_index]);
      f.valid      = true;
      f.count      = 1;
      f.support[0] = node.source_index;
      f.bits       = {2};
    } else if (live[id]) {
      const auto a = translated(node.inputs[0]), b = translated(node.inputs[1]);
      map[id] = node.kind == Xag::Kind::and_gate ? candidate.land(a, b) : candidate.lxor(a, b);
      f       = combine(functions[node.inputs[0].id], functions[node.inputs[1].id], node, support_limit, work);
      if (work.exhausted) {
        result.status = Status::search_exhausted;
        return result;
      }
    } else {
      continue;
    }
    if (!f.valid) {
      if (live[id] && node.kind != Xag::Kind::source) {
        result.limited = true;
      }
      continue;
    }
    if (2 * f.bits.size() > table_words_limit - std::min(table_words_limit, result.table_words)) {
      f.valid = false;
      f.bits.clear();
      result.limited = true;
      continue;
    }
    result.table_words += 2 * f.bits.size();
    auto       key      = f;
    const bool phase    = key.bits[0] & 1;
    if (phase) {
      complement(key);
    }
    const auto positive = phase ? ~map[id] : map[id];
    auto [it, inserted] = representatives.emplace(key, positive);
    if (!inserted) {
      const auto match = phase ? ~it->second : it->second;
      if (candidate.node(match.id).level <= candidate.node(map[id].id).level) {
        if (match != map[id]) {
          ++result.confirmations;
        }
        map[id] = match;
      } else {
        it->second = positive;
      }
    }
  }
  for (auto output : outputs) {
    auto signal = translated(output);
    auto key    = functions[output.id];
    if (key.valid) {
      const bool phase = key.bits[0] & 1;
      if (phase) {
        complement(key);
      }
      const auto it = representatives.find(key);
      if (it != representatives.end()) {
        const auto match = (phase != output.inverted) ? ~it->second : it->second;
        if (candidate.node(match.id).level <= candidate.node(signal.id).level) {
          if (match != signal) {
            ++result.confirmations;
          }
          signal = match;
        }
      }
    }
    result.outputs.push_back(signal);
  }
  result.graph  = std::move(candidate);
  result.status = Status::feasible;
  return result;
}
}  // namespace livehd::usyn
