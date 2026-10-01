// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "tmap_cache.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <unistd.h>  // mkdtemp

#include "hash_util.hpp"

namespace livehd::synth {
namespace fs = std::filesystem;
namespace {
std::string small_file(const fs::path& path, uint64_t cap) {
  if (fs::is_symlink(fs::symlink_status(path)) || !fs::is_regular_file(path) || fs::file_size(path) > cap) {
    throw std::runtime_error("invalid mapping cache metadata");
  }
  std::ifstream in(path, std::ios::binary);
  std::string   value(fs::file_size(path), '\0');
  in.read(value.data(), static_cast<std::streamsize>(value.size()));
  if (!in || in.peek() != std::char_traits<char>::eof()) {
    throw std::runtime_error("incomplete mapping cache metadata");
  }
  return value;
}
void write_file(const fs::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  out.close();
  if (!out) {
    throw std::runtime_error("cannot write mapping cache metadata");
  }
}
}  // namespace

bool Tmap_cache::admit() {
  if (resource_refused || (admission && !admission("mapping-cache"))) {
    resource_refused = true;
    return false;
  }
  return true;
}
fs::path Tmap_cache::make_generation() {
  if (!admit()) {
    throw std::runtime_error("mapping cache process admission");
  }
  fs::create_directories(root);
  auto pattern = (root / "entry-XXXXXX").string();
  if (!mkdtemp(pattern.data())) {
    throw std::runtime_error("cannot create mapping cache generation");
  }
  return pattern;
}
std::string Tmap_cache::inventory(const fs::path& directory) {
  std::vector<fs::path> files;
  uint64_t              total = 0, entries = 0;
  for (const auto name : {"mapped", "mapped_pre"}) {
    const auto path = directory / name;
    if (fs::is_symlink(fs::symlink_status(path)) || !fs::is_directory(path)) {
      throw std::runtime_error("missing mapping cache library");
    }
    for (const auto& entry : fs::recursive_directory_iterator(path)) {
      if (!admit() || ++entries > 100000 || fs::is_symlink(entry.symlink_status())) {
        throw std::runtime_error("mapping cache inventory admission");
      }
      if (entry.is_directory()) {
        continue;
      }
      if (!entry.is_regular_file()) {
        throw std::runtime_error("invalid mapping cache file");
      }
      const auto size = entry.file_size();
      if (size > byte_limit - total) {
        throw std::runtime_error("mapping cache byte admission");
      }
      total += size;
      files.push_back(entry.path().lexically_relative(directory));
    }
  }
  std::sort(files.begin(), files.end());
  uint64_t                hash = hash_util::fnv1a64("tmap-cache-v1"), read_total = 0;
  std::array<char, 65536> bytes;
  for (const auto& relative : files) {
    hash            = hash_util::fnv1a64(relative.generic_string(), hash);
    const auto path = directory / relative;
    const auto size = fs::file_size(path);
    if (size > byte_limit - read_total) {
      throw std::runtime_error("mapping cache changed during inventory");
    }
    read_total += size;
    hash        = hash_util::fnv1a64_u64(size, hash);
    std::ifstream in(path, std::ios::binary);
    uint64_t      read = 0;
    while (in && read < size) {
      if (!admit()) {
        throw std::runtime_error("mapping cache process admission");
      }
      in.read(bytes.data(), static_cast<std::streamsize>(std::min<uint64_t>(bytes.size(), size - read)));
      const auto count  = static_cast<size_t>(in.gcount());
      hash              = hash_util::fnv1a64(std::string_view(bytes.data(), count), hash);
      read             += count;
    }
    if (!in || read != size || in.peek() != std::char_traits<char>::eof()) {
      throw std::runtime_error("incomplete mapping cache body");
    }
  }
  // Also bound the JSON parser's allocation separately from graph storage.
  const auto manifest = directory / "mapped" / "abc_cache.json";
  if (!fs::is_regular_file(manifest) || fs::file_size(manifest) > (64ULL << 20)) {
    throw std::runtime_error("mapping cache manifest admission");
  }
  return "tmap-cache-v1 " + std::to_string(files.size()) + " " + std::to_string(total) + " " + std::to_string(hash) + "\n";
}

Tmap_cache::Tmap_cache(std::string directory, uint64_t salt, uint64_t max_bytes, std::function<bool(std::string_view)> callback)
    : root(std::move(directory)), byte_limit(max_bytes), admission(std::move(callback)) {
  if (root.empty() || !admit()) {
    return;
  }
  try {
    if (fs::exists(root / "current")) {
      const auto name = small_file(root / "current", 64);
      if (!name.starts_with("entry-") || name.size() != 12
          || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-") != std::string::npos) {
        throw std::runtime_error("invalid mapping cache generation");
      }
      const auto generation = root / name;
      if (fs::is_symlink(fs::symlink_status(generation)) || small_file(generation / "integrity", 128) != inventory(generation)) {
        throw std::runtime_error("mapping cache integrity mismatch");
      }
      if (!admit()) {
        return;
      }
      cache = std::make_unique<Region_cache>((generation / "mapped").string(), salt, true);
      return;
    }
  } catch (const std::exception&) {
    invalid_snapshot = !resource_refused;
  }
  try {
    pending = make_generation();
    cache   = std::make_unique<Region_cache>((pending / "mapped").string(), salt, true);
  } catch (const std::exception&) {
    // Ordinary cache I/O failure leaves technology mapping available.
  }
}
Tmap_cache::~Tmap_cache() {
  if (!pending.empty()) {
    std::error_code ec;
    fs::remove_all(pending, ec);
  }
}
bool Tmap_cache::save() {
  if (!cache || !admit()) {
    return false;
  }
  try {
    if (pending.empty()) {
      pending = make_generation();
    }
    if (!cache->stage_snapshot((pending / "mapped").string())) {
      return true;  // all hits: no new snapshot or pointer publication
    }
    const auto checksum = inventory(pending);
    if (!admit()) {
      return false;
    }
    write_file(pending / "integrity", checksum);
    write_file(pending / "pointer", pending.filename().string());
    fs::rename(pending / "pointer", root / "current");
    pending.clear();
    return true;
  } catch (const std::exception&) {
    return false;
  }
}
}  // namespace livehd::synth
