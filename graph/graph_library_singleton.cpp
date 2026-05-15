//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "graph_library_singleton.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

#include "absl/container/flat_hash_map.h"

namespace livehd {

namespace {

std::mutex& registry_mu() {
  static std::mutex m;
  return m;
}

absl::flat_hash_map<std::string, std::unique_ptr<hhds::GraphLibrary>>& registry() {
  static absl::flat_hash_map<std::string, std::unique_ptr<hhds::GraphLibrary>> r;
  return r;
}

}  // namespace

std::string Hhds_graph_library::canonical_path(std::string_view path) {
  std::error_code ec;
  auto            abs = std::filesystem::absolute(std::filesystem::path{std::string{path}}, ec);
  if (ec) {
    return std::string{path};
  }
  return abs.lexically_normal().string();
}

hhds::GraphLibrary& Hhds_graph_library::instance(std::string_view path) {
  auto key = canonical_path(path);

  std::lock_guard<std::mutex> guard(registry_mu());
  auto&                       r  = registry();
  auto                        it = r.find(key);
  if (it != r.end()) {
    return *it->second;
  }

  auto lib = std::make_unique<hhds::GraphLibrary>();
  // Lazy load from disk if a prior save exists for this path.
  std::error_code ec;
  auto            library_txt = std::filesystem::path{key} / "library.txt";
  if (std::filesystem::exists(library_txt, ec) && !ec) {
    lib->load(key);
  }

  auto* raw = lib.get();
  r.emplace(key, std::move(lib));
  return *raw;
}

void Hhds_graph_library::save(std::string_view path) {
  auto key = canonical_path(path);

  std::lock_guard<std::mutex> guard(registry_mu());
  auto&                       r  = registry();
  auto                        it = r.find(key);
  if (it == r.end()) {
    return;
  }
  std::error_code ec;
  std::filesystem::create_directories(key, ec);
  it->second->save(key);
}

}  // namespace livehd
