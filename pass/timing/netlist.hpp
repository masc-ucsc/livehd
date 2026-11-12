// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
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
  // After finalize: the same cell with another type of the same interface (a resize). The pins keep their
  // indices, so the order and the nets stay valid; it throws when the new type has other pins (names, order),
  // other arc connectivity or another untimed class, and the netlist is then unchanged.
  void     retype(uint32_t cell, uint32_t type);

  // ---- What-if topology edits after finalize. They are undone in reverse order by edit_rollback; `order()`
  // does not know the new pins (the engine keeps its own keys for an edited netlist), and the sinks of an
  // edited net are kept apart from the compact arrays.
  [[nodiscard]] size_t edit_mark() const { return edit_log_.size(); }
  void                 edit_rollback(size_t mark);
  // A new cell without connections; its pins come after the existing ones.
  uint32_t             append_cell(uint32_t type);
  // Make the net driven by `driver` have exactly `sinks` (a net is made when the driver has none). A sink that
  // was on another net leaves it, and a former sink that is not listed has no net. The nets that lost a sink
  // other than the returned one are appended to `touched`. A pin is either a driver or a sink.
  uint32_t             set_net_sinks(uint32_t driver, std::span<const uint32_t> sinks, std::vector<uint32_t>* touched);

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
    if (!edited_.empty()) {
      if (const auto it = edited_.find(net); it != edited_.end()) {
        return it->second;
      }
    }
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

  struct Edit {
    enum class Kind : uint8_t { append_cell, net_created, pin_net, net_sinks } kind;
    uint32_t              a = 0, b = 0;
    bool                  had = false;
    std::vector<uint32_t> sinks;
  };
  std::vector<Edit>                                    edit_log_;
  std::unordered_map<uint32_t, std::vector<uint32_t>> edited_;
  void set_pin_net(uint32_t pin, uint32_t net);
  void set_edited_sinks(uint32_t net, std::vector<uint32_t> sinks);
};

}  // namespace livehd::timing::fast
