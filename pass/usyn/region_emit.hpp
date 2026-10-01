// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "logical_writer.hpp"
#include "region_blast.hpp"

namespace livehd::usyn {

struct Region_emission {
  Status      status = Status::invalid;
  std::string reason;
};

// Fill a fresh partition body with the independent CMOS expansion and its
// native memory/ICG/opaque/wiring boundaries. `selected` must come from this
// logical blast; input/output order is the shared boundary identity. Child
// definitions with bodies must already exist in the destination library.
// No added hierarchy, Liberty, ABC or solver. Source graph is read-only.
// Serialize graph access. On refusal the caller must discard its fresh output
// library; like partition's other body builders, this may leave a partial body.
Region_emission emit_logical_region(const partition::Region_body& region, const synth::Region_blast& blast,
                                    const Stateful_region& selected, Budget& work, uint32_t max_nodes = 2000000);

}  // namespace livehd::usyn
