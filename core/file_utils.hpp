//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace livehd::file_utils {

// Directory holding the running binary (used to locate bundled tools/scripts).
[[nodiscard]] std::string get_exe_path();

// Whole-file read (binary, no newline translation). nullopt when the file
// cannot be opened; the error policy stays at the caller, whose contracts
// differ (skip / throw / start-empty).
[[nodiscard]] inline std::optional<std::string> read_file(const std::string& path) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open()) {
    return std::nullopt;
  }
  std::ostringstream ss;
  ss << ifs.rdbuf();
  return std::move(ss).str();
}

}  // namespace livehd::file_utils
