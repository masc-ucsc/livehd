// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "mapping_wire.hpp"

#include <algorithm>
#include <limits>
namespace livehd::synth::wire {
std::string encode_request(const Mapping_request& r) {
  Writer w;
  w.text(r.library);
  w.number(r.inputs.size());
  for (const auto& input : r.inputs) {
    w.text(input.name);
    w.text(input.driving_cell);
    w.real(input.arrival_ps);
  }
  w.number(r.terms.size());
  for (const auto& term : r.terms) {
    w.number(term.size());
    for (auto port : term) {
      w.number(port);
    }
  }
  w.number(r.inverter);
  w.real(r.output_load_ff);
  w.real(r.required_ps);
  w.number(r.proof_conflicts);
  w.number(r.max_cells);
  return std::move(w.data);
}

std::string encode_fragment(const Mapped_fragment& f) {
  Writer w;
  w.text(f.reason);
  w.number(f.inputs);
  w.number(f.output);
  w.real(f.area);
  w.real(f.output_arrival_ps);
  w.text(f.output_environment.name);
  w.text(f.output_environment.driving_cell);
  w.real(f.output_environment.arrival_ps);
  w.number(f.input_loads_ff.size());
  for (auto load : f.input_loads_ff) {
    w.real(load);
  }
  w.number(f.cells.size());
  for (const auto& cell : f.cells) {
    if (cell.inputs.size() != cell.input_pins.size()) {
      throw std::runtime_error("invalid template pin correspondence");
    }
    w.text(cell.name);
    w.text(cell.output_pin);
    w.text(cell.cell_sop);
    w.real(cell.area);
    w.number(cell.inputs.size());
    for (size_t i = 0; i < cell.inputs.size(); ++i) {
      w.number(cell.inputs[i]);
      w.text(cell.input_pins.at(i));
    }
  }
  return std::move(w.data);
}

Mapped_fragment decode_fragment(std::string_view data) {
  Reader          r{data};
  Mapped_fragment f;
  f.reason                          = r.text();
  f.inputs                          = static_cast<uint32_t>(r.count(24));
  f.output                          = static_cast<Id>(r.count(std::numeric_limits<Id>::max()));
  f.area                            = r.real();
  f.output_arrival_ps               = r.real();
  f.output_environment.name         = r.text();
  f.output_environment.driving_cell = r.text();
  f.output_environment.arrival_ps   = r.real();
  const auto loads                  = r.count(f.inputs);
  for (size_t i = 0; i < loads; ++i) {
    f.input_loads_ff.push_back(r.real());
  }
  const auto cells = r.count(data.size() / 40);
  double     area  = 0;
  for (size_t i = 0; i < cells; ++i) {
    Mapped_cell cell;
    cell.name       = r.text();
    cell.output_pin = r.text();
    cell.cell_sop   = r.text();
    cell.area       = r.real();
    if (cell.area < 0) {
      throw std::runtime_error("invalid template area");
    }
    auto pins = r.count(data.size() / 16);
    for (size_t p = 0; p < pins; ++p) {
      const auto wire = r.number();
      if (wire >= f.inputs + i) {
        throw std::runtime_error("invalid template wire");
      }
      cell.inputs.push_back(static_cast<Id>(wire));
      cell.input_pins.push_back(r.text());
    }
    area += cell.area;
    f.cells.push_back(std::move(cell));
  }
  if (!r.data.empty() || f.output >= f.inputs + f.cells.size() || f.area < 0 || !std::isfinite(area)
      || std::abs(area - f.area) > 1e-8 * std::max(1.0, area)) {
    throw std::runtime_error("invalid template fragment");
  }
  f.status = Map_status::mapped;
  return f;
}

Mapping_request decode_request(std::string_view data) {
  Reader          r{data};
  Mapping_request request;
  request.library   = r.text();
  const auto inputs = r.count(24);
  for (size_t i = 0; i < inputs; ++i) {
    request.inputs.push_back({r.text(), r.text(), r.real()});
  }
  const auto terms = r.count(4096);
  for (size_t i = 0; i < terms; ++i) {
    std::vector<uint32_t> term;
    const auto            size = r.count(inputs);
    for (size_t j = 0; j < size; ++j) {
      term.push_back(r.count(inputs ? inputs - 1 : 0));
    }
    request.terms.push_back(std::move(term));
  }
  request.inverter        = r.count(1);
  request.output_load_ff  = r.real();
  request.required_ps     = r.real();
  request.proof_conflicts = r.count(std::numeric_limits<uint32_t>::max());
  request.max_cells       = r.count(std::numeric_limits<uint32_t>::max());
  if (!r.data.empty()) {
    throw std::runtime_error("trailing mapping request bytes");
  }
  return request;
}
}  // namespace livehd::synth::wire
