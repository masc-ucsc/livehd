//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "sub_node.hpp"

void Sub_node::to_json(rapidjson::PrettyWriter<rapidjson::StringBuffer> &writer) const {

  writer.Key("lgid");
  writer.Uint64(lgid);

  writer.Key("name");
  writer.String(name.c_str());

  writer.Key("io_pins");
  writer.StartArray();
  bool skip_first = false;
  for(const auto &pin:io_pins) {
    if (!skip_first) {
      skip_first = true;
      continue;
    }
    writer.StartObject();

    writer.Key("name");
    writer.String(pin.name.c_str());

    writer.Key("graph_io_pid");
    writer.Uint(pin.graph_io_pid);

    writer.Key("dir");
    if (pin.dir == Direction::Invalid)
      writer.String("inv");
    else if (pin.dir == Direction::Output)
      writer.String("out");
    else if (pin.dir == Direction::Input)
      writer.String("inp");
    else
      I(false);

    writer.EndObject();
  }

  writer.EndArray();
}

void Sub_node::from_json(const rapidjson::Value &entry) {

  I(entry.HasMember("lgid"));
  I(entry.HasMember("name"));

  io_pins.resize(1); // No id ZERO

  const rapidjson::Value &io_pins_array = entry["io_pins"];
  I(io_pins_array.IsArray());
  for(const auto &io_pin : io_pins_array.GetArray()) {
    I(io_pin.IsObject());

    I(io_pin.HasMember("name"));
    I(io_pin.HasMember("dir"));
    I(io_pin.HasMember("graph_io_pid"));

    std::string dir_str = io_pin["dir"].GetString();
    Direction dir;
    if (dir_str == "inv")
      dir = Direction::Invalid;
    else if (dir_str == "out")
      dir = Direction::Output;
    else if (dir_str == "inp")
      dir = Direction::Input;
    else
      I(false);

    io_pins.emplace_back(io_pin["name"].GetString()
        ,dir
        ,io_pin["graph_io_pid"].GetUint());
  }

}

