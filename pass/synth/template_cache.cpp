// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "template_cache.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>

#include "hash_util.hpp"
#include "mapping_wire.hpp"

namespace livehd::synth {
namespace {
constexpr size_t file_limit     = 64 * 1024 * 1024;
constexpr size_t context_limit  = 32 * 1024 * 1024;
constexpr size_t entry_overhead = 128;

using wire::Reader;
using wire::Writer;

std::optional<std::string> read_file(const std::filesystem::path& file, size_t limit) {
  std::error_code error;
  auto            size = std::filesystem::file_size(file, error);
  if (error || size > limit) {
    return {};
  }
  std::ifstream input(file, std::ios::binary);
  std::string   result(static_cast<size_t>(size), '\0');
  input.read(result.data(), static_cast<std::streamsize>(size));
  if (!input || input.peek() != std::char_traits<char>::eof()) {
    return {};
  }
  return result;
}

}  // namespace

std::string template_context(const std::filesystem::path& library, std::string_view backend_version) {
  auto content = read_file(library, context_limit);
  if (!content || backend_version.empty()) {
    return {};
  }
  Writer w;
  w.text("livehd-function-templates-v1");
  w.text(backend_version);
  w.text(*content);
  return std::move(w.data);
}

Template_cache::Template_cache(Tmap_backend& backend, std::string context, size_t max_bytes)
    : backend_(backend), context_(std::move(context)), max_bytes_(std::min(max_bytes, size_t{16 * 1024 * 1024})) {}

Mapped_fragment Template_cache::map(const Mapping_request& request) {
  const auto admit = [&] { return !request.admission || request.admission(); };
  const auto exhausted
      = [] { return Mapped_fragment{.status = Map_status::exhausted, .reason = "template resource budget exhausted"}; };
  if (!admit()) {
    return exhausted();
  }
  if (!enabled()) {
    return backend_.map(request);
  }
  const auto request_key = wire::encode_request(request);
  auto       found       = entries_.find(request_key);
  if (found != entries_.end()) {
    try {
      auto result = wire::decode_fragment(found->second);
      if (!admit()) {
        return exhausted();
      }
      ++statistics_.hits;
      return result;
    } catch (const std::runtime_error&) {
      bytes_ -= found->first.size() + found->second.size() + entry_overhead;
      entries_.erase(found);
    }
  }
  ++statistics_.misses;
  auto result = backend_.map(request);
  if (!admit()) {
    return exhausted();
  }
  if (result.status != Map_status::mapped) {
    return result;
  }
  std::string encoded;
  try {
    encoded = wire::encode_fragment(result);
  } catch (const std::runtime_error& error) {
    return {.status = Map_status::invalid, .reason = error.what()};
  }
  const auto cost = request_key.size() + encoded.size() + entry_overhead;
  if (cost <= max_bytes_) {
    // Deterministic bounded eviction. Avoid retaining a second full mapping
    // object; serialization also gives every consumer its own value copy.
    if (cost > max_bytes_ - bytes_) {
      statistics_.evictions += entries_.size();
      entries_.clear();
      bytes_ = 0;
    }
    entries_.emplace(request_key, std::move(encoded));
    bytes_ += cost;
  }
  return result;
}

bool Template_cache::load(const std::filesystem::path& file) {
  if (!enabled()) {
    return false;
  }
  auto content = read_file(file, file_limit);
  if (!content || content->size() < 8) {
    return false;
  }
  try {
    std::string_view payload(*content);
    Reader           checksum{payload.substr(payload.size() - 8)};
    payload.remove_suffix(8);
    if (checksum.number() != hash_util::fnv1a64(payload)) {
      return false;
    }
    Reader r{payload};
    if (r.text() != context_) {
      return false;
    }
    auto                               count = r.count(max_bytes_ / entry_overhead);
    std::map<std::string, std::string> loaded;
    size_t                             bytes = 0;
    for (size_t i = 0; i < count; ++i) {
      auto k = r.text(), v = r.text();
      auto cost = k.size() + v.size() + entry_overhead;
      // Treat structurally malformed fragments as a cold cache, even if the
      // container checksum is intact. Semantic proof still gates publication.
      wire::decode_fragment(v);
      if (cost > max_bytes_ - bytes || !loaded.emplace(std::move(k), std::move(v)).second) {
        return false;
      }
      bytes += cost;
    }
    if (!r.data.empty()) {
      return false;
    }
    entries_           = std::move(loaded);
    bytes_             = bytes;
    statistics_.loaded = entries_.size();
    return true;
  } catch (const std::runtime_error&) {
    return false;
  }
}

bool Template_cache::save(const std::filesystem::path& file) const {
  if (!enabled()) {
    return false;
  }
  Writer w;
  w.text(context_);
  w.number(entries_.size());
  for (const auto& [k, v] : entries_) {
    w.text(k);
    w.text(v);
  }
  w.number(hash_util::fnv1a64(w.data));
  if (w.data.size() > file_limit) {
    return false;
  }
  std::ofstream output(file, std::ios::binary | std::ios::trunc);
  output.write(w.data.data(), static_cast<std::streamsize>(w.data.size()));
  output.close();
  return bool(output);
}
}  // namespace livehd::synth
