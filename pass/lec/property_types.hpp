// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string_view>

namespace livehd::lec {

// Shared by the encoder and single-design prover without linking the
// relational query engines or their optional ABC cone implementation.
enum class Verdict { Proven, Refuted, Unknown };

[[nodiscard]] inline bool is_assume_kind(std::string_view kind) { return kind == "assume" || kind == "assume_nocheck"; }

[[nodiscard]] inline bool is_unchecked_assume_class(std::string_view aclass) {
  return aclass == "unchecked" || aclass == "check_disabled";
}

}  // namespace livehd::lec
