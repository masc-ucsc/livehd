// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace lhd {
// Publish a complete exported file without opening the old destination for
// writing. Retain its permissions when replacing it; a new export inherits
// the source permissions. Existing hardlink/symlink referents are not written.
void copy_export_file(const std::filesystem::path& source, const std::filesystem::path& destination);
struct Lef_view {
  std::filesystem::path source;
  std::filesystem::path relative_path;
};
void export_lef_views(const std::filesystem::path& directory, const std::vector<Lef_view>& views,
                      std::vector<std::string>& outputs);
}  // namespace lhd
