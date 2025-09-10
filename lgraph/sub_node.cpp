//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "sub_node.hpp"

#include <print>

void Sub_node::copy_from(std::string_view new_name, Lg_type_id new_lgid, const Sub_node &sub) {
  name                   = new_name;
  lgid                   = new_lgid;
  io_pins                = sub.io_pins;
  name2id                = sub.name2id;
  graph_pos2instance_pid = sub.graph_pos2instance_pid;
}

void Sub_node::to_json(rapidjson::PrettyWriter<rapidjson::StringBuffer> &writer) const {
  writer.Key("lgid");
  writer.Uint64(lgid);

  writer.Key("name");
  writer.String(name.c_str());

  writer.Key("loop_last");
  writer.Bool(loop_last);

  writer.Key("loop_first");
  writer.Bool(loop_first);

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

    writer.Key("bits");
    writer.Uint(pin.bits);

    writer.Key("dir");
    if (pin.dir == Direction::Invalid) {
      writer.String("inv");
    } else if (pin.dir == Direction::Output) {
      writer.String("out");
    } else if (pin.dir == Direction::Input) {
      writer.String("inp");
    } else {
      I(false);
    }

    writer.EndObject();
    pos++;
  }

  writer.EndArray();
}

void Sub_node::from_json(const rapidjson::Value &entry) {
  I(entry.HasMember("lgid"));
  I(entry.HasMember("name"));
  I(entry["name"].IsString());
  I(entry["loop_last"].IsBool());
  I(entry["loop_first"].IsBool());

  lgid       = entry["lgid"].GetUint64();
  name       = entry["name"].GetString();
  loop_last  = entry["loop_last"].GetBool();
  loop_first = entry["loop_first"].GetBool();

  io_pins.resize(1);  // No id ZERO

  const rapidjson::Value &io_pins_array = entry["io_pins"];
  I(io_pins_array.IsArray());
  for (const auto &io_pin : io_pins_array.GetArray()) {
    I(io_pin.IsObject());

    I(io_pin.HasMember("name"));
    I(io_pin.HasMember("dir"));

    std::string dir_str = io_pin["dir"].GetString();
    Direction   dir;
    if (dir_str == "inv") {
      dir = Direction::Invalid;
    } else if (dir_str == "out") {
      dir = Direction::Output;
    } else if (dir_str == "inp") {
      dir = Direction::Input;
    } else {
      dir = Direction::Invalid;
      I(false);
    }

    Port_ID pid = Port_invalid;
    if (io_pin.HasMember("graph_io_pos")) {
      pid = io_pin["graph_io_pos"].GetUint();
    }
    size_t instance_pid = io_pin["instance_pid"].GetUint();

    size_t bits = io_pin["bits"].GetUint();

    auto io_name     = io_pin["name"].GetString();
    name2id[io_name] = instance_pid;
    if (io_pins.size() <= instance_pid) {
      io_pins.resize(instance_pid + 1);
    }

    io_pins[instance_pid].name         = io_name;
    io_pins[instance_pid].dir          = dir;
    io_pins[instance_pid].graph_io_pos = pid;
    io_pins[instance_pid].bits         = bits;

    if (io_pins[instance_pid].is_invalid()) {
      deleted.emplace_back(instance_pid);
    }

    if (pid != Port_invalid) {
      map_pin_int(instance_pid, pid);
    }
  }

  std::sort(deleted.begin(), deleted.end(), std::greater<>());
}

/* LCOV_EXCL_START */
void Sub_node::dump() const {
  std::print("lgid:{} name:{} #iopins:{}\n", (int)lgid, name, io_pins.size());

  int pos = 0;
  for (const auto &pin : io_pins) {
    if (pos == 0) {
      pos++;
      continue;
    }
    std::string_view dir;
    if (pin.dir == Direction::Invalid) {
      dir = "invalid";
    } else if (pin.dir == Direction::Output) {
      dir = "out";
    } else if (pin.dir == Direction::Input) {
      dir = "inp";
    } else {
      dir = "ERROR";
    }

    std::print(" pin:{} name:{} pos:{} dir:{}\n", pos, pin.name, pin.graph_io_pos, dir);
    pos++;
  }
}
/* LCOV_EXCL_STOP */

std::vector<Sub_node::IO_pin> Sub_node::get_sorted_io_pins() const {
  std::vector<IO_pin> sort_io;
  for (auto i = 1u; i < io_pins.size(); ++i) {
    if (io_pins[i].is_invalid()) {
      continue;
    }
    sort_io.emplace_back(io_pins[i]);
  }

  // Sort based on port_id first, then name
  std::sort(sort_io.begin(), sort_io.end(), [](const IO_pin &a, const IO_pin &b) {
    if (a.graph_io_pos == Port_invalid && b.graph_io_pos == Port_invalid) {
      return a.name < b.name;
    }
    if (a.graph_io_pos == Port_invalid) {
      return true;
    }
    if (b.graph_io_pos == Port_invalid) {
      return false;
    }

    return a.graph_io_pos < b.graph_io_pos;
  });

#if 0
  // make sure that if a graph_pos was explicit, it is respected
  Port_ID pos = 0;
  for (auto &p : sort_io) {
    pos++;
    if (p.graph_io_pos == Port_invalid)
      continue;
    if (p.graph_io_pos == pos)
      continue;

    auto pos_swap = p.graph_io_pos;

    for (auto i=0u;i<sort_io.size();++i) {

      std::swap(sort_io[pos], sort_io[pos_swap]);
      if (pos_swap > pos)
        break;
      if (sort_io[pos].graph_io_pos == pos || sort_io[pos].graph_io_pos == Port_invalid)
        break;
      pos_swap = sort_io[pos].graph_io_pos;
    }
  }
#endif

  return sort_io;
}

void Sub_node::populate_graph_pos() {
  if (graph_pos2instance_pid.size() == io_pins.size() - 1) {
    return;  // all the pins are already populated
  }

  Port_ID pos = 0;
  for (auto &sorted_pin : get_sorted_io_pins()) {
    pos++;
    if (sorted_pin.graph_io_pos != pos) {
      map_graph_pos(sorted_pin.name, sorted_pin.dir, pos);
    }
  }
}

void Sub_node::del_pin(Port_ID instance_pid) {
  if (instance_pid == 0) {
    return;  // $ and % are special cases
  }

  I(has_instance_pin(instance_pid));

  name2id.erase(io_pins[instance_pid].name);
  auto pos = io_pins[instance_pid].graph_io_pos;
  if (pos != Port_invalid) {
    I(graph_pos2instance_pid.size() > pos);
    I(graph_pos2instance_pid[pos] != Port_invalid);
    graph_pos2instance_pid[pos] = Port_invalid;
  }
  auto keep_name = io_pins[instance_pid].name;
  io_pins[instance_pid].clear();  // do not erase to avoid remap of all the instance_pids (users)
  io_pins[instance_pid].name = keep_name;
  I(io_pins[instance_pid].instance_pid == instance_pid);

  deleted.emplace_back(instance_pid);
  std::sort(deleted.begin(), deleted.end(), std::greater<>());
}
