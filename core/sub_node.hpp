//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/prettywriter.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/span.h"

#include "lgraph_base_core.hpp"
#include "tech_library.hpp"

class Sub_node {
protected:

  void add_phys_pin_int(const Port_ID instance_pid, const Tech_pin &ppin) {
    I(io_pins.size() > instance_pid);
#ifndef NDEBUG
    // Make sure not to double insert
    for(const auto &p:io_pins[instance_pid].phys) {
      I(!p.overlap(ppin));
    }
#endif
    io_pins[instance_pid].phys.emplace_back(ppin);
  }

public:

  enum class Direction { Invalid, Output, Input };

  struct Physical_cell {
    Physical_cell() : height(0), width(0) { }
    float height;
    float width;
  };

  struct IO_pin {
    IO_pin() : dir(Direction::Invalid), graph_io_pos(Port_invalid) { }
    IO_pin(std::string_view _name, Direction _dir, Port_ID _graph_io_pos)
    :name(_name)
    ,dir(_dir)
    ,graph_io_pos(_graph_io_pos) {
    }
    std::string           name;
    Direction             dir;
    Port_ID               graph_io_pos;
    std::vector<Tech_pin> phys; // There could be more than one location per pin

    bool is_mapped() const { return graph_io_pos != Port_invalid; }
    bool is_input()  const { return dir == Direction::Input; }
    bool is_output() const { return dir == Direction::Output; }
    Port_ID get_graph_pos() const { return graph_io_pos; }
  };

private:
  std::string      name;
  Lg_type_id       lgid;
  Physical_cell    phys;

  std::vector<IO_pin> io_pins;

  absl::flat_hash_map<std::string, Port_ID> name2id;
  std::vector<Port_ID> graph_pos2instance_pid;

  void map_pin_int(Port_ID instance_pid, Port_ID graph_pos) {
    I(graph_pos!=Port_invalid);
    I(instance_pid); // Must be non zero for input/output pid
    I(io_pins[instance_pid].graph_io_pos == graph_pos);

    if (graph_pos2instance_pid.size()<=graph_pos) {
      graph_pos2instance_pid.resize(graph_pos+1, Port_invalid);
      I(graph_pos2instance_pid[graph_pos]==Port_invalid);
    }else{
      I(graph_pos2instance_pid[graph_pos]==Port_invalid || graph_pos2instance_pid[graph_pos]==instance_pid);
    }
    graph_pos2instance_pid[graph_pos] = instance_pid;
  }

public:
  Sub_node() {
    name.clear();
    expunge();
  }
  //Sub_node(const Sub_node &s) = delete;
  Sub_node & operator=(const Sub_node&) = delete;

  void to_json(rapidjson::PrettyWriter<rapidjson::StringBuffer> &writer) const;
  void from_json(const rapidjson::Value &entry);

  void reset_pins() {
    clear_io_pins();
    io_pins.clear();   // WARNING: Do NOT remove mappings, just port id. (allows to reload designs)
    io_pins.resize(1); // No id ZERO
    name2id.clear();
  }

  void reset(std::string_view _name, Lg_type_id _lgid) {
    name = _name;
    lgid = _lgid;

    reset_pins();
  }

  void rename(std::string_view _name) {
    I(!is_invalid());
    I(name != _name);
    name = _name;
  }

  void expunge() {
    name2id.clear();
    io_pins.clear();
    lgid = 0;
  }

  bool is_black_box() const {
    return graph_pos2instance_pid.empty(); // BBox if we still do not know how to map from instance_pid to pos
  }

  void clear_io_pins() {
    graph_pos2instance_pid.clear();
    //io_pins.clear();   // WARNING: Do NOT remove mappings, just port id. (allows to reload designs)
    //io_pins.resize(1); // No id ZERO
  }

  bool is_invalid() const { return lgid==0; }

  Lg_type_id get_lgid() const { I(lgid); return lgid; }

  std::string_view get_name() const { I(lgid); return name; }

  Port_ID add_input_pin(std::string_view io_name) {
    return add_pin(io_name, Direction::Input);
  }

  Port_ID add_output_pin(std::string_view io_name) {
    return add_pin(io_name, Direction::Output);
  }

  Port_ID add_pin(std::string_view io_name, Direction dir, Port_ID graph_pos=Port_invalid) {
    I(lgid);
    I(!has_pin(io_name));
    Port_ID instance_pid = io_pins.size();
    io_pins.emplace_back(io_name, dir, graph_pos);
    name2id[io_name] = instance_pid;
    I(io_pins[instance_pid].name == io_name);
    if (graph_pos != Port_invalid)
      map_pin_int(instance_pid, graph_pos);

    return instance_pid;
  }

  Port_ID map_graph_pos(std::string_view io_name, Direction dir, Port_ID graph_pos) {
    I(graph_pos); // 0 possition is also not used (to catch bugs)
    I(has_pin(io_name));

    Port_ID instance_pid = name2id[io_name];
    I(io_pins[instance_pid].name == io_name);
    I(io_pins[instance_pid].graph_io_pos == graph_pos || !has_graph_pin(graph_pos));
    io_pins[instance_pid].dir = dir;

    if (graph_pos != Port_invalid) {
      io_pins[instance_pid].graph_io_pos = graph_pos;
      map_pin_int(instance_pid, graph_pos);
    }

    return instance_pid;
  }

