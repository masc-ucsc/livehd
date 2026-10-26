// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "design_synth.hpp"
#include "domino_reference.hpp"
#include "physical_select.hpp"

namespace livehd::domplace {

struct Physical_report_inputs {
  const Domino_reference* reference = nullptr;
  const Select_options*   options   = nullptr;
  std::string             cmos_register;  // the CMOS map's register pick
  double                  cmos_register_area = 0;
  uint64_t                      model_work         = 0;  // Liberty cost-model read charged to the profile
  // The published model bundle (directory name beside the report), if any.
  std::string              bundle;
  uint64_t                 bundle_macros = 0;
  std::vector<std::string> bundle_diagnostics;
  std::map<std::string, double> footprints;  // the bundle's rounded footprint (um^2) by cell
};

// The `physical` object of pass.usyn's report for the domino_provisional
// profile: the settings and named reference cells, totals (eligible,
// converted, retained by reason, masters), and one row per frozen state of
// every region with its decision, reason, master, PDN metrics, x, area terms
// and rails. Areas are Liberty-basis proxies, not characterized PPA; the
// rounded footprint is the bundle's LEF basis, reported separately.
std::string physical_report(const usyn::Logical_design& design, const Physical_report_inputs& inputs);

}  // namespace livehd::domplace
