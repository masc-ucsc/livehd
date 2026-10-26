// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Explicit offline proposal for correcting legacy macro origins to actual tracks.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "inou/lefdef/physical.hpp"
#include "pass/place/macro_tracks.hpp"

namespace ph = livehd::physical;
int main(int argc, char** argv) {
  try {
    std::string              definition, output;
    std::vector<std::string> lefs;
    bool                     move_fixed = false;
    int64_t                  limit      = 0;
    for (int i = 1; i < argc; ++i) {
      const std::string key = argv[i];
      if (key == "--move-fixed") {
        move_fixed = true;
        continue;
      }
      if (++i == argc) {
        throw std::invalid_argument("missing value for " + key);
      }
      if (key == "--def") {
        definition = argv[i];
      } else if (key == "--lef") {
        lefs.emplace_back(argv[i]);
      } else if (key == "--output") {
        output = argv[i];
      } else if (key == "--max-shift-dbu") {
        const std::string value    = argv[i];
        size_t            consumed = 0;
        limit                      = std::stoll(value, &consumed);
        if (consumed != value.size()) {
          throw std::invalid_argument("invalid DBU movement bound");
        }
      } else {
        throw std::invalid_argument("unknown argument: " + key);
      }
    }
    if (definition.empty() || lefs.empty() || output.empty() || (move_fixed && limit <= 0)) {
      throw std::invalid_argument(
          "usage: macro_track_align --def DEF --lef LEF [--lef LEF] --output TSV [--move-fixed --max-shift-dbu N]");
    }
    if (std::filesystem::exists(output)) {
      throw std::invalid_argument("refusing to overwrite an existing proposal");
    }
    const auto tech = ph::read_lef(lefs);
    const auto fp   = ph::read_def(definition);
    if (fp.dbu != tech.dbu || fp.tracks.empty()) {
      throw std::invalid_argument("matching DBU and actual DEF tracks are required");
    }
    auto cells = fp.components;
    for (auto& cell : cells) {
      const auto& master = tech.macros.at(cell.macro);
      cell.place.width   = std::llround(master.width * fp.dbu);
      cell.place.height  = std::llround(master.height * fp.dbu);
      if (ph::swaps_axes(cell.place.orientation)) {
        std::swap(cell.place.width, cell.place.height);
      }
      if (!master.klass.starts_with("CORE") && ph::placement_mask(cell, tech) == ph::Placement_mask::none) {
        cell.place.status = move_fixed ? ph::Status::PLACED : ph::Status::FIXED;
      }
    }
    livehd::placement::legalize_macro_tracks(tech, fp, cells, 0, 0, move_fixed);
    int64_t maximum = 0;
    size_t  moved   = 0;
    // Validate the complete proposal before opening the output file.
    for (size_t i = 0; i < cells.size(); ++i) {
      const auto&   old   = fp.components[i];
      const auto&   cell  = cells[i];
      const int64_t shift = std::abs(cell.place.x - old.place.x) + std::abs(cell.place.y - old.place.y);
      if (!shift) {
        continue;
      }
      if (!move_fixed || tech.macros.at(cell.macro).klass.starts_with("CORE") || shift > limit) {
        throw std::runtime_error("proposal exceeds explicit macro-only movement bound: " + cell.name);
      }
      if (cell.name.find_first_of("\t\r\n") != std::string::npos) {
        throw std::invalid_argument("TSV cannot represent instance name");
      }
      maximum = std::max(maximum, shift);
      ++moved;
    }
    std::ofstream file(output);
    file << "instance\told_x\told_y\tx\ty\torientation\n";
    for (size_t i = 0; i < cells.size(); ++i) {
      const auto& old  = fp.components[i];
      const auto& cell = cells[i];
      if (old.place.x == cell.place.x && old.place.y == cell.place.y) {
        continue;
      }
      file << cell.name << '\t' << old.place.x << '\t' << old.place.y << '\t' << cell.place.x << '\t' << cell.place.y << '\t'
           << ph::orientation_name(cell.place.orientation) << '\n';
    }
    if (!file) {
      throw std::runtime_error("cannot write macro alignment proposal");
    }
    std::cout << "macros_moved=" << moved << " maximum_manhattan_shift_dbu=" << maximum << '\n';
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
