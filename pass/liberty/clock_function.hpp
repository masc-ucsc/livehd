// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace livehd::liberty {
std::optional<uint64_t> function_truth(std::string_view function, const std::vector<std::string>& inputs);
// At levels[i] on every declared input, the output equals active_output. All other rows give its complement.
struct Clock_gate_form {
  std::vector<bool> levels;
  bool              active_output;
};
std::optional<Clock_gate_form> clock_gate_form(std::string_view function, const std::vector<std::string>& inputs);
}  // namespace livehd::liberty
