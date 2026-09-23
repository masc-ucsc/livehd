// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <array>
#include <bit>
#include <map>
#include <set>

#include "network_builder.hpp"

namespace livehd::synth {
namespace {
using Literals                = std::vector<std::pair<Id, bool>>;
struct Expression {
  bool     valid = false, negative = false;
  Literals literals;  // F = AND(source XOR literal.negative) XOR negative
};

std::optional<std::pair<Cube, bool>> cube_form(const Truth_table& table, Budget& budget) {
  const auto count = uint32_t{1} << table.inputs;
  if (!budget.spend(count * 2)) {
    return {};
  }
  for (bool negative : {false, true}) {
    uint32_t ones = count - 1, zeros = count - 1, terms = 0;
    for (uint32_t x = 0; x < count; ++x) {
      if (table.get(x) != negative) {
        ones  &= x;
        zeros &= ~x;
        ++terms;
      }
    }
    const auto care = ones | zeros;
    if (terms && terms == (uint32_t{1} << (table.inputs - std::popcount(care)))) {
      return std::pair{
          Cube{care, ones},
          negative
      };
    }
  }
  return {};
}

Expression expand(const Logic_node& node, const std::vector<Expression>& earlier, uint32_t limit, Budget& budget) {
  auto form = cube_form(node.table, budget);
  if (!form) {
    return {};
  }
  std::map<Id, bool> literals;
  const auto& [cube, negative] = *form;
  if (std::popcount(cube.care) == 1) {
    const auto  j     = std::countr_zero(cube.care);
    const auto& child = earlier[node.inputs[j]];
    if (!child.valid || !budget.spend(child.literals.size() + 1)) {
      return {};
    }
    auto result     = child;
    result.negative = child.negative != (negative != (((cube.ones >> j) & 1) == 0));
    return result;
  }
  for (uint32_t j = 0; j < node.inputs.size(); ++j) {
    if (!((cube.care >> j) & 1)) {
      continue;
    }
    const auto& child = earlier[node.inputs[j]];
    if (!child.valid || !budget.spend(child.literals.size() + 1)) {
      return {};
    }
    const bool invert = child.negative != (((cube.ones >> j) & 1) == 0);
    if (invert && child.literals.empty()) {
      return {true, !negative, {}};
    }
    if (invert && child.literals.size() > 1) {
      return {};
    }
    for (const auto& [id, polarity] : child.literals) {
      const bool rail           = polarity != invert;
      const auto [it, inserted] = literals.emplace(id, rail);
      if (!inserted && it->second != rail) {
        return {true, !negative, {}};
      }
      if (literals.size() > limit) {
        return {};
      }
    }
  }
  return {true, negative, Literals(literals.begin(), literals.end())};
}
}  // namespace

void reshape_residue(const Logic_network& source, uint32_t limit, uint32_t symbolic_nodes, Attempt& attempt, Budget& budget) {
  if (!limit) {
    attempt.reshape_reason = "disabled";
    return;
  }
  if (attempt.recipe.levels < 2) {
    attempt.reshape_reason = "requires_two_levels";
    return;
  }
  const auto fail = [&](std::string_view reason) {
    attempt.reshape_reason = budget.exhausted ? "work_or_resource_exhausted" : std::string(reason);
  };
  std::set<Id> roots;
  for (auto output : source.outputs) {
    if (!source.nodes[output].source) {
      roots.insert(output);
    }
  }
  if (roots.size() > limit) {
    fail("output_admission_limit");
    return;
  }
  if (!budget.spend(source.nodes.size() + source.outputs.size())) {
    fail("");
    return;
  }
  uint32_t recognition_limit = 1;
  for (uint32_t level = 0; level < attempt.recipe.levels; ++level) {
    recognition_limit = std::min(uint32_t{256}, recognition_limit * attempt.recipe.support);
  }
  std::vector<Expression> expressions(source.nodes.size());
  for (Id id = 0; id < source.nodes.size() && !budget.exhausted; ++id) {
    if (!budget.spend()) {
      break;
    }
    expressions[id] = source.nodes[id].source ? Expression{true, false, {{id, false}}}
                                              : expand(source.nodes[id], expressions, recognition_limit, budget);
  }
  if (budget.exhausted) {
    fail("");
    return;
  }
  std::map<Literals, bool> positive_demand;
  for (auto root : roots) {
    if (!expressions[root].valid) {
      fail("unsupported_associative_output");
      return;
    }
    positive_demand[expressions[root].literals] |= !expressions[root].negative;
  }
  detail::Network_builder builder(source, attempt.recipe, budget);
  struct Decoder {
    Id                root;
    std::vector<Id>   inputs;
    Dependency_result relation;
    Form              form;
  };
  std::vector<Decoder>   decoders;
  std::map<Literals, Id> groups;
  std::map<Id, unsigned> rails;
  if (!budget.spend(source.nodes.size())) {
    fail("");
    return;
  }
  auto extended = source;
  for (auto root : roots) {
    const auto& expression = expressions[root];
    const auto  width      = std::min({attempt.recipe.support,
                                       attempt.recipe.literals,
                                       positive_demand[expression.literals] ? attempt.recipe.series : attempt.recipe.support});
    if (!width) {
      fail("function_complexity_limit");
      return;
    }
    auto     divisors = expression.literals;
    uint32_t level    = 1;
    while (divisors.size() > width) {
      if (width < 2 || level >= attempt.recipe.levels) {
        fail("regrouping_depth_limit");
        return;
      }
      Literals next;
      for (size_t begin = 0; begin < divisors.size(); begin += width) {
        const auto end = std::min(divisors.size(), begin + width);
        if (end - begin == 1) {
          next.push_back(divisors[begin]);
          continue;
        }
        if (!budget.spend(end - begin)) {
          fail("");
          return;
        }
        Literals key(divisors.begin() + begin, divisors.begin() + end);
        if (const auto found = groups.find(key); found != groups.end()) {
          next.emplace_back(found->second, false);
          continue;
        }
        if (groups.size() >= max_encoder_nodes) {
          fail("encoder_admission_limit");
          return;
        }
        std::vector<Id> bound;
        uint32_t        assignment = 0;
        for (uint32_t j = 0; j < key.size(); ++j) {
          bound.push_back(key[j].first);
          assignment |= uint32_t{!key[j].second} << j;
        }
        Truth_table table(bound.size());
        table.set(assignment, true);
        const auto id = extended.add_function(bound, table);
        groups.emplace(std::move(key), id);
        builder.network.encodings.push_back({false, std::move(bound), std::move(table)});
        next.emplace_back(id, false);
      }
      divisors = std::move(next);
      ++level;
    }
    std::vector<Id> inputs;
    for (const auto& [id, negative] : divisors) {
      (void)negative;
      inputs.push_back(id);
    }
    // Query roots are temporary so later encoder identities remain exactly
    // source.nodes.size() + definition index, independent of output order.
    const auto definition = source.nodes[root];
    const auto copy       = extended.add_function(definition.inputs, definition.table);
    auto       relation   = exact_dependency(extended, copy, inputs, max_logical_inputs, budget, symbolic_nodes);
    extended.nodes.pop_back();
    if (relation.status != Dependency_status::proven) {
      if (relation.status == Dependency_status::refuted || relation.status == Dependency_status::invalid) {
        attempt.status = Status::invalid;
        attempt.reason = "associative regrouping does not reconstruct the original output";
        fail("invalid_witness");
      } else {
        fail("dependency_query_inconclusive");
      }
      return;
    }
    // Associative decoders use total care. Unreachable divisor codes must not
    // change the fixed AND/NAND completion selected by regrouping.
    Truth_table completed(inputs.size());
    for (uint32_t x = 0; x < (uint32_t{1} << inputs.size()); ++x) {
      if (!budget.spend()) {
        fail("");
        return;
      }
      bool value = true;
      for (uint32_t j = 0; j < inputs.size(); ++j) {
        value &= (((x >> j) & 1) != 0) != divisors[j].second;
      }
      completed.set(x, value != expression.negative);
    }
    for (uint32_t x = 0; x < (uint32_t{1} << inputs.size()); ++x) {
      if (!budget.spend()) {
        fail("");
        return;
      }
      if (relation.care.get(x) && relation.table.get(x) != completed.get(x)) {
        attempt.status = Status::invalid;
        attempt.reason = "associative decoder completion mismatch";
        fail("invalid_witness");
        return;
      }
    }
    auto form = make_form(completed, attempt.recipe.literals, attempt.recipe.series, budget);
    if (form.status == Status::invalid) {
      attempt.status = Status::invalid;
      attempt.reason = "invalid associative decoder form";
    }
    if (form.status != Status::feasible) {
      fail("decoder_complexity_limit");
      return;
    }
    relation.table = std::move(completed);
    for (uint32_t j = 0; j < inputs.size(); ++j) {
      rails[inputs[j]] |= (((form.positive >> j) & 1) ? 1 : 0) | (((form.negative >> j) & 1) ? 2 : 0);
    }
    decoders.push_back({root, std::move(inputs), std::move(relation), std::move(form)});
  }
  // Demand flows from decoders back through the definition DAG. Forms are
  // frozen before emission so each actual rail is built exactly once, after
  // every producer it reads. Complemented encoders demand real lower rails.
  std::vector<std::array<std::optional<Form>, 2>> forms(builder.network.encodings.size());
  for (size_t end = forms.size(); end > 0; --end) {
    const auto  index      = end - 1;
    const auto  id         = static_cast<Id>(source.nodes.size() + index);
    const auto& definition = builder.network.encodings[index];
    for (uint32_t rail = 0; rail < 2; ++rail) {
      if (!((rails[id] >> rail) & 1)) {
        continue;
      }
      const auto table = rail ? definition.table.complement() : definition.table;
      auto       form  = make_form(table, attempt.recipe.literals, attempt.recipe.series, budget);
      if (form.status == Status::invalid) {
        attempt.status = Status::invalid;
        attempt.reason = "invalid associative encoder form";
      }
      if (form.status != Status::feasible) {
        fail("encoder_complexity_limit");
        return;
      }
      for (uint32_t j = 0; j < definition.inputs.size(); ++j) {
        rails[definition.inputs[j]] |= (((form.positive >> j) & 1) ? 1 : 0) | (((form.negative >> j) & 1) ? 2 : 0);
      }
      forms[index][rail] = std::move(form);
    }
  }
  for (uint32_t index = 0; index < forms.size(); ++index) {
    const auto  id         = static_cast<Id>(source.nodes.size() + index);
    const auto& definition = builder.network.encodings[index];
    for (uint32_t rail = 0; rail < 2; ++rail) {
      if (!forms[index][rail]) {
        continue;
      }
      const auto table = rail ? definition.table.complement() : definition.table;
      if (!builder.append(id, rail != 0, definition.inputs, table, *forms[index][rail])) {
        fail("encoder_emission_limit");
        return;
      }
    }
  }
  for (const auto& decoder : decoders) {
    if (!builder.append(decoder.root,
                        false,
                        decoder.inputs,
                        decoder.relation.table,
                        decoder.form,
                        decoder.relation.care,
                        decoder.relation.sources,
                        true)) {
      fail("decoder_emission_limit");
      return;
    }
  }
  builder.outputs();
  if (!verify(source, builder.network, attempt.recipe, budget)) {
    if (!budget.exhausted) {
      attempt.status = Status::invalid;
      attempt.reason = "associative regrouping witness mismatch";
    }
    fail("invalid_witness");
    return;
  }
  attempt.network         = std::move(builder.network);
  attempt.status          = Status::feasible;
  attempt.reason          = "verified associative depth reshaping";
  attempt.covered_outputs = source.outputs.size();
  attempt.reshape_roots   = roots.size();
  attempt.reshape_groups  = groups.size();
  attempt.reshape_reason  = "verified_associative_reshaping";
}
}  // namespace livehd::synth
