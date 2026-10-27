// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "physical_cache.hpp"

#include <array>
#include <fstream>
#include <map>
#include <stdexcept>

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/SHA256.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace lhd {
namespace {
namespace fs    = std::filesystem;
using Inventory = std::map<std::string, std::string>;
constexpr std::array kPlaceArtifacts{"out.def",
                                     "qor.json",
                                     "preroute.json",
                                     "power_grid.json",
                                     "io_resynth.json",
                                     "memory_proxies.lef",
                                     "magnet.json",
                                     "macro_animation.html",
                                     "macro/gp_history.json",
                                     "macro/gp_history.jsonl",
                                     "macro/fixed.def",
                                     "route_options.json",
                                     "sat_route_grid.json",
                                     "sat_route_guard.json"};

std::string digest(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot read physical cache input: " + path.string());
  }
  llvm::SHA256            hash;
  std::array<char, 65536> bytes;
  while (input.read(bytes.data(), bytes.size()) || input.gcount() > 0) {
    hash.update(llvm::StringRef(bytes.data(), input.gcount()));
  }
  if (input.bad()) {
    throw std::runtime_error("incomplete physical cache input: " + path.string());
  }
  std::string    result;
  constexpr char hex[] = "0123456789abcdef";
  for (const auto b : hash.final()) {
    result += hex[b >> 4];
    result += hex[b & 15];
  }
  return result;
}

Inventory inventory(const fs::path& directory) {
  Inventory result;
  for (const auto& entry : fs::recursive_directory_iterator(directory)) {
    if (entry.is_symlink()) {
      throw std::runtime_error("symlink in physical cache generation");
    }
    if (entry.is_regular_file()) {
      result.emplace(entry.path().lexically_relative(directory).generic_string(), digest(entry.path()));
    }
  }
  return result;
}

Inventory dependencies(const std::vector<std::string>& paths) {
  Inventory result;
  for (const auto& path : paths) {
    result.emplace(path, digest(path));
  }
  return result;
}

bool matches(const rapidjson::Value& object, const Inventory& files) {
  if (!object.IsObject() || object.MemberCount() != files.size()) {
    return false;
  }
  for (const auto& [name, hash] : files) {
    const auto found = object.FindMember(name.c_str());
    if (found == object.MemberEnd() || !found->value.IsString() || hash != found->value.GetString()) {
      return false;
    }
  }
  return true;
}

void write_inventory(rapidjson::Writer<rapidjson::StringBuffer>& writer, const Inventory& files) {
  writer.StartObject();
  for (const auto& [path, hash] : files) {
    writer.Key(path.c_str());
    writer.String(hash.c_str());
  }
  writer.EndObject();
}

void copy_tree(const fs::path& source, const fs::path& destination) {
  fs::create_directories(destination);
  for (const auto& entry : fs::recursive_directory_iterator(source)) {
    const auto target = destination / entry.path().lexically_relative(source);
    if (entry.is_directory()) {
      fs::create_directories(target);
    } else if (entry.is_regular_file() && !entry.is_symlink()) {
      fs::create_directories(target.parent_path());
      fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
    } else {
      throw std::runtime_error("unsupported physical cache artifact");
    }
  }
}
}  // namespace

Physical_cache::Physical_cache(fs::path directory, fs::path compiled, std::string descriptor, std::vector<std::string> dependencies)
    : directory_(std::move(directory))
    , compiled_(std::move(compiled))
    , descriptor_(std::move(descriptor))
    , dependencies_(std::move(dependencies)) {}

