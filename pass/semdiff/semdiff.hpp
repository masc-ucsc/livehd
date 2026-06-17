// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <string>

#include "hhds/graph.hpp"

// pass/semdiff — structural diff/match between two LGraphs (task 2f-semdiff): a
// *structural LEC*. It mirrors pass/lec's shape (two designs, a C++ API the
// kernel calls directly), but instead of an SMT proof it establishes a
// structural correspondence and isolates the regions that differ.
//
// structural_match() stamps the `match` attribute (graph/attrs.hpp) on the
// nodes + driver-pins of BOTH graphs: corresponding nodes get a shared id, a
// node with no counterpart gets 0. The mark is greppable end to end:
//   lhd tool grep -v match=0 lg:ref   # what in ref matched
//   lhd tool grep    match=0 lg:impl  # what in impl is unique (the diff)
//
// It is the MATCH step in the LEC plan: a future lec can run semdiff first,
// assume the matched region equal, and hand only the unmatched gaps to the
// solver — shrinking the miter.
namespace livehd::semdiff {

struct Semdiff_options {
  std::string alg            = "structural";  // v1; future: region | functional
  bool        matching_names = false;         // anchor internal flops/mems by hier name
  std::string id_granularity = "pair";        // pair | region
  bool        verbose        = false;         // print per-side statistics
};

struct Match_result {
  uint32_t regions     = 0;  // distinct match ids assigned (> 0)
  uint32_t a_matched   = 0, a_unmatched = 0;
  uint32_t b_matched   = 0, b_unmatched = 0;
  double   similarity  = 0;  // matched / total (both sides)
};

// Stamp the `match` attribute on nodes + driver pins of BOTH graphs: a shared id
// for corresponding nodes, 0 for nodes with no counterpart. Mirrors
// lec::prove_equal's signature so lec can pre-match before building its miter.
Match_result structural_match(hhds::Graph* a, hhds::Graph* b, const Semdiff_options& opts = {});

}  // namespace livehd::semdiff
