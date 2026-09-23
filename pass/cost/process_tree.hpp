// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <vector>

namespace livehd::cost {
struct Process_reading {
  int      pid = 0, parent = 0;
  uint64_t birth = 0, birth_fraction = 0, bytes = 0;
  bool     stopped = false, zombie = false;
};
struct Process_tree_reading {
  std::vector<Process_reading> processes;
  uint64_t                     bytes     = 0;
  uint64_t                     missing   = 0;
  bool                         truncated = false;
};
// Current descendants, across process groups. Bounded to 4096 processes/tasks
// per scan. Reads are sequential samples, not an atomic OS-wide snapshot.
// macOS uses physical footprint; Linux uses RSS. Missing reads stay explicit.
Process_tree_reading read_process_tree(int root);
// Stop descendants of a child the caller still owns (has not reaped). Freeze
// parents before enumerating children so they cannot reap/reuse their PIDs.
// Returns false on incomplete cleanup. The caller must reap its root child.
bool                 stop_process_tree(int root);
}  // namespace livehd::cost
