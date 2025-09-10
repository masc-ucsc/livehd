//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "absl/container/flat_hash_set.h"
// #include "absl/container/inlined_vector.h"

#include "cell.hpp"
#include "hierarchy.hpp"
#include "lconst.hpp"
#include "lgraph_base_core.hpp"
#include "node_pin.hpp"
#include "sub_node.hpp"

class Ann_place;
class Ann_dim;
using Node_iterator = std::vector<Node>;

class Node {
protected:
  Lgraph         *top_g{nullptr};
  mutable Lgraph *current_g{nullptr};
  Hierarchy_index hidx{-1};
  Index_id        nid{0};

  friend class Lgraph;
  friend class Lgraph_attributes;
  friend class Node_pin;
  friend class XEdge;
  friend class Fast_edge_iterator;
  friend class Flow_base_iterator;
  friend class Fwd_edge_iterator;
  friend class Bwd_edge_iterator;
  friend class Hierarchy;

  constexpr Node(Lgraph *_g, Lgraph *_c_g, Hierarchy_index _hidx, Index_id _nid)
      : top_g(_g), current_g(_c_g), hidx(_hidx), nid(_nid) {
    assert(nid);
    assert(top_g);
    assert(current_g);
  }

  void invalidate(Lgraph *_g);
  void invalidate();
  void update(Index_id _nid) { nid = _nid; }
  void update(Hierarchy_index _hidx, Index_id _nid);

public:
  class __attribute__((packed)) Compact {
  protected:
    Hierarchy_index hidx{-1};          // 4 bytes
    uint32_t        nid : Index_bits;  // 4 bytes

    friend class Lgraph;
    friend class Lgraph_attributes;
    friend class Node;
    friend class Node_pin;
    friend class XEdge;
    friend class Fast_edge_iterator;
    friend class Flow_base_iterator;
    friend class Fwd_edge_iterator;
    friend class Bwd_edge_iterator;

  public:
    constexpr Compact(Hierarchy_index _hidx, Index_id _nid) : hidx(_hidx), nid(_nid) { assert(nid); };
    constexpr Compact() : nid(0) {};

    [[nodiscard]] constexpr Index_id get_nid() const { return nid; }  // Mostly for debugging or to know order

    [[nodiscard]] constexpr Hierarchy_index get_hidx() const {
      I(!Hierarchy::is_invalid(hidx));
      return hidx;
    }

    // Can not be constexpr find current_g
    Node get_node(Lgraph *lg) const { return {lg, *this}; }

    [[nodiscard]] constexpr bool is_invalid() const { return nid == 0u; }

    constexpr bool operator==(const Compact &other) const {
      return nid == other.nid && (hidx == other.hidx || Hierarchy::is_invalid(hidx) || Hierarchy::is_invalid(other.hidx));
    }
    constexpr bool operator!=(const Compact &other) const { return !(*this == other); }

    template <typename H>
    friend H AbslHashValue(H h, const Compact &s) {
      return H::combine(std::move(h), s.hidx, s.nid);
    };
  };

  class __attribute__((packed)) Compact_flat {
  protected:
    uint32_t lgid{0};
    uint32_t nid : Index_bits;

    friend class Lgraph;
    friend class Lgraph_attributes;
    friend class Node;
    friend class Node_pin;
    friend class XEdge;
    friend class Fast_edge_iterator;
    friend class Flow_base_iterator;
    friend class Fwd_edge_iterator;
    friend class Bwd_edge_iterator;

  public:
    Compact_flat(const Compact_flat &obj) : lgid(obj.lgid), nid(obj.nid) {}
    constexpr Compact_flat(const Lg_type_id &_lgid, Index_id _nid) : lgid(_lgid.value), nid(_nid) { assert(nid); };
    constexpr Compact_flat() : nid(0) {};

    [[nodiscard]] constexpr Index_id get_nid() const { return nid; }  // Mostly for debugging or to know order

    // Can not be constexpr find current_g
    [[nodiscard]] Node get_node(std::string_view path) const { return {path, *this}; }

