// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// Repeated-hierarchy detector behind `synth.place.fp=auto` (pass/place/README.md).
//
// The area proxy is flop + memory area: sequential standard cells and memories
// (hard BLOCK macros and native memory proxies) at their physical area.
// Combinational cells are ignored; synthesis renames and restructures them,
// while flop and memory instance names keep the hierarchy path even in a
// flattened netlist.
//
// Hierarchy scopes come from the module tree when the netlist has one, and
// from the instance names otherwise: a leaf name splits on `.` and `/`
// (outside brackets) and every component but the last is a scope. The last
// component (with its indices, `mem_reg[12][3]`) is a register array inside
// its scope: it adds area there but never forms copies.
//
// Sibling scopes are copies when their makeup matches: equal memory count,
// memory area and flop count within `tolerance` (relative). Instances of one
// module always match. Counts only recognize copies; the score is area:
// share = (proxy area inside the copies) / (total proxy area).
//
// Leaf flop arrays are reported but never select the floorplan stage: its
// candidate discovery needs movable macros per scope, so a register file never
// gets a centroid of its own (pass/place/README.md, fp=auto evidence).
namespace livehd::placement {
struct Fp_rep_leaf {
  std::string name;  // instance name inside its module, possibly hierarchical (flattened)
  double      area   = 0;
  bool        memory = false;  // false: flop/latch
};
struct Fp_rep_child {
  std::string name;
  size_t      module = 0;
};
struct Fp_rep_module {
  std::string               name;
  std::vector<Fp_rep_leaf>  leaves;  // flops and memories only
  std::vector<Fp_rep_child> children;
};
struct Fp_rep_design {
  std::vector<Fp_rep_module> modules;
  size_t                     top = 0;
};
struct Fp_rep_options {
  double share          = .2;   // synth.place.fp.auto_share
  int    min_copies     = 2;    // synth.place.fp.auto_min_copies
  double min_copy_share = .02;  // synth.place.fp.auto_min_copy_share: each copy's own share
  double tolerance      = .05;  // synth.place.fp.auto_tolerance: flop-count / memory-area mismatch
  // synth.place.fp.auto_candidates: count only groups the floorplan stage can
  // center (more than fp_min_macros memories per copy, fp_min_copies copies).
  bool   candidates     = true;
  int    fp_min_macros  = 2;  // synth.place.fp.min_macros
  int    fp_min_copies  = 4;  // synth.place.fp.min_copies
  void   validate() const;
};
struct Fp_rep_group {
  std::string              parent;  // enclosing scope ("" is the top)
  std::string              module;  // shared module name; empty when the copies are name scopes or differ
  std::vector<std::string> copies;
  size_t                   flops = 0, memories = 0;  // of the first copy
  double                   copy_area = 0, area = 0, share = 0;
  bool                     fp_candidate = false;  // a floorplan centroid family under fp_min_macros/fp_min_copies
};
struct Fp_rep_array {
  std::string scope, name;
  size_t      flops = 0;
  double      area = 0, share = 0;
};
struct Fp_rep_result {
  double                    total_area = 0, flop_area = 0, memory_area = 0;
  size_t                    flops = 0, memories = 0, scopes = 0;
  bool                      hierarchy = false;  // modules below the top hold flops or memories
  std::vector<Fp_rep_group> groups;             // eligible groups, best first
  std::vector<Fp_rep_array> arrays;             // largest leaf flop arrays, best first (reported only)
  double                    share = 0, array_share = 0;
  double                    candidate_share = 0;  // best share among fp_candidate groups
  bool                      enabled         = false;
  std::string               trigger         = "none";  // "repetition" or "none"
};
Fp_rep_result            detect_fp_repetition(const Fp_rep_design&, const Fp_rep_options&);
std::vector<std::string> fp_rep_split(std::string_view name);
// A LEF master that counts as a memory (hard BLOCK macro): soft blocks (CLASS BLOCK SOFT) are standard-cell
// regions that floorplan candidate discovery never sees as macros.
bool                     fp_rep_memory_class(std::string_view lef_class);
std::string              fp_rep_array_base(std::string_view leaf);
// `limit` groups/arrays; the decision fields are always complete.
std::string              fp_rep_json(const Fp_rep_result&, const Fp_rep_options&, size_t limit = 5);
}  // namespace livehd::placement
