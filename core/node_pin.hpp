//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

class LGraph;
class XEdge;
class Node;

enum class Node_pin_mode {
  Driver,
  Sink,
  Both
};

class Node_pin {
protected:
  friend class LGraph;
  friend class Node;
  friend class XEdge;
  friend class Edge_raw;

  const Index_ID      idx;
  const Port_ID       pid;
  LGraph             *g;
  const Hierarchy_id  hid;
  const bool          sink;

  Node_pin(LGraph *_g, Hierarchy_id _hid, Index_ID _idx, Port_ID _pid, bool _sink) : idx(_idx), pid(_pid), g(_g), hid(_hid), sink(_sink) { I(_g); I(_idx); }

  const Index_ID get_idx()   const { I(idx); return idx;    }
public:
  struct __attribute__((packed)) Compact {
    const uint32_t idx  : Index_bits;
    const uint32_t sink : 1;

    // Hash
    constexpr size_t operator()(const Compact &obj) const { return obj.idx|(obj.sink<<31); }
    constexpr operator size_t() const { return idx|(sink<<31); }

    Compact(const Compact &obj): idx(obj.idx), sink(obj.sink) { }
    Compact(const Index_ID &_idx, bool _sink) :idx(_idx) ,sink(_sink) { };
    Compact(size_t raw) :idx((raw<<1)>>1) ,sink(raw&(1UL<<31)?1:0) { };
    Compact() :idx(0) ,sink(0) { };
    Compact &operator=(const Compact &obj) {
      I(this != &obj);
      *((uint32_t *)this)  = *((uint32_t *)&obj); // NASTY, but how else to preserve the const???

      I(idx  == obj.idx);
      I(sink == obj.sink);

      return *this;
    };

    bool is_invalid() const { return idx == 0; }

    template <typename H>
    friend H AbslHashValue(H h, const Compact& s) {
      return H::combine(std::move(h), s.idx, s.sink);
    };
  };
  Node_pin() : idx(0), pid(0), g(0), hid(0), sink(false) { }
  Node_pin(LGraph *_g, Hierarchy_id _hid, Compact comp);
  Node_pin(LGraph *_g, Hierarchy_id _hid, Compact comp, Node_pin_mode mode);
  Node_pin &operator=(const Node_pin &obj) {
    I(this != &obj); // Do not assing object to itself. works but wastefull
    g   = obj.g;
    const_cast<Index_ID&>(idx)     = obj.idx;
    const_cast<Port_ID&>(pid)      = obj.pid;
    const_cast<Hierarchy_id&>(hid) = obj.hid;
    const_cast<bool&>(sink)        = obj.sink;

    return *this;
  };

  Compact get_compact() const {
    return Compact(idx,sink);
  };
  Compact get_compact(Node_pin_mode mode) const {
		if (mode==Node_pin_mode::Both)
			return Compact(idx,sink);
		return Compact(idx,false);
  };

  LGraph       *get_lgraph() const { return g; };
  Hierarchy_id  get_hid() const { return hid; };

  const Port_ID  get_pid()   const { I(idx); return pid;    }

  bool     is_graph_io()  const;
  bool     is_graph_input()  const;
  bool     is_graph_output() const;

  bool     is_input()  const { I(idx); return sink;  }
  bool     is_output() const { I(idx); return !sink; }

  bool     is_sink()   const { I(idx); return sink;  }
  bool     is_driver() const { I(idx); return !sink; }

  Node get_node() const;

  void connect_sink(Node_pin &dst);
  void connect_driver(Node_pin &dst);
  void connect(Node_pin &dst) {
    if (dst.is_sink() && is_driver())
      return connect_sink(dst);
    I(dst.is_driver() && is_sink());
    return connect_driver(dst);
  }

  bool operator==(const Node_pin &other) const { I(idx); I(g == other.g); return (idx == other.idx) && (pid == other.pid) && (sink == other.sink) && (hid == other.hid); }

  bool operator!=(const Node_pin &other) const { I(idx); I(g == other.g); return (idx != other.idx) || (pid != other.pid) || (sink != other.sink) || (hid != other.hid); }

  bool operator<(const Node_pin &other) const {
    I(idx);
    I(g == other.g);
    I(hid == other.hid); // Not clear in what case to mix. Just to catch likely bugs, but it should work well without this
    return (idx < other.idx) || (idx == other.idx && pid < other.pid) ||
           (idx == other.idx && pid == other.pid && sink && !other.sink);
  }

  //static Node_pin get_out_pin(const Edge_raw *edge_raw);
  //static Node_pin get_inp_pin(const Edge_raw *edge_raw);

  bool is_invalid() const { return g==nullptr || idx==0; }

  // BEGIN ATTRIBUTE ACCESSORS

  uint16_t get_bits() const;
  void     set_bits(uint16_t bits);

  std::string_view get_type_subgraph_io_name() const;
  std::string_view get_type_tmap_io_name() const;

  std::string_view set_name(std::string_view wname);
  std::string_view create_name() const;
  std::string_view get_name() const;
  bool has_name() const;

  void set_offset(uint16_t offset);
  uint16_t get_offset() const;

  // END ATTRIBUTE ACCESSORS
};

