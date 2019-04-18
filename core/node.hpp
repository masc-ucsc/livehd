//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <vector>

#include "node_pin.hpp"

class Ann_place;

using XEdge_iterator    = std::vector<XEdge>;
using Node_pin_iterator = std::vector<Node_pin>;

class LGraph;
class Node_Type;

class Node {
private:
  const Index_ID nid;
  const Hierarchy_id hid;
  LGraph *g;

protected:
  friend LGraph;
  friend Node_pin;
  friend XEdge;
  friend Node_set;

  Index_ID get_nid() const { return nid; }

  Node(LGraph *_g, Hierarchy_id _hid, Index_ID _nid)
    :nid(_nid)
    ,hid(_hid)
    ,g(_g) {
      I(nid);
      I(g);
    };
public:
  struct __attribute__((packed)) Compact {
    uint32_t nid:Index_bits;
    uint16_t hid;

    constexpr size_t operator()(const Compact &obj) const { return obj.nid; }
    constexpr operator size_t() const { return nid; }

    Compact(const Index_ID &_nid, const Hierarchy_id &_hid) :nid(_nid), hid(_hid) { I(nid); };
    Compact() :nid(0),hid(0) { };
    Compact &operator=(const Compact &obj) {
      I(this != &obj);
      nid  = obj.nid;
      hid  = obj.hid;

      return *this;
    };
    bool is_invalid() const { return nid == 0; }

    template <typename H>
    friend H AbslHashValue(H h, const Compact& s) {
      return H::combine(std::move(h), s.nid, s.hid);
    };
  };
  template <typename H>
  friend H AbslHashValue(H h, const Node& s) {
    return H::combine(std::move(h), (int)s.nid, (int)s.hid); // Ignore lgraph pointer in hash
  };
  bool operator==(const Node &other) const { I(nid); I(g == other.g); return (nid == other.nid) && (hid == other.hid); }

  bool operator!=(const Node &other) const { I(nid); I(g == other.g); return (nid != other.nid) || (hid != other.hid); }

  // NOTE: No operator<() needed for std::set std::map to avoid their use. Use flat_map_set for speed

  Node()
    :nid(0)
    ,hid(0)
    ,g(0) {
  };
  Node(LGraph *_g, Hierarchy_id _hid, Compact comp)
    :nid(comp.nid)
    ,hid(_hid)
    ,g(_g) {
      I(nid);
      I(g);
  };
  Node &operator=(const Node &obj) {
    I(this != &obj); // Do not assign object to itself. works but wastefull
    g   = obj.g;
    const_cast<Index_ID&>(nid)     = obj.nid;
    const_cast<Hierarchy_id&>(hid) = obj.hid;

    return *this;
  };

  inline Compact get_compact() const {
    return Compact(nid);
  }

  LGraph *get_lgraph() const { return g; }
  Hierarchy_id  get_hid()    const { return hid; }

  Node_pin get_driver_pin() const;
  Node_pin get_driver_pin(Port_ID pid) const;
  Node_pin get_sink_pin(Port_ID pid) const;
  Node_pin get_sink_pin() const;

  bool has_inputs () const;
  bool has_outputs() const;

  bool is_invalid() const { return g==nullptr || nid==0; }

  bool is_root() const;

  void              set_type_lut(Lut_type_id lutid);
  Lut_type_id       get_type_lut() const;

  const Node_Type  &get_type() const;
  void              set_type(const Node_Type_Op op);
  void              set_type(const Node_Type_Op op, uint16_t bits);
  bool              is_type(const Node_Type_Op op) const;

  void              set_type_sub(Lg_type_id subid);
  Lg_type_id        get_type_sub() const;
  Sub_node         &get_type_sub_node() const;

  // WARNING: Do not call this. Use create_node_const... to reuse node if already exists
  //void              set_type_const_value(std::string_view str);
  //void              set_type_const_sview(std::string_view str);
  //void              set_type_const_value(uint32_t val);
  uint32_t          get_type_const_value() const;
  std::string_view  get_type_const_sview() const;

  Node_pin          setup_driver_pin(std::string_view name);
  Node_pin          setup_driver_pin(Port_ID pid);
  Node_pin          setup_driver_pin() const;

  Node_pin          setup_sink_pin(std::string_view name);
  Node_pin          setup_sink_pin(Port_ID pid);
  Node_pin          setup_sink_pin() const;

  Node_pin_iterator out_connected_pins() const;
  Node_pin_iterator inp_connected_pins() const;
  Node_pin_iterator out_setup_pins() const;
  Node_pin_iterator inp_setup_pins() const;

  XEdge_iterator    out_edges() const;
  XEdge_iterator    inp_edges() const;

  void del_node();

  // BEGIN ATTRIBUTE ACCESSORS
  std::string      debug_name() const;

  std::string_view set_name(std::string_view iname);
  std::string_view get_name() const;
  std::string_view create_name() const;
  bool has_name() const;

  const Ann_place &get_place() const;
  Ann_place *ref_place();
  bool has_place() const;

  // END ATTRIBUTE ACCESSORS
};
