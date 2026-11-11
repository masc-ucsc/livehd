// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "cell_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <map>
#include <stdexcept>

namespace livehd::timing::fast {
namespace lt = liberty::timing;
namespace {
constexpr float infinity = std::numeric_limits<float>::infinity();

Variable convert(lt::Variable variable) {
  switch (variable) {
    case lt::Variable::none            : return Variable::none;
    case lt::Variable::input_slew      : return Variable::input_slew;
    case lt::Variable::output_load     : return Variable::output_load;
    case lt::Variable::related_slew    : return Variable::related_slew;
    case lt::Variable::constrained_slew: return Variable::constrained_slew;
  }
  throw std::invalid_argument("invalid NLDM variable");
}
Direction direction_of(const std::string& text) {
  if (text == "output") {
    return Direction::output;
  }
  if (text == "inout") {
    return Direction::inout;
  }
  if (text == "internal") {
    return Direction::internal;
  }
  return Direction::input;
}
std::string key_of(const lt::Arc& arc) { return arc.related_pin + '|' + arc.type + '|' + arc.when; }

struct Classified {
  Arc_kind    kind = Arc_kind::ignored;
  const char* why  = nullptr;  // a reason worth a diagnostic; null for arcs that are simply not setup/hold/delay
};
Classified classify(const std::string& type) {
  if (type == "combinational" || type == "combinational_rise" || type == "combinational_fall" || type == "three_state_enable"
      || type == "three_state_disable" || type == "clear" || type == "preset") {
    return {Arc_kind::combinational, nullptr};
  }
  if (type == "rising_edge") {
    return {Arc_kind::launch_rise, nullptr};
  }
  if (type == "setup_rising" || type == "recovery_rising") {
    return {Arc_kind::setup_rise, nullptr};
  }
  if (type == "hold_rising" || type == "removal_rising") {
    return {Arc_kind::hold_rise, nullptr};
  }
  if (type == "falling_edge" || type == "setup_falling" || type == "hold_falling" || type == "recovery_falling"
      || type == "removal_falling") {
    return {Arc_kind::ignored, "falling-edge timing"};
  }
  return {Arc_kind::ignored, nullptr};  // min_pulse_width, minimum_period, nochange, skew, ...
}
}  // namespace

uint32_t Cell_types::add_axis(const std::vector<double>& values) {
  if (values.empty()) {
    return no_index;
  }
  std::string key(values.size() * sizeof(float), '\0');
  for (size_t i = 0; i < values.size(); ++i) {
    const auto value = static_cast<float>(values[i]);
    std::memcpy(key.data() + i * sizeof(float), &value, sizeof(float));
  }
  if (const auto found = axis_by_content_.find(key); found != axis_by_content_.end()) {
    return found->second;
  }
  const auto id = static_cast<uint32_t>(axes_.size());
  axes_.push_back({static_cast<uint32_t>(axis_values_.size()), static_cast<uint32_t>(values.size())});
  for (const auto value : values) {
    axis_values_.push_back(static_cast<float>(value));
  }
  axis_by_content_.emplace(std::move(key), id);
  return id;
}

uint32_t Cell_types::add_table(const lt::Table& table) {
  Pooled_table pooled;
  pooled.x_axis = add_axis(table.x);
  pooled.y_axis = add_axis(table.y);
  pooled.x_var  = convert(table.x_variable);
  pooled.y_var  = convert(table.y_variable);
  pooled.nx     = static_cast<uint16_t>(std::max<size_t>(1, table.x.size()));
  pooled.ny     = static_cast<uint16_t>(std::max<size_t>(1, table.y.size()));
  if (table.values.size() != static_cast<size_t>(pooled.nx) * pooled.ny) {
    throw std::invalid_argument("invalid NLDM table dimensions");
  }
  pooled.values = static_cast<uint32_t>(values_.size());
  for (const auto value : table.values) {
    values_.push_back(static_cast<float>(value));
  }
  tables_.push_back(pooled);
  return static_cast<uint32_t>(tables_.size() - 1);
}

Cell_types Cell_types::build(std::span<const lt::Library* const> corners, std::span<const std::string> names) {
  if (corners.empty() || corners.size() != names.size()) {
    throw std::invalid_argument("Cell_types::build needs one name per corner and at least one corner");
  }
  Cell_types types;
  types.corner_names_.assign(names.begin(), names.end());
  const size_t count = corners.size();

  // The two port types come first, so that a primary input or output is a cell like any other.
  const auto add_port_type = [&](const char* type_name, const char* pin_name, Direction direction) {
    Type type;
    type.name      = type_name;
    type.first_pin = static_cast<uint32_t>(types.pins_.size());
    type.pin_count = 1;
    types.pins_.push_back({pin_name, direction, false});
    types.pin_first_arc_.push_back(0);
    types.pin_arc_count_.push_back(0);
    types.types_.push_back(std::move(type));
    types.caps_.insert(types.caps_.end(), count * 2, 0.f);
    types.max_cap_.insert(types.max_cap_.end(), count, infinity);
    types.max_tran_.insert(types.max_tran_.end(), count, infinity);
  };
  add_port_type("$input", "o", Direction::output);
  add_port_type("$output", "i", Direction::input);

  for (const auto& [cell_name, reference] : corners[0]->cells) {
    // The same cell in every other corner (or null).
    std::vector<const lt::Cell*> cell(count, nullptr);
    cell[0] = &reference;
    for (size_t k = 1; k < count; ++k) {
      if (const auto found = corners[k]->cells.find(cell_name); found != corners[k]->cells.end()) {
        cell[k] = &found->second;
      } else {
        types.diagnostics_.push_back(std::format("cell {} is not in corner {}: that corner uses {}",
                                                 cell_name,
                                                 types.corner_names_[k],
                                                 types.corner_names_[0]));
      }
    }

    Type type;
    type.name       = cell_name;
    type.area       = static_cast<float>(reference.area);
    type.first_pin  = static_cast<uint32_t>(types.pins_.size());
    type.first_arc  = static_cast<uint32_t>(types.arcs_.size());
    type.sequential = reference.sequential;
    type.latch      = reference.latch;
    type.memory     = reference.memory;
    type.clock_gate = reference.clock_gate;

    // Pins, in the reference corner's order, with their capacitances and limits per corner.
    std::map<std::string, uint16_t> local;
    for (const auto& [pin_name, pin] : reference.pins) {
      local.emplace(pin_name, static_cast<uint16_t>(local.size()));
      types.pins_.push_back({pin_name, direction_of(pin.direction), pin.clock});
      types.pin_first_arc_.push_back(0);  // set with the arcs below
      types.pin_arc_count_.push_back(0);
      for (size_t k = 0; k < count; ++k) {
        const lt::Pin* in_corner = &pin;
        if (cell[k] != nullptr) {
          if (const auto found = cell[k]->pins.find(pin_name); found != cell[k]->pins.end()) {
            in_corner = &found->second;
          }
        }
        for (unsigned t = 0; t < 2; ++t) {
          types.caps_.push_back(static_cast<float>(in_corner->capacitance_ff[t].value_or(0)));
        }
        types.max_cap_.push_back(in_corner->max_capacitance_ff ? static_cast<float>(*in_corner->max_capacitance_ff) : infinity);
        types.max_tran_.push_back(in_corner->max_transition_ps ? static_cast<float>(*in_corner->max_transition_ps) : infinity);
      }
    }
    type.pin_count = static_cast<uint16_t>(local.size());

    // The caps above were pushed pin by pin, corner by corner: [pin][corner][transition], as pin_cap reads them.
    // Arcs: the arcs of a pin are the timing groups on it; a check sits on its constrained pin.
    size_t arcs_of_type = 0;
    for (const auto& [pin_name, pin] : reference.pins) {
      const auto pin_slot            = type.first_pin + local.at(pin_name);
      types.pin_first_arc_[pin_slot] = static_cast<uint32_t>(types.arcs_.size());
      std::map<std::string, unsigned> ordinal;  // among the arcs of this pin with the same key
      for (const auto& arc : pin.arcs) {
        const auto key = key_of(arc);
        const auto nth = ordinal[key]++;
        Arc        entry;
        entry.to           = local.at(pin_name);
        const auto related = local.find(arc.related_pin);
        const auto sort    = classify(arc.type);
        entry.kind         = sort.kind;
        if (!arc.unsupported.empty()) {
          entry.kind = Arc_kind::ignored;
          types.diagnostics_.push_back(std::format("{}: arc {} -> {} ({}) uses an unsupported model: {}",
                                                   cell_name,
                                                   arc.related_pin,
                                                   pin_name,
                                                   arc.type,
                                                   arc.unsupported.front()));
        } else if (related == local.end()) {
          entry.kind = Arc_kind::ignored;
          types.diagnostics_.push_back(
              std::format("{}: arc {} -> {} relates to a pin the cell does not have", cell_name, arc.related_pin, pin_name));
        } else if (sort.why != nullptr) {
          types.diagnostics_.push_back(
              std::format("{}: arc {} -> {} ({}) is not timed: {}", cell_name, arc.related_pin, pin_name, arc.type, sort.why));
        }
        if (entry.kind == Arc_kind::ignored) {
          ++types.ignored_arcs_;
          continue;
        }
        entry.from  = related->second;
        entry.sense = arc.sense == lt::Sense::positive   ? Sense::positive
                      : arc.sense == lt::Sense::negative ? Sense::negative
                                                         : Sense::non_unate;
        if (!arc.when.empty()) {
          entry.when = static_cast<uint32_t>(types.conditions_.size());
          types.conditions_.push_back(arc.when);
        }
        entry.tables = static_cast<uint32_t>(types.arc_tables_.size());
        for (size_t k = 0; k < count; ++k) {
          // The matching arc of corner k: the same key, the same ordinal.
          const lt::Arc* match = &arc;
          if (cell[k] != nullptr && k != 0) {
            match = nullptr;
            if (const auto found = cell[k]->pins.find(pin_name); found != cell[k]->pins.end()) {
              unsigned seen = 0;
              for (const auto& candidate : found->second.arcs) {
                if (key_of(candidate) == key && seen++ == nth) {
                  match = &candidate;
                  break;
                }
              }
            }
            if (match == nullptr || !match->unsupported.empty()) {
              types.diagnostics_.push_back(std::format("{}: arc {} -> {} is not usable in corner {}: that corner uses {}",
                                                       cell_name,
                                                       arc.related_pin,
                                                       pin_name,
                                                       types.corner_names_[k],
                                                       types.corner_names_[0]));
              match = &arc;
            }
          } else if (cell[k] == nullptr) {
            match = &arc;
          }
          const std::array<const std::array<std::optional<lt::Table>, 2>*, 3> groups{&match->delay,
                                                                                     &match->slew,
                                                                                     &match->constraint};
          for (const auto* group : groups) {
            for (unsigned t = 0; t < 2; ++t) {
              types.arc_tables_.push_back((*group)[t] ? types.add_table(*(*group)[t]) : no_index);
            }
          }
        }
        types.arcs_.push_back(entry);
        ++arcs_of_type;
        ++types.pin_arc_count_[pin_slot];
      }
    }
    type.arc_count = static_cast<uint16_t>(arcs_of_type);
    types.by_name_.emplace(cell_name, static_cast<uint32_t>(types.types_.size()));
    types.types_.push_back(std::move(type));
  }
  return types;
}

uint32_t Cell_types::find(std::string_view cell) const {
  const auto found = by_name_.find(std::string(cell));
  return found == by_name_.end() ? no_index : found->second;
}

uint32_t Cell_types::find_pin(uint32_t type, std::string_view pin_name) const {
  const auto& entry = types_[type];
  for (uint32_t i = 0; i < entry.pin_count; ++i) {
    if (pins_[entry.first_pin + i].name == pin_name) {
      return i;
    }
  }
  return no_index;
}

float Cell_types::coordinate(Variable variable, const Query& query) {
  switch (variable) {
    case Variable::none            : return 0;
    case Variable::input_slew      : return query.input_slew;
    case Variable::output_load     : return query.output_load;
    case Variable::related_slew    : return query.related_slew;
    case Variable::constrained_slew: return query.constrained_slew;
  }
  return 0;
}

float Cell_types::lookup(uint32_t id, const Query& query) const {
  const auto& table = tables_[id];
  // The two nearest points of the axis around the value; beyond either end the line through the two points at
  // that end continues, as in liberty::timing::Table::lookup. A negative value (a rounding artifact) is zero.
  struct Place {
    uint32_t lo = 0, hi = 0;
    float    fraction = 0;
  };
  const auto place = [&](uint32_t axis_id, float value) {
    Place at;
    if (axis_id == no_index || axes_[axis_id].size < 2) {
      return at;
    }
    value                = std::max(value, 0.f);
    const float*   axis  = axis_values_.data() + axes_[axis_id].offset;
    const uint32_t size  = axes_[axis_id].size;
    const auto     upper = static_cast<uint32_t>(std::upper_bound(axis, axis + size, value) - axis);
    at.hi                = std::clamp<uint32_t>(upper, 1, size - 1);
    at.lo                = at.hi - 1;
    at.fraction          = (value - axis[at.lo]) / (axis[at.hi] - axis[at.lo]);
    return at;
  };
  const auto   a = place(table.x_axis, coordinate(table.x_var, query)), b = place(table.y_axis, coordinate(table.y_var, query));
  const float* v    = values_.data() + table.values;
  const auto   ny   = table.ny;
  const float  low  = std::lerp(v[a.lo * ny + b.lo], v[a.lo * ny + b.hi], b.fraction);
  const float  high = std::lerp(v[a.hi * ny + b.lo], v[a.hi * ny + b.hi], b.fraction);
  return std::lerp(low, high, a.fraction);
}

size_t Cell_types::bytes() const {
  const auto of
      = [](const auto& vector) { return vector.capacity() * sizeof(typename std::decay_t<decltype(vector)>::value_type); };
  size_t total = of(types_) + of(pins_) + of(arcs_) + of(arc_tables_) + of(caps_) + of(max_cap_) + of(max_tran_) + of(tables_)
                 + of(axes_) + of(axis_values_) + of(values_);
  for (const auto& type : types_) {
    total += type.name.capacity();
  }
  for (const auto& pin : pins_) {
    total += pin.name.capacity();
  }
  return total;
}

}  // namespace livehd::timing::fast
