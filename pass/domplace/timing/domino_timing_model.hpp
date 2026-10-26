// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "domino_bundle.hpp"
#include "pass/liberty/timing_model.hpp"
#include "pass/place/late_timing.hpp"
#include "physical_emission.hpp"

// The provisional domino timing model (2d-domplace E): one characterized cell
// per bundle variant for the native late proxy. It lives outside the
// //pass/domplace:domplace_salt glob, so editing it never changes a bundle.
namespace livehd::domplace::timing {
namespace late = placement::late;
// The pin names every domino master declares: R*, CLK and Q.
inline const usyn::Physical_cell_interface kDominoPins{};

// The schedule coefficients of stage_model.hpp, in ps. nullopt is unmeasured
// and is never read as zero.
struct Stage_values {
  std::optional<double> setup_eval_ps, uncertainty_first_ps, uncertainty_second_ps;
  std::optional<double> eval_delay_ps, precharge_ps, precharge_margin_ps;
};

// The timing part of a schema-2 bundle sidecar.
struct Domino_timing_view {
  std::string                        producer_salt, corner, clock, digest;  // digest: of the sidecar bytes
  uint32_t                           model_version = 0, stage_version = 0, bundle_schema = 0;
  std::optional<double>              fo4_ps;
  std::optional<int64_t>             dbu;
  Stage_values                       stage;
  std::map<std::string, std::string> sources;  // coefficient -> "bundle" | "override"
  using Variant = Catalog_entry;               // the sidecar's own variant record
  std::vector<Variant> variants;
  struct Instance {
    std::string region, state, master;
    uint32_t    state_index = 0;
  };
  std::vector<Instance> instances;
  const Variant*        find(std::string_view name) const;
};
// nullopt with the reason in `why` for a missing, malformed or other-schema
// bundle (read_domino_sidecar validates the placement view first).
std::optional<Domino_timing_view> read_domino_timing(const std::filesystem::path& bundle, std::string* why);

// Coefficient overrides for offline sweeps. stage_ps keys are the
// Stage_values names ("eval_delay_ps", ...); an FO4 multiple sets its
// coefficient from the bundle's FO4 estimate. Each applied value is recorded
// with source "override". An unknown key, a negative or non-finite value, or
// an FO4 multiple without an FO4 estimate throws std::invalid_argument.
struct Model_overrides {
  std::map<std::string, double> stage_ps;
  std::optional<double>         eval_fo4, precharge_fo4, setup_eval_fo4, precharge_margin_fo4;
  bool                          empty() const;
};
void apply_overrides(Domino_timing_view& view, const Model_overrides& overrides);

// One sequential provisional cell per variant, named after it:
// - R* inputs with capacitance rail_ff and a scalar setup_rising arc to CLK
//   of setup_eval_ps (the T/2 rail capture);
// - CLK, a clock input with capacitance clock_ff;
// - Q, the register's own clock-to-Q (rising_edge) arcs of its Q pin with the
//   related pin renamed to CLK and its max capacitance/transition, unswapped:
//   the register's Q arcs already describe its output pin's transitions;
// - the register's thresholds.
// Every cell captures at {0.5, uncertainty_first_ps}. A null coefficient or a
// register missing from `base` becomes Cell::unsupported, a null capacitance
// stays absent, so the proxy never certifies a silent zero. `generation`
// hashes the sidecar digest, the effective coefficients and the cells.
std::shared_ptr<const late::Provisional_cells> domino_timing_cells(const Domino_timing_view&       view,
                                                                   const liberty::timing::Library& base, uint64_t* generation,
                                                                   std::vector<std::string>* diagnostics);

// The coefficient names in report order with their effective values.
std::vector<std::pair<std::string, std::optional<double>>> stage_coefficients(const Stage_values& stage);
}  // namespace livehd::domplace::timing
