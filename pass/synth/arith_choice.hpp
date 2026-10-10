// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The arithmetic-architecture vocabulary shared by every mapper (pass.abc and
// pass.usyn): the spellings, the canonical JSON of a choice, and the candidate
// families an exploration tries. The generators themselves live in arith.hpp
// and are consumed through blast.hpp by both mappers; keeping the *selection*
// vocabulary here is what stops the two from drifting (different candidate
// sets, different encodings, different pin-hints).

#include <format>
#include <string>
#include <string_view>

#include "arith.hpp"

namespace livehd::synth {

inline constexpr arith::Adder_kind kAdderKinds[] = {arith::Adder_kind::rca,
                                                    arith::Adder_kind::cla,
                                                    arith::Adder_kind::cska,
                                                    arith::Adder_kind::prefix,
                                                    arith::Adder_kind::brent};

// sn first: it is the strongest default candidate for wide non-constant products.
inline constexpr arith::Mult_kind kMultiplierKinds[] = {arith::Mult_kind::sn,
                                                        arith::Mult_kind::csa,
                                                        arith::Mult_kind::tree,
                                                        arith::Mult_kind::array};

[[nodiscard]] inline std::string_view adder_name(arith::Adder_kind kind) {
  switch (kind) {
    case arith::Adder_kind::rca: return "rca";
    case arith::Adder_kind::cla: return "cla";
    case arith::Adder_kind::cska: return "cska";
    case arith::Adder_kind::prefix: return "prefix";
    case arith::Adder_kind::brent: return "brent";
  }
  return "rca";
}

[[nodiscard]] inline std::string_view multiplier_name(arith::Mult_kind kind) {
  switch (kind) {
    case arith::Mult_kind::array: return "array";
    case arith::Mult_kind::tree: return "tree";
    case arith::Mult_kind::csa: return "csa";
    case arith::Mult_kind::sn: return "sn";
  }
  return "array";
}

[[nodiscard]] inline std::string_view barrel_name(bool reverse) { return reverse ? "reverse" : "log"; }

// Canonical JSON of one choice. `O` is any options struct carrying `adder`,
// `multiplier`, `reverse_barrel` and `block_size` (Blast_options, Driver_options).
// `extra` is raw trailing JSON members (",\"heuristic\":true").
template <class O>
[[nodiscard]] inline std::string arith_choice_json(const O& o, std::string_view extra = {}) {
  return std::format("{{\"adder\":\"{}\",\"multiplier\":\"{}\",\"barrel\":\"{}\",\"block_size\":{}{}}}",
                     adder_name(o.adder),
                     multiplier_name(o.multiplier),
                     barrel_name(o.reverse_barrel),
                     o.block_size,
                     extra);
}

}  // namespace livehd::synth
