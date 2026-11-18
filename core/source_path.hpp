//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Workspace-relative source paths for the hhds Source_locator.
//
// Source_locator::mint asserts paths are workspace-relative: SourceIds hash the
// path, so the same file must yield the same id in every artifact and every
// run — an absolute path would bake the build machine's layout into the id.
// This is the one place that normalizes ingress paths (prp/slang/yosys); do
// NOT reuse Hhds_graph_library::canonical_path, which absolutizes.

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace livehd::srcloc {

namespace detail {
inline std::mutex& alias_mutex() {
  static std::mutex m;
  return m;
}
inline std::vector<std::pair<std::string, std::string>>& aliases() {
  static std::vector<std::pair<std::string, std::string>> v;
  return v;
}
}  // namespace detail

// A generated-source directory (e.g. a per-run temporary directory with a random name) whose files must still mint
// the same SourceId in every run: paths under `abs_prefix` are recorded as `logical/<rest>`. Unregister when the
// directory goes away.
inline void register_source_alias(std::string abs_prefix, std::string logical) {
  while (abs_prefix.size() > 1 && abs_prefix.back() == '/') {
    abs_prefix.pop_back();
  }
  std::lock_guard lock(detail::alias_mutex());
  detail::aliases().emplace_back(std::move(abs_prefix), std::move(logical));
}
inline void unregister_source_alias(std::string_view abs_prefix) {
  while (abs_prefix.size() > 1 && abs_prefix.back() == '/') {
    abs_prefix.remove_suffix(1);
  }
  std::lock_guard lock(detail::alias_mutex());
  auto&           v = detail::aliases();
  std::erase_if(v, [&](const auto& a) { return a.first == abs_prefix; });
}

inline std::string workspace_relative(std::string_view path) {
  {
    std::lock_guard lock(detail::alias_mutex());
    if (!detail::aliases().empty() && !path.empty()) {
      // Front ends may spell the file relative to the current directory (slang under yosys does): compare the
      // lexically absolute form.
      std::string abs{path};
      if (abs.front() != '/') {
        std::error_code ec;
        const auto      cwd = std::filesystem::current_path(ec);
        if (!ec) {
          abs = (cwd / abs).lexically_normal().string();
        }
      }
      for (const auto& [prefix, logical] : detail::aliases()) {
        if (abs.size() > prefix.size() && abs.compare(0, prefix.size(), prefix) == 0 && abs[prefix.size()] == '/') {
          return logical + abs.substr(prefix.size());
        }
      }
    }
  }
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
