//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <vector>

#include "lgraph_base_core.hpp"
#include "node_type_base.hpp"
#include "node_pin.hpp"
#include "sub_node.hpp"

class Ann_place;

using XEdge_iterator    = std::vector<XEdge>;
using Node_pin_iterator = std::vector<Node_pin>;

class Node {
private:
  LGraph *top_g;
  const Hierarchy_id hid;
  const Index_ID nid;

protected:
  friend class LGraph;
  friend class Node_pin;
  friend class XEdge;
  friend class Node_set;
  friend class CFast_edge_iterator;
  friend class Edge_raw_iterator_base;

  Index_ID get_nid() const { return nid; }

  Node(LGraph *_g, Index_ID _nid, Hierarchy_id _hid)
    :top_g(_g)
    ,hid(_hid)
    ,nid(_nid) {
      I(nid);
      I(top_g);
    };
public:
  static constexpr char Hardcoded_input_nid  = 1;
  static constexpr char Hardcoded_output_nid = 2;

  struct __attribute__((packed)) Compact {
    uint64_t  nid:Index_bits;
    uint64_t  hid; // FIXME: vector for more than 8 nested levels hierarchy

    //constexpr size_t operator()(const Compact &obj) const { return obj.nid; }
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
  bool operator==(const Node &other) const { I(nid); I(top_g == other.top_g); return (nid == other.nid) && (hid == other.hid); }

  bool operator!=(const Node &other) const { I(nid); I(top_g == other.top_g); return (nid != other.nid) || (hid != other.hid); }

  // NOTE: No operator<() needed for std::set std::map to avoid their use. Use flat_map_set for speed

  Node()
    :top_g(0)
    ,hid(0)
    ,nid(0) {
  };
  Node(LGraph *_g, Compact comp)
    :top_g(_g)
    ,hid(comp.hid)
    ,nid(comp.nid) {
      I(nid);
      I(top_g);
  };
  Node &operator=(const Node &obj) {
    I(this != &obj); // Do not assign object to itself. works but wastefull
    top_g   = obj.top_g;
    const_cast<Index_ID&>(nid)     = obj.nid;
    const_cast<Hierarchy_id&>(hid) = obj.hid;

    return *this;
  };

  inline Compact get_compact() const {
    return Compact(nid, hid);
  }

  LGraph *get_top_lgraph() const { return top_g; }
  Hierarchy_id  get_hid()  const { return hid;   }

  Node_pin get_driver_pin() const;
  Node_pin get_driver_pin(Port_ID pid) const;
  Node_pin get_sink_pin(Port_ID pid) const;
  Node_pin get_sink_pin() const;

  bool has_inputs () const;
  bool has_outputs() const;

  bool is_invalid() const { return top_g==nullptr || nid==0; }

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
  std::string      debug_name(bool nowarning=false) const;

  std::string_view set_name(std::string_view iname);
  std::string_view get_name() const;
  std::string_view create_name() const;
  bool has_name() const;

  const Ann_place &get_place() const;
  Ann_place *ref_place();
  bool has_place() const;

  // END ATTRIBUTE ACCESSORS
};
