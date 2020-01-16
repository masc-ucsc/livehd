//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lgraph_base_core.hpp"
#include "node_pin.hpp"
#include "node_type_base.hpp"
#include "sub_node.hpp"

class Ann_place;

class Node {
protected:
  LGraph *        top_g;
  mutable LGraph *current_g;
  Hierarchy_index hidx;
  Index_ID        nid;

  friend class LGraph;
  friend class LGraph_Node_Type;
  friend class Node_pin;
  friend class XEdge;
  friend class Fast_edge_iterator;
  friend class Flow_base_iterator;
  friend class Fwd_edge_iterator;
  friend class Bwd_edge_iterator;
  friend class Hierarchy_tree;

  Index_ID get_nid() const { return nid; }

  Node(LGraph *_g, LGraph *_c_g, const Hierarchy_index &_hidx, Index_ID _nid);

  void invalidate(LGraph *_g);
  void invalidate();
  void update(Index_ID _nid) { nid = _nid; }
  void update(const Hierarchy_index &_hidx, Index_ID _nid);

public:
  void update(const Hierarchy_index &_hidx);

  static constexpr Index_ID Hardcoded_input_nid  = 1;
  static constexpr Index_ID Hardcoded_output_nid = 2;

  class __attribute__((packed)) Compact {
  protected:
    Hierarchy_index hidx;
    uint64_t        nid : Index_bits;

    friend class LGraph;
    friend class LGraph_Node_Type;
    friend class Node;
    friend class Node_pin;
    friend class XEdge;
    friend class Fast_edge_iterator;
    friend class Flow_base_iterator;
    friend class Fwd_edge_iterator;
    friend class Bwd_edge_iterator;
    friend class mmap_lib::hash<Node::Compact>;

  public:
    // constexpr operator size_t() const { I(0); return nid; }

    Compact(const Hierarchy_index &_hidx, Index_ID _nid) : hidx(_hidx), nid(_nid) { I(nid); };
    Compact() : nid(0){};
    Compact &operator=(const Compact &obj) {
      I(this != &obj);
      hidx = obj.hidx;
      nid  = obj.nid;

      return *this;
    }

    Index_ID get_nid() const { return nid; }  // Mostly for debugging or to know order

    Node get_node(LGraph *lg) const { return Node(lg, *this); }

    constexpr bool is_invalid() const { return nid == 0; }

    constexpr bool operator==(const Compact &other) const { return hidx == other.hidx && nid == other.nid; }
    constexpr bool operator!=(const Compact &other) const { return !(*this == other); }

    template <typename H>
    friend H AbslHashValue(H h, const Compact &s) {
      return H::combine(std::move(h), s.hidx.get_hash(), s.nid);
    };
  };

  class __attribute__((packed)) Compact_class {
  protected:
    uint64_t nid : Index_bits;

    friend class LGraph;
    friend class LGraph_Node_Type;
    friend class Node;
    friend class Node_pin;
    friend class XEdge;
    friend class Fast_edge_iterator;
    friend class Flow_base_iterator;
    friend class Fwd_edge_iterator;
    friend class Bwd_edge_iterator;
    friend class Hierarchy_tree;
    friend class mmap_lib::hash<Node::Compact_class>;

  public:
    // constexpr operator size_t() const { return nid; }
    constexpr Compact_class() : nid(0){};

    Compact_class(const Index_ID &_nid) : nid(_nid) { I(nid); };
    Compact_class &operator=(const Compact_class &obj) {
      I(this != &obj);
      nid = obj.nid;

      return *this;
    }

    Node get_node(LGraph *lg) const noexcept { return Node(lg, *this); }

    constexpr Index_ID get_nid() const noexcept { return nid; }
    constexpr bool     is_invalid() const noexcept { return nid == 0; }

    constexpr bool operator==(const Compact_class &other) const noexcept { return nid == other.nid; }
    constexpr bool operator!=(const Compact_class &other) const noexcept { return nid != other.nid; }

    template <typename H>
    friend H AbslHashValue(H h, const Compact_class &s) {
      return H::combine(std::move(h), s.nid);
    };
  };
  template <typename H>
  friend H AbslHashValue(H h, const Node &s) {
    return H::combine(std::move(h), (int)s.hidx.get_hash(), (int)s.nid);  // Ignore lgraph pointer in hash
  };

  // NOTE: No operator<() needed for std::set std::map to avoid their use. Use flat_map_set for speed
  void update(LGraph *_g, const Node::Compact &comp);
  void update(const Node::Compact &comp);
  void update(const Node &node);

  Node() : top_g(0), current_g(0), nid(0) {}
  // Node(LGraph *_g);
  Node(LGraph *_g, const Compact &comp) { update(_g, comp); }
  Node(LGraph *_g, const Hierarchy_index &_hidx, const Compact_class &comp);
  Node(LGraph *_g, const Compact_class &comp);
#if 0
  Node &operator=(const Node &obj) {
    I(this != &obj); // Do not assign object to itself. works but wastefull
    top_g     = obj.top_g;
    current_g = obj.current_g;
    const_cast<Index_ID&>(nid)     = obj.nid;
    const_cast<Hierarchy_index&>(hidx) = obj.hidx;

    return *this;
  };
#endif