    [[nodiscard]] constexpr bool is_invalid() const { return nid == 0u; }

    constexpr bool operator==(const Compact_flat &other) const { return nid == other.nid && lgid == other.lgid; }
    constexpr bool operator!=(const Compact_flat &other) const { return !(*this == other); }

    template <typename H>
    friend H AbslHashValue(H h, const Compact_flat &s) {
      return H::combine(std::move(h), s.lgid, s.nid);
    };
  };

  class __attribute__((packed)) Compact_class {
  protected:
    uint32_t nid : Index_bits;

    friend class Lgraph;
    friend class Lgraph_attributes;
    friend class Node;
    friend class Node_pin;
    friend class XEdge;
    friend class Fast_edge_iterator;
    friend class Flow_base_iterator;
    friend class Fwd_edge_iterator;
    friend class Bwd_edge_iterator;
    friend class Hierarchy;

  public:
    // constexpr operator size_t() const { return nid; }
    constexpr Compact_class() : nid(0) {};  // needed for lgthree which allocates empty data

    constexpr Compact_class(const Index_id &_nid) : nid(_nid) {};

    Node get_node(Lgraph *lg) const { return {lg, *this}; }

    [[nodiscard]] constexpr Index_id get_nid() const { return nid; }
    [[nodiscard]] constexpr bool     is_invalid() const { return nid == 0u; }

    constexpr bool operator==(const Compact_class &other) const { return nid == other.nid; }
    constexpr bool operator!=(const Compact_class &other) const { return nid != other.nid; }

    template <typename H>
    friend H AbslHashValue(H h, const Compact_class &s) {
      return H::combine(std::move(h), s.nid);
    };
  };

  void update(Hierarchy_index _hidx);

  template <typename H>
  friend H AbslHashValue(H h, const Node &s) {
    return H::combine(std::move(h), (int)s.hidx, (int)s.nid);  // Ignore lgraph pointer in hash
  };

  // NOTE: No operator<() needed for std::set std::map to avoid their use. Use flat_map_set for speed
  void update(Lgraph *_g, const Node::Compact &comp);
  void update(const Node::Compact &comp);
  void update(const Node &node);

  constexpr Node() = default;

  Node(Lgraph *_g, const Compact &comp) { update(_g, comp); }
  Node(std::string_view path, const Compact_flat &comp);
  Node(Lgraph *_g, const Compact_flat &comp);
  Node(Lgraph *_g, Hierarchy_index _hidx, const Compact_class &comp);
  constexpr Node(Lgraph *_g, const Compact_class &comp) : top_g(_g), hidx(Hierarchy::non_hierarchical()), nid(comp.nid) {
    I(nid);
    I(top_g);

    current_g = top_g;
  }
#if 0
  Node &operator=(const Node &obj) {
    I(this != &obj); // Do not assign object to itself. works but wastefull
    top_g     = obj.top_g;
    current_g = obj.current_g;
    const_cast<Index_id&>(nid)     = obj.nid;
    const_cast<Hierarchy_index&>(hidx) = obj.hidx;

    return *this;
  };
#endif

  inline Compact get_compact() const { return {hidx, nid}; }

  Compact_flat         get_compact_flat() const;
  inline Compact_class get_compact_class() const {
    // OK to pick a hierarchical to avoid replication of info like names
    return {nid};
  }

  Lgraph        *get_top_lgraph() const { return top_g; }
  Lgraph        *get_class_lgraph() const { return current_g; }
  Lgraph        *get_lg() const { return current_g; }  // To handle hierarchical API
  Graph_library *ref_library() const;

  Index_id        get_nid() const { return nid; }
  Hierarchy_index get_hidx() const { return hidx; }

  Node_pin get_driver_pin() const {
    I(!Ntype::is_multi_driver(get_type_op()));
    Node_pin pin(top_g, current_g, hidx, nid, 0, false);
    return pin;
  }
  Node_pin get_sink_pin() const {
    I(!Ntype::is_multi_sink(get_type_op()));
    return {top_g, current_g, hidx, nid, 0, true};
  }

