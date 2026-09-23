//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "graph_library_singleton.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
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

std::unique_ptr<hhds::GraphLibrary> load_library(const std::string& key) {
  auto            lib = std::make_unique<hhds::GraphLibrary>();
  std::error_code ec;
  if (std::filesystem::exists(std::filesystem::path{key} / "library.txt", ec) && !ec) {
    lib->load(key);
  }
  return lib;
}

}  // namespace

std::string Hhds_graph_library::canonical_path(std::string_view path) {
  std::error_code ec;
  auto            abs = std::filesystem::absolute(std::filesystem::path{std::string{path}}, ec);
  if (ec) {
    return std::string{path};
  }
  auto normalized = abs.lexically_normal();
  // lexically_normal preserves a trailing separator (including one from /./).
  // A directory must have one registry key regardless of that spelling.
  while (normalized != normalized.root_path() && normalized.filename().empty()) {
    normalized = normalized.parent_path();
  }
  return normalized.string();
}

hhds::GraphLibrary& Hhds_graph_library::instance(std::string_view path) {
  auto key = canonical_path(path);

  std::lock_guard<std::mutex> guard(registry_mu());
  auto&                       r  = registry();
  auto                        it = r.find(key);
  if (it != r.end()) {
    return *it->second;
  }

  auto lib = load_library(key);

  auto* raw = lib.get();
  r.emplace(key, std::move(lib));
  return *raw;
}

Hhds_graph_library::Scoped_instance::Scoped_instance(std::string_view path) : key_(canonical_path(path)) {
  std::lock_guard<std::mutex> guard(registry_mu());
  auto&                       r = registry();
  if (r.contains(key_)) {
    throw std::logic_error("cannot scope an already registered graph library: " + key_);
  }
  r.emplace(key_, load_library(key_));
}

Hhds_graph_library::Scoped_instance::~Scoped_instance() {
  std::unique_ptr<hhds::GraphLibrary> released;
  {
    std::lock_guard<std::mutex> guard(registry_mu());
    auto&                       r  = registry();
    auto                        it = r.find(key_);
    if (it != r.end()) {
      released = std::move(it->second);
      r.erase(it);
    }
  }
  // Destroy graph bodies outside the registry lock.
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

size_t Hhds_graph_library::registered_instances() {
  std::lock_guard<std::mutex> guard(registry_mu());
  return registry().size();
}

}  // namespace livehd
