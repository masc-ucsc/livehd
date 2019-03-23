//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once


#if 0
// WARNING: deprecated
// Node_driver_pin : public Node_pin
// Node_sink_pin   : public Node_pin
#endif

class LGraph;
class XEdge;

class Node_pin {
protected:
  friend class ConstNode;
  friend class Node;
  friend class Edge_raw;
  friend class LGraph_Base;
  friend class LGraph;
  // TODO: add LGraph so that it has more self-contained operations
  // E.g: it can find/check that idx matches the (nid,pid) entry for speed
  LGraph   *g;
  const Index_ID idx;
  const Port_ID  pid;
  const bool     sink;

  Node_pin(Index_ID _idx, Port_ID _pid, bool _sink) : idx(_idx), pid(_pid), sink(_sink) { I(_idx); }
public:
  Node_pin() : idx(0), pid(0), sink(false) { }

#if 0
  // TODO: once we have the attribute and collapse lgraphbase and lgraph
  static driver_pin(const Node &node, Port_ID _pid) {
    return Node_pin(node.get_nid(), _pid, false);
  }
#endif

#if 1
  // WARNING: deprecated: This should be moved to protected, and friend by lgraph only
  const Index_ID get_idx()   const { I(idx); return idx;    }
  const Port_ID  get_pid()   const { I(idx); return pid;    }
#endif

  LGraph  *get_lgraph() const { return g; };

  bool     is_input()  const { I(idx); return sink;  }
  bool     is_output() const { I(idx); return !sink; }

  bool     is_sink()   const { I(idx); return sink;  }
  bool     is_driver() const { I(idx); return !sink; }

#if 0
  // FUTURE: Once Node_pin has LGraph ptr
  std::string_view get_name() const; // First wirename, otherwise default type name
  void set_name(std::string_view name); // Set wirename
#endif

  XEdge connect_to(const Node_pin &dst);

  bool operator==(const Node_pin &other) const { I(idx); return (idx == other.idx) && (pid == other.pid) && (sink == other.sink); }

  bool operator!=(const Node_pin &other) const { I(idx); return (idx != other.idx) || (pid != other.pid) || (sink != other.sink); }

  bool operator<(const Node_pin &other) const {
    I(idx);
    return (idx < other.idx) || (idx == other.idx && pid < other.pid) ||
           (idx == other.idx && pid == other.pid && sink && !other.sink);
  }

  //static Node_pin get_out_pin(const Edge_raw *edge_raw);
  //static Node_pin get_inp_pin(const Edge_raw *edge_raw);
};

