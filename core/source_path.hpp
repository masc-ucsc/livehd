//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Workspace-relative source paths for the hhds Source_locator.
//
// Source_locator::mint asserts paths are workspace-relative: SourceIds hash the
// path, so the same file must yield the same id in every artifact and every
// run — an absolute path would bake the build machine's layout into the id.
// This is the one place that normalizes ingress paths (prp/slang/yosys); do
// NOT reuse Hhds_graph_library::canonical_path, which absolutizes.

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace livehd::srcloc {

inline std::string workspace_relative(std::string_view path) {
  if (path.empty() || path.front() != '/') {
    return std::string(path);  // already relative (the common CLI case)
  }
  std::error_code ec;
  const auto      cwd = std::filesystem::current_path(ec).string();
  if (!ec && !cwd.empty() && path.size() > cwd.size() + 1 && path.substr(0, cwd.size()) == cwd && path[cwd.size()] == '/') {
    return std::string(path.substr(cwd.size() + 1));
  }
  // Outside the workspace (e.g. an LSP buffer in another checkout): strip the
  // leading slashes so the id is still deterministic for that absolute name.
  size_t i = 0;
  while (i < path.size() && path[i] == '/') {
    ++i;
  }
  return std::string(path.substr(i));
}

}  // namespace livehd::srcloc
