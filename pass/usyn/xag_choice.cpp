// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "xag_choice.hpp"

#include <algorithm>

namespace livehd::usyn {
namespace {
std::optional<bool> depends(const Xag& graph, Xsignal signal, Id root, Budget& work) {
  if (signal.id >= graph.size()) {
    return {};
  }
  // The graph is append-only: every fanin id is below its node's id, so only
  // nodes created after `root` can reach it. Basis leaves and old nodes answer
  // immediately; a scratch candidate walks only the nodes above `root`.
  if (signal.id < root) {
    return false;
  }
  if (!work.spend(signal.id - root + 1)) {
    return {};
  }
  std::vector<bool> seen(signal.id - root + 1);
  std::vector<Id>   pending{signal.id};
  while (!pending.empty()) {
    if (!work.spend()) {
      return {};
    }
    const auto id = pending.back();
    pending.pop_back();
    if (id == root) {
      return true;
    }
    if (id < root || seen[id - root]) {
      continue;
    }
    seen[id - root]  = true;
    const auto& node = graph.node(id);
    if (node.kind == Xag::Kind::and_gate || node.kind == Xag::Kind::xor_gate) {
      pending.push_back(node.inputs[0].id);
      pending.push_back(node.inputs[1].id);
    }
  }
  return false;
}
}  // namespace

std::optional<Xag_choice> make_choice(const Xag& graph, Xsignal root, std::span<const Id> basis, Budget& work) {
  if (!root.id || root.id >= graph.size() || basis.empty() || basis.size() > 16) {
    return {};
  }
  if (graph.node(root.id).kind != Xag::Kind::and_gate && graph.node(root.id).kind != Xag::Kind::xor_gate) {
    return {};
  }
  for (auto leaf : basis) {
    const auto dependency = depends(graph, {leaf, false}, root.id, work);
    if (!dependency || *dependency) {
      return {};
    }
  }
  auto function = basis_function(graph, root, basis, {16, 256}, work);
  if (function.status != Status::feasible) {
    return {};
  }
  Xag_choice result;
  result.root = root;
  result.basis.assign(basis.begin(), basis.end());
  result.function = std::move(function.table);
  result.alternatives.push_back(root);
  return result;
}

Status retain_choice(const Xag& graph, Xag_choice& choice, Xsignal alternative, Budget& work, uint32_t cap) {
  if (cap < 1 || cap > 3 || alternative.id >= graph.size()) {
    return Status::invalid;
  }
  if (std::find(choice.alternatives.begin(), choice.alternatives.end(), alternative) != choice.alternatives.end()) {
    return Status::feasible;
  }
  if (choice.alternatives.size() >= cap) {
    return Status::unsupported;
  }
  const auto dependency = depends(graph, alternative, choice.root.id, work);
  if (!dependency) {
    return Status::search_exhausted;
  }
  if (*dependency) {
    return Status::invalid;
  }
  auto candidate = basis_function(graph, alternative, choice.basis, {16, 1024}, work);
  if (candidate.status != Status::feasible) {
    return candidate.status;
  }
  if (candidate.table != choice.function) {
    return Status::invalid;
  }
  choice.alternatives.push_back(alternative);
  return Status::feasible;
}

Choice_extraction extract_choices(const Xag& source, std::span<const Xag_choice> choices, std::span<const uint32_t> selected,
                                  std::span<const Xsignal> outputs, Budget& work, uint32_t max_nodes) {
  Choice_extraction result;
  if (choices.size() != selected.size() || source.size() > max_nodes || !max_nodes) {
    return result;
  }
  if (!work.spend(3 * source.size() + choices.size() + outputs.size())) {
    result.status = Status::search_exhausted;
    return result;
  }
  std::vector<Xsignal> replacement(source.size()), mapped(source.size());
  std::vector<bool>    present(source.size());
  std::vector<uint8_t> visit(source.size());
  Xag                  target;
  visit[0] = 2;
  for (Id id = 1; id < source.size(); ++id) {
    const auto& node = source.node(id);
    if (node.kind == Xag::Kind::source) {
      mapped[id] = target.input(source.input_names()[node.source_index]);
      visit[id]  = 2;
    }
  }
  for (size_t i = 0; i < choices.size(); ++i) {
    const auto root = choices[i].incumbent();
    if (root.id >= source.size() || present[root.id] || selected[i] >= choices[i].members().size()) {
      return result;
    }
    const auto member = choices[i].members()[selected[i]];
    if (member.id >= source.size()) {
      return result;
    }
    present[root.id]     = true;
    replacement[root.id] = root.inverted ? ~member : member;
  }
  struct Frame {
    Id   id;
    bool exit;
  };
  std::vector<Frame>   pending;
  std::vector<Xsignal> extracted;
  const auto           translated = [&](Xsignal signal) { return signal.inverted ? ~mapped[signal.id] : mapped[signal.id]; };
  for (auto output : outputs) {
    if (output.id >= source.size()) {
      return result;
    }
    pending.push_back({output.id, false});
    while (!pending.empty()) {
      if (!work.spend() || target.size() > max_nodes) {
        result.status = Status::search_exhausted;
        return result;
      }
      const auto frame = pending.back();
      pending.pop_back();
      const auto id = frame.id;
      if (visit[id] == 2) {
        continue;
      }
      const auto& node     = source.node(id);
      const bool  replaced = present[id] && replacement[id] != Xsignal{id, false};
      if (frame.exit) {
        if (replaced) {
          mapped[id] = translated(replacement[id]);
        } else {
          const auto a = translated(node.inputs[0]), b = translated(node.inputs[1]);
          mapped[id] = node.kind == Xag::Kind::and_gate ? target.land(a, b) : target.lxor(a, b);
        }
        visit[id] = 2;
        continue;
      }
      if (visit[id] == 1) {
        return result;
      }
      visit[id] = 1;
      pending.push_back({id, true});
      if (replaced) {
        pending.push_back({replacement[id].id, false});
      } else if (node.kind == Xag::Kind::and_gate || node.kind == Xag::Kind::xor_gate) {
        pending.push_back({node.inputs[1].id, false});
        pending.push_back({node.inputs[0].id, false});
      } else {
        return result;
      }
    }
    extracted.push_back(translated(output));
  }
  if (target.size() > max_nodes) {
    result.outputs.clear();
    result.status = Status::search_exhausted;
    return result;
  }
  result.graph   = std::move(target);
  result.outputs = std::move(extracted);
  result.status  = Status::feasible;
  return result;
}
}  // namespace livehd::usyn
