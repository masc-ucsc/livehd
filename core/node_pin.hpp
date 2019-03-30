//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

class LGraph;
class XEdge;
class Node;

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
    const uint16_t sink : 1;

    // Hash
    constexpr size_t operator()(const Compact &obj) const { return (obj.idx<<1)|obj.sink; }
    constexpr operator size_t() const { return (idx<<1)|sink; }

    Compact(const Compact &obj): idx(obj.idx), sink(obj.sink) { }
    Compact(const Index_ID &_idx, bool _sink) :idx(_idx) ,sink(_sink) { };
    Compact(size_t raw) :idx(raw>>1) ,sink(raw&1) { };
    Compact() :idx(0) ,sink(0) { };
    Compact &operator=(const Compact &obj) {
      I(this != &obj);
      *((uint32_t *)this)  = *((uint32_t *)&obj); // NASTY, but how else to preserve the const???

      I(idx  == obj.idx);
      I(sink == obj.sink);

      return *this;
    };
  };
  Node_pin() : idx(0), pid(0), g(0), hid(0), sink(false) { }
  Node_pin(LGraph *_g, Hierarchy_id _hid, Compact comp);

  Compact get_compact() const {
    return Compact(idx,sink);
  };

  LGraph       *get_lgraph() const { return g; };
  Hierarchy_id  get_hid() const { return hid; };

  const Port_ID  get_pid()   const { I(idx); return pid;    }


  bool     is_input()  const { I(idx); return sink;  }
  bool     is_output() const { I(idx); return !sink; }

  bool     is_sink()   const { I(idx); return sink;  }
  bool     is_driver() const { I(idx); return !sink; }

  Node get_node() const;

  XEdge connect_to(const Node_pin &dst);

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

  bool is_valid() const { return idx!=0; }

  // BEGIN ATTRIBUTE ACCESSORS

  uint16_t get_bits() const;
  void     set_bits(uint16_t bits);

  void set_name(std::string_view wname);
  std::string_view get_name() const;
  bool has_name() const;

  void set_offset(uint16_t offset);
  uint16_t get_offset() const;

  // END ATTRIBUTE ACCESSORS
};

