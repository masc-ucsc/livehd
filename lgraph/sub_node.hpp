//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// NOTE:
//
// instace_pid is the PID used inside and outside the lgraph to connect the input/output.
// Both the subgraph and the calling graph have the same PID
//
// graph_pos is the "optional" IO position needed when a module is called
// without names, just by position. Each language should specify the way to
// populate graph_pos for verilog interoperativity. E.g: alphabetical order, or
// declaration order or ??

#include "absl/container/flat_hash_map.h"
#include "absl/types/span.h"
#include "hhds/graph.hpp"
#include "lgraph_base_core.hpp"
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/prettywriter.h"
#include "tech_library.hpp"

class Sub_node {
protected:
public:
  enum class Direction { Invalid, Output, Input };

  struct IO_pin {
    IO_pin() = default;

    IO_pin(std::string_view _name, Port_ID ipid, Direction _dir, Port_ID _graph_io_pos)
        : name(_name), dir(_dir), graph_io_pos(_graph_io_pos), instance_pid(ipid) {}
    std::string name;
    Direction   dir          = Direction::Invalid;
    Port_ID     graph_io_pos = Port_invalid;
    Port_ID     instance_pid = 0;
    Bits_t      bits         = 0;

    [[nodiscard]] bool    is_mapped() const { return graph_io_pos != Port_invalid; }
    [[nodiscard]] bool    is_input() const { return dir == Direction::Input; }
    [[nodiscard]] bool    is_output() const { return dir == Direction::Output; }
    [[nodiscard]] bool    is_invalid() const { return dir == Direction::Invalid; }
    [[nodiscard]] bool    has_io_pos() const { return graph_io_pos != Port_invalid; }
    [[nodiscard]] Port_ID get_io_pos() const { return graph_io_pos; }
    [[nodiscard]] Port_ID get_instance_pid() const { return instance_pid; }

    void clear() {
      dir          = Direction::Invalid;
      name         = "INVALID_PID";
      graph_io_pos = Port_invalid;
      bits         = 0;  // Blackboxes may need to set bits for semantics (bitwidth pass)
      // KEEP instance_pid
    }
  };

private:
  std::string name;
  Lg_type_id  lgid;
  bool        loop_first;
  bool        loop_last;

  std::vector<IO_pin> io_pins;

  absl::flat_hash_map<std::string, Port_ID> name2id;
  std::vector<Port_ID>                      graph_pos2instance_pid;
  std::vector<Port_ID>                      deleted;

  // Non-owning pointer to the paired hhds::GraphIO. Set by Graph_library when
  // this Sub_node is created (HHDS migration Phase G1.2 —
  // docs/contracts/hhds_graph_migration_plan.md). Operations mirror to the
  // GraphIO when non-null; reads still consult the local io_pins[].
  hhds::GraphIO* hhds_io_ = nullptr;

  void map_pin_int(Port_ID instance_pid, Port_ID graph_pos) {
    I(graph_pos != Port_invalid);
    I(instance_pid);  // Must be non zero for input/output pid
    I(io_pins[instance_pid].graph_io_pos == graph_pos);

    if (graph_pos2instance_pid.size() <= graph_pos) {
      graph_pos2instance_pid.resize(graph_pos + 1, Port_invalid);
      I(graph_pos2instance_pid[graph_pos] == Port_invalid);
    }
    graph_pos2instance_pid[graph_pos] = instance_pid;
  }

public:
  Sub_node() { expunge(); }

  Sub_node(const Sub_node& s)          = default;
  Sub_node& operator=(const Sub_node&) = delete;

  // HHDS migration Phase G1.2 — set by Graph_library to a non-owning pointer
  // to the paired hhds::GraphIO. Operations mirror to it when non-null.
  // The Graph_library owns the underlying shared_ptr.
  void                         set_hhds_io(hhds::GraphIO* io) { hhds_io_ = io; }
  [[nodiscard]] hhds::GraphIO* ref_hhds_io() const { return hhds_io_; }

  void to_json(rapidjson::PrettyWriter<rapidjson::StringBuffer>& writer) const;
  void from_json(const rapidjson::Value& entry);

  void reset_pins() {
    clear_io_pins();
    io_pins.clear();    // WARNING: Do NOT remove mappings, just port id. (allows to reload designs)
    io_pins.resize(1);  // No id ZERO
    deleted.clear();
    name2id.clear();
    // Mirror to paired hhds::GraphIO. Use reset_declarations() (clears
    // declared input/output pins) — NOT clear(), which would tombstone the
    // whole GraphIO and invalidate the paired Graph.
    if (hhds_io_ && hhds_io_->get_library() != nullptr) {
      hhds_io_->reset_declarations();
    }
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
    deleted.clear();

    lgid       = 0;
    loop_last  = true;
    loop_first = false;
  }

