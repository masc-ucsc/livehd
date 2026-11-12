// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "netlist.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>

namespace livehd::timing::fast {

uint32_t Netlist::add_cell(uint32_t type) {
  if (type >= types_->types()) {
    throw std::invalid_argument("Netlist::add_cell: unknown cell type");
  }
  const auto cell = static_cast<uint32_t>(cell_type_.size());
  cell_type_.push_back(type);
  const auto count = types_->type(type).pin_count;
  pin_cell_.insert(pin_cell_.end(), count, cell);
  first_pin_.push_back(first_pin_.back() + count);
  return cell;
}

uint32_t Netlist::add_input_port(std::string name) {
  const auto cell = add_cell(Cell_types::input_port_type);
  ports_.push_back({cell, std::move(name), true});
  return cell;
}

uint32_t Netlist::add_output_port(std::string name) {
  const auto cell = add_cell(Cell_types::output_port_type);
  ports_.push_back({cell, std::move(name), false});
  return cell;
}

uint32_t Netlist::pin(uint32_t cell, std::string_view name) const {
  const auto local = types_->find_pin(cell_type_[cell], name);
  return local == no_index ? no_index : first_pin_[cell] + local;
}

void Netlist::retype(uint32_t cell, uint32_t type) {
  const auto& from = types_->type(cell_type_[cell]);
  const auto& to   = types_->type(type);
  bool        same = from.pin_count == to.pin_count && from.sequential == to.sequential && from.latch == to.latch
              && from.memory == to.memory && from.clock_gate == to.clock_gate;
  for (uint32_t local = 0; same && local < from.pin_count; ++local) {
    const auto& a = types_->pin(cell_type_[cell], local);
    const auto& b = types_->pin(type, local);
    same          = a.name == b.name && a.direction == b.direction && a.clock == b.clock;
  }
  const auto arcs_from = types_->arcs(cell_type_[cell]), arcs_to = types_->arcs(type);
  same = same && arcs_from.size() == arcs_to.size();
  for (size_t i = 0; same && i < arcs_from.size(); ++i) {
    same = arcs_from[i].from == arcs_to[i].from && arcs_from[i].to == arcs_to[i].to && arcs_from[i].kind == arcs_to[i].kind
           && arcs_from[i].sense == arcs_to[i].sense;
  }
  if (!same) {
    throw std::invalid_argument(std::format("Netlist::retype: {} and {} do not have the same interface", from.name, to.name));
  }
  cell_type_[cell] = type;
}

void Netlist::connect(uint32_t driver_pin, uint32_t sink_pin) {
  if (driver_pin >= pins() || sink_pin >= pins()) {
    throw std::invalid_argument("Netlist::connect: pin out of range");
  }
  connections_.emplace_back(driver_pin, sink_pin);
}

void Netlist::finalize() {
  const size_t pin_count = pins();
  pin_net_.assign(pin_count, no_index);
  net_driver_.clear();
  net_sink_start_.clear();
  net_sinks_.clear();

  // Nets in driver order, sinks in pin order: the same netlist always gives the same arrays.
  std::sort(connections_.begin(), connections_.end());
  connections_.erase(std::unique(connections_.begin(), connections_.end()), connections_.end());
  for (const auto& [driver, sink] : connections_) {
    if (pin_net_[sink] != no_index) {
      diagnostics_.push_back(std::format("pin {} has more than one driver: the first is kept", sink));
      continue;
    }
    if (pin_net_[driver] == no_index) {
      pin_net_[driver] = static_cast<uint32_t>(net_driver_.size());
      net_driver_.push_back(driver);
      net_sink_start_.push_back(static_cast<uint32_t>(net_sinks_.size()));
    } else if (net_driver_[pin_net_[driver]] != driver) {
      // A pin is the driver of a net or a sink of one, not both (no bidirectional pins).
      diagnostics_.push_back(std::format("pin {} is both a driver and a sink: the connection to {} is ignored", driver, sink));
      continue;
    }
    // Drivers are sorted, so the net of this driver is the last one made.
    pin_net_[sink] = pin_net_[driver];
    net_sinks_.push_back(sink);
  }
  net_sink_start_.push_back(static_cast<uint32_t>(net_sinks_.size()));
  connections_.clear();
  connections_.shrink_to_fit();

  // Cells the engine does not time.
  untimed_.assign(pin_count, 0);
  untimed_cells_ = 0;
  for (uint32_t cell = 0; cell < cells(); ++cell) {
    const auto& type = types_->type(cell_type_[cell]);
    if (type.latch || type.memory || type.clock_gate) {
      ++untimed_cells_;
      for (uint32_t p = first_pin_[cell]; p < first_pin_[cell + 1]; ++p) {
        untimed_[p] = 1;
      }
    }
  }
  if (untimed_cells_ != 0) {
    diagnostics_.push_back(std::format("{} cells (latches, memories, clock gates) are not timed", untimed_cells_));
  }

  // Levelization (Kahn): the edges are net edges (driver -> sink) and the delay arcs inside a cell.
  const auto timed_arc = [](const Arc& arc) { return arc.kind == Arc_kind::combinational || arc.kind == Arc_kind::launch_rise; };
  std::vector<uint32_t> waiting(pin_count, 0);
  for (uint32_t p = 0; p < pin_count; ++p) {
    if (pin_net_[p] != no_index && net_driver_[pin_net_[p]] != p) {
      ++waiting[p];
    }
  }
  for (uint32_t cell = 0; cell < cells(); ++cell) {
    for (const auto& arc : types_->arcs(cell_type_[cell])) {
      if (timed_arc(arc)) {
        ++waiting[first_pin_[cell] + arc.to];
      }
    }
  }
  order_.clear();
  order_.reserve(pin_count);
  std::vector<uint8_t> queued(pin_count, 0);
  for (uint32_t p = 0; p < pin_count; ++p) {
    if (waiting[p] == 0) {
      order_.push_back(p);
      queued[p] = 1;
    }
  }
  loop_pins_.clear();
  uint32_t cursor = 0;  // every pin below it is queued
  // The pins in dependency order. When none is ready the rest sit on combinational loops: one pin of the
  // loop is released as if its remaining fan-in were done (the arcs into it from pins not yet ordered are
  // cut), and the walk goes on. The pin is the one with the fewest arcs still waiting among the first few
  // that wait, so a loop is cut where it has been entered most; the rule only depends on the netlist.
  for (size_t head = 0;;) {
    if (head == order_.size()) {
      while (cursor < pin_count && queued[cursor]) {
        ++cursor;
      }
      if (cursor == pin_count) {
        break;
      }
      uint32_t best = cursor;
      for (uint32_t p = cursor, seen = 0; p < pin_count && seen < 64; ++p) {
        if (!queued[p]) {
          ++seen;
          if (waiting[p] < waiting[best]) {
            best = p;
          }
        }
      }
      loop_pins_.push_back(best);
      waiting[best] = 0;
      queued[best]  = 1;
      order_.push_back(best);
    }
    const uint32_t p = order_[head++];
    if (pin_net_[p] != no_index && net_driver_[pin_net_[p]] == p) {
      for (const auto sink : sinks_of(pin_net_[p])) {
        if (!queued[sink] && --waiting[sink] == 0) {
          order_.push_back(sink);
          queued[sink] = 1;
        }
      }
    }
    const uint32_t cell  = pin_cell_[p];
    const uint32_t local = p - first_pin_[cell];
    for (const auto& arc : types_->arcs(cell_type_[cell])) {
      if (timed_arc(arc) && arc.from == local) {
        const uint32_t out = first_pin_[cell] + arc.to;
        if (!queued[out] && --waiting[out] == 0) {
          order_.push_back(out);
          queued[out] = 1;
        }
      }
    }
  }
  if (!loop_pins_.empty()) {
    diagnostics_.push_back(
        std::format("{} combinational loops were cut, at one pin each: the timing through them is approximate", loop_pins_.size()));
  }
}

size_t Netlist::bytes() const {
  const auto of
      = [](const auto& vector) { return vector.capacity() * sizeof(typename std::decay_t<decltype(vector)>::value_type); };
  return of(cell_type_) + of(first_pin_) + of(pin_cell_) + of(pin_net_) + of(net_driver_) + of(net_sink_start_) + of(net_sinks_)
         + of(order_) + of(untimed_);
}

}  // namespace livehd::timing::fast
