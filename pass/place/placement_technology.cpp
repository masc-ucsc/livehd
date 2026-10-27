// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "placement_technology.hpp"

#include <array>
#include <fstream>
#include <stdexcept>

#include "hash_util.hpp"
#include "place.hpp"

namespace livehd::placement {
Technology::Technology(const std::vector<std::string>& paths, const std::string& lib)
    : lefs(paths), liberty_path(lib), lef(paths.empty() ? physical::Technology{} : physical::read_lef(paths)), cells(lib) {
  content_hash = hash_util::fnv1a64(cells.contents);
  for (const auto& path : paths) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
      throw std::runtime_error("cannot read " + path);
    }
    std::array<char, 65536> buffer;
    while (input.read(buffer.data(), buffer.size()) || input.gcount()) {
      content_hash = hash_util::fnv1a64(std::string_view(buffer.data(), input.gcount()), content_hash);
    }
    if (!input.eof()) {
      throw std::runtime_error("incomplete LEF input: " + path);
    }
  }
}
const Technology& Options::technology() const {
  if (!technology_cache || technology_cache->lefs != lefs || technology_cache->liberty_path != liberty) {
    technology_cache = std::make_shared<const Technology>(lefs, liberty);
  }
  return *technology_cache;
}
}  // namespace livehd::placement
