//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

class Lgraph;
class XEdge;
class Node;

#include <vector>

#include "hierarchy.hpp"
#include "lgedge.hpp"
#include "mmap_map.hpp"

using XEdge_iterator    = std::vector<XEdge>;
using Node_pin_iterator = std::vector<Node_pin>;

class Node_pin {
protected:
  friend class Lgraph;
  friend class Lgraph_Node_Type;
  friend class XEdge;
  friend class Node;
  friend class Fast_edge_iterator;
  friend class Flow_base_iterator;
  friend class Fwd_edge_iterator;
  friend class Bwd_edge_iterator;
  friend class Edge_raw;

  Lgraph *        top_g;
  Lgraph *        current_g;
  Hierarchy_index hidx;
  Index_id        idx;
  Port_ID         pid;
  bool            sink;

  constexpr Node_pin(Lgraph *_g, Lgraph *_c_g, const Hierarchy_index &_hidx, Index_id _idx, Port_ID _pid, bool _sink)
      : top_g(_g), current_g(_c_g), hidx(_hidx), idx(_idx), pid(_pid), sink(_sink) {
    assert(_g);
    // Could be IDX=0 for invalid
  }

  const Index_id get_idx() const {
    I(idx);
    return idx;
  }
  const Index_id get_root_idx() const;

  Node_pin switch_to_driver() const;
  Node_pin switch_to_sink() const;

public:
  class __attribute__((packed)) Compact {
  protected:
    Hierarchy_index hidx;
    uint32_t        idx : Index_bits;
    uint32_t        sink : 1;

    friend class Lgraph;
    friend class Lgraph_Node_Type;
    friend class Node;
    friend class Node_pin;
    friend class XEdge;
    friend class Fast_edge_iterator;
    friend class Flow_base_iterator;
    friend class Fwd_edge_iterator;
    friend class Bwd_edge_iterator;
    friend class mmap_lib::hash<Node_pin::Compact>;

  public:
    // constexpr operator size_t() const { I(0); return idx|(sink<<31); }

    Compact(const Compact &obj) : hidx(obj.hidx), idx(obj.idx), sink(obj.sink) {}
    Compact(const Hierarchy_index _hidx, Index_id _idx, bool _sink) : hidx(_hidx), idx(_idx), sink(_sink) {
      I(!Hierarchy::is_invalid(hidx));
    };
    Compact() : idx(0), sink(0){};
    Compact &operator=(const Compact &obj) {
      I(this != &obj);
      hidx = obj.hidx;
      idx  = obj.idx;
      sink = obj.sink;

      return *this;
    };

    constexpr bool is_invalid() const { return idx == 0; }

    constexpr bool operator==(const Compact &other) const {
      return idx == other.idx && sink == other.sink && (hidx == other.hidx || Hierarchy::is_invalid(hidx) || Hierarchy::is_invalid(other.hidx));
    }
    constexpr bool operator!=(const Compact &other) const { return !(*this == other); }

    template <typename H>
    friend H AbslHashValue(H h, const Compact &s) {
      return H::combine(std::move(h), s.hidx, s.idx, s.sink);
    };
  };
  class __attribute__((packed)) Compact_flat {
  protected:
    uint32_t lgid;
    uint32_t idx : Index_bits;
    uint32_t sink : 1;

    friend class Lgraph;
    friend class Lgraph_Node_Type;
    friend class Node;
    friend class Node_pin;
    friend class XEdge;
    friend class Fast_edge_iterator;
    friend class Flow_base_iterator;
    friend class Fwd_edge_iterator;
    friend class Bwd_edge_iterator;
    friend class mmap_lib::hash<Node_pin::Compact_flat>;

  public:
    // constexpr operator size_t() const { I(0); return idx|(sink<<31); }

    Compact_flat(const Compact_flat &obj) : lgid(obj.lgid), idx(obj.idx), sink(obj.sink) {}
    constexpr Compact_flat(const Lg_type_id _lgid, Index_id _idx, bool _sink) : lgid(_lgid), idx(_idx), sink(_sink){};
    constexpr Compact_flat() : lgid(0), idx(0), sink(0){};

    Compact_flat &operator=(const Compact_flat &obj) {
      I(this != &obj);
      lgid = obj.lgid;
      idx  = obj.idx;
      sink = obj.sink;

      return *this;
    };

