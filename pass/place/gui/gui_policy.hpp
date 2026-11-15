// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

namespace livehd::placement {
// Prohibitions on subsequent changes to a prepared implementation. Evaluators
// read this at operation boundaries, independently of whether STA is enabled.
struct Mutation_policy {
  bool no_resynthesis  = false;
  bool no_sizing       = false;
  bool no_buffer_edits = false;
};
}  // namespace livehd::placement
