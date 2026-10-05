// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "residual.hpp"
#include "xag_lnet.hpp"
#include "xag_opt.hpp"

namespace livehd::usyn {
struct Cmos_cleanup {
  Residual_options         options;
  Budget*                  search         = nullptr;
  bool                     sop_tree       = false;
  bool                     multi_rep      = false;
  const Native_cost_model* cost_model     = nullptr;
  bool                     gate_objective = true;
};
struct Cmos_cleanup_result {
  Status                    status = Status::invalid;
  std::optional<Xag_region> region;
  Residual_report           report;
  std::string               reason;
  Choice_report             choices{};
};

// P2-C acts on a private behavioral expansion after endpoint validation. It
// protects every D/PO/control output and preserves every original Q, name/init
// and input position. The frozen DOMINO model/evidence is never mutated or
// unpinned. Refused search leaves the incumbent with its caller; process refusal
// remains sticky. Source provenance is owned outside this temporary expansion.
Cmos_cleanup_result clean_cmos_expansion(const Xag_region& source, const Residual_options& options, Budget& search,
                                         bool sop_tree = false, bool multi_rep = false,
                                         const Native_cost_model* cost_model = nullptr, bool gate_objective = true);
}  // namespace livehd::usyn
