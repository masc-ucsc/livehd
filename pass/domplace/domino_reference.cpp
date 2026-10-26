// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "domino_reference.hpp"

#include <algorithm>
#include <format>
#include <sstream>
#include <tuple>

#include "file_utils.hpp"
#include "pass/liberty/timing_model.hpp"

namespace livehd::domplace {
namespace {
// cell_truth repeats a one-input table over 64 bits: f(0)=1, f(1)=0.
constexpr uint64_t kNotTruth = 0x5555555555555555ULL;

bool is_not(const liberty::Comb_cell& cell) { return cell.inputs.size() == 1 && liberty::cell_truth(cell) == kNotTruth; }

// Dff_cell stores an absent timing table as zero.
std::optional<double> sampled(double value) { return value != 0 ? std::optional<double>{value} : std::nullopt; }
}  // namespace

std::optional<Domino_reference> resolve_domino_reference(const std::string& liberty_files, std::string_view inverter,
                                                         std::string_view reg, uint32_t aux, uint32_t variants, std::string* why) {
  const auto refuse = [&](std::string reason) -> std::optional<Domino_reference> {
    if (why) {
      *why = std::move(reason);
    }
    return std::nullopt;
  };
  if (aux > 64) {
    return refuse(std::format("domino_aux {} exceeds 64 devices", aux));
  }
  if (variants == 0 || variants > 3) {
    return refuse(std::format("domino_variants {} is outside 1..3", variants));
  }
  Domino_reference reference;
  reference.aux_nonregister = aux;
  reference.inverter_source = inverter.empty() ? "liberty-function" : "explicit";
  reference.register_source = reg.empty() ? "liberty-function" : "explicit";

  const liberty::Comb_cell* pick  = nullptr;
  const auto                cells = liberty::scan_comb_cells(liberty_files);
  for (const auto& cell : cells) {
    if (!inverter.empty()) {
      if (cell.name == inverter) {
        pick = &cell;
      }
    } else if (is_not(cell) && !cell.physical_barrier
               && (!pick || std::tie(cell.area, cell.name) < std::tie(pick->area, pick->name))) {
      pick = &cell;
    }
  }
  // The inventory admits only cells with a positive area.
  if (!pick || !is_not(*pick)) {
    return refuse(inverter.empty()
                      ? std::string{"no single-input NOT cell with a Liberty area"}
                      : std::format("domino_inverter '{}' is not a single-input NOT cell with a Liberty area", inverter));
  }
  reference.inverter = *pick;

  const auto base = liberty::find_dff_cell(liberty_files, reg);
  if (!base) {
    return refuse(reg.empty() ? std::string{"no plain positive-edge D flip-flop in the Liberty"}
                              : std::format("domino_register '{}' is not a plain positive-edge D flip-flop", reg));
  }
  const auto ladder = liberty::find_dff_ladder(liberty_files, *base);
  const auto drives = liberty::scan_output_drives(liberty_files);
  for (auto it = std::find_if(ladder.begin(), ladder.end(), [&](const auto& c) { return c.name == base->name; });
       it != ladder.end() && reference.registers.size() < variants;
       ++it) {
    if (!(it->area > 0)) {
      return refuse(std::format("reference register {} has no Liberty area", it->name));
    }
    Domino_register r;
    r.name        = it->name;
    r.d_pin       = it->d_pin;
    r.clk_pin     = it->clk_pin;
    r.q_pin       = it->q_pin;
    r.area        = it->area;
    r.clk_to_q_ps = sampled(it->clk_to_q_ps);
    r.setup_ps    = sampled(it->setup_ps);
    r.q_inverted  = it->q_inverted;
    // Dff_cell carries no hold value; hold_ps stays absent until characterized.
    for (const auto& drive : drives) {
      if (drive.name == it->name && drive.output == it->q_pin) {
        r.drive = drive;
      }
    }
    reference.registers.push_back(std::move(r));
  }
  if (reference.registers.empty()) {
    return refuse(std::format("reference register {} is missing from its own drive ladder", base->name));
  }

  // All references must come from one library group (one corner). The timing
  // parser rejects a cell name repeated across groups, so the lookup is exact.
  std::string        text;
  std::istringstream list(liberty_files);
  for (std::string path; list >> path;) {
    const auto contents = file_utils::read_file(path);
    if (!contents) {
      return refuse("cannot read Liberty file " + path);
    }
    text += *contents;
    text += '\n';
  }
  liberty::timing::Library library;
  try {
    library = liberty::timing::parse(text);
  } catch (const std::exception& error) {
    return refuse(std::string{"cannot parse the Liberty timing model: "} + error.what());
  }
  const auto group = [&](const std::string& name) {
    const auto it = library.cells.find(name);
    return it == library.cells.end() ? std::string{} : it->second.library;
  };
  reference.corner = group(reference.inverter.name);
  for (auto& r : reference.registers) {
    if (reference.corner.empty() || group(r.name) != reference.corner) {
      return refuse(std::format("reference cells {} and {} are not in one Liberty library group", reference.inverter.name, r.name));
    }
    // The clock load a domino stage keeps from its register, and the D load
    // its output inverter drives; absent stays absent.
    const auto& pins = library.cells.at(r.name).pins;
    for (auto [pin, target] : {
             std::pair{r.clk_pin, &r.clock_ff},
             std::pair{  r.d_pin,     &r.d_ff}
    }) {
      if (const auto found = pins.find(pin); found != pins.end()) {
        for (const auto& value : found->second.capacitance_ff) {
          if (value && (!*target || *value > **target)) {
            *target = value;
          }
        }
      }
    }
  }
  reference.inverter_timing = library.cells.at(reference.inverter.name);
  return reference;
}

Domino_area domino_area(const usyn::Gate_formula::Metrics& metrics, const Domino_reference& reference, size_t variant) {
  Domino_area area;
  area.x          = static_cast<double>(domino_proxy_devices(metrics, reference)) / 2;
  area.a_not      = reference.inverter.area;
  area.a_register = reference.registers.at(variant).area;
  // pct / 100.0 is exactly 1 at the default, so the default area is unchanged.
  area.a_domino   = area.x * area.a_not * (reference.pair_area_pct / 100.0) + area.a_register;
  return area;
}

}  // namespace livehd::domplace
