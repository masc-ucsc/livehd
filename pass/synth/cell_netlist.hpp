// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// A mapped region as a backend hands it back (abc_cleanup.md section 5): cells
// of a technology library over the region's translation boundary (the Lnet's
// inputs, latches and outputs), with no backend object in sight. The region
// writer (region_writer.hpp) turns it into the region body.
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "hhds/graph.hpp"

namespace livehd::synth {

// One single-output cell of the technology library.
struct Cell_type {
  std::string              name;
  std::vector<std::string> inputs;  // pin order == fanin order
  std::string              output;
  double                   area  = 0;
  // Replicated 64-bit truth table over the inputs (bit x = f(x)) for at most
  // six pins; 0 for a wider cell.
  uint64_t                 truth = 0;
};

// The cell IO decl in an output library, find-or-create: pid k+1 is input
// pin k, then the output. Every producer of a cell instance (the region
// writer, a backend re-sizing cells in place) must agree on these port ids.
struct Cell_decl {
  std::shared_ptr<hhds::GraphIO> io;
  std::string                    name;
  std::string                    output_name;
  std::vector<std::string>       input_names;
};
Cell_decl declare_cell(hhds::GraphLibrary& outlib, const Cell_type& type);

class Cell_library {
public:
  // Types are added in the backend's library order: the inverting-twin choice
  // breaks area ties by that order.
  uint32_t                              add(Cell_type type);
  [[nodiscard]] const Cell_type&        type(uint32_t id) const { return types_[id]; }
  [[nodiscard]] size_t                  size() const { return types_.size(); }
  void                                  set_inverter(uint32_t id) { inverter_ = id; }
  [[nodiscard]] std::optional<uint32_t> inverter() const { return inverter_; }
  // A single-input non-inverting cell (truth 0xAA..): a buffer.
  [[nodiscard]] bool                    is_identity(uint32_t id) const;
  [[nodiscard]] bool                    is_inverter(uint32_t id) const;
  // The cheapest cell computing the complement of `id` over the same pins in
  // the same order (AND2 -> NAND2, AOI21 -> AO21), or nullopt.
  [[nodiscard]] std::optional<uint32_t> inverting_twin(uint32_t id) const;
  // The decl of `id` in `outlib`, cached per type.
  const Cell_decl&                      decl(hhds::GraphLibrary& outlib, uint32_t id) const;

private:
  std::vector<Cell_type>                                     types_;
  absl::flat_hash_map<std::pair<size_t, uint64_t>, uint32_t> twin_;  // (pins, truth) -> cheapest, first on ties
  std::optional<uint32_t>                                    inverter_;
  mutable std::vector<std::optional<Cell_decl>>              decls_;
  mutable hhds::GraphLibrary*                                decl_lib_ = nullptr;
};

struct Cell_netlist {
  static constexpr uint32_t kNone = std::numeric_limits<uint32_t>::max();
  enum class Kind : uint8_t { source, latch, cell };
  struct Signal {
    Kind     kind  = Kind::source;
    uint32_t index = 0;  // PI (in creation order), latch or cell index
  };
  struct Cell {
    uint32_t              type = 0;
    std::vector<uint32_t> fanins;  // signals, in pin order
    uint32_t              output = kNone;
  };
  enum class Init : uint8_t { none, zero, one, dont_care };
  struct Latch {
    uint32_t q    = kNone;  // its output signal
    uint32_t d    = kNone;  // the signal it samples (kNone: undriven)
    Init     init = Init::none;
  };
  std::vector<Signal>   signals;
  std::vector<uint32_t> sources;  // the PI signals, in creation order
  // Cell i and latch k name their instances g<i>_<cell> and g<k>_<dff cell>:
  // names never depend on a backend's object numbering.
  std::vector<Cell>     cells;
  std::vector<Latch>    latches;  // in the order the translation created them, unless reshaped
  std::vector<uint32_t> outputs;  // one signal per PO, in creation order (kNone: undriven)

  // Readers of each signal as the backend counts them: every cell fanin slot,
  // output and latch input that reads it.
  [[nodiscard]] std::vector<uint32_t> readers() const;
  // Readers that are cell fanins only.
  [[nodiscard]] std::vector<uint32_t> cell_readers() const;
};

}  // namespace livehd::synth