  inline Compact       get_compact() const { return Compact(hidx, nid); }
  inline Compact_class get_compact_class() const { return Compact_class(nid); }

  LGraph *get_top_lgraph() const { return top_g; }
  LGraph *get_class_lgraph() const { return current_g; }

  Hierarchy_index get_hidx() const { return hidx; }

  Node_pin get_driver_pin() const;
  Node_pin get_sink_pin() const;

  Node_pin get_driver_pin(Port_ID pid) const;
  Node_pin get_sink_pin(Port_ID pid) const;

  Node_pin get_driver_pin(std::string_view pname) const;
  Node_pin get_sink_pin(std::string_view pname) const;

  bool has_inputs() const;
  bool has_outputs() const;
  int  get_num_inputs() const;
  int  get_num_outputs() const;

  constexpr bool is_invalid() const { return nid == 0; }

  constexpr bool operator==(const Node &other) const {
    GI(nid == 0, hidx.is_invalid());
    GI(other.nid == 0, other.hidx.is_invalid());
    return top_g == other.top_g && hidx == other.hidx && nid == other.nid;
  }
  constexpr bool operator!=(const Node &other) const {
    GI(nid == 0, hidx.is_invalid());
    GI(other.nid == 0, other.hidx.is_invalid());
    GI(nid && other.nid, top_g == other.top_g);
    return (nid != other.nid || hidx != other.hidx);
  };

  void        set_type_lut(Lut_type_id lutid);
  Lut_type_id get_type_lut() const;

  const Node_Type &get_type() const;
  void             set_type(const Node_Type_Op op);
  void             set_type(const Node_Type_Op op, uint32_t bits);
  bool             is_type(const Node_Type_Op op) const;
  bool             is_type_sub() const;
  bool             is_type_const() const;
  bool             is_type_io() const;
  bool             is_type_loop_breaker() const;

  Hierarchy_index hierarchy_go_down() const;
  Hierarchy_index hierarchy_go_up() const;
  Node            get_up_node() const;
  bool            is_root() const;

  void            set_type_sub(Lg_type_id subid);
  Lg_type_id      get_type_sub() const;
  const Sub_node &get_type_sub_node() const;
  Sub_node *      ref_type_sub_node() const;
  LGraph *        ref_type_sub_lgraph() const;  // Slower than other get_type_sub
  bool            is_type_sub_present() const;

  // WARNING: Do not call this. Use create_node_const... to reuse node if already exists
  // void              set_type_const_value(std::string_view str);
  // void              set_type_const_sview(std::string_view str);
  // void              set_type_const_value(uint32_t val);
  uint32_t         get_type_const_value() const;
  std::string_view get_type_const_sview() const;

  Node_pin setup_driver_pin(std::string_view name);
  Node_pin setup_driver_pin(Port_ID pid);
  Node_pin setup_driver_pin() const;

  Node_pin setup_sink_pin(std::string_view name);
  Node_pin setup_sink_pin(Port_ID pid);
  Node_pin setup_sink_pin() const;

  void nuke();  // Delete all the pins, edges, and attributes of this node

  Node_pin_iterator out_connected_pins() const;
  Node_pin_iterator inp_connected_pins() const;

  Node_pin_iterator out_setup_pins() const;
  Node_pin_iterator inp_setup_pins() const;

  XEdge_iterator out_edges() const;
  XEdge_iterator inp_edges() const;

  XEdge_iterator out_edges_ordered() const;  // Slower than inp_edges, but edges ordered by driver.pid
  XEdge_iterator inp_edges_ordered() const;  // Slower than inp_edges, but edges ordered by sink.pid

  bool is_graph_io() const { return nid == Hardcoded_input_nid || nid == Hardcoded_output_nid; }
  bool is_graph_input() const { return nid == Hardcoded_input_nid; }
  bool is_graph_output() const { return nid == Hardcoded_output_nid; }

  void del_node();

  // BEGIN ATTRIBUTE ACCESSORS
  std::string debug_name() const;

  void             set_name(std::string_view iname);
  std::string_view get_name() const;
  std::string_view create_name() const;
  bool             has_name() const;

  const Ann_place &get_place() const;
  Ann_place *      ref_place();
  bool             has_place() const;

  // END ATTRIBUTE ACCESSORS
};

namespace mmap_lib {
template <>
struct hash<Node::Compact> {
  size_t operator()(Node::Compact const &o) const {
    uint64_t h = o.nid;
    h          = (h << 12) ^ o.hidx.get_hash() ^ o.nid;
    return hash<uint64_t>{}(h);
  }
};

template <>
struct hash<Node::Compact_class> {
  size_t operator()(Node::Compact_class const &o) const { return hash<uint32_t>{}(o.nid); }
};
}  // namespace mmap_lib