bool Physical_cache::restore(const fs::path& net, const fs::path& place) {
  reason_ = "no completed generation";
  try {
    std::ifstream input(directory_ / "manifest.json");
    if (!input) {
      return false;
    }
    const std::string   text{std::istreambuf_iterator<char>(input), {}};
    rapidjson::Document manifest;
    manifest.Parse(text.c_str());
    reason_ = "incompatible or incomplete generation";
    if (manifest.HasParseError() || !manifest.IsObject() || !manifest.HasMember("schema") || !manifest["schema"].IsInt()
        || manifest["schema"].GetInt() != 1 || !manifest.HasMember("descriptor") || !manifest["descriptor"].IsString()
        || manifest["descriptor"].GetString() != descriptor_ || !manifest.HasMember("compiled")
        || !manifest.HasMember("dependencies") || !manifest.HasMember("artifacts")) {
      return false;
    }
    reason_ = "compiled design changed";
    if (!matches(manifest["compiled"], inventory(compiled_))) {
      return false;
    }
    reason_ = "technology or timing inputs changed";
    if (!matches(manifest["dependencies"], dependencies(dependencies_))) {
      return false;
    }
    reason_              = "damaged physical generation";
    const auto artifacts = directory_ / "artifacts";
    if (!matches(manifest["artifacts"], inventory(artifacts)) || !fs::is_regular_file(artifacts / "net/library.txt")
        || !fs::is_regular_file(artifacts / "place/out.def") || !fs::is_regular_file(artifacts / "place/qor.json")) {
      return false;
    }
    // Validate the complete generation before replacing any destination file.
    // Optional reports absent from this generation must not survive from a
    // different placement in the same workdir (e.g. pre-routing now disabled).
    for (const auto* name : kPlaceArtifacts) {
      if (!fs::exists(artifacts / "place" / name)) {
        fs::remove(place / name);
      }
    }
    fs::remove_all(net);
    copy_tree(artifacts / "net", net);
    copy_tree(artifacts / "place", place);
    reason_ = "completed repaired state restored";
    return true;
  } catch (const std::exception& error) {
    reason_ = error.what();
    return false;
  }
}

fs::path Physical_cache::placement_hint() const {
  try {
    std::ifstream input(directory_ / "manifest.json");
    if (!input) {
      return {};
    }
    const std::string   text{std::istreambuf_iterator<char>(input), {}};
    rapidjson::Document manifest;
    manifest.Parse(text.c_str());
    if (manifest.HasParseError() || !manifest.IsObject() || !manifest.HasMember("schema") || !manifest["schema"].IsInt()
        || manifest["schema"].GetInt() != 1 || !manifest.HasMember("descriptor") || !manifest["descriptor"].IsString()
        || manifest["descriptor"].GetString() != descriptor_ || !manifest.HasMember("dependencies")
        || !matches(manifest["dependencies"], dependencies(dependencies_)) || !manifest.HasMember("artifacts")
        || !manifest["artifacts"].IsObject()) {
      return {};
    }
    const auto found = manifest["artifacts"].FindMember("place/out.def");
    const auto path  = directory_ / "artifacts/place/out.def";
    if (found == manifest["artifacts"].MemberEnd() || !found->value.IsString() || fs::is_symlink(path) || !fs::is_regular_file(path)
        || digest(path) != found->value.GetString()) {
      return {};
    }
    return path;
  } catch (const std::exception&) {
    // Hints are optional: a damaged or stale artifact never blocks a fresh run.
    return {};
  }
}

void Physical_cache::publish(const fs::path& net, const fs::path& place) {
  const auto pending = fs::path(directory_.string() + ".pending");
  fs::remove_all(pending);
  fs::create_directories(pending / "artifacts/place");
  copy_tree(net, pending / "artifacts/net");
  for (const auto* name : kPlaceArtifacts) {
    if (fs::is_regular_file(place / name)) {
      fs::create_directories((pending / "artifacts/place" / name).parent_path());
      fs::copy_file(place / name, pending / "artifacts/place" / name);
    }
  }
  const auto artifacts = inventory(pending / "artifacts");
  if (!artifacts.contains("net/library.txt") || !artifacts.contains("place/out.def") || !artifacts.contains("place/qor.json")) {
    throw std::runtime_error("incomplete final physical state");
  }
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  writer.StartObject();
  writer.Key("schema");
  writer.Int(1);
  writer.Key("descriptor");
  writer.String(descriptor_.c_str());
  writer.Key("compiled");
  write_inventory(writer, inventory(compiled_));
  writer.Key("dependencies");
  write_inventory(writer, dependencies(dependencies_));
  writer.Key("artifacts");
  write_inventory(writer, artifacts);
  writer.EndObject();
  std::ofstream output(pending / "manifest.json");
  output << buffer.GetString() << '\n';
  output.close();
  if (!output) {
    throw std::runtime_error("cannot publish physical cache manifest");
  }
  const auto previous = fs::path(directory_.string() + ".previous");
  fs::remove_all(previous);
  if (fs::exists(directory_)) {
    fs::rename(directory_, previous);
  }
  fs::rename(pending, directory_);
  fs::remove_all(previous);
}

}  // namespace lhd
