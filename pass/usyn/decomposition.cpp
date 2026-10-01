// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "decomposition.hpp"

#include <bit>
#include <map>

namespace livehd::usyn {
namespace {
Joint_decomposition partition(std::span<const Truth_table> functions, uint32_t bound_mask, uint32_t max_top_inputs, Budget& work) {
  Joint_decomposition out;
  const auto&         function = functions.front();
  if (!valid_truth_table(function) || function.inputs < 2 || bound_mask == 0 || bound_mask >= (1U << function.inputs) - 1
      || max_top_inputs == 0 || max_top_inputs > max_logical_inputs) {
    return out;
  }
  for (const auto& other : functions) {
    if (!valid_truth_table(other) || other.inputs != function.inputs) {
      return out;
    }
  }
  out.status = Status::search_exhausted;
  for (uint32_t i = 0; i < function.inputs; ++i) {
    (bound_mask & (1U << i) ? out.bound : out.free).push_back(i);
  }
  if (out.free.size() >= max_top_inputs) {
    out.status = Status::unsupported;
    return out;
  }
  const auto nb = 1U << out.bound.size(), nr = 1U << out.free.size();
  if (!work.spend(uint64_t{nb} * (out.bound.size() + 1) + uint64_t{nr} * (out.free.size() + 1))) {
    return out;
  }
  const auto expand = [](uint32_t value, const std::vector<uint32_t>& positions) {
    uint32_t result = 0;
    for (uint32_t i = 0; i < positions.size(); ++i) {
      result |= ((value >> i) & 1) << positions[i];
    }
    return result;
  };
  std::vector<uint32_t> bound(nb), free(nr), codes(nb);
  for (uint32_t b = 0; b < nb; ++b) {
    bound[b] = expand(b, out.bound);
  }
  for (uint32_t r = 0; r < nr; ++r) {
    free[r] = expand(r, out.free);
  }
  std::map<std::vector<uint64_t>, uint32_t> classes;
  std::vector<uint32_t>                     representatives;
  const auto                                max_classes = 1U << (max_top_inputs - out.free.size());
  for (uint32_t b = 0; b < nb; ++b) {
    const auto words = (nr + 63) / 64;
    if (!work.spend(functions.size() * (nr + uint64_t{words} * (2 * std::bit_width(classes.size() + 1) + 1)))) {
      return out;
    }
    std::vector<uint64_t> signature(words * functions.size());
    for (size_t i = 0; i < functions.size(); ++i) {
      for (uint32_t r = 0; r < nr; ++r) {
        signature[i * words + r / 64] |= uint64_t{functions[i].get(bound[b] | free[r])} << (r % 64);
      }
    }
    auto [it, inserted] = classes.try_emplace(std::move(signature), classes.size());
    if (inserted) {
      if (classes.size() > max_classes) {
        out.status = Status::unsupported;
        return out;
      }
      representatives.push_back(b);
    }
    codes[b] = it->second;
  }
  const auto bits = static_cast<uint32_t>(std::bit_width(classes.size() - 1));
  if (bits == 0) {
    // No bound-variable dependence: ordinary support reduction handles this.
    out.status = Status::unsupported;
    return out;
  }
  if (!work.spend(uint64_t{nb} * bits + (functions.size() + 1) * (uint64_t{1} << (bits + out.free.size())))) {
    return out;
  }
  out.divisors.assign(bits, Truth_table(static_cast<uint32_t>(out.bound.size())));
  for (uint32_t b = 0; b < nb; ++b) {
    for (uint32_t bit = 0; bit < bits; ++bit) {
      out.divisors[bit].set(b, (codes[b] >> bit) & 1);
    }
  }
  for (size_t i = 0; i < functions.size(); ++i) {
    out.tops[i] = Truth_table(bits + static_cast<uint32_t>(out.free.size()));
  }
  out.top_care = Truth_table(out.tops[0].inputs);
  for (uint32_t code = 0; code < representatives.size(); ++code) {
    for (uint32_t r = 0; r < nr; ++r) {
      for (size_t i = 0; i < functions.size(); ++i) {
        out.tops[i].set(code | (r << bits), functions[i].get(bound[representatives[code]] | free[r]));
      }
      out.top_care.set(code | (r << bits), true);
    }
  }
  out.status = Status::feasible;
  return out;
}
}  // namespace

Decomposition decompose_function(const Truth_table& function, uint32_t bound_mask, uint32_t max_top_inputs, Budget& work) {
  auto joint = partition(std::span{&function, 1}, bound_mask, max_top_inputs, work);
  return {joint.status,
          std::move(joint.bound),
          std::move(joint.free),
          std::move(joint.divisors),
          std::move(joint.tops[0]),
          std::move(joint.top_care)};
}

Joint_decomposition decompose_pair(const std::array<Truth_table, 2>& functions, uint32_t bound_mask, uint32_t max_top_inputs,
                                   Budget& work) {
  return partition(functions, bound_mask, max_top_inputs, work);
}

Joint_decomposition recode_pair(const Joint_decomposition& original, Joint_code_change change, Budget& work) {
  Joint_decomposition out;
  const auto          bits = original.divisors.size();
  if (original.status != Status::feasible || bits < 2 || bits > max_logical_inputs || change.target >= bits || change.source >= bits
      || change.target == change.source || original.bound.empty() || original.bound.size() > max_logical_inputs
      || bits + original.free.size() > max_logical_inputs || !valid_truth_table(original.top_care)
      || original.top_care.inputs != bits + original.free.size()) {
    return out;
  }
  uint64_t words = 0;
  for (const auto& divisor : original.divisors) {
    if (!valid_truth_table(divisor) || divisor.inputs != original.bound.size()) {
      return out;
    }
    words += divisor.words.size();
  }
  for (const auto& top : original.tops) {
    if (!valid_truth_table(top) || top.inputs != original.top_care.inputs) {
      return out;
    }
  }
  out.status = Status::search_exhausted;
  if (!work.spend(words + original.bound.size() + original.free.size())) {
    return out;
  }
  out.bound          = original.bound;
  out.free           = original.free;
  out.divisors       = original.divisors;
  auto&       target = out.divisors[change.target];
  const auto& source = original.divisors[change.source];
  for (size_t i = 0; i < target.words.size(); ++i) {
    if (!work.spend()) {
      return out;
    }
    target.words[i] ^= source.words[i];
  }
  for (auto& top : out.tops) {
    top = Truth_table(original.top_care.inputs);
  }
  out.top_care = Truth_table(original.top_care.inputs);
  for (uint32_t x = 0; x < (1U << original.top_care.inputs); ++x) {
    if (!work.spend(4)) {
      return out;
    }
    const auto mapped = x ^ (((x >> change.source) & 1U) << change.target);
    for (size_t i = 0; i < out.tops.size(); ++i) {
      out.tops[i].set(mapped, original.tops[i].get(x));
    }
    out.top_care.set(mapped, original.top_care.get(x));
  }
  out.status = Status::feasible;
  return out;
}
}  // namespace livehd::usyn
