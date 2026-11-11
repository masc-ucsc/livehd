// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "pass/liberty/timing_model.hpp"

// The cell side of the fast timing engine (todo 2q-fasttimer, step A): the cells of N Liberty corners
// as one table of types, with pooled single-precision NLDM tables.
//
// A type has pins, arcs and, for every corner, a pin capacitance and the tables of each arc. The tables of
// all corners and all arcs live in one pool and share their axes (the axes of a library are a handful of
// distinct vectors). A lookup is the interpolation of liberty::timing::Table::lookup, evaluated in float:
// the same cell, the same slew and load give the same delay to within float rounding.
//
// Corners are matched by cell name, and inside a cell by pin name and by (related pin, timing type,
// condition) of the arc. The first corner is the reference that decides the order of types, pins and
// arcs. A cell that is missing from another corner, or an arc that is, is a diagnostic and that corner
// uses the reference corner's numbers for it.
namespace livehd::timing::fast {

inline constexpr uint32_t no_index = std::numeric_limits<uint32_t>::max();

enum class Variable : uint8_t { none, input_slew, output_load, related_slew, constrained_slew };
enum class Direction : uint8_t { input, output, inout, internal };
enum class Sense : uint8_t { positive, negative, non_unate };
// What an arc is to the engine. `launch_rise` is a clock-to-output arc of a rising-edge element; the checks
// are on the constrained (data) pin with the clock as the related pin. Recovery and removal are timed as
// setup and hold. Arcs the engine does not time (pulse width, no-change, skew, falling-edge launch and
// capture, unsupported tables) are `ignored` and counted.
enum class Arc_kind : uint8_t { combinational, launch_rise, setup_rise, hold_rise, ignored };

struct Query {
  float input_slew = 0, output_load = 0, related_slew = 0, constrained_slew = 0;
};

struct Pin_type {
  std::string name;
  Direction   direction = Direction::input;
  bool        clock     = false;
};

struct Arc {
  uint16_t from = 0, to = 0;  // local pin indices; for a check, from is the clock and to the data pin
  Sense    sense  = Sense::non_unate;
  Arc_kind kind   = Arc_kind::combinational;
  // First of corners * 6 table ids: [corner][delay, slew, constraint][rise, fall]; no_index when absent.
  uint32_t tables = no_index;
  uint32_t when   = no_index;  // index into Cell_types::conditions(); no_index without a `when`
};

struct Type {
  std::string name;
  float       area       = 0;
  uint32_t    first_pin  = 0;
  uint32_t    first_arc  = 0;
  uint16_t    pin_count  = 0;
  uint16_t    arc_count  = 0;
  bool        sequential = false;
  // Cells the engine does not time; their pins are reported as unsupported instead of timed.
  bool        latch = false, memory = false, clock_gate = false;
};

struct Pooled_table {
  uint32_t x_axis = no_index, y_axis = no_index;  // no_index: a scalar axis
  uint32_t values = 0;                            // offset into the value pool, row-major x then y
  Variable x_var = Variable::none, y_var = Variable::none;
  uint16_t nx = 1, ny = 1;
};

class Cell_types {
public:
  static constexpr uint32_t input_port_type  = 0;  // one output pin "o": a primary input
  static constexpr uint32_t output_port_type = 1;  // one input pin "i": a primary output

  // `names` are the corner names, same order as `corners`; both non-empty and of equal size.
  static Cell_types build(std::span<const liberty::timing::Library* const> corners, std::span<const std::string> names);

