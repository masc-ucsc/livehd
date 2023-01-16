//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

class Lgraph;
class XEdge;
class Node;

#include <vector>

#include "hierarchy.hpp"
#include "lgedge.hpp"

using XEdge_iterator    = std::vector<XEdge>;
using Node_pin_iterator = std::vector<Node_pin>;

class Node_pin {
protected:
  friend class Lgraph;
  friend class Lgraph_attributes;
  friend class XEdge;
  friend class Node;
  friend class Fast_edge_iterator;
  friend class Flow_base_iterator;
  friend class Fwd_edge_iterator;
  friend class Bwd_edge_iterator;
  friend class Edge_raw;

  Lgraph         *top_g;
  Lgraph         *current_g;
  Hierarchy_index hidx;
  Index_id        idx;
  Port_ID         pid;
  bool            sink;

  constexpr Node_pin(Lgraph *_g, Lgraph *_c_g, Hierarchy_index _hidx, Index_id _idx, Port_ID _pid, bool _sink)
      : top_g(_g), current_g(_c_g), hidx(_hidx), idx(_idx), pid(_pid), sink(_sink) {
    I(_g);
    // Could be IDX=0 for invalid
  }

  [[nodiscard]] const Index_id get_idx() const {
    I(idx);
    return idx;
  }
  [[nodiscard]] const Index_id get_root_idx() const;

  [[nodiscard]] Node_pin switch_to_driver() const;
  [[nodiscard]] Node_pin switch_to_sink() const;

public:
  class __attribute__((packed)) Compact {
  protected:
    Hierarchy_index hidx;
    uint32_t        idx : Index_bits;
    uint32_t        sink : 1;

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

    [[nodiscard]] constexpr bool is_invalid() const { return idx == 0u; }

    [[nodiscard]] constexpr bool operator==(const Compact &other) const {
      return idx == other.idx && sink == other.sink
             && (hidx == other.hidx || Hierarchy::is_invalid(hidx) || Hierarchy::is_invalid(other.hidx));
    }
    [[nodiscard]] constexpr bool operator!=(const Compact &other) const { return !(*this == other); }

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
    friend class Lgraph_attributes;
    friend class Node;
    friend class Node_pin;
    friend class XEdge;
    friend class Fast_edge_iterator;
    friend class Flow_base_iterator;
    friend class Fwd_edge_iterator;
    friend class Bwd_edge_iterator;

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

    [[nodiscard]] constexpr bool is_invalid() const { return idx == 0u; }

    [[nodiscard]] constexpr bool operator==(const Compact_flat &other) const {
      return idx == other.idx && sink == other.sink && (lgid == other.lgid || lgid == 0u || other.lgid == 0u);
    }
    [[nodiscard]] constexpr bool operator!=(const Compact_flat &other) const { return !(*this == other); }
    [[nodiscard]] constexpr bool operator<(const Compact_flat &other) const {
      return idx < other.idx || (idx == other.idx && lgid < other.lgid);
    }

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
    friend class Lgraph_attributes;
    friend class Node;
    friend class Node_pin;
    friend class XEdge;
    friend class Fast_edge_iterator;
    friend class Flow_base_iterator;
    friend class Fwd_edge_iterator;
    friend class Bwd_edge_iterator;

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

    [[nodiscard]] constexpr bool is_invalid() const { return idx == 0u; }

    [[nodiscard]] constexpr bool operator==(const Compact_driver &other) const {
      return idx == other.idx && (hidx == other.hidx || Hierarchy::is_invalid(hidx) || Hierarchy::is_invalid(other.hidx));
    }
    [[nodiscard]] constexpr bool operator!=(const Compact_driver &other) const { return !(*this == other); }

    template <typename H>
    friend H AbslHashValue(H h, const Compact_driver &s) {
      return H::combine(std::move(h), s.hidx, s.idx);
    };
  };

  class __attribute__((packed)) Compact_class {
  protected:
  public:
    uint32_t idx : Index_bits;
    uint32_t sink : 1;

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
    friend class Lgraph_attributes;
    friend class Node;
    friend class Node_pin;
    friend class XEdge;
    friend class Fast_edge_iterator;
    friend class Flow_base_iterator;
    friend class Fwd_edge_iterator;
    friend class Bwd_edge_iterator;

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

