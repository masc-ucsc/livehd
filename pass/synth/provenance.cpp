// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "provenance.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/SHA256.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::synth {
namespace {
std::string hex_digest(llvm::SHA256& hash) {
  std::string    result;
  constexpr char hex[] = "0123456789abcdef";
  for (auto byte : hash.final()) {
    result += hex[byte >> 4];
    result += hex[byte & 15];
  }
  return result;
}
void write(const std::filesystem::path& path, std::string_view text) {
  std::ofstream out(path, std::ios::binary);
  out.write(text.data(), text.size());
  out.close();
  if (!out) {
    throw std::runtime_error("cannot write synthesis provenance: " + path.string());
  }
}
struct Entry {
  std::string path, status, blob, digest;
  uint64_t    bytes = 0;
};
}  // namespace

std::string archive_provenance(const std::filesystem::path& destination, std::string_view context, std::string_view mapper_revision,
                               Provenance_limits limits) {
  namespace fs = std::filesystem;
  if (limits.bytes > 64 * 1024 * 1024 || limits.entries > 4096 || context.size() > 1024 * 1024) {
    throw std::invalid_argument("synthesis provenance exceeds archive admission bounds");
  }
  if (fs::exists(destination) || !fs::create_directories(destination / "files")) {
    throw std::runtime_error("synthesis provenance destination must be fresh");
  }
  rapidjson::Document invocation;
  invocation.Parse(context.data(), context.size());
  const bool has_context
      = !invocation.HasParseError() && invocation.IsObject() && invocation.HasMember("inputs") && invocation["inputs"].IsArray();
  bool                  complete = has_context;
  std::vector<Entry>    entries;
  std::set<std::string> files;
  uint32_t              visits = 0;
  uint64_t              bytes = 0, charged = 0;
  const auto            refused = [&](const fs::path& path, std::string status) {
    complete = false;
    entries.push_back({path.string(), std::move(status), {}, {}, 0});
  };
  const auto archive = fs::weakly_canonical(destination);
  if (has_context) {
    for (const auto& value : invocation["inputs"].GetArray()) {
      if (visits >= limits.entries) {
        refused({}, "entry_limit");
        break;
      }
      ++visits;
      if (!value.IsString() || !value.GetStringLength() || value.GetStringLength() > 4096
          || std::string_view(value.GetString(), value.GetStringLength()).find('\0') != std::string_view::npos) {
        refused({}, "invalid_input_path");
        continue;
      }
      const auto      root = fs::absolute(fs::path(std::string(value.GetString(), value.GetStringLength()))).lexically_normal();
      std::error_code error;
      const auto      status = fs::status(root, error);
      if (error || !fs::exists(status)) {
        refused(root, "unreadable_or_missing");
      } else if (fs::is_regular_file(status)) {
        files.insert(root.string());
      } else if (fs::is_directory(status)) {
        const auto relative = archive.lexically_relative(fs::weakly_canonical(root));
        if (!relative.empty() && *relative.begin() != "..") {
          refused(root, "archive_overlaps_input");
          continue;
        }
        fs::recursive_directory_iterator it(root, error), end;
        while (!error && it != end) {
          if (visits >= limits.entries) {
            refused(root, "entry_limit");
            break;
          }
          ++visits;
          const auto child = it->symlink_status(error);
          if (error) {
            break;
          }
          if (fs::is_symlink(child) && it->is_directory(error)) {
            it.disable_recursion_pending();
            refused(it->path(), "symlink_directory_not_followed");
          } else if (it->is_regular_file(error)) {
            files.insert(it->path().string());
          } else if (!it->is_directory(error)) {
            refused(it->path(), "unsupported_file_type");
          }
          it.increment(error);
        }
        if (error) {
          refused(root, "directory_read_failed");
        }
      } else {
        refused(root, "unsupported_file_type");
      }
    }
  }
  for (const auto& path : files) {
    std::error_code error;
    const auto      size = fs::file_size(path, error);
    if (error) {
      refused(path, "stat_failed");
      continue;
    }
    const auto before = fs::last_write_time(path, error);
    if (error) {
      refused(path, "stat_failed");
      continue;
    }
    if (size > limits.bytes - charged) {
      refused(path, "byte_limit");
      continue;
    }
    charged            += size;  // Failed/changed reads still consume this invocation's I/O allowance.
    const auto    blob  = "files/" + std::to_string(entries.size()) + ".bin";
    std::ifstream input(path, std::ios::binary);
    if (!input) {
      refused(path, "read_failed");
      continue;
    }
    std::ofstream output(destination / blob, std::ios::binary);
    if (!output) {
      throw std::runtime_error("cannot create synthesis provenance blob");
    }
    llvm::SHA256            hash;
    std::array<char, 65536> buffer;
    uint64_t                copied = 0;
    while (copied < size && input) {
      input.read(buffer.data(), std::min<uint64_t>(buffer.size(), size - copied));
      const auto count = input.gcount();
      output.write(buffer.data(), count);
      hash.update(llvm::StringRef(buffer.data(), count));
      copied += count;
    }
    const bool extra = input.peek() != std::char_traits<char>::eof();
    output.close();
    if (!output) {
      throw std::runtime_error("cannot write synthesis provenance blob");
    }
    const auto after = fs::last_write_time(path, error);
    if (input.bad() || copied != size || extra || error || before != after) {
      fs::remove(destination / blob);
      refused(path, "input_changed_or_read_failed");
      continue;
    }
    bytes += copied;
    entries.push_back({path, "captured", blob, hex_digest(hash), copied});
  }
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
  const auto                                 string = [&](std::string_view value) { json.String(value.data(), value.size()); };
  json.StartObject();
  json.Key("schema_version");
  json.Uint(1);
  json.Key("kind");
  string("synth_provenance");
  json.Key("scope");
  string("kernel_observed_inputs_before_mapping");
  json.Key("dependency_closure_certified");
  json.Bool(false);
  json.Key("capture_complete");
  json.Bool(complete);
  json.Key("mapper_revision");
  string(mapper_revision);
  json.Key("byte_limit");
  json.Uint64(limits.bytes);
  json.Key("entry_limit");
  json.Uint(limits.entries);
  json.Key("bytes");
  json.Uint64(bytes);
  json.Key("context");
  if (has_context) {
    invocation.Accept(json);
  } else {
    json.Null();
  }
  json.Key("files");
  json.StartArray();
  uint32_t captured = 0, omitted = 0;
  for (const auto& entry : entries) {
    json.StartObject();
    json.Key("path");
    string(entry.path);
    json.Key("status");
    string(entry.status);
    if (entry.status == "captured") {
      ++captured;
      json.Key("blob");
      string(entry.blob);
      json.Key("sha256");
      string(entry.digest);
      json.Key("bytes");
      json.Uint64(entry.bytes);
    } else {
      ++omitted;
    }
    json.EndObject();
  }
  json.EndArray();
  json.EndObject();
  const std::string_view manifest(buffer.GetString(), buffer.GetSize());
  write(destination / "manifest.json", manifest);
  llvm::SHA256 hash;
  hash.update(llvm::StringRef(manifest.data(), manifest.size()));
  const auto digest = hex_digest(hash);
  buffer.Clear();
  json.Reset(buffer);
  json.StartObject();
  json.Key("capture_complete");
  json.Bool(complete);
  json.Key("dependency_closure_certified");
  json.Bool(false);
  json.Key("manifest_sha256");
  string(digest);
  json.Key("captured_files");
  json.Uint(captured);
  json.Key("omitted_files");
  json.Uint(omitted);
  json.Key("bytes");
  json.Uint64(bytes);
  json.EndObject();
  return {buffer.GetString(), buffer.GetSize()};
}
}  // namespace livehd::synth