  Node_pin get_driver_pin_raw(Port_ID pid) const;
  Node_pin get_sink_pin_raw(Port_ID pid) const;

  Node_pin get_sink_pin_driver(std::string_view pname) const { return get_sink_pin(pname).get_driver_pin(); }

  Node_pin get_driver_pin_slow(std::string_view pname) const;
  Node_pin get_driver_pin(std::string_view pname) const {
    assert(pname.size());
    if (unlikely(is_type_sub() && pname != "%")) {
      return get_driver_pin_slow(pname);
    }
    I(!Ntype::is_multi_driver(get_type_op()));       // Use direct pid for multidriver
    return {top_g, current_g, hidx, nid, 0, false};  // could be invalid if not setup
  }
  Node_pin get_sink_pin_slow(std::string_view pname) const;
  Node_pin get_sink_pin(std::string_view pname) const {
    assert(pname.size());
    if (unlikely(is_type_sub() && pname != "$")) {
      return get_sink_pin_slow(pname);
    }
    auto pid = Ntype::get_sink_pid(get_type_op(), pname);
    if (pid) {
      return get_sink_pin_raw(pid);
    }
    return {top_g, current_g, hidx, nid, 0, true};  // could be invalid if not setup
  }

  Node_pin setup_driver_pin(std::string_view pname) const;
  Node_pin setup_driver_pin_slow(std::string_view name) const;
  Node_pin setup_driver_pin_raw(Port_ID pid) const;
  Node_pin setup_driver_pin() const;

  Node_pin setup_sink_pin_slow(std::string_view name);
  Node_pin setup_sink_pin(std::string_view pname) {
    assert(pname.size());
    if (unlikely(is_type_sub() && pname != "$")) {
      return setup_sink_pin_slow(pname);
    }
    auto pid = Ntype::get_sink_pid(get_type_op(), pname);
    if (pid) {
      return setup_sink_pin_raw(pid);
    }
    return {top_g, current_g, hidx, nid, 0, true};  // could be invalid if not setup
  }

  Node_pin setup_sink_pin_raw(Port_ID pid);
  Node_pin setup_sink_pin() const;

  bool has_inputs() const;
  bool has_outputs() const;
  int  get_num_inp_edges() const;
  int  get_num_out_edges() const;
  int  get_num_edges() const;

  constexpr bool is_invalid() const { return nid == 0; }
  constexpr bool is_down_node() const { return top_g != current_g; }
  constexpr bool is_hierarchical() const { return !Hierarchy::is_invalid(hidx); }
  Node           get_non_hierarchical() const;

  constexpr bool operator==(const Node &other) const {
    GI(nid == 0, Hierarchy::is_invalid(hidx));
    GI(other.nid == 0, Hierarchy::is_invalid(other.hidx));
    GI(nid && other.nid, top_g == other.top_g);

    return nid == other.nid && (hidx == other.hidx || Hierarchy::is_invalid(hidx) || Hierarchy::is_invalid(other.hidx));
  }
  constexpr bool operator!=(const Node &other) const { return !(*this == other); }

  void   set_type_lut(const Lconst &lutid);
  Lconst get_type_lut() const;

  std::string_view get_type_name() const;
  Ntype_op         get_type_op() const;
  void             set_type(const Ntype_op op);
  void             set_type(const Ntype_op op, Bits_t bits);
  bool             is_type(const Ntype_op op) const;
  bool             is_type_sub() const { return get_type_op() == Ntype_op::Sub; }
  bool             is_type_synth() const { return Ntype::is_synthesizable(get_type_op()); }
  bool             is_type_const() const;
  bool             is_type_attr() const;
  bool             is_type_flop() const;
  bool             is_type_register() const;  // Flop/Latch/Memory
  bool             is_type_tup() const;
  bool             is_type_io() const { return nid == Hardcoded_input_nid || nid == Hardcoded_output_nid; }
  bool             is_type_loop_first() const;
  bool             is_type_loop_last() const;
  bool             is_type_multi_driver() const { return Ntype::is_multi_driver(get_type_op()); }

