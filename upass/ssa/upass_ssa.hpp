//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>

#include "lnast.hpp"

// Standalone pass that runs on a single Lnast (post-func_extract shape).
//
// When the LNAST has the post-func_extract layout
//   top → [io(ref input_ref, ref output_ref), stmts(...)]
// this pass:
//   1. Harvests I/O field names from the matching tuple_add nodes and stores
//      them in lnast->io_meta() (populating Lnast_tree_io.inputs / .outputs).
//   2. Builds a new staging body that replaces the io+tuple_add structure with
//      a flat stmts containing only the original body statements.
//   3. Drops the top-level 'io' node and both I/O tuple_add nodes from the
//      output tree (the lowerer reads I/O from io_meta() instead).
//
// If the LNAST does NOT have the post-func_extract layout (e.g. a top-level
// module with no 'io' child), run() is a no-op.
//
// SSA renaming for user variables assigned more than once is a planned
// extension (documented in upass/upass.md §Slice 8 / §Slice 9).
class uPass_ssa {
public:
  // Run the pass on `lnast` in-place.  After this call:
  //   - lnast->io_meta() is populated (if the LNAST had the func_extract shape)
  //   - lnast's tree body is the SSA-normalised form (io+tuple_add I/O replaced
  //     by flat stmts with the body only)
  static void run(const std::shared_ptr<Lnast>& lnast);

private:
  // Recursively copy the subtree rooted at src_nid from src into dst,
  // appending it as a child of dst_parent.
  static void copy_subtree(const std::shared_ptr<Lnast>& src,
                            const Lnast_nid&               src_nid,
                            const std::shared_ptr<Lnast>& dst,
                            const Lnast_nid&               dst_parent);
};
