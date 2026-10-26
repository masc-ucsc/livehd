// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "design_synth.hpp"
#include "domino_cell.hpp"
#include "domino_geometry.hpp"
#include "domino_reference.hpp"
#include "stage_model.hpp"

namespace livehd::domplace {

// The provisional model bundle written beside pass.usyn's report as
// <qor>.domino/ (2d-domplace C5). Placement reads the LEF, LEC reads the lg
// models, external simulators read the Verilog models; the sidecar records the
// units, corner, technology, reference cells, coefficients, masters and the
// original state that owns every instance.
inline constexpr uint32_t         kBundleSchema      = 1;
inline constexpr std::string_view kBundleLef         = "domino_cells.lef";
inline constexpr std::string_view kBundleSidecar     = "domino_models.json";
inline constexpr std::string_view kBundleVerilog     = "domino_models.v";
inline constexpr std::string_view kBundleModels      = "models";
inline constexpr std::string_view kBundleAccess      = "row_access.json";
// Version of the stand-in access rules check_domino_access applies.
inline constexpr uint32_t         kAccessRuleVersion = 1;

// One converted original state: the explicit owner of a domino instance.
struct Bundle_instance {
  std::string region;           // definition region (module)
  std::string state;            // original register bit/stage name
  std::string cell;             // the instantiated master interface (variant 0)
  uint32_t    state_index = 0;  // frozen state index == state_bits index
};
struct Bundle_design {
  std::vector<Domino_master>   masters;  // unique, ordered by base name
  std::vector<Bundle_instance> instances;
};
// Every converted state of the design: its master, rebuilt from the selection's
// checked positive formula (so it names the same cell), and its owner. nullopt
// only when the work runs out.
std::optional<Bundle_design> collect_domino_design(const usyn::Logical_design& design, usyn::Budget& work);

struct Bundle_inputs {
  const Domino_reference*                          reference = nullptr;
  Geometry_inputs                                  geometry;    // its footprint's inverter must be reference->inverter
  std::string                                      technology;  // the route policy's source (domino_route_policy)
  Stage_model                                      stage;       // its clock; coefficients the bundle cannot derive stay null
  // Hand-set evaluate and precharge delays in FO4 of the reference inverter;
  // absent values stay null in the sidecar with a diagnostic.
  std::optional<double>                            eval_fo4, precharge_fo4;
  // Input digests recorded for provenance, e.g. ("lef", "<path> sha256:<hex>").
  std::vector<std::pair<std::string, std::string>> provenance;
};

struct Bundle_result {
  bool                     ok = false;
  std::string              reason;       // why the bundle was refused
  std::vector<std::string> diagnostics;  // labeled estimates, null coefficients, unverified pin access
  size_t                   macros = 0;   // LEF macros: masters x register variants
  std::map<std::string, double> footprints;   // rounded LEF footprint (um^2) by macro
};

// Write out_dir/{domino_cells.lef, domino_models.json, domino_models.v,
// row_access.json, models/}: every master in every variant of the reference
// register ladder (template t0, sizing disabled so only variant 0 is
// instantiated). out_dir is replaced; the caller writes into a staging
// directory and publishes it whole. Each variant's abstract must pass
// check_domino_access; a technology rule the pin-access engine cannot verify
// is recorded as unverified, not as a pass. Any failure refuses the bundle.
// Regeneration from the same inputs gives identical bytes.
Bundle_result generate_domino_bundle(const Bundle_design& design, const Bundle_inputs& inputs,
                                     const std::filesystem::path& out_dir);

// The placement view of a published bundle.
struct Domino_sidecar {
  std::filesystem::path                                      lef;
  physical::Row_access_convention                            row_access;
  std::map<std::string, std::array<physical::Row_access, 2>> templates;  // local row access by LEF macro
  size_t                                                     instances = 0;
};
// nullopt with the reason in `why` for a missing or malformed bundle.
std::optional<Domino_sidecar> read_domino_sidecar(const std::filesystem::path& dir, std::string* why);

}  // namespace livehd::domplace
