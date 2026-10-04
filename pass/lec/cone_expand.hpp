// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <utility>
#include <vector>

#include "cvc5/cvc5.h"

namespace livehd::lec {
// Inline unique scalar definitions that the encoder asserts, then expand finite
// array reads by the exact read-over-write identity. The resulting obligations
// are equivalent under those definitions; their standalone proofs transfer.
// Keep these expanded terms in proof-cache keys: the definitions are premises.
std::vector<cvc5::Term> expand_cone_definitions(cvc5::TermManager&                                    tm,
                                                const std::vector<std::pair<cvc5::Term, cvc5::Term>>& definitions,
                                                const std::vector<cvc5::Term>&                        obligations);
}  // namespace livehd::lec
