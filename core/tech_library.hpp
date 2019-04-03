//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <string>

#include <cassert>
#include <map>
#include <vector>
#include "absl/container/flat_hash_map.h"

class Tech_cell {
public:
  typedef uint16_t pin_type;
  typedef float    pos_type;

  typedef enum { input, output } Direction;

  typedef struct {
    std::string metal_name;  // FIXME make it an enum
    pos_type    xl, yl, xh, yh;
  } Physical_pin;

  typedef struct {
    std::vector<Physical_pin> phys;
    Direction                 dir;
    std::string               name;
    std::string               use;  // FIXME: make it an enum
    pin_type                  io_id;
  } Pin;

  typedef std::pair<pin_type, pin_type> ppair;

private:
  std::string cell_name;
  std::string function;  // FIXME: maybe we want to limit the possibilities here

  uint16_t id;

  double height;
  double width;

  std::vector<pin_type> inputs;
  std::vector<pin_type> outputs;

  std::vector<Pin> pins;

  absl::flat_hash_map<std::string, pin_type> pname2id;

  // FIXME: technically, this should be a full table for each pair?
  std::map<ppair, float> delay;  // maps an (ipin x opin) to delay

public:
  explicit Tech_cell(std::string_view name, uint16_t id) : cell_name(name), id(id), height(0), width(0) {}

  uint16_t get_id() const { return id; }

  std::string_view get_name() const { return cell_name; }

  const std::pair<double, double> get_cell_size() const { return std::make_pair(height, width); }

  void set_cell_size(double _height, double _width) {
    height = _height;
    width  = _width;
  }

  std::string_view get_function() const { return function; }
  void             set_function(std::string _function) { function = _function; }

  pin_type add_pin(std::string_view name, Direction dir) {
    pin_type id = pins.size();
    Pin      aPin;
    aPin.name  = name;
    aPin.dir   = dir;
    aPin.use   = "";
    aPin.io_id = 0;
    pins.push_back(aPin);
    pname2id[name] = id;

    if (dir == input) {
      assert(std::find(inputs.begin(), inputs.end(), id) == inputs.end());

      pins[id].io_id = inputs.size();
      inputs.push_back(id);
    } else {
      assert(std::find(outputs.begin(), outputs.end(), id) == outputs.end());
      assert(dir == output);

      pins[id].io_id = outputs.size();
      outputs.push_back(id);
    }

    return id;
  }

  bool include_pin(std::string_view name) const { return pname2id.find(name) != pname2id.end(); }

  // FIXME: remove this
  // return a pointer point to "pins" vector, which is a type of std::vector<Pin>
  std::vector<Pin> *get_vec_pins() { return &pins; }

  int n_inps() const { return inputs.size(); }

  int n_outs() const { return outputs.size(); }

  const pin_type get_pin_id(std::string_view name) const {
    assert(pname2id.find(name) != pname2id.end());
    return pname2id.at(name);
  }

  bool pin_name_exist(std::string_view name) const {
    if (pname2id.find(name) != pname2id.end())
      return true;
    else
      return false;
  }

  const pin_type get_out_pid(std::string_view name) const {
    assert(pname2id.find(name) != pname2id.end());
    pin_type pin_id = pname2id.at(name);
    assert(pins[pin_id].dir == Direction::output);
    return pins[pin_id].io_id;
  }

  const pin_type get_inp_pid(std::string_view name) const {
    assert(pname2id.find(name) != pname2id.end());
    pin_type pin_id = pname2id.at(name);
    assert(pins[pin_id].dir == Direction::input);
    return pins[pin_id].io_id;
  }

  const int get_pins_size() const { return pins.size(); };

  std::string_view get_name(pin_type id) const {
    assert(pins.size() > id);
    return pins[id].name;
  }

  std::string_view get_input_name(pin_type id) const {
    assert(inputs.size() > id);
    return pins[inputs[id]].name;
  }

  std::string_view get_output_name(pin_type id) const {
    assert(outputs.size() > id);
    return pins[outputs[id]].name;
  }

  const Direction get_direction(pin_type id) const {
    assert(pins.size() > id);
    return pins[id].dir;
  }

  const std::vector<pin_type> &get_inputs() const { return inputs; }

  bool is_input(std::string_view name) const {
    assert(pname2id.find(name) != pname2id.end());

    pin_type inpid = pname2id.at(name);
    return (pins.at(inpid).dir == Direction::input);
  }

  bool is_output(std::string_view name) const {
    assert(pname2id.find(name) != pname2id.end());

    pin_type outid = pname2id.at(name);
    return (pins[outid].dir == Direction::output);
  }

  const std::vector<pin_type> &get_outputs() const { return outputs; }

