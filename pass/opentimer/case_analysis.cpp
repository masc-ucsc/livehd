// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <deque>
#include <stdexcept>
#include <tuple>

#include "pass/liberty/liberty_dff.hpp"
#include "pass_opentimer.hpp"

void Pass_opentimer::apply_case_analysis() {
  timer.update_timing();
  const auto& library = timer.celllib(ot::MAX);
  if (!library) {
    return;
  }
  struct Function {
    std::vector<std::string>                       inputs;
    std::string                                    output;
    uint64_t                                       truth;
    std::map<std::string, std::optional<uint64_t>> conditions;
  };
  std::map<std::string, std::vector<Function>> functions;
  for (const auto& [name, cell] : library->cells) {
    std::vector<std::string> inputs;
    for (const auto& [pin_name, pin] : cell.cellpins) {
      if (pin.direction == ot::CellpinDirection::INPUT) {
        inputs.push_back(pin_name);
      }
    }
    if (inputs.size() > 6) {
      continue;
    }
    std::sort(inputs.begin(), inputs.end());
    for (const auto& [pin_name, pin] : cell.cellpins) {
      if (pin.direction != ot::CellpinDirection::OUTPUT || !pin.function || pin.three_state) {
        continue;
      }
      livehd::liberty::Comb_cell description;
      description.inputs   = inputs;
      description.output   = pin_name;
      description.function = *pin.function;
      if (description.inputs.empty()) {
        // The shared truth-table API requires one variable. Use a name absent
        // from the expression, so only input-independent functions can parse.
        std::string unused = "__lhd_case_unused";
        while (description.function.find(unused) != std::string::npos) {
          unused += '_';
        }
        description.inputs.push_back(std::move(unused));
      }
      // State functions refer to internal FF/latch variables, so they cannot
      // be evaluated as combinational truth tables over the signal inputs.
      if (const auto truth = livehd::liberty::cell_truth(description)) {
        Function function{inputs, pin_name, *truth, {}};
        for (const auto& arc : pin.timings) {
          if (arc.when) {
            description.function = *arc.when;
            function.conditions.emplace(*arc.when, livehd::liberty::cell_truth(description));
          }
        }
        functions[name].push_back(std::move(function));
      }
    }
  }
  struct Model {
    std::vector<const ot::Pin*> inputs;
    const ot::Pin*              output;
    uint64_t                    truth;
    const Function*             function;
  };
  std::vector<Model>                                       models;
  absl::flat_hash_map<const ot::Net*, std::vector<size_t>> consumers;
  for (const auto& [name, gate] : timer.gates()) {
    const auto found = functions.find(gate.cell_name());
    if (found == functions.end()) {
      continue;
    }
    for (const auto& function : found->second) {
      Model model{{}, &timer.pins().at(name + ":" + function.output), function.truth, &function};
      for (const auto& input : function.inputs) {
        const auto* pin = &timer.pins().at(name + ":" + input);
        model.inputs.push_back(pin);
        if (pin->net()) {
          consumers[pin->net()].push_back(models.size());
        }
      }
      models.push_back(std::move(model));
    }
  }
  absl::flat_hash_map<const ot::Net*, bool> known;
  std::deque<size_t>                        pending;
  std::vector<bool>                         queued(models.size(), true);
  for (size_t i = 0; i < models.size(); ++i) {
    pending.push_back(i);
  }
  const auto fixed = [&](const Model& model) {
    unsigned mask = 0, value = 0;
    for (unsigned i = 0; i < model.inputs.size(); ++i) {
      const auto*         pin = model.inputs[i];
      std::optional<bool> bit;
      if (const auto it = timing_constant_pins_.find(pin->name()); it != timing_constant_pins_.end()) {
        bit = it->second;
      } else if (const auto net = known.find(pin->net()); net != known.end()) {
        bit = net->second;
      }
      if (bit) {
        mask  |= 1U << i;
        value |= unsigned(*bit) << i;
      }
    }
    return std::pair{mask, value};
  };
  while (!pending.empty()) {
    const auto index = pending.front();
    pending.pop_front();
    queued[index]     = false;
    const auto& model = models[index];
    if (!model.output->net() || known.contains(model.output->net())) {
      continue;
    }
    const auto [mask, value] = fixed(model);
    if (mask == 0 && model.truth != 0 && model.truth != ~uint64_t{0}) {
      continue;
    }
    std::optional<bool> constant;
    bool                varies = false;
    for (unsigned row = 0; row < (1U << model.inputs.size()); ++row) {
      if ((row & mask) != value) {
        continue;
      }
      const bool output = (model.truth >> row) & 1U;
      if (constant && *constant != output) {
        varies = true;
        break;
      }
      constant = output;
    }
    if (!varies && constant) {
      known.emplace(model.output->net(), *constant);
      if (const auto it = consumers.find(model.output->net()); it != consumers.end()) {
        for (auto consumer : it->second) {
          if (!queued[consumer]) {
            pending.push_back(consumer);
            queued[consumer] = true;
          }
        }
      }
    }
  }
  case_constants_ = known.size();
  case_disabled_ = case_narrowed_ = 0;
  std::vector<std::tuple<std::string, std::string, uint8_t>> masks;
  for (const auto& model : models) {
    const auto [fixed_mask, fixed_value] = fixed(model);
    if (fixed_mask == 0 && !known.contains(model.output->net())) {
      continue;
    }
    for (unsigned input = 0; input < model.inputs.size(); ++input) {
      const auto bit  = 1U << input;
      uint8_t    mask = 0;
      if (!(fixed_mask & bit)) {
        for (unsigned row = 0; row < (1U << model.inputs.size()); ++row) {
          if ((row & bit) || (row & fixed_mask) != fixed_value) {
            continue;
          }
          const bool low = (model.truth >> row) & 1U, high = (model.truth >> (row | bit)) & 1U;
          if (low != high) {
            mask |= low ? 6U : 9U;  // Negative / positive unate transition pairs.
          }
        }
      }
      if (mask != 15) {
        masks.emplace_back(model.inputs[input]->name(), model.output->name(), mask);
        case_disabled_ += mask == 0;
        case_narrowed_ += mask != 0;
      }
    }
  }
  std::sort(masks.begin(), masks.end());
  timer.set_case_masks(std::move(masks));
  absl::flat_hash_map<const ot::Pin*, const Model*> by_output;
  for (const auto& model : models) {
    if (!model.function->conditions.empty()) {
      by_output.emplace(model.output, &model);
    }
  }
  const auto condition_value = [&](const Model& model, const std::string& expression) -> std::optional<bool> {
    const auto found = model.function->conditions.find(expression);
    if (found == model.function->conditions.end() || !found->second) {
      return {};
    }
    const auto [mask, value] = fixed(model);
    std::optional<bool> result;
    for (unsigned row = 0; row < (1U << model.inputs.size()); ++row) {
      if ((row & mask) != value) {
        continue;
      }
      const bool bit = (*found->second >> row) & 1U;
      if (result && *result != bit) {
        return {};
      }
      result = bit;
    }
    return result;
  };
  std::vector<std::pair<size_t, uint8_t>> conditional_masks;
  for (const auto& arc : timer.arcs()) {
    if (!arc.is_cell_arc()) {
      continue;
    }
    const auto found = by_output.find(&arc.to());
    if (found == by_output.end()) {
      continue;
    }
    const auto  view   = arc.timing_view();
    const auto* timing = view[ot::MAX] ? view[ot::MAX] : view[ot::MIN];
    if (!timing || timing->is_constraint()) {
      continue;
    }
    bool disabled = false;
    if (timing->when) {
      const auto value = condition_value(*found->second, *timing->when);
      disabled         = value && !*value;
    } else {
      // A default arc is suppressed when a conditional alternative from the
      // same input is definitely selected. Unknown conditions stay active.
      const auto* pin = arc.to().cellpin(view[ot::MAX] ? ot::MAX : ot::MIN);
      for (const auto& candidate : pin->timings) {
        if (candidate.related_pin == timing->related_pin && candidate.when) {
          const auto value = condition_value(*found->second, *candidate.when);
          if (value && *value) {
            disabled = true;
            break;
          }
        }
      }
    }
    if (disabled) {
      conditional_masks.emplace_back(arc.idx(), 0);
    }
  }
  case_conditions_ = conditional_masks.size();
  timer.refine_case_masks(std::move(conditional_masks));
}
