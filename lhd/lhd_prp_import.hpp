//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The one on-disk Pyrope import-resolution contract shared by normal compile
// discovery and the incremental closure scanner. Importer-directory-relative,
// case-sensitive, no ancestor/cwd fallback.

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace lhd {

namespace import_detail {

inline std::string dir_of(std::string_view path) {
  const auto slash = path.rfind('/');
  return slash == std::string_view::npos ? std::string(".") : std::string(path.substr(0, slash));
}

inline std::string unit_name_of(std::string_view path) {
  const auto slash = path.rfind('/');
  const auto base  = slash == std::string_view::npos ? path : path.substr(slash + 1);
  const auto dot   = base.rfind('.');
  return std::string(dot == std::string_view::npos ? base : base.substr(0, dot));
}

inline std::string abspath_of(std::string_view path) {
  std::error_code ec;
  auto            abs = std::filesystem::absolute(std::filesystem::path(path), ec);
  return ec ? std::string(path) : abs.lexically_normal().string();
}

inline std::vector<std::string> candidates(std::string_view raw) {
  std::vector<std::string> names;
  const auto               slash = raw.rfind('/');
  const auto               dot   = raw.rfind('.');
  if (dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash)) {
    names.emplace_back(raw.substr(0, dot));
  }
  names.emplace_back(raw);
  return names;
}

class Resolver {
public:
  std::string find(const std::string& importer_dir, const std::string& stem) {
    std::string scan_dir = importer_dir;
    std::string leaf     = stem;
    if (const auto slash = stem.rfind('/'); slash != std::string::npos) {
      scan_dir = (std::filesystem::path(importer_dir) / stem.substr(0, slash)).lexically_normal().string();
      leaf     = stem.substr(slash + 1);
    }
    auto [listing, first] = listings_.try_emplace(scan_dir);
    if (first) {
      std::error_code ec;
      for (std::filesystem::directory_iterator it(scan_dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) {
          continue;
        }
        const auto filename = it->path().filename().string();
        if (filename.ends_with(".prp")) {
          listing->second.emplace(filename, it->path().string());
        }
      }
    }
    const auto found = listing->second.find(leaf + ".prp");
    return found == listing->second.end() ? std::string{} : found->second;
  }

private:
  std::map<std::string, std::map<std::string, std::string>> listings_;
};

}  // namespace import_detail

}  // namespace lhd
