// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "function.hpp"
#include "liberty_dff.hpp"

namespace livehd::domplace {

// One drive variant's output storage: a register of the reference family.
// Timing values are Liberty samples (Dff_cell); absent data stays nullopt.
struct Domino_register {
  std::string                          name;
  double                               area = 0;  // the full register, charged once per variant
  std::optional<double>                clk_to_q_ps, setup_ps, hold_ps;
  std::optional<liberty::Output_drive> drive;
  bool                                 q_inverted = false;
};

// The named technology cells the provisional area model is expressed in. Each
// pick records how it was made: "explicit" (named by an option) or
// "liberty-function" (the smallest NOT cell; the register-mapping pick).
struct Domino_reference {
  std::string                  inverter_source, register_source;
  liberty::Comb_cell           inverter;
  std::vector<Domino_register> registers;  // the drive ladder from the base register up, area order
  std::string                  corner;     // the Liberty library group holding every reference cell
  uint32_t                     aux_nonregister = 4;
};

// Resolve the reference inverter and register family from the
// whitespace-separated Liberty files. Empty names pick by Liberty function: the
// smallest-area single-input NOT cell, and find_dff_cell's register-mapping pick.
// The register ladder (find_dff_ladder) starts at the base register and keeps
// at most `variants` rungs. A missing cell, a missing area or cells from
// different library groups return nullopt with the reason in `why`; nothing
// defaults to zero.
std::optional<Domino_reference> resolve_domino_reference(const std::string& liberty_files, std::string_view inverter,
                                                         std::string_view reg, uint32_t aux, uint32_t variants, std::string* why);

// 2x = N_PDN + aux, the one rule behind the inverter-equivalent count x: the
// literal devices of the formula (metrics.transistors, repeats included) and
// the domino-only devices outside the register. Integral, so a basis area times
// it stays exact.
inline int64_t domino_proxy_devices(const usyn::Gate_formula::Metrics& metrics, const Domino_reference& reference) {
  return int64_t{metrics.transistors} + reference.aux_nonregister;
}

struct Domino_area {
  double x = 0, a_not = 0, a_register = 0, a_domino = 0;
};
// A_domino = x * A_NOT + A_register(variant), x = domino_proxy_devices / 2, on
// the Liberty basis. The PDN term is shared by every variant; the drive
// changes only the register area.
Domino_area domino_area(const usyn::Gate_formula::Metrics& metrics, const Domino_reference& reference, size_t variant);

}  // namespace livehd::domplace
