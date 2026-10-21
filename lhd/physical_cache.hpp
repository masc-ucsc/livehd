// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace lhd {

// One completed physical generation: repaired color bodies, inter-color
// buffers, placement/RC attributes and their final boundary feedback travel
// together. A partial/edited input falls back to ordinary region mapping.
// Never authorize replay by overwriting the old mapper's IO environment.
class Physical_cache {
public:
  Physical_cache(std::filesystem::path directory, std::filesystem::path compiled, std::string descriptor,
                 std::vector<std::string> dependencies);
  bool               restore(const std::filesystem::path& net, const std::filesystem::path& place);
  void               publish(const std::filesystem::path& net, const std::filesystem::path& place);
  const std::string& reason() const { return reason_; }

private:
  std::filesystem::path    directory_, compiled_;
  std::string              descriptor_, reason_;
  std::vector<std::string> dependencies_;
};

}  // namespace lhd
