// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>

#include "function.hpp"

namespace livehd::usyn {
// F(B,R) = H(G0(B), ..., Gk(B), R). Bound assignments with identical
// cofactors over R share one code; each code bit is a newly synthesized
// divisor. top_care records reachable codes; top initially fills unused codes
// with zero, allowing bounded completion search without assuming reachability.
// This is a candidate generator, not an optimal decomposition/encoding search.
struct Decomposition {
  Status                   status = Status::invalid;
  std::vector<uint32_t>    bound, free;
  std::vector<Truth_table> divisors;
  Truth_table              top;
  Truth_table              top_care;
};

// Bound and free positions refer to the original table's variable order.
// max_top_inputs limits code bits plus free variables; physical factoring and
// per-divisor gate legality belong to the caller. Unsupported means this
// partition cannot fit that interface, not that other decompositions cannot.
Decomposition decompose_function(const Truth_table& function, uint32_t bound_mask, uint32_t max_top_inputs, Budget& work);

// One cofactor classification for both outputs. Equal codes preserve the
// complete pair of free-variable cofactors, not just either output alone.
// Divisors and reachable top codes are shared; each top has its own function.
struct Joint_decomposition {
  Status                     status = Status::invalid;
  std::vector<uint32_t>      bound, free;
  std::vector<Truth_table>   divisors;
  std::array<Truth_table, 2> tops;
  Truth_table                top_care;
};
Joint_decomposition decompose_pair(const std::array<Truth_table, 2>& functions, uint32_t bound_mask, uint32_t max_top_inputs,
                                   Budget& work);

struct Joint_code_change {
  uint32_t target = 0, source = 1;
};

// Replace code[target] by code[target] XOR code[source]. This bijection is
// its own inverse; permute both tops and the reachable image consistently.
// It changes producer functions, unlike merely swapping or complementing rails.
// Failure leaves the input intact and returns no feasible partial encoding.
Joint_decomposition recode_pair(const Joint_decomposition& original, Joint_code_change change, Budget& work);
}  // namespace livehd::usyn