  void set_position(pin_type id, pin_type phy_id, pos_type in_xl, pos_type in_yl, pos_type in_xh, pos_type in_yh) {
    assert(pins.size() > id);
    pins[id].phys[phy_id].xl = in_xl;
    pins[id].phys[phy_id].yl = in_yl;
    pins[id].phys[phy_id].xh = in_xh;
    pins[id].phys[phy_id].yh = in_yh;
  }

  const std::pair<pos_type, pos_type> get_position(pin_type id, pin_type phy_id) const {
    assert(pins.size() > id);
    return std::make_pair(pins[id].phys[phy_id].xl, pins[id].phys[phy_id].yl);
  }

  // SH: not sure whether set_psize and get_psize function would be used, mark temporary
  /*
    void set_psize(pin_type id,pin_type phy_id, pos_type height, pos_type width) {
      assert(pins.size() > id);
      pins[id].phys[phy_id].height = height;
      pins[id].phys[phy_id].width  = width;
    }

    const std::pair<pos_type, pos_type> get_psize(pin_type id, pin_type phy_id) const {
      assert(pins.size() > id);
      return std::make_pair(pins[id].phys[phy_id].height, pins[id].phys[phy_id].width);
    }
     */
};

typedef struct tech_layer_s_ {
public:
  std::string         name;
  bool                horizontal;
  double              minwidth;
  double              area;
  double              width;
  std::vector<double> spacing_eol;
  std::vector<double> spacing_tb;
  std::vector<double> pitches;
  double              spctb_prl;      // parallel running length
  std::vector<double> spctb_width;    // width in spacing table
  std::vector<double> spctb_spacing;  // spacing in spacing table

  // void to_json(rapidjson::PrettyWriter<rapidjson::StringBuffer>&) const;
} Tech_layer;

typedef struct tech_rect_s_ {
public:
  double xl, yl, xh, yh;
} Tech_rect;

typedef struct tech_via_layer_s_ {
public:
  std::string layer_name;
  Tech_rect   rect;
} Tech_via_layer;

typedef struct tech_via_s_ {
public:
  std::string                 name;
  std::vector<Tech_via_layer> vlayers;
  // void to_json(rapidjson::PrettyWriter<rapidjson::StringBuffer>&) const;
} Tech_via;

class Tech_library {
private:
  const std::string lgdb;
  const std::string lib_file;

  bool clean;

  std::vector<Tech_cell>  cell_types;
  std::vector<Tech_layer> layers;  // only for routing
  std::vector<Tech_via>   vias;    // only for routing

  absl::flat_hash_map<std::string, uint16_t> cname2id;

  explicit Tech_library(std::string_view _path) : lgdb(_path), lib_file("tech_library") {
    cname2id.clear();
    cell_types.clear();
    clean = true;
    // load();
    try_load_json();
  }

  static absl::flat_hash_map<std::string, Tech_library *> instances;

  void to_yaml() const;
  void to_json() const;

public:
  std::string test_str;
  void        load();
  void        try_load_json();

  void sync() {
    if (!clean) {
      to_json();
      clean = true;
    }
  }

  void clear_tech_lib() {
    cname2id.clear();
    cell_types.clear();
  }

  bool include(std::string_view name) const;

  uint16_t create_cell_id(std::string_view name);

  uint16_t get_cell_id(std::string_view name) const;

  Tech_cell *get_cell(uint16_t cell_id);

  const Tech_cell *get_const_cell(uint16_t cell_id) const;

  std::string_view get_cell_name(uint16_t cell_id) const { return get_const_cell(cell_id)->get_name(); }

  // multiton pattern, one singleton per lgdb
  static Tech_library *instance(std::string path = "lgdb") {
    if (Tech_library::instances.find(path) == Tech_library::instances.end()) {
      Tech_library::instances.insert(std::make_pair(path, new Tech_library(path)));
    }
    return Tech_library::instances[path];
  }

  // adding routing only member function here : layers and via
  // return a pointer point to "cell_types" vector, which is a type of std::vector<Tech_layer>
  // don't use pointer of vector, need to be modified
  std::vector<Tech_cell> *get_vec_cell_types() { return &cell_types; }

  int get_cell_types_size() const { return cell_types.size(); }

  // return a pointer point to "layers" vector, which is a type of std::vector<Tech_layer>
  std::vector<Tech_layer> *get_vec_layers() { return &layers; }

  void increase_vec_layers_size() { layers.resize(layers.size() + 1); }

  // return a pointer point to "layers" vector, which is a type of std::vector<Tech_via>
  std::vector<Tech_via> *get_vec_vias() { return &vias; }

  void increase_vec_vias_size() { vias.resize(vias.size() + 1); }
};
