// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cell_types.hpp"

// The connectivity side of the fast timing engine (todo 2q-fasttimer, step B): a mapped netlist as numbers.
//
// A cell is an index with a type; its pins are the consecutive indices from first_pin(cell), in the order
// of its type. A net is an index with one driver pin and a list of sink pins. Primary inputs and outputs
// are cells of the two port types, so the engine has one kind of object to walk. Names exist only on ports,
// for the constraints; a caller that has a graph keeps its own handle per cell and pin index.
//
// `finalize` fixes the topology and puts the pins in levelized order: every pin comes after the pins it
// depends on (the driver of its net; the input pins that feed its output through a delay arc). The engine
// walks that array front to back and back to front. A combinational loop is cut at one pin (the arcs into
// that pin from pins not yet ordered are dropped); the cut pins are listed, never cut silently.
namespace livehd::timing::fast {

struct Port {
  uint32_t    cell = 0;
  std::string name;
  bool        input = true;
};

class Netlist {
public:
  explicit Netlist(const Cell_types& types) : types_(&types) { first_pin_.push_back(0); }

  // Building. A pin has at most one driver; a second one is reported by `diagnostics` and ignored.
  uint32_t add_cell(uint32_t type);
  uint32_t add_input_port(std::string name);
  uint32_t add_output_port(std::string name);
  void     connect(uint32_t driver_pin, uint32_t sink_pin);
  void     finalize();

  [[nodiscard]] const Cell_types& types() const { return *types_; }
  [[nodiscard]] size_t            cells() const { return cell_type_.size(); }
  [[nodiscard]] size_t            pins() const { return first_pin_.back(); }
  [[nodiscard]] size_t            nets() const { return net_driver_.size(); }
  [[nodiscard]] uint32_t          type_of(uint32_t cell) const { return cell_type_[cell]; }
  [[nodiscard]] uint32_t          first_pin(uint32_t cell) const { return first_pin_[cell]; }
  [[nodiscard]] uint32_t          pin(uint32_t cell, uint32_t local) const { return first_pin_[cell] + local; }
  [[nodiscard]] uint32_t          pin(uint32_t cell, std::string_view name) const;
  [[nodiscard]] uint32_t          cell_of(uint32_t pin) const { return pin_cell_[pin]; }
  [[nodiscard]] uint32_t          local_of(uint32_t pin) const { return pin - first_pin_[pin_cell_[pin]]; }
  [[nodiscard]] const Pin_type&   pin_type(uint32_t pin) const { return types_->pin(cell_type_[pin_cell_[pin]], local_of(pin)); }
  [[nodiscard]] const std::vector<Port>& ports() const { return ports_; }

  // After finalize.
  [[nodiscard]] uint32_t                  net_of(uint32_t pin) const { return pin_net_[pin]; }
  [[nodiscard]] uint32_t                  driver_of(uint32_t net) const { return net_driver_[net]; }
  [[nodiscard]] std::span<const uint32_t> sinks_of(uint32_t net) const {
    return {net_sinks_.data() + net_sink_start_[net], net_sink_start_[net + 1] - net_sink_start_[net]};
  }
  // True for the driver pin of its net.
  [[nodiscard]] bool is_driver(uint32_t pin) const { return pin_net_[pin] != no_index && net_driver_[pin_net_[pin]] == pin; }
  // The pins in dependency order, and the pins at which a combinational loop was cut.
  [[nodiscard]] std::span<const uint32_t>       order() const { return order_; }
  [[nodiscard]] const std::vector<uint32_t>&    loop_pins() const { return loop_pins_; }
  [[nodiscard]] const std::vector<std::string>& diagnostics() const { return diagnostics_; }
  // Pins of cells the engine does not time (latches, memories, clock gates): never reached, never an endpoint.
  [[nodiscard]] bool                            untimed(uint32_t pin) const { return untimed_[pin] != 0; }
  [[nodiscard]] size_t                          untimed_cells() const { return untimed_cells_; }
  [[nodiscard]] size_t                          bytes() const;

private:
  const Cell_types*                          types_;
  std::vector<uint32_t>                      cell_type_, first_pin_, pin_cell_;
  std::vector<Port>                          ports_;
  std::vector<std::pair<uint32_t, uint32_t>> connections_;  // (driver, sink) while building
  std::vector<uint32_t>                      pin_net_, net_driver_, net_sink_start_, net_sinks_, order_, loop_pins_;
  std::vector<uint8_t>                       untimed_;
  std::vector<std::string>                   diagnostics_;
  size_t                                     untimed_cells_ = 0;
};

}  // namespace livehd::timing::fast
