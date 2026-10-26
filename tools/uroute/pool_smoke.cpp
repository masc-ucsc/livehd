// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <iostream>
#include <stdexcept>

#include "dreamplace/native/placement.hpp"
#include "pass/place/uroute.hpp"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace up = livehd::placement;
namespace dp = DreamPlace::native;
int main(int argc, char** argv) {
  try {
    if (argc == 3 && std::string(argv[1]) == "export") {
      dp::Design d;
      d.layout             = {0, 0, 16, 16, 1, 1};
      d.node_x             = {1, 15, 6};
      d.node_y             = {2, 14, 0};
      d.node_size_x        = {1, 0, 4};
      d.node_size_y        = {1, 0, 16};
      d.node_macro         = {0, 0, 1};
      d.pin2node           = {0, 1};
      d.pin_offset_x       = {0, 0};
      d.pin_offset_y       = {0, 0};
      d.flat_net2pin       = {0, 1};
      d.flat_net2pin_start = {0, 2};
      d.net_weights        = {1};
      d.build_maps();
      const up::Uroute_identity id{"fixture", "tech", "macro", 16, 16, .5, 1000};
      up::export_uroute(argv[2], d, id, 4, 4, .1, .2);
      return 0;
    }
    if (argc != 9) {
      throw std::invalid_argument("usage: pool_smoke query.bin directory top tech shots min_shots fallback");
    }
    up::Uroute_options o;
    o.dir       = argv[2];
    o.shots     = std::stoi(argv[5]);
    o.min_shots = std::stoi(argv[6]);
    o.fallback  = argv[7];
    const up::Uroute_pool                      pool(o);
    up::Uroute_identity                        id{argv[3], argv[4], argv[8], 16, 16, .5, 1000};
    const auto                                 selected = pool.select(dp::read_uroute_maps(argv[1]), id);
    rapidjson::StringBuffer                    buffer;
    rapidjson::Writer<rapidjson::StringBuffer> out(buffer);
    out.StartObject();
    out.Key("skipped");
    out.String(selected.skipped.c_str());
    out.Key("usable");
    out.Bool(bool(selected.context));
    out.Key("cache_identity");
    out.String(selected.cache_identity.c_str());
    out.Key("sample_ids");
    out.StartArray();
    for (const auto& sample : selected.sample_ids) {
      out.String(sample.c_str());
    }
    out.EndArray();
    out.EndObject();
    std::cout << buffer.GetString() << '\n';
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