  [[nodiscard]] bool is_black_box() const {
    return graph_pos2instance_pid.empty();  // BBox if we still do not know how to map from instance_pid to pos
  }

  void clear_io_pins() {
    graph_pos2instance_pid.clear();
    // io_pins.clear();   // FIXME: version?? Do NOT remove mappings, just port id. (allows to reload designs)
    // io_pins.resize(1); // No id ZERO
  }

  [[nodiscard]] bool is_loop_first() const { return loop_first; }

  [[nodiscard]] bool is_loop_last() const { return loop_last; }
  void               set_loop_last() { loop_last = true; }
  void               clear_loop_last() { loop_last = false; }

  [[nodiscard]] bool is_invalid() const { return lgid == 0; }

  [[nodiscard]] Lg_type_id get_lgid() const {
    I(lgid);
    return lgid;
  }

  [[nodiscard]] std::string_view get_name() const {
    I(lgid);
    return name;
  }

  Port_ID add_input_pin(std::string_view io_name, Port_ID graph_pos = Port_invalid) {
    return add_pin(io_name, Direction::Input, graph_pos);
  }

  Port_ID add_output_pin(std::string_view io_name, Port_ID graph_pos = Port_invalid) {
    return add_pin(io_name, Direction::Output, graph_pos);
  }

  Port_ID add_pin(std::string_view io_name, Direction dir, Port_ID graph_pos = Port_invalid) {
    I(lgid);
    I(!has_pin(io_name));
    I(io_name != "%");  // reserved for default output
    I(io_name != "$");  // reserved for default input

    Port_ID instance_pid = 0;

    auto it = name2id.find(io_name);
    if (it != name2id.end()) {
      instance_pid = it->second;
      I(io_pins.size() > instance_pid);
    } else {
      while (!deleted.empty()) {
        instance_pid = deleted.back();
        deleted.pop_back();
        I(io_pins.size() > instance_pid);
        if (io_pins[instance_pid].is_invalid()) {
          break;
        }
      }
    }

    if (io_pins[instance_pid].is_invalid()) {
      instance_pid = io_pins.size();
      io_pins.emplace_back(io_name, instance_pid, dir, graph_pos);
    } else {
      if (io_pins.size() <= instance_pid) {
        io_pins.resize(instance_pid + 1);
      }
      io_pins[instance_pid].name         = io_name;
      io_pins[instance_pid].dir          = dir;
      io_pins[instance_pid].graph_io_pos = graph_pos;
      I(io_pins[instance_pid].instance_pid == instance_pid);
    }
    name2id[io_name] = instance_pid;
    I(io_pins[instance_pid].name == io_name);
    if (graph_pos != Port_invalid) {
      map_pin_int(instance_pid, graph_pos);
    }

    // Mirror to paired hhds::GraphIO (HHDS migration Phase G1.2). The
    // get_library() guard makes us robust against the corner case observed
    // during initial bring-up where an invalidated GraphIO would be reachable
    // via a stale Sub_node->hhds_io_ pointer.
    if (hhds_io_ && hhds_io_->get_library() != nullptr) {
      const bool already = (dir == Direction::Input) ? hhds_io_->has_input(io_name) : hhds_io_->has_output(io_name);
      if (!already) {
        if (dir == Direction::Input) {
          hhds_io_->add_input(io_name, static_cast<hhds::Port_id>(instance_pid));
        } else if (dir == Direction::Output) {
          hhds_io_->add_output(io_name, static_cast<hhds::Port_id>(instance_pid));
        }
      }
    }

    return instance_pid;
  }
  void del_pin(Port_ID instance_pid);

  Port_ID map_graph_pos(std::string_view io_name, Direction dir, Port_ID graph_pos) {
    I(graph_pos);  // 0 possition is also not used (to catch bugs)
    I(has_pin(io_name));

    Port_ID instance_pid = name2id[io_name];
    I(io_pins[instance_pid].name == io_name);
    // I(io_pins[instance_pid].graph_io_pos == graph_pos || !has_graph_pos_pin(graph_pos));
    io_pins[instance_pid].dir = dir;

    if (io_pins[instance_pid].graph_io_pos != Port_invalid) {
      I(graph_pos2instance_pid.size() > io_pins[instance_pid].graph_io_pos);
      graph_pos2instance_pid[io_pins[instance_pid].graph_io_pos] = Port_invalid;
    }

    if (graph_pos != Port_invalid) {
      io_pins[instance_pid].graph_io_pos = graph_pos;
      map_pin_int(instance_pid, graph_pos);
    }

    return instance_pid;
  }