  Hierarchy_index hierarchy_go_down() const;
  Hierarchy_index hierarchy_go_up() const;
  Node            get_up_node() const;
  bool            is_root() const;

  void            set_type_sub(Lg_type_id subid);
  void            set_type_const(const Lconst &val);
  Lg_type_id      get_type_sub() const;
  const Sub_node &get_type_sub_node() const;
  Sub_node       *ref_type_sub_node() const;
  Lgraph         *ref_type_sub_lgraph() const;  // Slower than other get_type_sub
  bool            is_type_sub_present() const;

  Lconst get_type_const() const;

  void connect_sink(const Node &n2) const { setup_sink_pin().connect_driver(n2.setup_driver_pin()); }
  void connect_driver(const Node &n2) const { setup_driver_pin().connect_sink(n2.setup_sink_pin()); }

  void connect_sink(const Node_pin &dpin) const { setup_sink_pin().connect_driver(dpin); }
  void connect_driver(const Node_pin &spin) const { setup_driver_pin().connect_sink(spin); }

  void nuke();  // Delete all the pins, edges, and attributes of this node

  bool is_sink_connected(std::string_view v) const;
  bool is_driver_connected(std::string_view v) const;

  Node_pin_iterator out_connected_pins() const;
  Node_pin_iterator inp_connected_pins() const;

  XEdge_iterator out_edges() const;
  XEdge_iterator inp_edges() const;

  XEdge_iterator out_edges_ordered() const;  // Slower than inp_edges, but edges ordered by driver.pid
  XEdge_iterator inp_edges_ordered() const;  // Slower than inp_edges, but edges ordered by sink.pid

  XEdge_iterator out_edges_ordered_reverse() const;  // Slower than inp_edges, but edges ordered by driver.pid
  XEdge_iterator inp_edges_ordered_reverse() const;  // Slower than inp_edges, but edges ordered by sink.pid

  Node_pin_iterator inp_drivers() const;
  Node_pin_iterator out_sinks() const;

  bool is_graph_io() const { return nid == Hardcoded_input_nid || nid == Hardcoded_output_nid; }
  bool is_graph_input() const { return nid == Hardcoded_input_nid; }
  bool is_graph_output() const { return nid == Hardcoded_output_nid; }

  void del_node();

  Node create(Ntype_op op) const;                                                        // create a new node, keep same hierarchy
  Node create(Ntype_op op, std::pair<uint64_t, uint64_t> loc, std::string fname) const;  // create a new node, keep same hierarchy
  Node create_const(const Lconst &value) const;                                          // create a new node, keep same hierarchy

  // BEGIN ATTRIBUTE ACCESSORS
  std::string debug_name() const;
  std::string default_instance_name() const;

  // non-hierarchical node name (1 for all nodes)
  void                           set_name(std::string_view iname);
  std::string                    get_name() const;
  [[nodiscard]] std::string_view get_hier_name() const;
  std::string                    get_or_create_name() const;
  bool                           has_name() const;

  void            set_place(const Ann_place &p);
  const Ann_place get_place() const;
  bool            has_place() const;

  Bits_t get_bits() const;

  void                                set_loc1(uint64_t pos);
  void                                set_loc2(uint64_t pos);
  void                                set_loc(uint64_t pos1, uint64_t pos2);
  void                                set_loc(std::pair<uint64_t, uint64_t> loc) { set_loc(loc.first, loc.second); };
  const std::pair<uint64_t, uint64_t> get_loc() const;
  bool                                has_loc() const;
  void                                del_loc();

  void        set_source(std::string_view fname);
  std::string get_source() const;
  void        del_source();

  void set_color(int color);
  void del_color();
  int  get_color() const;
  bool has_color() const;
  // END ATTRIBUTE ACCESSORS

  void dump() const;
};
