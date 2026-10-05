// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "npn4.hpp"

#include <algorithm>

namespace livehd::usyn {
namespace {
struct Npn_step {
  uint8_t xor_gate, a, b;
};
struct Npn_record {
  uint16_t offset;
  uint8_t  count, root;
};
#include "npn4_data.inc"
}  // namespace

Npn_candidates npn4_candidates(Xag& graph, uint16_t truth, const std::array<Xsignal, 4>& inputs, Budget& work, uint32_t max_nodes) {
  Npn_candidates result;
  for (auto input : inputs) {
    if (input.id >= graph.size()) {
      return result;
    }
  }
  const auto  transform    = npn_lookup[truth];
  const auto  cls          = transform & 255;
  const auto& permutation  = npn_permutations[(transform >> 8) & 31];
  const auto  phase        = (transform >> 13) & 15;
  const bool  output_phase = (transform >> 17) & 1;
  result.status            = Status::search_exhausted;
  for (uint32_t variant = 0; variant < 2; ++variant) {
    const auto& record = npn_records[2 * cls + variant];
    if (graph.size() + record.count > max_nodes || !work.spend(5 + record.count)) {
      result.signals.clear();
      return result;
    }
    std::array<Xsignal, 15> signals{};  // constant, four inputs, at most ten gates
    for (uint32_t i = 0; i < 4; ++i) {
      const auto signal = inputs[permutation[i]];
      signals[i + 1]    = (phase & (1U << i)) ? ~signal : signal;
    }
    const auto decode = [&](uint8_t encoded) {
      const auto signal = signals[encoded >> 1];
      return (encoded & 1) ? ~signal : signal;
    };
    for (uint32_t i = 0; i < record.count; ++i) {
      const auto& step = npn_steps[record.offset + i];
      const auto  a = decode(step.a), b = decode(step.b);
      signals[i + 5] = step.xor_gate ? graph.lxor(a, b) : graph.land(a, b);
    }
    const auto root      = decode(record.root);
    const auto candidate = output_phase ? ~root : root;
    if (std::find(result.signals.begin(), result.signals.end(), candidate) == result.signals.end()) {
      result.signals.push_back(candidate);
    }
  }
  result.status = Status::feasible;
  return result;
}
}  // namespace livehd::usyn