  [[nodiscard]] bool has_pin(std::string_view io_name) const {
    I(lgid);
    // HHDS Phase G2 (reads): consult hhds::GraphIO when the mirror is
    // available. Falls back to the legacy io_pins[] when hhds_io_ is
    // unset or invalidated (e.g., during ctor before set_hhds_io).
    if (hhds_io_ && hhds_io_->get_library() != nullptr) {
      if (hhds_io_->has_input(io_name) || hhds_io_->has_output(io_name)) {
        return true;
      }
      // GraphIO doesn't know about LiveHD's `%` / `$` defaults — fall
      // through to the legacy check for those.
    }
    const auto it = name2id.find(io_name);
    if (it == name2id.end()) {
      return io_name == "%" || io_name == "$";
    }

    return !io_pins[it->second].is_invalid();  // It could be deleted and name preserved to remap
  }
  [[nodiscard]] bool has_graph_pos_pin(Port_ID graph_pos) const {
    I(lgid);
    return graph_pos2instance_pid.size() > graph_pos && graph_pos2instance_pid[graph_pos] != Port_invalid;
  }
  [[nodiscard]] bool has_instance_pin(Port_ID instance_pid) const {
    I(lgid);
    // HHDS Phase G2 (reads): consult GraphIO when available. instance_pid is
    // stored as the HHDS Port_id during add_pin, so has_pin_with_port_id
    // resolves authoritatively. Falls back to io_pins[] when mirror is unset.
    if (hhds_io_ && hhds_io_->get_library() != nullptr) {
      return hhds_io_->has_pin_with_port_id(static_cast<hhds::Port_id>(instance_pid));
    }
    return io_pins.size() > instance_pid && !io_pins[instance_pid].is_invalid();
  }

  [[nodiscard]] Port_ID get_instance_pid(std::string_view io_name) const {
    if (io_name == "$" || io_name == "%") {
      return 0;
    }
    // HHDS Phase G2: consult GraphIO when the mirror is available. We
    // stored `instance_pid` as the HHDS Port_id during add_input/add_output,
    // so get_input_port_id / get_output_port_id round-trips correctly.
    if (hhds_io_ && hhds_io_->get_library() != nullptr) {
      if (hhds_io_->has_input(io_name)) {
        return static_cast<Port_ID>(hhds_io_->get_input_port_id(io_name));
      }
      if (hhds_io_->has_output(io_name)) {
        return static_cast<Port_ID>(hhds_io_->get_output_port_id(io_name));
      }
    }
    I(has_pin(io_name));
    return name2id.at(io_name);
  }

  [[nodiscard]] Port_ID get_io_pos(std::string_view io_name) const {
    auto instance_pid = get_instance_pid(io_name);
    return io_pins[instance_pid].graph_io_pos;
  }

  [[nodiscard]] Port_ID get_instance_pid_from_graph_pos(Port_ID graph_pos) const {
    I(has_graph_pos_pin(graph_pos));
    return graph_pos2instance_pid[graph_pos];
  }

  [[nodiscard]] const IO_pin& get_io_pin_from_graph_pos(Port_ID graph_pos) const {
    I(has_graph_pos_pin(graph_pos));
    return io_pins[graph_pos2instance_pid[graph_pos]];
  }

  [[nodiscard]] const IO_pin& get_io_pin_from_instance_pid(Port_ID instance_pid) const {
    if (instance_pid >= io_pins.size()) {
      return io_pins[0];  // invalid PID
    }
    return io_pins[instance_pid];
  }

  void set_bits(std::string_view io_name, Bits_t bits) {
    I(has_pin(io_name));
    auto instance_pid = name2id.at(io_name);
    I(io_pins[instance_pid].name == io_name);
    io_pins[instance_pid].bits = bits;
  }

  void set_bits(Port_ID instance_pid, Bits_t bits) {
    I(has_instance_pin(instance_pid));
    io_pins[instance_pid].bits = bits;
  }

  [[nodiscard]] const IO_pin& get_pin(std::string_view io_name) const {
    I(has_pin(io_name));
    auto instance_pid = name2id.at(io_name);
    I(io_pins[instance_pid].name == io_name);
    return io_pins[instance_pid];
  }