    constexpr bool is_invalid() const { return idx == 0; }

    constexpr bool operator==(const Compact_flat &other) const {
      return idx == other.idx && sink == other.sink && (lgid == other.lgid || lgid == 0 || other.lgid == 0);
    }
    constexpr bool operator!=(const Compact_flat &other) const { return !(*this == other); }

    template <typename H>
    friend H AbslHashValue(H h, const Compact_flat &s) {
      return H::combine(std::move(h), s.lgid, s.idx, s.sink);
    };
  };

  class __attribute__((packed)) Compact_driver {
  protected:
    Hierarchy_index hidx;
    uint32_t        idx : Index_bits;

    friend class Lgraph;
    friend class Lgraph_Node_Type;
    friend class Node;
    friend class Node_pin;
    friend class XEdge;
    friend class Fast_edge_iterator;
    friend class Flow_base_iterator;
    friend class Fwd_edge_iterator;
    friend class Bwd_edge_iterator;
    friend class mmap_lib::hash<Node_pin::Compact_driver>;

  public:
    // constexpr operator size_t() const { I(0); return idx|(sink<<31); }

    Compact_driver(const Compact_driver &obj) : hidx(obj.hidx), idx(obj.idx) {}
    Compact_driver(const Hierarchy_index _hidx, Index_id _idx) : hidx(_hidx), idx(_idx) { I(!Hierarchy::is_invalid(hidx)); };
    Compact_driver() : idx(0){};
    Compact_driver &operator=(const Compact_driver &obj) {
      I(this != &obj);
      hidx = obj.hidx;
      idx  = obj.idx;

      return *this;
    };

    constexpr bool is_invalid() const { return idx == 0; }

    constexpr bool operator==(const Compact_driver &other) const {
      return idx == other.idx && (hidx == other.hidx || Hierarchy::is_invalid(hidx) || Hierarchy::is_invalid(other.hidx));
    }
    constexpr bool operator!=(const Compact_driver &other) const { return !(*this == other); }

    template <typename H>
    friend H AbslHashValue(H h, const Compact_driver &s) {
      return H::combine(std::move(h), s.hidx, s.idx);
    };
  };

  class __attribute__((packed)) Compact_class {
  protected:
    uint32_t idx : Index_bits;
    uint32_t sink : 1;

    friend class Lgraph;
    friend class Lgraph_Node_Type;
    friend class Node;
    friend class Node_pin;
    friend class XEdge;
    friend class Fast_edge_iterator;
    friend class Flow_base_iterator;
    friend class Fwd_edge_iterator;
    friend class Bwd_edge_iterator;
    friend class mmap_lib::hash<Node_pin::Compact_class>;

  public:
    // constexpr operator size_t() const { I(0); return idx|(sink<<31); }

    Compact_class(const Compact_class &obj) : idx(obj.idx), sink(obj.sink) {}
    Compact_class(Index_id _idx, bool _sink) : idx(_idx), sink(_sink) {}
    Compact_class() : idx(0), sink(0) {}
    Compact_class &operator=(const Compact_class &obj) {
      I(this != &obj);
      idx  = obj.idx;
      sink = obj.sink;

      return *this;
    }

    constexpr bool is_invalid() const { return idx == 0; }

    constexpr bool operator==(const Compact_class &other) const { return idx == other.idx && sink == other.sink; }
    constexpr bool operator!=(const Compact_class &other) const { return !(*this == other); }

    template <typename H>
    friend H AbslHashValue(H h, const Compact_class &s) {
      return H::combine(std::move(h), s.idx, s.sink);
    }
  };

  class __attribute__((packed)) Compact_class_driver {
  protected:
    uint32_t idx : Index_bits;

    friend class Lgraph;
    friend class Lgraph_Node_Type;
    friend class Node;
    friend class Node_pin;
    friend class XEdge;
    friend class Fast_edge_iterator;
    friend class Flow_base_iterator;
    friend class Fwd_edge_iterator;
    friend class Bwd_edge_iterator;
    friend class mmap_lib::hash<Node_pin::Compact_class_driver>;

  public:
    // constexpr operator size_t() const { I(0); return idx|(sink<<31); }

