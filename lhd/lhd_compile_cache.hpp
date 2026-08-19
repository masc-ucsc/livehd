//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

class Eprp_var;

namespace lhd {

struct Options;
struct Result;

// Parse the complete Pyrope source closure through the persistent Tier-A cache.
// `var` already contains any explicit ln: imports; source units are appended in
// deterministic logical-name order. Returns the number of appended source file
// units (derived lambdas do not exist until upass).
size_t compile_cache_parse_sources(Options& opts, Result& res, Eprp_var& var, const std::vector<std::string>& seed_files,
                                   bool defer_clean_lnasts = false);

// Materialize the current hermetic Tier-A generation only when a lower tier
// actually needs LNAST bodies.  An all-clean Tier-B hit must not deserialize
// and re-hash the complete source forest merely to return a cached LGraph.
void compile_cache_materialize_sources(Result& res, Eprp_var& var);

// Tier B all-clean fast path. Restore validates the graph inventory against
// Tier A's current Merkle closure key, checks every cached graph/interface,
// and copies the exact final post-formal library into `lib_path`.
bool compile_cache_restore_graphs(Options& opts, Result& res, Eprp_var& var, const std::string& lib_path);
// All-clean, lg-only fast path: validate the immutable cached library by its
// published file inventory and reuse/materialize it without deserializing IR.
bool compile_cache_restore_lg_artifact(Options& opts, Result& res, const std::string& lib_path);
void compile_cache_store_graphs(Options& opts, Result& res, const Eprp_var& var, const std::string& lib_path);
// F6 ghost-def elimination, MANIFEST-scoped: delete stored graphs that this
// compile's own units used to define but no longer do (renamed/removed
// entities). Modules owned by units outside this compile are never touched —
// a shared emit lg: dir legitimately accumulates them across compiles.
void compile_cache_prune_graphs(const Eprp_var& var, const Result& res, const std::string& lib_path);

}  // namespace lhd
