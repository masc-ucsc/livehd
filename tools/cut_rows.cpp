// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Row-cut/endcap/tap parity audit tool; does not claim an aligned handoff.
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "pass/place/handoff_rows.hpp"
#include "pass/place/routing_tracks.hpp"

int main(int argc, char** argv) {
  namespace ph = livehd::physical;
  try {
    std::vector<std::string> lefs;
    std::string              def, output, endcap, tap, tracks;
    int64_t                  hx = -1, hy = -1, minimum = -1, distance = -1;
    for (int i = 1; i < argc; ++i) {
      const std::string key = argv[i];
      if (i + 1 == argc) {
        throw std::invalid_argument("missing argument value");
      }
      const std::string value = argv[++i];
      if (key == "--lef") {
        lefs.push_back(value);
      } else if (key == "--def") {
        def = value;
      } else if (key == "--tap-master") {
        tap = value;
      } else if (key == "--endcap-master") {
        endcap = value;
      } else if (key == "--output") {
        output = value;
      } else if (key == "--tracks") {
        tracks = value;
      } else {
        size_t     consumed = 0;
        const auto n        = std::stoll(value, &consumed);
        if (consumed != value.size()) {
          throw std::invalid_argument("expected integral DBU");
        }
        if (key == "--tap-distance-dbu") {
          distance = n;
        } else if (key == "--halo-x-dbu") {
          hx = n;
        } else if (key == "--halo-y-dbu") {
          hy = n;
        } else if (key == "--min-row-width-dbu") {
          minimum = n;
        } else {
          throw std::invalid_argument("unknown argument: " + key);
        }
      }
    }
    if (lefs.empty() || def.empty() || output.empty() || hx < 0 || hy < 0 || minimum < 0) {
      throw std::invalid_argument("cut_rows requires --lef, --def, --output, --halo-x-dbu, --halo-y-dbu, --min-row-width-dbu");
    }
    const auto tech = ph::read_lef(lefs);
    auto       fp   = ph::read_def(def);
    if (!tracks.empty()) {
      std::ifstream file(tracks);
      if (!file) {
        throw std::invalid_argument("cannot read tracks file: " + tracks);
      }
      std::ostringstream text;
      text << file.rdbuf();
      livehd::placement::apply_routing_tracks(tech, fp, text.str());
    }
    fp.rows = livehd::placement::cut_macro_rows(fp, tech, hx, hy, minimum);
    if (!endcap.empty()) {
      auto cells = livehd::placement::row_endcaps(fp, tech, endcap);
      fp.components.insert(fp.components.end(), cells.begin(), cells.end());
    }
    if (!tap.empty()) {
      auto cells = livehd::placement::row_taps(fp, tech, tap, distance);
      fp.components.insert(fp.components.end(), cells.begin(), cells.end());
    }
    ph::write_def(fp, output);
    ph::verify_def_roundtrip(fp, ph::read_def(output));
  } catch (const std::exception& e) {
    std::cerr << "cut_rows: " << e.what() << '\n';
    return 1;
  }
}
