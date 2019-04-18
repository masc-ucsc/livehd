//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "bm.h"
#include "bmsparsevec.h"

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/prettywriter.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/span.h"

#include "lgraph_base_core.hpp"
#include "tech_library.hpp"

//using bmsparse = bm::sparse_vector<uint32_t, bm::bvector<> >;
using bmsparse = bm::bvector<>;

class Sub_node {
protected:
  bmsparse input_ids;
  bmsparse output_ids;

  void add_phys_pin_int(Port_ID instance_pid, const Tech_pin &ppin) {
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
    IO_pin() : dir(Direction::Invalid), graph_io_idx(0), graph_io_pid(0) { }
    IO_pin(std::string_view _name, Direction _dir, Index_ID _graph_io_idx, Port_ID _graph_io_pid)
    :name(_name)
    ,dir(_dir)
    ,graph_io_idx(_graph_io_idx)
    ,graph_io_pid(_graph_io_pid) {
    }
    std::string           name;
    Direction             dir;
    Index_ID              graph_io_idx;
    Port_ID               graph_io_pid;
    std::vector<Tech_pin> phys; // There could be more than one location per pin
  };

private:
  std::string      name;
  Lg_type_id       lgid;
  Physical_cell    phys;

  std::vector<IO_pin> io_pins;

  absl::flat_hash_map<std::string, Port_ID> name2id;
  std::vector<Port_ID> graph_pid2instance_pid;

  void map_pin_int(Port_ID instance_pid, Index_ID idx, Port_ID graph_pid) {
    I(idx);
    I(instance_pid);

    if (io_pins[instance_pid].dir == Direction::Input) {
      input_ids.set(idx, true);
    }else{
      I(io_pins[instance_pid].dir == Direction::Output);
      output_ids.set(idx, true);
    }

    if (graph_pid2instance_pid.size()<=graph_pid)
      graph_pid2instance_pid.resize(graph_pid+1);
    I(graph_pid2instance_pid[graph_pid]==0);
    graph_pid2instance_pid[graph_pid] = instance_pid;
  }

public:
  Sub_node() {
    name.clear();
    expunge();
  }

  void to_json(rapidjson::PrettyWriter<rapidjson::StringBuffer> &writer) const;
  void from_json(const rapidjson::Value &entry);

  void setup(std::string_view _name, Lg_type_id _lgid) {
    I(io_pins.empty());
    io_pins.resize(1); // No id ZERO
    name = _name;
    lgid   = _lgid;
  }

  void rename(std::string_view _name) {
    I(!is_invalid());
    I(name != _name);
    name = _name;
  }

  void expunge() {
    input_ids.clear();
    output_ids.clear();
    io_pins.clear();
    lgid = 0;
  }

  void clear_io_pins() {
    io_pins.clear();
    io_pins.resize(1); // No id ZERO
    input_ids.clear();
    output_ids.clear();
  }

  bool is_invalid() const { return lgid==0; }

  Lg_type_id get_lgid() const { I(lgid); return lgid; }

  std::string_view get_name() const { I(lgid); return name; }

  Port_ID add_pin(std::string_view name, Direction dir, Index_ID graph_idx=0, Port_ID graph_pid=0) {
    I(lgid);
    Port_ID instance_pid = io_pins.size();
    io_pins.emplace_back(name, dir, graph_idx, graph_pid);
    name2id[name] = instance_pid;
    if (graph_idx)
      map_pin_int(instance_pid, graph_idx, graph_pid);

    return instance_pid;
  }

  Port_ID map_graph_pid(std::string_view name, Index_ID graph_idx, Port_ID graph_pid) {
    I(has_pin(name));
    I(!has_graph_pin(graph_pid));
    I(graph_idx);

    Port_ID instance_pid = name2id[name];
    I(io_pins[instance_pid].graph_io_idx==0); // remap not supported (could be added if needed as remap_pin)
    io_pins[instance_pid].graph_io_idx = graph_idx;
    io_pins[instance_pid].graph_io_pid = graph_pid;

    map_pin_int(instance_pid, graph_idx, graph_pid);

    return instance_pid;
  }

