// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// No-ABC builds retain every obligation for the ordinary CVC5 query engine.
#include "cone_abc.hpp"

namespace livehd::lec {
Cone_verdict abc_prove_unsat(const cvc5::Term&, int64_t, Cone_stats* st) {
  if (st) {
    *st = {0, 0, "ABC cone accelerator is unavailable in this build"};
  }
  return Cone_verdict::Unsupported;
}

std::vector<Cone_verdict> abc_prove_unsat_batch(const std::vector<cvc5::Term>& diffs, int64_t, int64_t, std::vector<Cone_stats>* st,
                                                const Cone_merge_map*) {
  if (st) {
    st->assign(diffs.size(), Cone_stats{0, 0, "ABC cone accelerator is unavailable in this build"});
  }
  return std::vector<Cone_verdict>(diffs.size(), Cone_verdict::Unsupported);
}
}  // namespace livehd::lec