  [[nodiscard]] const IO_pin& get_graph_output_io_pin(std::string_view io_name) const {
    I(has_pin(io_name));
    auto instance_pid = name2id.at(io_name);
    I(io_pins[instance_pid].name == io_name);
    I(io_pins[instance_pid].dir == Direction::Output);
    return io_pins[instance_pid];
  }

  [[nodiscard]] const IO_pin& get_graph_input_io_pin(std::string_view io_name) const {
    I(has_pin(io_name));
    auto instance_pid = name2id.at(io_name);
    I(io_pins[instance_pid].name == io_name);
    I(io_pins[instance_pid].dir == Direction::Input);
    return io_pins[instance_pid];
  }

  [[nodiscard]] Port_ID get_graph_io_pos(std::string_view io_name) const {
    I(has_pin(io_name));
    auto instance_pid = name2id.at(io_name);
    I(io_pins[instance_pid].name == io_name);
    return io_pins[instance_pid].graph_io_pos;
  }

  [[nodiscard]] std::string_view get_name_from_instance_pid(Port_ID instance_pid) const {
    if (!has_instance_pin(instance_pid)) {
      return "INVALID_PID";
    }
    return io_pins[instance_pid].name;
  }

  [[nodiscard]] std::string_view get_name_from_graph_pos(Port_ID graph_pos) const {
    I(has_graph_pos_pin(graph_pos));  // The pos does not seem to exist
    return io_pins[graph_pos2instance_pid[graph_pos]].name;
  }

  [[nodiscard]] bool is_input_from_instance_pid(Port_ID instance_pid) const {
    I(has_instance_pin(instance_pid));
    // HHDS Phase G2 (reads): authoritative from GraphIO when mirror is set.
    if (hhds_io_ && hhds_io_->get_library() != nullptr) {
      return hhds_io_->has_input_with_port_id(static_cast<hhds::Port_id>(instance_pid));
    }
    return io_pins[instance_pid].dir == Direction::Input;
  }

  [[nodiscard]] bool is_input(std::string_view io_name) const {
    if (io_name == "$") {
      return true;
    }
    // HHDS Phase G2 (reads): consult GraphIO when mirror is available.
    if (hhds_io_ && hhds_io_->get_library() != nullptr && hhds_io_->has_input(io_name)) {
      return true;
    }
    const auto it = name2id.find(io_name);
    if (it == name2id.end()) {
      return false;
    }

    return io_pins[it->second].is_input();
  }

  [[nodiscard]] bool is_output_from_instance_pid(Port_ID instance_pid) const {
    I(has_instance_pin(instance_pid));
    // HHDS Phase G2 (reads): authoritative from GraphIO when mirror is set.
    if (hhds_io_ && hhds_io_->get_library() != nullptr) {
      return hhds_io_->has_output_with_port_id(static_cast<hhds::Port_id>(instance_pid));
    }
    return io_pins[instance_pid].dir == Direction::Output;
  }

  [[nodiscard]] bool is_output(std::string_view io_name) const {
    if (io_name == "%") {
      return true;
    }
    // HHDS Phase G2 (reads): consult GraphIO when mirror is available.
    if (hhds_io_ && hhds_io_->get_library() != nullptr && hhds_io_->has_output(io_name)) {
      return true;
    }
    const auto it = name2id.find(io_name);
    if (it == name2id.end()) {
      return false;
    }

    return io_pins[it->second].is_output();
  }

  [[nodiscard]] size_t size() const { return io_pins.size() - 1; };

  [[nodiscard]] absl::Span<const IO_pin> get_io_pins() const { return absl::MakeSpan(io_pins); }

  [[nodiscard]] const std::vector<std::pair<const IO_pin*, Port_ID>> get_output_pins() const {
    I(io_pins.size() >= 1);
    std::vector<std::pair<const IO_pin*, Port_ID>> v;
    Port_ID                                        i = 0;
    for (const auto& e : io_pins) {
      if (e.is_output()) {
        v.emplace_back(&e, i);
      }
      ++i;
    }
    return v;
  }

  [[nodiscard]] const std::vector<std::pair<const IO_pin*, Port_ID>> get_input_pins() const {
    I(io_pins.size() >= 1);
    std::vector<std::pair<const IO_pin*, Port_ID>> v;
    Port_ID                                        i = 0;
    for (const auto& e : io_pins) {
      if (e.is_input()) {
        v.emplace_back(&e, i);
      }
      ++i;
    }
    return v;
  }

  [[nodiscard]] std::vector<IO_pin> get_sorted_io_pins() const;

  void dump() const;
};
