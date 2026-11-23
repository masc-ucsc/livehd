// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "option_text.hpp"
#include "query.hpp"

namespace livehd::lec {

// Each entry point supplies its label lookup and its existing integer and Boolean conversions.
inline Lec_options parse_formal_options(const auto& label, const auto& integer, const auto& boolean) {
  Lec_options o;
  o.engine       = label("engine", "auto");
  o.solver       = label("solver", "cvc5");
  o.portfolio    = label("portfolio", "auto");
  o.assume_check = boolean(label("assume_check", "true"));
  o.bound        = integer(label("bound", "6"));
  o.timeout      = integer(label("timeout", "120"));
  o.witness      = boolean(label("witness", "true"));
  o.phase        = label("phase", "after_reset");
  o.reset_cycles = integer(label("reset_cycles", "2"));
  o.reset        = label("reset", "");
  o.partitions   = integer(label("partitions", "4"));
  o.split        = label("split", "auto");
  str_tools::append_csv_names(o.ignore_memory, label("ignore_memory", ""));
  return o;
}

inline void parse_comparison_options(Lec_options& o, const auto& label, const auto& integer, const auto& boolean) {
  o.gold_x      = label("gold_x", "ignore");
  o.decompose   = label("decompose", "auto");
  o.cones       = label("cones", "auto");
  o.conelimit   = integer(label("conelimit", "10000"));
  o.phase_sched = boolean(label("phase_sched", "true"));
  o.int_blast   = label("int_blast", "auto");
  o.box_seq     = label("box_model", "seq") != "uf";
  o.semdiff     = lec_canon_semdiff(label("semdiff", "structural"));
  o.profile     = label("profile", "internal");
  str_tools::append_csv_names(o.collapse, label("collapse", ""));
  str_tools::append_csv_names(o.trust, label("trust", ""));
}

inline bool engine_is_forced(const Lec_options& o) { return o.engine != "auto" || o.portfolio != "auto"; }

}  // namespace livehd::lec