    [[nodiscard]] constexpr bool is_invalid() const { return idx == 0u; }

    [[nodiscard]] constexpr bool operator==(const Compact_class_driver &other) const { return idx == other.idx; }
    [[nodiscard]] constexpr bool operator!=(const Compact_class_driver &other) const { return !(*this == other); }

    template <typename H>
    friend H AbslHashValue(H h, const Compact_class_driver &s) {
      return H::combine(std::move(h), s.idx);
    }
  };

  template <typename H>
  friend H AbslHashValue(H h, const Node_pin &s) {
    return H::combine(std::move(h), s.hidx, (int)s.idx, s.sink);  // Ignore lgraph pointer in hash
  }

  constexpr Node_pin() : top_g(nullptr), current_g(nullptr), hidx(-1), idx(0), pid(0), sink(false) {}
  // rest can not be constexpr (find pid)
  Node_pin(Lgraph *_g, const Compact &comp);
  Node_pin(std::string_view path, const Compact_flat &comp);
  Node_pin(Lgraph *_g, const Compact_driver &comp);
  Node_pin(Lgraph *_g, const Compact_class &comp);
  Node_pin(Lgraph *_g, Hierarchy_index hidx, const Compact_class &comp);
  Node_pin(Lgraph *_g, const Compact_class_driver &comp);

  // No constexpr (get_root_idx)

  [[nodiscard]] Compact get_compact() const {
    if (Hierarchy::is_invalid(hidx)) {
      return Compact(Hierarchy::hierarchical_root(), get_root_idx(), sink);
    }
    return Compact(hidx, get_root_idx(), sink);
  }
  [[nodiscard]] Compact_flat   get_compact_flat() const;
  [[nodiscard]] Compact_driver get_compact_driver() const;
  [[nodiscard]] Compact_class  get_compact_class() const {
    // OK to pick a hierarchical to avoid replication of info like names
    return Compact_class(idx, sink);
  }

  [[nodiscard]] Compact_class_driver get_compact_class_driver() const {
    // OK to pick a hierarchical to avoid replication of info like names
    I(!sink);  // Only driver pin allowed
    return Compact_class_driver(get_root_idx());
  }

  [[nodiscard]] Lgraph         *get_top_lgraph() const { return top_g; };
  [[nodiscard]] Lgraph         *get_class_lgraph() const { return current_g; };
  [[nodiscard]] Lgraph         *get_lg() const { return current_g; };
  [[nodiscard]] Hierarchy_index get_hidx() const { return hidx; };

  [[nodiscard]] constexpr Port_ID get_pid() const { return pid; }

  [[nodiscard]] bool has_inputs() const;
  [[nodiscard]] bool has_outputs() const;

  [[nodiscard]] bool is_graph_io() const;
  [[nodiscard]] bool is_graph_input() const;
  [[nodiscard]] bool is_graph_output() const;

  // Some redundant code with node (implemented because frequent)
  [[nodiscard]] bool   is_type_single_driver() const;
  [[nodiscard]] bool   is_type_const() const;
  [[nodiscard]] bool   is_type_tup() const;
  [[nodiscard]] bool   is_type_flop() const;
  [[nodiscard]] bool   is_type_register() const;
  [[nodiscard]] bool   is_type(const Ntype_op op) const;
  [[nodiscard]] Lconst get_type_const() const;

  [[nodiscard]] Node_pin change_to_sink_from_graph_out_driver() const {
    I(is_graph_output());
    return switch_to_sink();
  }

  [[nodiscard]] Node_pin change_to_driver_from_graph_out_sink() const {
    I(is_graph_output());
    return switch_to_driver();
  }

  [[nodiscard]] constexpr bool is_sink() const {
    I(idx);
    return sink;
  }
  [[nodiscard]] constexpr bool is_driver() const {
    I(idx);
    return !sink;
  }

  [[nodiscard]] Index_id get_node_nid() const;
  [[nodiscard]] Node     get_node() const;
  [[nodiscard]] Ntype_op get_type_op() const;

