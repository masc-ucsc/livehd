// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "sop_factor.hpp"

#include <algorithm>
#include <map>

namespace livehd::usyn {
namespace {
using Product                 = std::vector<Xsignal>;
using Products                = std::vector<Product>;
constexpr uint32_t term_limit = 64, literal_limit = 16, expansion_limit = 256, control_limit = 8;

bool products(const Xag& graph, Xsignal root, Products& terms, Budget& work) {
  std::vector<Xsignal> pending{root};
  uint32_t             expanded = 0;
  while (!pending.empty()) {
    if (!work.spend() || ++expanded > expansion_limit || terms.size() >= term_limit) {
      return false;
    }
    const auto signal = pending.back();
    pending.pop_back();
    const auto& node = graph.node(signal.id);
    if (signal.inverted && node.kind == Xag::Kind::and_gate) {
      pending.push_back(~node.inputs[0]);
      pending.push_back(~node.inputs[1]);
      continue;
    }
    Product              product;
    std::vector<Xsignal> factors{signal};
    uint32_t             visits = 0;
    while (!factors.empty()) {
      if (!work.spend() || ++visits > expansion_limit) {
        return false;
      }
      const auto literal = factors.back();
      factors.pop_back();
      const auto& child = graph.node(literal.id);
      if (!literal.inverted && child.kind == Xag::Kind::and_gate) {
        factors.push_back(child.inputs[0]);
        factors.push_back(child.inputs[1]);
      } else {
        if (product.size() >= literal_limit) {
          return false;
        }
        product.push_back(literal);
      }
    }
    std::sort(product.begin(), product.end());
    product.erase(std::unique(product.begin(), product.end()), product.end());
    bool    contradictory = false;
    Product normalized;
    for (auto literal : product) {
      if (literal.id == 0) {
        if (!literal.inverted) {
          contradictory = true;
        }
        continue;
      }
      if (!normalized.empty() && normalized.back().id == literal.id) {
        contradictory = true;
      }
      normalized.push_back(literal);
    }
    if (!contradictory) {
      terms.push_back(std::move(normalized));
    }
  }
  return true;
}

class Builder {
public:
  Builder(Xag& target, Budget& budget, uint32_t limit) : graph(target), work(budget), max_nodes(limit) {}
  bool     refused   = false;
  uint64_t cofactors = 0;
  Xsignal  build(const Products& terms, uint32_t depth = 0) {
    if (!work.spend(1 + terms.size())) {
      refused = true;
      return {};
    }
    std::map<Id, std::array<uint32_t, 2>> occurrences;
    for (const auto& product : terms) {
      if (product.empty()) {
        return graph.constant(true);
      }
      if (!work.spend(product.size())) {
        refused = true;
        return {};
      }
      for (auto literal : product) {
        ++occurrences[literal.id][literal.inverted];
      }
    }
    Id       control = 0;
    uint32_t best = 1, both = 0;
    if (depth < control_limit) {
      for (const auto& [id, counts] : occurrences) {
        const auto repeated = counts[0] + counts[1], mixed = std::min(counts[0], counts[1]);
        if (repeated > best || (repeated == best && mixed > both)) {
          best    = repeated;
          both    = mixed;
          control = id;
        }
      }
    }
    if (!control) {
      std::vector<Xsignal> sums;
      for (const auto& product : terms) {
        sums.push_back(fold(product, true));
      }
      return fold(sums, false);
    }
    Products negative, positive;
    for (const auto& product : terms) {
      Product remainder;
      bool    found = false, phase = false;
      for (auto literal : product) {
        if (literal.id == control) {
          found = true;
          phase = literal.inverted;
        } else {
          remainder.push_back(literal);
        }
      }
      if (!found || phase) {
        negative.push_back(remainder);
      }
      if (!found || !phase) {
        positive.push_back(std::move(remainder));
      }
    }
    ++cofactors;
    const auto f = build(negative, depth + 1);
    if (refused) {
      return {};
    }
    const auto t = build(positive, depth + 1);
    if (refused) {
      return {};
    }
    // Native SOP mux form, retained as a mapping experiment. No ABC rewrite.
    const Xsignal select{control, false};
    return lor(land(select, t), land(~select, f));
  }

private:
  Xag&     graph;
  Budget&  work;
  uint32_t max_nodes;
  Xsignal  land(Xsignal a, Xsignal b) {
    if (!work.spend() || graph.size() >= max_nodes) {
      refused = true;
      return {};
    }
    return graph.land(a, b);
  }
  Xsignal lor(Xsignal a, Xsignal b) { return ~land(~a, ~b); }
  Xsignal fold(std::vector<Xsignal> signals, bool conjunction) {
    while (signals.size() > 1) {
      std::vector<Xsignal> next;
      for (size_t i = 0; i < signals.size(); i += 2) {
        if (i + 1 == signals.size()) {
          next.push_back(signals[i]);
        } else {
          next.push_back(conjunction ? land(signals[i], signals[i + 1]) : lor(signals[i], signals[i + 1]));
        }
        if (refused) {
          return {};
        }
      }
      signals = std::move(next);
    }
    return signals.empty() ? graph.constant(conjunction) : signals.front();
  }
};
}  // namespace

Sop_result factor_sop(const Xag& source, std::span<const Xsignal> outputs, Budget& work, uint32_t max_nodes) {
  Sop_result result;
  if (!max_nodes) {
    return result;
  }
  if (source.size() > max_nodes || outputs.size() > max_nodes || !work.spend(source.size() + outputs.size())) {
    result.status = Status::search_exhausted;
    return result;
  }
  for (auto output : outputs) {
    if (output.id >= source.size()) {
      return result;
    }
  }
  Xag                   candidate = source;
  Builder               builder(candidate, work, max_nodes);
  std::map<Id, Xsignal> replacements;
  for (auto output : outputs) {
    if (!output.inverted || source.node(output.id).kind != Xag::Kind::and_gate) {
      result.outputs.push_back(output);
      continue;
    }
    Products terms;
    if (!products(source, output, terms, work)) {
      result.limited = true;
      if (work.exhausted) {
        result.outputs.clear();
        result.status = Status::search_exhausted;
        return result;
      }
      result.outputs.push_back(output);
      continue;
    }
    const auto root = builder.build(terms);
    if (builder.refused) {
      result.outputs.clear();
      result.status = Status::search_exhausted;
      return result;
    }
    result.outputs.push_back(root);
    if (root != output) {
      replacements[output.id] = output.inverted ? ~root : root;
      ++result.roots;
    }
  }
  // Both output rails use the same selected equivalent function. This rebinds
  // protected outputs, without mutating the incumbent's internal fanout/hash.
  for (size_t i = 0; i < outputs.size(); ++i) {
    const auto it = replacements.find(outputs[i].id);
    if (it != replacements.end()) {
      result.outputs[i] = outputs[i].inverted ? ~it->second : it->second;
    }
  }
  result.cofactors = builder.cofactors;
  result.graph     = std::move(candidate);
  result.status    = Status::feasible;
  return result;
}
}  // namespace livehd::usyn
