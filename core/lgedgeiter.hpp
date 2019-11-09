//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

#include "lgraph.hpp"
#include "lgedge.hpp"

class Edge_raw_iterator {
protected:
  const bool  inputs;
  const Edge_raw *b;
  const Edge_raw *e;

public:
  class CPod_iterator {
  private:
    const Edge_raw *ptr;
    const Edge_raw *e;
    const bool  inputs;

  public:
    CPod_iterator(const Edge_raw *_ptr, const Edge_raw *_e, bool _inputs) : ptr(_ptr), e(_e), inputs(_inputs) {}
    CPod_iterator operator++();  // FIXME: CPod_iterator &operator++()
    CPod_iterator operator--() { // FIXME: CPod_iterator &operator--()
      CPod_iterator i(ptr, e, inputs);
      ptr -= ptr->next_node_inc();
      return i;
    }
    bool        operator!=(const CPod_iterator &other) const noexcept { return ptr != other.ptr; }
    const Edge_raw &operator*() const { return *ptr; }

  };

  Edge_raw_iterator() = delete;
  explicit Edge_raw_iterator(const Edge_raw *_b, const Edge_raw *_e, bool _inputs) : inputs(_inputs) {
    b = _b;
    e = _e;
    I(Node_Internal::get(e).is_node_state());
  }

  CPod_iterator begin() const { return CPod_iterator(b, e, inputs); }
  CPod_iterator end() const { return CPod_iterator(e, e, inputs); }
};


class Fast_edge_iterator {
protected:
  LGraph                *top_g;
  const bool             visit_sub;

public:
  class Fast_iter {
  private:
    // TODO: It may be clear to have Node, not all the Node fileds here (historical reasons before Node existed)
    LGraph           *top_g;
    LGraph           *current_g;
    Hierarchy_index   hidx;
    Index_ID          nid;
    const bool        visit_sub;

  public:
    Fast_iter(LGraph *_g, LGraph *_cg, const Hierarchy_index &_hidx, const Index_ID _nid, bool _visit_sub) : top_g(_g), current_g(_cg), hidx(_hidx), nid(_nid), visit_sub(_visit_sub) { }
    Fast_iter(bool _visit_sub) : top_g(nullptr), current_g(nullptr), visit_sub(_visit_sub) { }

    Fast_iter &operator=(const Fast_iter &it) {
      // TO allow rewind/recover the iterator
      top_g     = it.top_g;
      current_g = it.current_g;
      hidx      = it.hidx;
      nid       = it.nid;
      I(visit_sub==it.visit_sub);

      return *this;
    }

    Fast_iter &operator++();

    bool operator!=(const Fast_iter &other) const {
      GI(nid && other.nid, top_g == other.top_g); // Same or invalid
      return nid != other.nid || hidx != other.hidx;
    }
    bool operator==(const Fast_iter &other) const {
      GI(nid && other.nid, top_g == other.top_g); // Same or invalid
      return nid == other.nid && hidx == other.hidx;
    }

    Node operator*() const {
      return Node(top_g, current_g, hidx, nid);
    }

    bool is_invalid() const { return nid == 0; }
  };

  Fast_edge_iterator() = delete;
  explicit Fast_edge_iterator(LGraph *_g, bool _visit_sub) : top_g(_g), visit_sub(_visit_sub) { }

  Fast_iter begin() const;
  Fast_iter end() const { return Fast_iter(visit_sub); }
};

class Flow_base_iterator {
protected:
  bool               linear_phase;
  Node               current_node;
  Fast_edge_iterator::Fast_iter global_it;

  // State built during iteration
  const bool      visit_sub;
  absl::flat_hash_set<Node::Compact>  visited;
  std::vector<Node>                   pending_stack;

  Flow_base_iterator(LGraph *lg, bool _visit_sub);
  Flow_base_iterator(bool _visit_sub);
public:

  const Node &operator*() const { return current_node; }
};



class Fwd_edge_iterator {
public:
class Fwd_iter : public Flow_base_iterator {
protected:

  void topo_add_chain_down(const Node_pin &dst_pin);
  void topo_add_chain_fwd(const Node_pin &driver_pin);
  void fwd_get_from_linear(LGraph *top);
  void fwd_get_from_pending();
  void fwd_first(LGraph *lg);
  void fwd_next();

public:
  Fwd_iter(LGraph *lg, bool _visit_sub) :Flow_base_iterator(lg, _visit_sub) {
    fwd_first(lg);
  }
  Fwd_iter(bool _visit_sub) :Flow_base_iterator(_visit_sub) {
    I(current_node.is_invalid());
  }

  bool operator!=(const Fwd_iter &other) const {
    GI(!current_node.is_invalid() && !other.current_node.is_invalid(), current_node.get_top_lgraph() == other.current_node.get_top_lgraph());
    return current_node != other.current_node;
  }

  Fwd_iter &operator++() {
    I(!current_node.is_invalid());  // Do not call ++ after end
    fwd_next();
    return *this;
  }

};
protected:
  LGraph *top_g;
  const bool visit_sub;

public:
  Fwd_edge_iterator() = delete;
  explicit Fwd_edge_iterator(LGraph *_g, bool _visit_sub) : top_g(_g), visit_sub(_visit_sub) { }

  Fwd_iter begin() const { return Fwd_iter(top_g, visit_sub); }

  Fwd_iter end() const { return Fwd_iter(visit_sub); }
};

class Bwd_edge_iterator {
public:
class Bwd_iter : public Flow_base_iterator {
protected:

  void bwd_first(LGraph *lg);
  void bwd_next();

public:
  Bwd_iter(LGraph *lg, bool _visit_sub) :Flow_base_iterator(lg, _visit_sub) {
    bwd_first(lg);
  }
  Bwd_iter(bool _visit_sub) :Flow_base_iterator(_visit_sub) {
    I(current_node.is_invalid());
  }

  bool operator!=(const Bwd_iter &other) const {
    GI(!current_node.is_invalid() && !other.current_node.is_invalid(), current_node.get_top_lgraph() == other.current_node.get_top_lgraph());
    return current_node != other.current_node;
  }

  Bwd_iter &operator++() {
    I(!current_node.is_invalid());  // Do not call ++ after end
    bwd_next();
    return *this;
  }

};
protected:
  LGraph *top_g;
  const bool visit_sub;

public:
  Bwd_edge_iterator() = delete;
  explicit Bwd_edge_iterator(LGraph *_g, bool _visit_sub) : top_g(_g), visit_sub(_visit_sub) { }

  Bwd_iter begin() const { return Bwd_iter(top_g, visit_sub); }

  Bwd_iter end() const { return Bwd_iter(visit_sub); }
};
