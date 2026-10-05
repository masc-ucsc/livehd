// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "xag_representations.hpp"

#include <algorithm>
#include <bit>
#include <map>

#include "function.hpp"

namespace livehd::usyn {
namespace {
class Builder {
public:
  Builder(Xag& target, std::span<const Xsignal> basis, Budget& budget, uint32_t node_limit)
      : graph(target), inputs(basis), work(budget), max_nodes(node_limit) {}
  bool     refused = false, limited = false;
  uint64_t bdd_nodes = 0;
  Xsignal  sop(const Form& form) {
    std::vector<Xsignal> sum;
    for (const auto& cube : form.cubes) {
      std::vector<Xsignal> product;
      for (uint32_t i = 0; i < inputs.size(); ++i) {
        if ((cube.care >> i) & 1) {
          product.push_back((cube.ones >> i) & 1 ? inputs[i] : ~inputs[i]);
        }
      }
      sum.push_back(fold(std::move(product), true));
      if (refused) {
        return {};
      }
    }
    return fold(std::move(sum), false);
  }
  Xsignal bdd(const Truth_table& function, uint32_t depth = 0) {
    if (!work.spend(1 + (1U << function.inputs))) {
      refused = true;
      return {};
    }
    bool zero = true, one = true;
    for (uint32_t pattern = 0; pattern < (1U << function.inputs); ++pattern) {
      zero &= !function.get(pattern);
      one  &= function.get(pattern);
    }
    if (zero || one) {
      return graph.constant(one);
    }
    if (function.inputs == 2 && (function.words[0] == 6 || function.words[0] == 9)) {
      if (!work.spend() || graph.size() >= max_nodes) {
        refused = true;
        return {};
      }
      const auto parity = graph.lxor(inputs[depth], inputs[depth + 1]);
      return function.words[0] == 9 ? ~parity : parity;
    }
    const auto key = std::make_pair(depth, function.words);
    if (const auto found = unique.find(key); found != unique.end()) {
      return found->second;
    }
    if (++bdd_nodes > 128) {
      limited = refused = true;
      return {};
    }
    Truth_table low(function.inputs - 1), high(function.inputs - 1);
    for (uint32_t pattern = 0; pattern < (1U << low.inputs); ++pattern) {
      low.set(pattern, function.get(pattern << 1));
      high.set(pattern, function.get((pattern << 1) | 1));
    }
    const auto f = bdd(low, depth + 1);
    if (refused) {
      return {};
    }
    const auto t = bdd(high, depth + 1);
    if (refused) {
      return {};
    }
    if (f == t) {
      unique.emplace(key, f);
      return f;
    }
    // One fixed variable order on every path; XAG's mux is a checked ITE
    // identity. Bounds include conversion gates, not only the BDD unique table.
    if (!work.spend(3) || graph.size() + 3 > max_nodes) {
      refused = true;
      return {};
    }
    const auto root = graph.mux(inputs[depth], t, f);
    unique.emplace(key, root);
    return root;
  }

private:
  Xag&                                                          graph;
  std::span<const Xsignal>                                      inputs;
  Budget&                                                       work;
  uint32_t                                                      max_nodes;
  std::map<std::pair<uint32_t, std::vector<uint64_t>>, Xsignal> unique;
  Xsignal                                                       combine(Xsignal a, Xsignal b, bool conjunction) {
    if (!work.spend() || graph.size() >= max_nodes) {
      refused = true;
      return {};
    }
    return conjunction ? graph.land(a, b) : graph.lor(a, b);
  }
  Xsignal fold(std::vector<Xsignal> signals, bool conjunction) {
    while (signals.size() > 1) {
      std::vector<Xsignal> next;
      for (size_t i = 0; i < signals.size(); i += 2) {
        next.push_back(i + 1 == signals.size() ? signals[i] : combine(signals[i], signals[i + 1], conjunction));
        if (refused) {
          return {};
        }
      }
      signals = std::move(next);
    }
    return signals.empty() ? graph.constant(conjunction) : signals.front();
  }
};

// If the A cofactors have at most two distinct total B functions, construct
// f(A,B) = H(g(A),B). The two supports are disjoint, and both recursive
// functions have fewer inputs. No reachable-state care assumption is used.
class Decomposer {
public:
  Decomposer(Xag& target, Budget& budget, uint32_t node_limit) : graph(target), work(budget), max_nodes(node_limit) {}
  bool     refused = false;
  uint64_t blocks = 0, bdd_nodes = 0;
  Xsignal  build(const Truth_table& function, std::span<const Xsignal> inputs) {
    const auto n = function.inputs;
    for (uint32_t mask = 1; n > 2 && mask < (1U << n) - 1; ++mask) {
      const auto width = static_cast<uint32_t>(std::popcount(mask));
      if (width < 2 || width == n) {
        continue;
      }
      if (++partitions > 64 || !work.spend((1U << n) * (n + 1))) {
        refused = true;
        return {};
      }
      std::vector<uint32_t> a, b;
      std::vector<Xsignal>  a_inputs, b_inputs;
      for (uint32_t i = 0; i < n; ++i) {
        (mask & (1U << i) ? a : b).push_back(i);
        (mask & (1U << i) ? a_inputs : b_inputs).push_back(inputs[i]);
      }
      std::array<uint64_t, 2> patterns{};
      uint32_t                distinct = 0;
      Truth_table             inner(width);
      bool                    prime = false;
      for (uint32_t ax = 0; ax < (1U << width); ++ax) {
        uint32_t fixed = 0;
        for (uint32_t i = 0; i < width; ++i) {
          fixed |= ((ax >> i) & 1) << a[i];
        }
        uint64_t cofactor = 0;
        for (uint32_t bx = 0; bx < (1U << b.size()); ++bx) {
          uint32_t x = fixed;
          for (uint32_t i = 0; i < b.size(); ++i) {
            x |= ((bx >> i) & 1) << b[i];
          }
          cofactor |= uint64_t{function.get(x)} << bx;
        }
        uint32_t index = 0;
        while (index < distinct && patterns[index] != cofactor) {
          ++index;
        }
        if (index == distinct) {
          if (distinct == 2) {
            prime = true;
            break;
          }
          patterns[distinct++] = cofactor;
        }
        inner.set(ax, index != 0);
      }
      if (prime) {
        continue;
      }
      ++blocks;
      if (distinct == 1) {
        Truth_table reduced(static_cast<uint32_t>(b.size()));
        reduced.words[0] = patterns[0];
        return build(reduced, b_inputs);
      }
      auto g = build(inner, a_inputs);
      if (refused) {
        return {};
      }
      Truth_table outer(static_cast<uint32_t>(b.size()) + 1);
      for (uint32_t x = 0; x < (1U << outer.inputs); ++x) {
        outer.set(x, (patterns[x & 1] >> (x >> 1)) & 1);
      }
      b_inputs.insert(b_inputs.begin(), g);
      return build(outer, b_inputs);
    }
    Builder builder(graph, inputs, work, max_nodes);
    auto    root  = builder.bdd(function);
    bdd_nodes    += builder.bdd_nodes;
    refused      |= builder.refused || bdd_nodes > 128;
    return root;
  }

private:
  Xag&     graph;
  Budget&  work;
  uint32_t max_nodes;
  uint32_t partitions = 0;
};
}  // namespace

Representation_result represent_function(Xag& graph, const Truth_table& function, std::span<const Xsignal> inputs, Budget& work,
                                         uint32_t max_nodes) {
  Representation_result result;
  if (function.inputs != inputs.size() || inputs.empty() || inputs.size() > 8 || graph.size() > max_nodes
      || !valid_truth_table(function)) {
    return result;
  }
  std::vector<Id> basis;
  for (auto input : inputs) {
    if (input.inverted || !input.id || input.id >= graph.size() || std::find(basis.begin(), basis.end(), input.id) != basis.end()) {
      return result;
    }
    basis.push_back(input.id);
  }
  const auto validate = [&](Xsignal signal, std::string name) {
    auto checked = basis_function(graph, signal, basis, {8, 1024}, work);
    if (checked.status != Status::feasible) {
      result.limited = true;
      return;
    }
    if (checked.table != function) {
      // Hash-consing against the original graph can alias a constructed
      // subfunction with another computed cut leaf. Such an identity holds on
      // the original graph's image, but not on this independent cut basis.
      // Refuse just this candidate and keep trying the other representations.
      ++result.rejected;
      return;
    }
    if (std::none_of(result.candidates.begin(), result.candidates.end(), [&](const auto& candidate) {
          return candidate.signal == signal;
        })) {
      result.candidates.push_back({signal, std::move(name)});
    }
  };
  result.status       = Status::feasible;
  auto       dsd_work = work.slice(8000, 8, 8192);
  Decomposer decomposition(graph, dsd_work, max_nodes);
  const auto decomposed = decomposition.build(function, inputs);
  work.absorb(dsd_work);
  result.dsd_blocks = decomposition.blocks;
  if (!decomposition.refused) {
    validate(decomposed, "dsd");
  } else {
    result.limited = true;
  }
  if (result.status == Status::invalid) {
    return result;
  }
  for (bool inverted : {false, true}) {
    const auto table     = inverted ? function.complement() : function;
    auto       form_work = work.slice(12000, 1, 8192);
    auto       form      = exact_form(table, 64, 8, false, form_work);
    work.absorb(form_work);
    if (form.status != Status::feasible) {
      result.limited = true;
      continue;
    }
    Builder    builder(graph, inputs, work, max_nodes);
    const auto signal = builder.sop(form);
    if (builder.refused) {
      result.limited = true;
      break;
    }
    result.sop_cubes += form.cubes.size();
    validate(inverted ? ~signal : signal, inverted ? "sop-negative" : "sop-positive");
    if (result.status == Status::invalid) {
      return result;
    }
  }
  auto       bdd_work = work.slice(8000, 1, 8192);
  Builder    builder(graph, inputs, bdd_work, max_nodes);
  const auto signal = builder.bdd(function);
  work.absorb(bdd_work);
  result.bdd_nodes = builder.bdd_nodes;
  if (!builder.refused) {
    validate(signal, "bdd");
  } else {
    result.limited = true;
  }
  if (work.resource_exhausted || work.exhausted) {
    result.candidates.clear();
    result.status = Status::search_exhausted;
  }
  if (result.candidates.empty() && result.limited && result.status == Status::feasible) {
    result.status = Status::search_exhausted;
  }
  if (result.candidates.empty() && result.rejected && result.status == Status::feasible) {
    result.status = Status::unsupported;
  }
  return result;
}
}  // namespace livehd::usyn