  bool has_pin(std::string_view io_name) const { I(lgid); return name2id.find(io_name) != name2id.end(); }
  bool has_graph_pin(Port_ID graph_pos) const { I(lgid); return graph_pos2instance_pid.size()>graph_pos && graph_pos2instance_pid[graph_pos]!=Port_invalid; }
  bool has_instance_pin(Port_ID instance_pid) const { I(lgid); return io_pins.size()>instance_pid; }

  Port_ID get_instance_pid(std::string_view io_name) const {
    has_pin(io_name);
    return name2id.at(io_name);
  }

  Port_ID get_graph_pos(std::string_view io_name) const {
    auto instance_pid = get_instance_pid(io_name);
    return io_pins[instance_pid].graph_io_pos;
  }

  Port_ID get_instance_pid_from_graph_pos(Port_ID graph_pos) const {
    I(has_graph_pin(graph_pos));
    return graph_pos2instance_pid[graph_pos];
  }

  const IO_pin &get_io_pin_from_graph_pos(Port_ID graph_pos) const {
    I(has_graph_pin(graph_pos));
    return io_pins[graph_pos2instance_pid[graph_pos]];
  }

  const IO_pin &get_io_pin_from_instance_pid(Port_ID instance_pid) const {
    I(has_instance_pin(instance_pid));
    return io_pins[instance_pid];
  }

  Port_ID get_graph_pos_from_instance_pid(Port_ID instance_pid) const {
    I(has_instance_pin(instance_pid));
    return io_pins[instance_pid].graph_io_pos;
  }

  const IO_pin &get_pin(std::string_view io_name) const {
    I(has_pin(io_name));
    auto instance_pid = name2id.at(io_name);
    I(io_pins[instance_pid].name == io_name);
    return io_pins[instance_pid];
  }

  const IO_pin &get_graph_output_io_pin(std::string_view io_name) const {
    I(has_pin(io_name));
    auto instance_pid = name2id.at(io_name);
    I(io_pins[instance_pid].name == io_name);
    I(io_pins[instance_pid].dir == Direction::Output);
    return io_pins[instance_pid];
  }

  const IO_pin &get_graph_input_io_pin(std::string_view io_name) const {
    I(has_pin(io_name));
    auto instance_pid = name2id.at(io_name);
    I(io_pins[instance_pid].name == io_name);
    I(io_pins[instance_pid].dir == Direction::Input);
    return io_pins[instance_pid];
  }

  Port_ID get_graph_io_pos(std::string_view io_name) const {
    I(has_pin(io_name));
    auto instance_pid = name2id.at(io_name);
    I(io_pins[instance_pid].name == io_name);
    return io_pins[instance_pid].graph_io_pos;
  }

  std::string_view get_name_from_instance_pid(Port_ID instance_pid) const {
    I(has_instance_pin(instance_pid));
    return io_pins[instance_pid].name;
  }

  std::string_view get_name_from_graph_pos(Port_ID graph_pos) const {
    I(has_graph_pin(graph_pos)); // The pos does not seem to exist
    return io_pins[graph_pos2instance_pid[graph_pos]].name;
  }

  bool is_input_from_instance_pid(Port_ID instance_pid) const {
    I(has_instance_pin(instance_pid));
    return io_pins[instance_pid].dir == Direction::Input;
  }

  bool is_input_from_graph_pos(Port_ID graph_pos) const {
    I(has_graph_pin(graph_pos));
    return io_pins[graph_pos2instance_pid[graph_pos]].dir == Direction::Input;
  }

  bool is_input(std::string_view io_name) const {
    I(has_pin(io_name));

    auto instance_pid = name2id.at(io_name);
    I(io_pins[instance_pid].name == io_name);
    return (io_pins.at(instance_pid).dir == Direction::Input);
  }

  bool is_output_from_instance_pid(Port_ID instance_pid) const {
    I(has_instance_pin(instance_pid));
    return io_pins[instance_pid].dir == Direction::Output;
  }

  bool is_output_from_graph_pos(Port_ID graph_pos) const {
    I(has_graph_pin(graph_pos));
    return io_pins[graph_pos2instance_pid[graph_pos]].dir == Direction::Output;
  }

  bool is_output(std::string_view io_name) const {
    I(has_pin(io_name));

    auto instance_pid = name2id.at(io_name);
    I(io_pins[instance_pid].name == io_name);
    return (io_pins[instance_pid].dir == Direction::Output);
  }

  void add_phys_pin(std::string_view io_name, const Tech_pin &ppin) {
    I(has_pin(io_name));
    add_phys_pin_int(name2id.at(io_name), ppin);
  }

  void add_phys_pin_from_instance_pid(Port_ID instance_pid, const Tech_pin &ppin) {
    I(has_instance_pin(instance_pid));
    add_phys_pin_int(instance_pid, ppin);
  }

  void add_phys_pin_from_graph_pos(Port_ID graph_pos, const Tech_pin &ppin) {
    I(has_graph_pin(graph_pos));
    add_phys_pin_int(graph_pos2instance_pid[graph_pos], ppin);
  }

  const absl::Span<const IO_pin> get_io_pins() const { I(io_pins.size()>=1); return absl::MakeSpan(io_pins).subspan(1); }

  void set_phys(const Physical_cell &&cphys) {
    I(lgid);
    phys = cphys;
  }

  const Physical_cell &get_phys() const {
    I(lgid);
    return phys;
  }

  Physical_cell *ref_phys() {
    I(lgid);
    return &phys;
  }

};