    Compact_class_driver(const Compact_class_driver &obj) : idx(obj.idx) {}
    Compact_class_driver(Index_id _idx) : idx(_idx) {}
    Compact_class_driver() : idx(0) {}
    Compact_class_driver &operator=(const Compact_class_driver &obj) {
      I(this != &obj);
      idx = obj.idx;

      return *this;
    }

    constexpr bool is_invalid() const { return idx == 0; }

    constexpr bool operator==(const Compact_class_driver &other) const { return idx == other.idx; }
    constexpr bool operator!=(const Compact_class_driver &other) const { return !(*this == other); }

    template <typename H>
    friend H AbslHashValue(H h, const Compact_class_driver &s) {
      return H::combine(std::move(h), s.idx);
    }
  };

  template <typename H>
  friend H AbslHashValue(H h, const Node_pin &s) {
    return H::combine(std::move(h), s.hidx, (int)s.idx, s.sink);  // Ignore lgraph pointer in hash
  }

  constexpr Node_pin() : top_g(0), current_g(0), idx(0), pid(0), sink(false) {}
  // rest can not be constexpr (find pid)
  Node_pin(Lgraph *_g, const Compact &comp);
  Node_pin(const mmap_lib::str &path, const Compact_flat &comp);
  Node_pin(Lgraph *_g, const Compact_driver &comp);
  Node_pin(Lgraph *_g, const Compact_class &comp);
  Node_pin(Lgraph *_g, const Hierarchy_index &hidx, const Compact_class &comp);
  Node_pin(Lgraph *_g, const Compact_class_driver &comp);

  // No constexpr (get_root_idx)

  Compact get_compact() const {
    if (Hierarchy::is_invalid(hidx))
      return Compact(Hierarchy::hierarchical_root(), get_root_idx(), sink);
    return Compact(hidx, get_root_idx(), sink);
  }
  Compact_flat   get_compact_flat() const;
  Compact_driver get_compact_driver() const;
  Compact_class  get_compact_class() const {
    // OK to pick a hierarchical to avoid replication of info like names
    return Compact_class(idx, sink);
  }

  Compact_class_driver get_compact_class_driver() const {
    // OK to pick a hierarchical to avoid replication of info like names
    I(!sink);  // Only driver pin allowed
    return Compact_class_driver(get_root_idx());
  }

  Lgraph *        get_top_lgraph() const { return top_g; };
  Lgraph *        get_class_lgraph() const { return current_g; };
  Lgraph *        get_lg() const { return current_g; };
  Hierarchy_index get_hidx() const { return hidx; };

  constexpr Port_ID get_pid() const { return pid; }

  bool has_inputs() const;
  bool has_outputs() const;

  bool is_graph_io() const;
  bool is_graph_input() const;
  bool is_graph_output() const;

  // Some redundant code with node (implemented because frequent)
  bool   is_type_const() const;
  bool   is_type_tup() const;
  bool   is_type_flop() const;
  bool   is_type_register() const;
  bool   is_type(const Ntype_op op) const;
  Lconst get_type_const() const;

  Node_pin change_to_sink_from_graph_out_driver() const {
    I(is_graph_output());
    return switch_to_sink();
  }

  Node_pin change_to_driver_from_graph_out_sink() const {
    I(is_graph_output());
    return switch_to_driver();
  }

  constexpr bool is_sink() const {
    assert(idx);
    return sink;
  }
  constexpr bool is_driver() const {
    assert(idx);
    return !sink;
  }

  Index_id get_node_nid() const;
  Node     get_node() const;
  Ntype_op get_type_op() const;

  Node              get_driver_node() const;  // common 0 or 1 driver case
  Node_pin          get_driver_pin() const;   // common 0 or 1 driver case
  Node_pin_iterator inp_drivers() const;
  Node_pin_iterator out_sinks() const;

  void del_driver(Node_pin &dst);
  void del_sink(Node_pin &dst);
  void del(Node_pin &dst) {
    if (dst.is_sink()) {
      I(is_driver());
      del_sink(dst);
    } else {
      I(dst.is_driver() && is_sink());  // they must be opposite
      del_driver(dst);
    }
  }
  void del();  // del self and all connections

  Node create(Ntype_op op) const;                // create a new node, keep same hierarchy
  Node create_const(const Lconst &value) const;  // create a new node, keep same hierarchy