  bool has_pin(std::string_view name) const { I(lgid); return name2id.find(name) != name2id.end(); }
  bool has_graph_pin(Port_ID graph_pid) const { I(lgid); return graph_pid2instance_pid.size()>graph_pid && graph_pid2instance_pid[graph_pid]!=0; }
  bool has_instance_pin(Port_ID instance_pid) const { I(lgid); return io_pins.size()>instance_pid; }

  Port_ID get_instance_pid(std::string_view name) const {
    has_pin(name);
    return name2id.at(name);
  }

  Port_ID get_instance_pid_from_graph_pid(Port_ID graph_pid) const {
    I(has_graph_pin(graph_pid));
    return graph_pid2instance_pid[graph_pid];
  }

  Port_ID get_graph_pid_from_instance_pid(Port_ID instance_pid) const {
    I(has_instance_pin(instance_pid));
    return io_pins[instance_pid].graph_io_pid;
  }

  const IO_pin &get_pin(std::string_view name) const {
    I(has_pin(name));
    auto instance_pid = name2id.at(name);
    return io_pins[instance_pid];
  }

  const IO_pin &get_graph_output_io_pin(std::string_view name) const {
    I(has_pin(name));
    auto instance_pid = name2id.at(name);
    I(io_pins[instance_pid].dir == Direction::Output);
    I(io_pins[instance_pid].graph_io_idx); // Must map before use
    return io_pins[instance_pid];
  }

  const IO_pin &get_graph_input_io_pin(std::string_view name) const {
    I(has_pin(name));
    auto instance_pid = name2id.at(name);
    I(io_pins[instance_pid].dir == Direction::Input);
    I(io_pins[instance_pid].graph_io_idx); // Must map before use
    return io_pins[instance_pid];
  }

  const Port_ID get_graph_io_pid(std::string_view name) const {
    I(has_pin(name));
    auto instance_pid = name2id.at(name);
    I(io_pins[instance_pid].graph_io_idx); // Must map before use
    return io_pins[instance_pid].graph_io_pid;
  }

  std::string_view get_name_from_instance_pid(Port_ID instance_pid) const {
    I(has_instance_pin(instance_pid));
    return io_pins[instance_pid].name;
  }

  std::string_view get_name_from_graph_pid(Port_ID graph_pid) const {
    I(has_graph_pin(graph_pid));
    return io_pins[graph_pid2instance_pid[graph_pid]].name;
  }

  bool is_input_from_instance_pid(Port_ID instance_pid) const {
    I(has_instance_pin(instance_pid));
    return io_pins[instance_pid].dir == Direction::Input;
  }

  bool is_input_from_graph_pid(Port_ID graph_pid) const {
    I(has_graph_pin(graph_pid));
    return io_pins[graph_pid2instance_pid[graph_pid]].dir == Direction::Input;
  }

  bool is_input(std::string_view name) const {
    I(has_pin(name));

    auto instance_pid = name2id.at(name);
    return (io_pins.at(instance_pid).dir == Direction::Input);
  }

  bool is_output_from_instance_pid(Port_ID instance_pid) const {
    I(has_instance_pin(instance_pid));
    return io_pins[instance_pid].dir == Direction::Output;
  }

  bool is_output_from_graph_pid(Port_ID graph_pid) const {
    I(has_graph_pin(graph_pid));
    return io_pins[graph_pid2instance_pid[graph_pid]].dir == Direction::Output;
  }

  bool is_output(std::string_view name) const {
    I(has_pin(name));

    auto instance_pid = name2id.at(name);
    return (io_pins[instance_pid].dir == Direction::Output);
  }

  void add_phys_pin(std::string_view name, const Tech_pin &ppin) {
    I(has_pin(name));
    add_phys_pin_int(name2id[name], ppin);
  }

  void add_phys_pin_from_instance_pid(Port_ID instance_pid, const Tech_pin &ppin) {
    I(has_instance_pin(instance_pid));
    add_phys_pin_int(instance_pid, ppin);
  }

  void add_phys_pin_from_graph_pid(Port_ID graph_pid, const Tech_pin &ppin) {
    I(has_graph_pin(graph_pid));
    add_phys_pin_int(graph_pid2instance_pid[graph_pid], ppin);
  }

  const bmsparse &get_input_ids()  const { return input_ids;  }
  const bmsparse &get_output_ids() const { return output_ids; }

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
