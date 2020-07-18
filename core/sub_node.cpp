//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "sub_node.hpp"

void Sub_node::to_json(rapidjson::PrettyWriter<rapidjson::StringBuffer> &writer) const {
  writer.Key("lgid");
  writer.Uint64(lgid);

  writer.Key("name");
  writer.String(name.c_str());

  writer.Key("io_pins");
  writer.StartArray();
  int pos = 0;
  for (const auto &pin : io_pins) {
    if (pos == 0) {
      pos++;
      continue;
    }
    writer.StartObject();

    writer.Key("name");
    writer.String(pin.name.c_str());

    if (pin.graph_io_pos != Port_invalid) {
      writer.Key("graph_io_pos");
      writer.Uint(pin.graph_io_pos);
    }
    writer.Key("instance_pid");
    writer.Uint(pos);

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
    pos++;
  }

  writer.EndArray();
}

void Sub_node::from_json(const rapidjson::Value &entry) {
  I(entry.HasMember("lgid"));
  I(entry.HasMember("name"));
  I(entry["name"].IsString());

  lgid            = entry["lgid"].GetUint64();
  std::string str = entry["name"].GetString();
  name            = str;

  io_pins.resize(1);  // No id ZERO

  const rapidjson::Value &io_pins_array = entry["io_pins"];
  I(io_pins_array.IsArray());
  for (const auto &io_pin : io_pins_array.GetArray()) {
    I(io_pin.IsObject());

    I(io_pin.HasMember("name"));
    I(io_pin.HasMember("dir"));

    std::string dir_str = io_pin["dir"].GetString();
    Direction   dir;
    if (dir_str == "inv")
      dir = Direction::Invalid;
    else if (dir_str == "out")
      dir = Direction::Output;
    else if (dir_str == "inp")
      dir = Direction::Input;
    else {
      dir = Direction::Invalid;
      I(false);
    }

    Port_ID pid = Port_invalid;
    if (io_pin.HasMember("graph_io_pos")) {
      pid = io_pin["graph_io_pos"].GetUint();
    }
    size_t instance_pid = io_pin["instance_pid"].GetUint();

    auto io_name     = io_pin["name"].GetString();
    name2id[io_name] = instance_pid;
    if (io_pins.size() <= instance_pid)
      io_pins.resize(instance_pid + 1);

    io_pins[instance_pid].name         = io_name;
    io_pins[instance_pid].dir          = dir;
    io_pins[instance_pid].graph_io_pos = pid;

    if (pid != Port_invalid) {
      map_pin_int(instance_pid, pid);
    }
  }
}

void Sub_node::dump() const {
  fmt::print("lgid:{} name:{} #iopins:{}\n", lgid, name, io_pins.size());

  int pos = 0;
  for (const auto &pin : io_pins) {
    if (pos == 0) {
      pos++;
      continue;
    }
    fmt::print(" pin:{} name:{} pos:{} dir:{}\n", pos, pin.name, pin.graph_io_pos, pin.dir);
    pos++;
  }
}
