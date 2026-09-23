// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <bit>
#include <map>
#include <numeric>
#include <set>

#include "network_builder.hpp"
#include "unate.hpp"

namespace livehd::synth {
namespace {
constexpr uint32_t output_bound = 8;  // root COUNT, a separate engine limit
constexpr uint32_t source_bound = max_logical_inputs;

uint32_t scatter(uint32_t assignment, const std::vector<uint32_t>& positions) {
  uint32_t result = 0;
  for (uint32_t j = 0; j < positions.size(); ++j) {
    result |= ((assignment >> j) & 1) << positions[j];
  }
  return result;
}
Truth_table completion(const Form& form, uint32_t inputs, Budget& budget) {
  Truth_table result(inputs);
  for (uint32_t x = 0; x < (uint32_t{1} << inputs); ++x) {
    if (!budget.spend(form.cubes.size() + 1)) {
      return result;
    }
    result.set(x, std::any_of(form.cubes.begin(), form.cubes.end(), [&](const auto& c) { return (x & c.care) == c.ones; }));
  }
  return result;
}

// Next injective class-to-code assignment in lexicographic order. Unused
// codes are not permuted, so no trial repeats an equivalent assignment.
bool next_codes(std::vector<uint32_t>& codes, uint32_t slots, Budget& budget) {
  if (!budget.spend(slots + codes.size())) {
    return false;
  }
  std::vector<bool> used(slots);
  for (auto code : codes) {
    used[code] = true;
  }
  for (size_t i = codes.size(); i-- > 0;) {
    used[codes[i]] = false;
    for (uint32_t code = codes[i] + 1; code < slots; ++code) {
      if (!budget.spend()) {
        return false;
      }
      if (used[code]) {
        continue;
      }
      codes[i]      = code;
      used[code]    = true;
      uint32_t next = 0;
      for (size_t j = i + 1; j < codes.size(); ++j) {
        while (next < slots && used[next]) {
          if (!budget.spend()) {
            return false;
          }
          ++next;
        }
        if (!budget.spend()) {
          return false;
        }
        codes[j]   = next;
        used[next] = true;
      }
      return true;
    }
  }
  return false;
}

}  // namespace

void encode_residue(const Logic_network& source, uint32_t limit, Attempt& attempt, Budget& budget, uint32_t pair_limit,
                    uint32_t code_limit) {
  if (!limit) {
    attempt.encoding_reason = "disabled";
    return;
  }
  if (attempt.recipe.levels < 2) {
    attempt.encoding_reason = "requires_two_levels";
    return;
  }
  attempt.encoding_reason = "bounded_cofactor_search_incomplete";
  if (!budget.spend(source.nodes.size() + source.outputs.size())) {
    attempt.encoding_reason = "work_or_resource_exhausted";
    return;
  }
  std::set<Id> roots, sources;
  for (auto out : source.outputs) {
    if (!source.nodes[out].source) {
      roots.insert(out);
    }
  }
  if (roots.size() > output_bound) {
    attempt.encoding_reason = "output_admission_limit";
    return;
  }
  if (roots.empty()) {
    attempt.encoding_reason = "no_function_outputs";
    return;
  }
  std::vector<bool> seen(source.nodes.size());
  std::vector<Id>   pending(roots.begin(), roots.end());
  while (!pending.empty() && !budget.exhausted) {
    const auto id = pending.back();
    pending.pop_back();
    if (!budget.spend()) {
      break;
    }
    if (seen[id]) {
      continue;
    }
    seen[id] = true;
    if (source.nodes[id].source) {
      sources.insert(id);
      if (sources.size() > source_bound) {
        attempt.encoding_reason = "source_admission_limit";
        return;
      }
    } else {
      pending.insert(pending.end(), source.nodes[id].inputs.begin(), source.nodes[id].inputs.end());
    }
  }
  const std::vector<Id>    ordered(sources.begin(), sources.end());
  const auto               n = static_cast<uint32_t>(ordered.size());
  std::vector<Truth_table> tables;
  if (!budget.spend(source.nodes.size())) {
    attempt.encoding_reason = "work_or_resource_exhausted";
    return;
  }
  auto query = source;
  for (auto root : roots) {
    const auto node     = source.nodes[root];
    const auto copy     = query.add_function(node.inputs, node.table);
    auto       relation = exact_dependency(query, copy, ordered, source_bound, budget);
    if (relation.status != Dependency_status::proven) {
      if (relation.status == Dependency_status::invalid || relation.status == Dependency_status::refuted) {
        attempt.status          = Status::invalid;
        attempt.reason          = "cofactor source table query failed";
        attempt.encoding_reason = "invalid_witness";
        return;
      }
      attempt.encoding_reason = "source_query_inconclusive";
      if (budget.exhausted) {
        attempt.encoding_reason = "work_or_resource_exhausted";
      }
      return;
    }
    tables.push_back(std::move(relation.table));
  }
  // Each disjoint bound set is classified against ALL its remaining sources
  // and outputs. Equal classes are substitutable in every context, so their
  // Cartesian product determines every output. Still prove that fact below;
  // a class-count lower bound is never accepted as a gate implementation.
  const auto proposal = [&](const std::vector<uint32_t>& masks) {
    struct Group {
      std::vector<uint32_t> bound, codes, labels;
      uint32_t              bits;
    };
    std::vector<Group> groups;
    uint32_t           used = 0, bits = 0, class_product = 1;
    for (auto mask : masks) {
      used |= mask;
      std::vector<uint32_t> bound, rest;
      for (uint32_t j = 0; j < n; ++j) {
        ((mask >> j) & 1 ? bound : rest).push_back(j);
      }
      std::map<std::vector<uint64_t>, uint32_t> classes;
      std::vector<uint32_t>                     codes;
      const auto                                words = ((uint32_t{1} << rest.size()) + 63) / 64;
      for (uint32_t b = 0; b < (uint32_t{1} << bound.size()) && !budget.exhausted; ++b) {
        if (!budget.spend(tables.size() * words)) {
          break;
        }
        std::vector<uint64_t> signature(tables.size() * words);
        const auto            fixed = scatter(b, bound);
        for (uint32_t f = 0; f < (uint32_t{1} << rest.size()); ++f) {
          if (!budget.spend(rest.size() + tables.size() + 1)) {
            break;
          }
          const auto assignment = fixed | scatter(f, rest);
          for (uint32_t output = 0; output < tables.size(); ++output) {
            if (tables[output].get(assignment)) {
              signature[output * words + f / 64] |= uint64_t{1} << (f % 64);
            }
          }
        }
        if (!budget.spend(signature.size() * (std::bit_width(classes.size()) + 1))) {
          break;
        }
        const auto [it, inserted] = classes.emplace(std::move(signature), static_cast<uint32_t>(classes.size()));
        (void)inserted;
        codes.push_back(it->second);
      }
      if (budget.exhausted) {
        return false;
      }
      const auto group_bits  = static_cast<uint32_t>(std::bit_width(static_cast<uint32_t>(classes.size() - 1)));
      bits                  += group_bits;
      class_product         *= classes.size();
      if (bits > attempt.recipe.support) {
        return false;
      }
      std::vector<uint32_t> labels(classes.size());
      std::iota(labels.begin(), labels.end(), 0);
      groups.push_back({std::move(bound), std::move(codes), std::move(labels), group_bits});
    }
    std::vector<Id> free;
    for (uint32_t j = 0; j < n; ++j) {
      if (!((used >> j) & 1)) {
        free.push_back(ordered[j]);
      }
    }
    if (bits + free.size() > attempt.recipe.support || !budget.spend(source.nodes.size())) {
      return false;
    }
    const auto evaluate_codes = [&] {
      ++attempt.encoding_code_queries;
      detail::Network_builder builder(source, attempt.recipe, budget);
      for (const auto& group : groups) {
        for (uint32_t bit = 0; bit < group.bits; ++bit) {
          Truth_table     encoding(group.bound.size());
          std::vector<Id> inputs;
          for (auto pos : group.bound) {
            inputs.push_back(ordered[pos]);
          }
          for (uint32_t b = 0; b < group.codes.size(); ++b) {
            if (!budget.spend()) {
              return false;
            }
            encoding.set(b, ((group.labels[group.codes[b]] >> bit) & 1) != 0);
          }
          builder.network.encodings.push_back({false, std::move(inputs), std::move(encoding)});
        }
      }
      auto            extended = source;
      std::vector<Id> divisors;
      for (const auto& definition : builder.network.encodings) {
        divisors.push_back(extended.add_function(definition.inputs, definition.table));
      }
      divisors.insert(divisors.end(), free.begin(), free.end());
      struct Decoder {
        Id                origin;
        Dependency_result relation;
        Truth_table       completed;
        Form              form;
      };
      std::vector<Decoder> decoders;
      uint32_t             positive = 0, negative = 0;
      bool                 viable = true;
      for (auto root : roots) {
        const auto node     = source.nodes[root];
        const auto copy     = extended.add_function(node.inputs, node.table);
        auto       relation = exact_dependency(extended, copy, divisors, source_bound, budget);
        if (relation.status != Dependency_status::proven) {
          viable = false;
          if (relation.status == Dependency_status::refuted || relation.status == Dependency_status::invalid) {
            attempt.status          = Status::invalid;
            attempt.reason          = "cofactor encoder does not reconstruct the original output";
            attempt.encoding_reason = "invalid_witness";
            return true;
          }
          break;
        }
        auto form = make_form(relation.table, relation.care, attempt.recipe.literals, attempt.recipe.series, budget);
        if (form.status == Status::invalid) {
          attempt.status          = Status::invalid;
          attempt.reason          = "cofactor decoder form failed verification";
          attempt.encoding_reason = "invalid_witness";
          return true;
        }
        if (form.status != Status::feasible) {
          viable = false;
          break;
        }
        auto completed = completion(form, divisors.size(), budget);
        if (budget.exhausted) {
          viable = false;
          break;
        }
        positive |= form.positive;
        negative |= form.negative;
        decoders.push_back({root, std::move(relation), std::move(completed), std::move(form)});
      }
      if (!viable) {
        return false;
      }
      for (uint32_t bit = 0; bit < bits && viable; ++bit) {
        const auto& definition = builder.network.encodings[bit];
        for (uint32_t rail = 0; rail < 2; ++rail) {
          if (!(((rail ? negative : positive) >> bit) & 1)) {
            continue;
          }
          const auto t    = rail ? definition.table.complement() : definition.table;
          const auto form = make_form(t, attempt.recipe.literals, attempt.recipe.series, budget);
          if (form.status == Status::invalid) {
            attempt.status          = Status::invalid;
            attempt.reason          = "cofactor encoder form failed verification";
            attempt.encoding_reason = "invalid_witness";
            return true;
          }
          if (form.status != Status::feasible || !builder.append(divisors[bit], rail != 0, definition.inputs, t, form)) {
            viable = false;
            break;
          }
        }
      }
      for (const auto& decoder : decoders) {
        if (!viable) {
          break;
        }
        viable = builder.append(decoder.origin,
                                false,
                                divisors,
                                decoder.completed,
                                decoder.form,
                                decoder.relation.care,
                                decoder.relation.sources,
                                true);
      }
      if (!viable) {
        return false;
      }
      builder.outputs();
      if (verify(source, builder.network, attempt.recipe, budget)) {
        attempt.network             = std::move(builder.network);
        attempt.status              = Status::feasible;
        attempt.reason              = "verified cofactor encoding decomposition";
        attempt.covered_outputs     = source.outputs.size();
        attempt.encoding_classes    = class_product;
        attempt.encoding_bits       = bits;
        attempt.encoding_bound_sets = masks.size();
        attempt.encoding_reason     = "verified_shared_encoding";
        return true;
      }
      if (!budget.exhausted) {
        attempt.status          = Status::invalid;
        attempt.reason          = "cofactor encoding witness mismatch";
        attempt.encoding_reason = "invalid_witness";
        return true;
      }
      return false;
    };
    for (uint32_t trial = 0; trial < code_limit && !budget.exhausted; ++trial) {
      if (!budget.spend(source.nodes.size()) || evaluate_codes()) {
        return !budget.exhausted;
      }
      // The last bound set varies fastest. Reset its exhausted assignment
      // before carrying to the previous set, just as a mixed-radix counter.
      bool more = false;
      for (size_t i = groups.size(); i-- > 0 && !budget.exhausted;) {
        auto& group = groups[i];
        if (next_codes(group.labels, uint32_t{1} << group.bits, budget)) {
          more = true;
          break;
        }
        std::iota(group.labels.begin(), group.labels.end(), 0);
      }
      if (!more) {
        return false;
      }
      if (trial + 1 == code_limit) {
        attempt.encoding_code_limited = true;
      }
    }
    return false;
  };
  bool single_capped = false;
  for (uint32_t size = std::min(n, attempt.recipe.support); size > 0 && !budget.exhausted && !single_capped; --size) {
    if (n - size > attempt.recipe.support) {
      continue;
    }
    for (uint32_t mask = 1; mask < (uint32_t{1} << n) && !budget.exhausted; ++mask) {
      if (!budget.spend()) {
        break;
      }
      if (std::popcount(mask) != static_cast<int>(size)) {
        continue;
      }
      if (attempt.encoding_queries >= limit) {
        attempt.encoding_reason = "proposal_limit";
        single_capped           = true;
        break;
      }
      ++attempt.encoding_queries;
      if (proposal({mask})) {
        return;
      }
    }
  }
  // Enumerate each unordered pair once: descending first-set size, ascending
  // first mask, then descending submasks of its complement. No source belongs
  // to both sets. Every scan and attempted class matrix shares the work budget.
  for (uint32_t size = std::min(n, attempt.recipe.support); pair_limit && size > 0 && !budget.exhausted; --size) {
    const auto all = (uint32_t{1} << n) - 1;
    for (uint32_t first = 1; first <= all && !budget.exhausted; ++first) {
      if (!budget.spend()) {
        break;
      }
      if (std::popcount(first) != static_cast<int>(size)) {
        continue;
      }
      const auto remaining = all ^ first;
      for (uint32_t second = remaining; second && !budget.exhausted; second = (second - 1) & remaining) {
        if (!budget.spend()) {
          break;
        }
        const auto second_size = static_cast<uint32_t>(std::popcount(second));
        if (first >= second || second_size > attempt.recipe.support || n - size - second_size > attempt.recipe.support) {
          continue;
        }
        if (attempt.encoding_pair_queries >= pair_limit) {
          attempt.encoding_reason = "pair_proposal_limit";
          return;
        }
        ++attempt.encoding_pair_queries;
        if (proposal({first, second})) {
          return;
        }
      }
    }
  }
  if (budget.exhausted) {
    attempt.encoding_reason = "work_or_resource_exhausted";
  }
}
}  // namespace livehd::synth
