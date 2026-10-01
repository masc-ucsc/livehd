// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "logical_cache.hpp"

#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <fstream>

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/SHA256.h"
#include "usyn_salt.hpp"

namespace livehd::usyn {
namespace {
bool valid_key(std::string_view key) {
  return key.size() == 64 && key.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}
std::string digest(std::string_view bytes) {
  llvm::SHA256 hash;
  hash.update(llvm::StringRef(bytes));
  std::string    out;
  constexpr char hex[] = "0123456789abcdef";
  for (const auto b : hash.final()) {
    out += hex[b >> 4];
    out += hex[b & 15];
  }
  return out;
}
}  // namespace

Logical_cache_probe probe_logical_cache(const Logical_cache_options& options, std::string_view module, const synth::Lnet& raw,
                                        const synth::Source_state_table& source, const Identity_names& names,
                                        const Logical_options& logical, uint64_t credits, Budget& io) {
  Logical_cache_probe result;
  if (options.directory.empty()) {
    return result;
  }
  auto identity = serialize_logical_identity(raw, source, names, logical, kUsynSrcSalt, options.context, io, options.limits);
  if (identity.status != Status::feasible || !io.spend(identity.bytes.size())) {
    result.outcome = Cache_lookup::refused;
    result.reason  = "logical cache identity admission";
    return result;
  }
  result.key = digest(identity.bytes);
  std::string{}.swap(identity.bytes);
  const auto      path = options.directory / (result.key + ".usyn-cache");
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) && !ec) {
    return result;
  }
  const auto size = std::filesystem::file_size(path, ec);
  if (ec || size > options.limits.bytes || !io.spend(size)) {
    result.outcome = Cache_lookup::refused;
    result.reason  = "logical cache read admission";
    return result;
  }
  std::ifstream file(path, std::ios::binary);
  std::string   bytes(size, '\0');
  file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!file || file.peek() != std::ifstream::traits_type::eof()) {
    result.outcome = Cache_lookup::invalid;
    result.reason  = "incomplete logical cache file";
    return result;
  }
  auto loaded = deserialize_selection_record(bytes, result.key, synth::State_target::cmos, io, options.limits);
  if (!loaded.record) {
    result.outcome = loaded.status == Status::search_exhausted ? Cache_lookup::refused : Cache_lookup::invalid;
    result.reason  = std::move(loaded.reason);
    return result;
  }
  // A valid decision for other credits: these would run a different search.
  if (!loaded.record->credit.reproduces(credits)) {
    result.outcome = Cache_lookup::credit_miss;
    result.reason  = "logical cache credit floor not reproduced";
    result.stored  = loaded.record->credit;
    return result;
  }
  // The stored module, port spellings and provenance may predate a rename.
  if (const auto bound = rebind_selection(*loaded.record, module, raw, source, io, options.limits); bound != Status::feasible) {
    result.outcome = bound == Status::search_exhausted ? Cache_lookup::refused : Cache_lookup::invalid;
    result.reason  = "logical cache rebinding to the fresh translation";
    return result;
  }
  result.outcome = Cache_lookup::hit;
  result.record  = std::move(loaded.record);
  return result;
}

bool replaces_stored_record(const Logical_cache_probe& probe, const Credit_floor& fresh) {
  return !(probe.outcome == Cache_lookup::credit_miss && probe.stored && !probe.stored->bound && fresh.bound);
}

bool store_logical_cache(const Logical_cache_options& options, std::string_view key, std::string_view module,
                         const Stateful_region& selected, const Logical_report& report, const Credit_floor& search,
                         uint64_t structural, Budget& io) {
  if (options.directory.empty() || !valid_key(key)) {
    return false;
  }
  auto encoded = serialize_selection_record(key, module, selected, report, search, structural, io, options.limits);
  if (encoded.status != Status::feasible || !io.spend(encoded.bytes.size())) {
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(options.directory, ec);
  if (ec) {
    return false;
  }
  auto      temporary = (options.directory / (std::string(key) + ".tmp-XXXXXX")).string();
  const int fd        = mkstemp(temporary.data());
  if (fd < 0) {
    return false;
  }
  size_t done = 0;
  while (done < encoded.bytes.size()) {
    const auto count = ::write(fd, encoded.bytes.data() + done, encoded.bytes.size() - done);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      break;
    }
    done += static_cast<size_t>(count);
  }
  const bool closed = close(fd) == 0;
  if (closed && done == encoded.bytes.size()) {
    std::filesystem::rename(temporary, options.directory / (std::string(key) + ".usyn-cache"), ec);
    if (!ec) {
      return true;
    }
  }
  std::filesystem::remove(temporary, ec);
  return false;
}
}  // namespace livehd::usyn