  void connect_sink(const Node_pin &dst) const;
  void connect_sink(const Node &dst) const;
  void connect_driver(const Node_pin &dst) const;
  void connect_driver(const Node &dst) const;
  void connect(const Node_pin &dst) const {
    if (dst.is_sink() && is_driver())
      return connect_sink(dst);
    I(dst.is_driver() && is_sink());
    return connect_driver(dst);
  }
  int get_num_edges() const;

  // NOTE: No operator<() needed for std::set std::map to avoid their use. Use flat_map_set for speed

  void           invalidate() { idx = 0; }
  constexpr bool is_invalid() const { return idx == 0; }
  constexpr bool is_down_node() const { return top_g != current_g; }
  constexpr bool is_hierarchical() const { return !Hierarchy::is_invalid(hidx); }
  Node_pin       get_non_hierarchical() const;
  Node_pin       get_hierarchical() const;

  bool operator==(const Node_pin &other) const {
    GI(idx == 0, Hierarchy::is_invalid(hidx));
    GI(other.idx == 0, Hierarchy::is_invalid(other.hidx));
    // GI(idx && other.idx, top_g == other.top_g);
    return get_root_idx() == other.get_root_idx() && sink == other.sink
           && (hidx == other.hidx || Hierarchy::is_invalid(hidx) || Hierarchy::is_invalid(other.hidx));
  }
  bool operator!=(const Node_pin &other) const { return !(*this == other); }

  void nuke();  // Delete all the edges, and attributes of this node_pin

  // BEGIN ATTRIBUTE ACCESSORS
  std::string debug_name() const;
  mmap_lib::str get_wire_name() const;

  void             set_name(const mmap_lib::str &wname);
  void             reset_name(const mmap_lib::str &wname);
  void             del_name();
  mmap_lib::str get_name() const;
  bool             has_name() const;
  static Node_pin  find_driver_pin(Lgraph *top, mmap_lib::str wname);
  mmap_lib::str get_pin_name() const;

  void             set_prp_vname(const mmap_lib::str &prp_vname);
  mmap_lib::str get_prp_vname() const;
  bool             has_prp_vname() const;
  void             dump_all_prp_vname() const;

  void  set_delay(float val);
  void  del_delay();
  float get_delay() const;
  bool  has_delay() const;

  void set_size(const Node_pin &dpin);  // set size and sign

  Bits_t get_bits() const;
  void   set_bits(Bits_t bits);

  void set_unsign();
  void set_sign();
  bool is_unsign() const;

  mmap_lib::str get_type_sub_pin_name() const;

  void   set_offset(Bits_t offset);
  Bits_t get_offset() const;

  uint32_t       get_ssa() const;
  void           set_ssa(uint32_t v);
  bool           has_ssa() const;

  bool           is_connected() const;
  bool           is_connected(const Node_pin &pin) const;

  // END ATTRIBUTE ACCESSORS
  XEdge_iterator out_edges() const;
  XEdge_iterator inp_edges() const;

  Node_pin get_down_pin() const;
  Node_pin get_up_pin() const;
};

namespace mmap_lib {
template <>
struct hash<Node_pin::Compact> {
  size_t operator()(Node_pin::Compact const &o) const {
    uint64_t h = o.idx;
    h          = (h << 12) ^ o.hidx.hash() ^ o.idx;
    return hash<uint64_t>{}((h << 1) + o.sink);
  }
};

template <>
struct hash<Node_pin::Compact_flat> {
  size_t operator()(Node_pin::Compact_flat const &o) const {
    uint64_t h = o.lgid;
    h          = (h << 12) ^ o.idx;
    return hash<uint64_t>{}((h << 1) + o.sink);
  }
};

template <>
struct hash<Node_pin::Compact_driver> {
  size_t operator()(Node_pin::Compact_driver const &o) const {
    uint64_t h = o.idx;
    h          = (h << 12) ^ o.hidx.hash() ^ o.idx;
    return hash<uint64_t>{}(h);
  }
};

template <>
struct hash<Node_pin::Compact_class> {
  size_t operator()(Node_pin::Compact_class const &o) const {
    uint32_t h = o.idx;
    return hash<uint32_t>{}((h << 1) + o.sink);
  }
};

template <>
struct hash<Node_pin::Compact_class_driver> {
  size_t operator()(Node_pin::Compact_class_driver const &o) const { return hash<uint32_t>{}(o.idx); }
};
}  // namespace mmap_lib