  [[nodiscard]] Node              get_driver_node() const;  // common 0 or 1 driver case
  [[nodiscard]] Node_pin          get_driver_pin() const;   // common 0 or 1 driver case
  [[nodiscard]] Node_pin_iterator inp_drivers() const;
  [[nodiscard]] Node_pin_iterator out_sinks() const;

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

  [[nodiscard]] Node create(Ntype_op op) const;                // create a new node, keep same hierarchy
  [[nodiscard]] Node create(Ntype_op op, std::pair<uint64_t, uint64_t> loc, std::string fname) const ;
  [[nodiscard]] Node create_const(const Lconst &value) const;  // create a new node, keep same hierarchy

  void connect_sink(const Node_pin &dst) const;
  void connect_sink(const Node &dst) const;
  void connect_driver(const Node_pin &dst) const;
  void connect_driver(const Node &dst) const;
  void connect(const Node_pin &dst) const {
    if (dst.is_sink() && is_driver()) {
      return connect_sink(dst);
    }
    I(dst.is_driver() && is_sink());
    return connect_driver(dst);
  }
  [[nodiscard]] int get_num_edges() const;

  // NOTE: No operator<() needed for std::set std::map to avoid their use. Use flat_map_set for speed

  void                         invalidate() { idx = 0; }
  [[nodiscard]] constexpr bool is_invalid() const { return idx == 0; }
  [[nodiscard]] constexpr bool is_down_node() const { return top_g != current_g; }
  [[nodiscard]] constexpr bool is_hierarchical() const { return !Hierarchy::is_invalid(hidx); }
  [[nodiscard]] Node_pin       get_non_hierarchical() const;
  [[nodiscard]] Node_pin       get_hierarchical() const;

  [[nodiscard]] bool operator==(const Node_pin &other) const {
    GI(idx == 0, Hierarchy::is_invalid(hidx));
    GI(other.idx == 0, Hierarchy::is_invalid(other.hidx));
    // GI(idx && other.idx, top_g == other.top_g);
    return get_root_idx() == other.get_root_idx() && sink == other.sink
           && (hidx == other.hidx || Hierarchy::is_invalid(hidx) || Hierarchy::is_invalid(other.hidx));
  }
  [[nodiscard]] bool operator!=(const Node_pin &other) const { return !(*this == other); }

  void nuke();  // Delete all the edges, and attributes of this node_pin

  // BEGIN ATTRIBUTE ACCESSORS
  [[nodiscard]] std::string      debug_name() const;
  [[nodiscard]] std::string      get_wire_name() const;
  [[nodiscard]] std::string_view get_hier_name() const;

  void                          set_name(std::string_view wname);
  void                          reset_name(std::string_view wname);
  void                          del_name();
  [[nodiscard]] std::string     get_name() const;
  [[nodiscard]] bool            has_name() const;
  [[nodiscard]] static Node_pin find_driver_pin(Lgraph *top, std::string_view wname);
  [[nodiscard]] std::string     get_pin_name() const;

  void                set_delay(float val);
  void                del_delay();
  [[nodiscard]] float get_delay() const;
  [[nodiscard]] bool  has_delay() const;

  void set_size(const Node_pin &dpin);  // set size and sign

  [[nodiscard]] Bits_t get_bits() const;
  void                 set_bits(Bits_t bits);

  void               set_unsign();
  void               set_sign();
  [[nodiscard]] bool is_unsign() const;

  [[nodiscard]] std::string_view get_type_sub_pin_name() const;

  void                 set_offset(Bits_t offset);
  [[nodiscard]] Bits_t get_offset() const;

  [[nodiscard]] bool is_connected() const;
  [[nodiscard]] bool is_connected(const Node_pin &pin) const;

  // END ATTRIBUTE ACCESSORS
  [[nodiscard]] XEdge_iterator out_edges() const;
  [[nodiscard]] XEdge_iterator inp_edges() const;

  [[nodiscard]] Node_pin get_down_pin() const;
  [[nodiscard]] Node_pin get_up_pin() const;
};
