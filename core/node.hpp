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
protected:
  LGraph            *top_g;
  mutable LGraph    *current_g;
  const Hierarchy_id hid;
  const Index_ID     nid;

  friend class LGraph;
  friend class LGraph_Node_Type;
  friend class Node_pin;
  friend class XEdge;
  friend class Node_set;
  friend class CFast_edge_iterator;
  friend class Edge_raw_iterator_base;
  friend class CForward_edge_iterator;
  friend class CBackward_edge_iterator;

  Index_ID get_nid() const { return nid; }

  Node(LGraph *_g, LGraph *_c_g, Index_ID _nid, Hierarchy_id _hid);

public:
  static constexpr char Hardcoded_input_nid  = 1;
  static constexpr char Hardcoded_output_nid = 2;

  class __attribute__((packed)) Compact {
  protected:
    Hierarchy_id  hid;
    uint64_t      nid:Index_bits;

    friend class LGraph;
    friend class LGraph_Node_Type;
    friend class Node;
    friend class Node_pin;
    friend class XEdge;
    friend class Node_set;
    friend class CFast_edge_iterator;
    friend class Edge_raw_iterator_base;
    friend class CForward_edge_iterator;
    friend class CBackward_edge_iterator;
    friend class Forward_edge_iterator;
    friend class Backward_edge_iterator;
  public:

    //constexpr operator size_t() const { I(0); return nid; }

    Compact(Hierarchy_id _hid, Index_ID _nid) :hid(_hid), nid(_nid) { I(nid); };
    Compact() :hid(0),nid(0) { };
    Compact &operator=(const Compact &obj) {
      I(this != &obj);
      hid  = obj.hid;
      nid  = obj.nid;

      return *this;
    }

    constexpr bool is_invalid() const { return nid == 0; }

    constexpr bool operator==(const Compact &other) const {
      return hid == other.hid && nid == other.nid;
    }
    constexpr bool operator!=(const Compact &other) const { return !(*this == other); }

    template <typename H>
    friend H AbslHashValue(H h, const Compact& s) {
      return H::combine(std::move(h), s.nid, s.hid);
    };
  };

  class __attribute__((packed)) Compact_class {
  protected:
    uint64_t      nid:Index_bits;

    friend class LGraph;
    friend class LGraph_Node_Type;
    friend class Node;
    friend class Node_pin;
    friend class XEdge;
    friend class Node_set;
    friend class CFast_edge_iterator;
    friend class Edge_raw_iterator_base;
    friend class CForward_edge_iterator;
    friend class CBackward_edge_iterator;
  public:

    // constexpr operator size_t() const { return nid; }

    Compact_class(const Index_ID &_nid) :nid(_nid) { I(nid); };
    Compact_class() :nid(0) { };
    Compact_class &operator=(const Compact_class &obj) {
      I(this != &obj);
      nid  = obj.nid;

      return *this;
    }

    constexpr bool is_invalid() const { return nid == 0; }

    constexpr bool operator==(const Compact_class &other) const {
      return nid == other.nid;
    }
    constexpr bool operator!=(const Compact_class &other) const { return !(*this == other); }

    template <typename H>
    friend H AbslHashValue(H h, const Compact_class& s) {
      return H::combine(std::move(h), s.nid);
    };
  };
  template <typename H>
  friend H AbslHashValue(H h, const Node& s) {
    return H::combine(std::move(h), (int)s.nid, (int)s.hid); // Ignore lgraph pointer in hash
  };

  // NOTE: No operator<() needed for std::set std::map to avoid their use. Use flat_map_set for speed

  Node() :top_g(0) ,current_g(0) ,hid(0) ,nid(0) { }
  Node(LGraph *_g, Compact comp);
  Node(LGraph *_g, Hierarchy_id _hid, Compact_class comp);
  Node(LGraph *_g, Compact_class comp);
  Node &operator=(const Node &obj) {
    I(this != &obj); // Do not assign object to itself. works but wastefull
    top_g     = obj.top_g;
    current_g = obj.current_g;
    const_cast<Index_ID&>(nid)     = obj.nid;
    const_cast<Hierarchy_id&>(hid) = obj.hid;

    return *this;
  };

  inline Compact get_compact() const {
    return Compact(nid, hid);
  }
  inline Compact_class get_compact_class() const {
    return Compact_class(nid);
  }

  LGraph *get_top_lgraph() const { I(top_g); return top_g; }
  LGraph *get_class_lgraph() const { I(current_g); return current_g; }

  Hierarchy_id  get_hid()  const { return hid;   }

  Node_pin get_driver_pin() const;
  Node_pin get_driver_pin(Port_ID pid) const;
  Node_pin get_sink_pin(Port_ID pid) const;
  Node_pin get_sink_pin() const;

  bool has_inputs () const;
  bool has_outputs() const;
  int  get_num_inputs()  const;
  int  get_num_outputs() const;

  constexpr bool is_invalid() const { return nid==0; }

  constexpr bool operator==(const Node &other) const {
    return top_g == other.top_g && hid == other.hid && nid == other.nid;
  }
  constexpr bool operator!=(const Node& other) const { return !(*this == other); };

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

  bool              has_graph_io()  const;
  bool              has_graph_input()  const;
  bool              has_graph_output() const;

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
