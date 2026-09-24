// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "metrics.hpp"

#include <algorithm>
#include <format>
#include <set>

namespace livehd::usyn {
std::string source_metrics_json(const livehd::synth::Lnet& source) {
  using livehd::synth::Lid;
  using livehd::synth::Lnet;
  const auto        outputs = livehd::synth::combinational_outputs(source);
  const auto        is_source = [&](Lid id) { return source.kind(id) == Lnet::Kind::source; };
  std::vector<bool> has_source(source.size()), reachable(source.size());
  for (Lid id = 0; id < source.size(); ++id) {
    const auto fanins = source.fanins(id);
    has_source[id]    = is_source(id) || std::any_of(fanins.begin(), fanins.end(), [&](Lid in) { return has_source[in]; });
  }
  uint64_t      direct = 0, constant = 0;
  std::set<Lid> unique, logic;
  for (auto root : outputs) {
    reachable[root] = true;
    unique.insert(root);
    if (is_source(root)) {
      ++direct;
    } else if (!has_source[root]) {
      ++constant;
    } else {
      logic.insert(root);
    }
  }
  uint64_t sources = 0, functions = 0;
  for (size_t id = source.size(); id-- > 0;) {
    if (!reachable[id]) {
      continue;
    }
    if (is_source(static_cast<Lid>(id))) {
      ++sources;
    } else {
      ++functions;
    }
    for (auto in : source.fanins(static_cast<Lid>(id))) {
      reachable[in] = true;
    }
  }
  return std::format(
      R"({{"outputs":{},"unique_outputs":{},"direct_source_outputs":{},"source_free_outputs":{},"logic_outputs":{},"unique_logic_outputs":{},"reachable_source_nodes":{},"reachable_function_nodes":{}}})",
      outputs.size(),
      unique.size(),
      direct,
      constant,
      outputs.size() - direct - constant,
      logic.size(),
      sources,
      functions);
}
}  // namespace livehd::usyn
