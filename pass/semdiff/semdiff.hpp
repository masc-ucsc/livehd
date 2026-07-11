// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
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
  bool        state_pairing  = false;         // tier-2: full-match (SRP/ERP signature) pairing
                                              // of name-unmatched state cells (2f-lec consumer)
  // Explicit cross-side state correspondence supplied by the caller (lec.match):
  // {a_name, b_name} raw hier names. Each resolvable pair becomes a tier-1
  // anchor (a resolved point) exactly like a name match, so the tier-2
  // signatures build on the user's certain pairs. Unresolvable names are
  // ignored (the consumer validates its own inputs).
  std::vector<std::pair<std::string, std::string>> seed_pairs;
  std::string id_granularity = "pair";        // pair | region
  bool        verbose        = false;         // print per-side statistics
  bool        dump_state     = false;         // per-state-cell listing (algorithm iteration aid)
  double      name_noise     = 0.0;           // NL2NL-style experiment: destroy this fraction of
                                              // impl-side state keys before tier-1 (0 = off)
  uint64_t    noise_seed     = 1;             // selects WHICH keys are destroyed (deterministic)
  uint32_t    synalign_maxiter = 64;          // tier-2 fixed-point round cap (64 ~ run to
                                              // convergence; 1 = single pass, no propagation)
  uint32_t    explain_noise  = 0;             // deep-dump up to N noised-but-unrecovered cells:
                                              // ground-truth twin's vs the cell's SRP/ERP with
                                              // named anchors + distances, set diff, bucket members
};

// State-cell (Flop/Latch/Fflop/Memory) correspondence statistics for ONE def
// pair — the iterate-on-the-matcher instrument: how much state exists, how much
// pairs by name (tier-1), how much more the full-match signature pass recovers
// (tier-2), and what stays unpaired (with the ambiguous subset called out,
// since an ambiguous bucket is matcher headroom while a no-counterpart cell is
// a genuine diff).
struct State_stats {
  uint32_t a_total = 0, b_total = 0;  // state cells per side
  uint32_t a_mems = 0, b_mems = 0;    // Memory subset of the totals
  uint32_t name_pairs     = 0;        // tier-1: state_key 1:1 across the sides
  uint32_t a_name_grouped = 0, b_name_grouped = 0;  // key on both sides but colliding within one
  uint32_t seed_pairs = 0;            // caller-supplied seed_pairs that resolved (anchored) a cell pair
  uint32_t full_pairs = 0;            // tier-2 full-match pairs (state_pairing)
  uint32_t rounds     = 0;            // fixed-point rounds that added a pair
  uint32_t a_ambiguous = 0, b_ambiguous = 0;  // unpaired: signature shared by >1 candidate
  uint32_t a_unpaired = 0, b_unpaired = 0;    // unpaired at every tier (includes ambiguous)
  // name_noise experiment: ground truth is known (the destroyed key), so tier-2
  // recovery splits into correct (paired with the true counterpart) and wrong.
  uint32_t noised = 0, noised_recovered = 0, noised_correct = 0;
  uint32_t explained = 0;  // explain_noise budget consumed by this def

  State_stats& operator+=(const State_stats& o) {
    a_total += o.a_total;
    b_total += o.b_total;
    a_mems += o.a_mems;
    b_mems += o.b_mems;
    name_pairs += o.name_pairs;
    a_name_grouped += o.a_name_grouped;
    b_name_grouped += o.b_name_grouped;
    seed_pairs += o.seed_pairs;
    full_pairs += o.full_pairs;
    rounds += o.rounds;
    a_ambiguous += o.a_ambiguous;
    b_ambiguous += o.b_ambiguous;
    a_unpaired += o.a_unpaired;
    b_unpaired += o.b_unpaired;
    noised += o.noised;
    noised_recovered += o.noised_recovered;
    noised_correct += o.noised_correct;
    explained += o.explained;
    return *this;
  }
};

// One tier-2 full-match state pair, exported for the LEC consumer (2f-lec).
// Names are the raw hier names (Node_class::get_hier_name) of the SAME graph
// objects lec will encode, so they drop directly into lec's uncertain-pair
// feed ({ref,impl} raw names, canon_flop_name applied at consumption). Tier-1
// name pairs are not exported — their names already agree, so an alias entry
// would be a no-op.
struct State_pair {
  std::string a_name;          // ref-side state-cell hier name
  std::string b_name;          // impl-side state-cell hier name
  bool        is_mem = false;  // Memory pair: lec's name_alias covers flops only (v1)
  uint32_t    round  = 0;      // fixed-point round that decided it (diagnostics)
};

struct Match_result {
  uint32_t regions     = 0;  // distinct match ids assigned (> 0)
  uint32_t a_matched   = 0, a_unmatched = 0;
  uint32_t b_matched   = 0, b_unmatched = 0;
  double   similarity  = 0;  // matched / total (both sides)
  State_stats state;         // filled when matching_names or state_pairing is set
  // state_pairing only: the concrete tier-2 pairs, and the still-unpaired state
  // cells as "name (reason)" lines — reason is one of `ambiguous` (signature
  // shared by >1 same-kind candidate), `kind/init mismatch` (a cross-side cell
  // has the same SRP/ERP but a different op/bits/init fold — the pair
  // precondition refuses), or `no full match` (no cross-side counterpart).
  std::vector<State_pair>  state_pairs;
  std::vector<std::string> a_state_unpaired, b_state_unpaired;
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

// Batch form: `memo` (keyed by def gid) persists ACROSS calls, so a driver
// digesting every def of one library (the hier-LEC loop) computes each subtree
// once — the per-call form above re-walks shared children per root, which is
// O(defs x subtree) on a deep hierarchy. One memo per side (a gid must resolve
// to one body).
Canonical_digest canonical_digest(hhds::Graph* g, const Digest_resolver& resolve,
                                  absl::flat_hash_map<hhds::Gid, Canonical_digest>& memo);

}  // namespace livehd::semdiff
