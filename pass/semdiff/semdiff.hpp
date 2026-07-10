// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <functional>
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

// A process-independent 128-bit structural digest of a module def: op kinds,
// pin widths, connectivity (commutative-normalized within each sink port,
// operand hashes sorted — the pass/submatch canonical form), constant values,
// IO names/widths/signs/port-bindings, and state-cell hierarchical names — the
// same name-based correspondence basis lec proofs rest on, so a digest-equal
// pair soundly transfers a verdict.
//
// HIERARCHICAL (Merkle): a Sub whose body `resolve` can produce folds the
// CHILD'S digest, recursively (bottom-up over the instance DAG) — an edited
// child that the encoder flattens into a parent proof must change the parent's
// digest, or a cached verdict would go stale. A Sub with no body (a true
// blackbox, abstracted as UF in proofs) folds only its def-name identity.
//
// This one-sided canonical form exists for comparing against a STORED digest
// (the 2f-fcore verdict cache, 2i-import module dedup). When both graphs are in
// hand, structural_match's joint traversal compares them directly — no
// canonicalization pass needed.
//
// valid=false — not digestable, callers must skip the cache: the def (or a
// resolved child) holds an ANONYMOUS state cell (its only key would be the
// per-run debug nid, and any constant fallback would fold two genuinely
// different graphs — swap the fan-outs of two anonymous flops — to one digest),
// or the instance graph is cyclic.
struct Canonical_digest {
  uint64_t h0    = 0;
  uint64_t h1    = 0;  // two independent chains: a 64-bit collision is a wrong verdict
  bool     valid = false;
  bool     operator==(const Canonical_digest&) const = default;
};

// Resolve a Sub's def gid to its body graph on THIS side's library (nullptr /
// empty function = blackbox). Digests are memoized per call across the
// instance DAG, so shared children are computed once.
using Digest_resolver = std::function<hhds::Graph*(hhds::Gid)>;

Canonical_digest canonical_digest(hhds::Graph* g, const Digest_resolver& resolve = {});

}  // namespace livehd::semdiff