  [[nodiscard]] size_t               corners() const { return corner_names_.size(); }
  [[nodiscard]] const std::string&   corner_name(size_t corner) const { return corner_names_[corner]; }
  [[nodiscard]] size_t               types() const { return types_.size(); }
  [[nodiscard]] const Type&          type(uint32_t index) const { return types_[index]; }
  [[nodiscard]] uint32_t             find(std::string_view cell) const;
  [[nodiscard]] uint32_t             find_pin(uint32_t type, std::string_view pin) const;
  [[nodiscard]] const Pin_type&      pin(uint32_t type, uint32_t local) const { return pins_[types_[type].first_pin + local]; }
  [[nodiscard]] std::span<const Arc> arcs(uint32_t type) const {
    return {arcs_.data() + types_[type].first_arc, types_[type].arc_count};
  }
  // The arcs that end on a pin (delay arcs into an output pin, checks on a data pin). They are contiguous
  // in arcs(type), so the index of one inside the type is its offset from arcs(type).data().
  [[nodiscard]] std::span<const Arc> arcs_into(uint32_t type, uint32_t local) const {
    const auto pin = types_[type].first_pin + local;
    return {arcs_.data() + pin_first_arc_[pin], pin_arc_count_[pin]};
  }
  [[nodiscard]] size_t arc_total() const { return arcs_.size(); }

  // Input pin capacitance in fF, `transition` 0 rise, 1 fall; 0 for a pin without one.
  [[nodiscard]] float pin_cap(uint32_t type, uint32_t local, size_t corner, unsigned transition) const {
    return caps_[((types_[type].first_pin + local) * corners() + corner) * 2 + transition];
  }
  // Electrical limits of a pin in a corner; infinity when the library gives no_index.
  [[nodiscard]] float max_capacitance(uint32_t type, uint32_t local, size_t corner) const {
    return max_cap_[(types_[type].first_pin + local) * corners() + corner];
  }
  [[nodiscard]] float max_transition(uint32_t type, uint32_t local, size_t corner) const {
    return max_tran_[(types_[type].first_pin + local) * corners() + corner];
  }
  // Table id of an arc: group 0 delay, 1 slew, 2 constraint; transition 0 rise, 1 fall; no_index when absent.
  [[nodiscard]] uint32_t table(const Arc& arc, size_t corner, unsigned group, unsigned transition) const {
    return arc_tables_[arc.tables + (corner * 3 + group) * 2 + transition];
  }
  [[nodiscard]] float lookup(uint32_t table, const Query& query) const;

  [[nodiscard]] const std::string&              condition(uint32_t index) const { return conditions_[index]; }
  // What the build left out or substituted; one line each, never silent.
  [[nodiscard]] const std::vector<std::string>& diagnostics() const { return diagnostics_; }
  [[nodiscard]] size_t                          ignored_arcs() const { return ignored_arcs_; }
  // Heap bytes held by the tables, for the memory report.
  [[nodiscard]] size_t                          bytes() const;
  [[nodiscard]] size_t                          table_count() const { return tables_.size(); }
  [[nodiscard]] size_t                          axis_count() const { return axes_.size(); }
  [[nodiscard]] size_t                          value_count() const { return values_.size(); }

private:
  struct Axis {
    uint32_t offset = 0, size = 0;
  };
  [[nodiscard]] static float coordinate(Variable variable, const Query& query);
  uint32_t                   add_table(const liberty::timing::Table& table);
  uint32_t                   add_axis(const std::vector<double>& values);

  std::vector<std::string>                  corner_names_;
  std::vector<Type>                         types_;
  std::vector<Pin_type>                     pins_;
  std::vector<Arc>                          arcs_;
  std::vector<uint32_t>                     pin_first_arc_;  // per pin: index into arcs_
  std::vector<uint16_t>                     pin_arc_count_;
  std::vector<uint32_t>                     arc_tables_;
  std::vector<float>                        caps_, max_cap_, max_tran_;
  std::vector<Pooled_table>                 tables_;
  std::vector<Axis>                         axes_;
  std::vector<float>                        axis_values_, values_;
  std::vector<std::string>                  conditions_, diagnostics_;
  std::unordered_map<std::string, uint32_t> by_name_;
  std::unordered_map<std::string, uint32_t> axis_by_content_;
  size_t                                    ignored_arcs_ = 0;
};

}  // namespace livehd::timing::fast
