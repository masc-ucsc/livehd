//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>
#include <string_view>

#include "hhds/graph.hpp"

namespace livehd {

// Path-keyed singleton wrapper around `hhds::GraphLibrary`. The HHDS library
// type itself is not copyable or movable, so we own one instance per unique
// path. Lazily loaded from disk on first access; persisted via
// `Hhds_graph_library::save(path)`.
//
// This is the canonical entry point for migrated passes that produce or
// consume graphs (yosys, meta_api, etc.). It replaces the legacy
// `Graph_library::instance(path)` accessor.
class Hhds_graph_library {
public:
  // Returns the library instance for `path` (lazily created + loaded). The
  // returned reference is stable for the lifetime of the process.
  [[nodiscard]] static hhds::GraphLibrary& instance(std::string_view path);

  // Returns the canonical path used for the `instance(path)` keyed above.
  [[nodiscard]] static std::string canonical_path(std::string_view path);

  // Save a specific instance to its on-disk directory.
  static void save(std::string_view path);
};

}  // namespace livehd
