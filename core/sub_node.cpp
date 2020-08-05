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

std::vector<Sub_node::IO_pin> Sub_node::get_sorted_io_pins() const {

  std::vector<IO_pin> slist;
  for (auto i = 1u; i < io_pins.size(); ++i) {
    slist.emplace_back(io_pins[i]);
  }

  // Sort based on port_id first, then name
  std::sort(slist.begin(), slist.end(), [](const IO_pin &a, const IO_pin &b) {
    if (a.graph_io_pos == Port_invalid && b.graph_io_pos == Port_invalid)
      return a.name < b.name;
    if (a.graph_io_pos == Port_invalid)
      return true;
    if (b.graph_io_pos == Port_invalid)
      return false;

    return a.graph_io_pos < b.graph_io_pos;
  });

  // make sure that if a graph_pos was explicit, it is respected
  Port_ID pos = 0;
  for (auto &p : slist) {
    pos++;
    if (p.graph_io_pos == Port_invalid)
      continue;
    if (p.graph_io_pos == pos)
      continue;

    auto pos_swap = p.graph_io_pos;
    int ntries = slist.size();
    while (ntries) {
      ntries--;
      std::swap(slist[pos], slist[pos_swap]);
      if (pos_swap>pos)
        break;
      if (slist[pos].graph_io_pos == pos || slist[pos].graph_io_pos == Port_invalid)
        break;
      pos_swap = slist[pos].graph_io_pos;
    }
  }

  return slist;
}

void Sub_node::populate_graph_pos() {
  if (graph_pos2instance_pid.size() == io_pins.size()-1)
    return; // all the pins are already populated

  Port_ID pos=0;
  for (auto &sorted_pin : get_sorted_io_pins()) {
    pos++;
    if (sorted_pin.graph_io_pos != pos)
      map_graph_pos(sorted_pin.name, sorted_pin.dir, pos);
  }
}
