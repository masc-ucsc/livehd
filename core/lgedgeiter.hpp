//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lgedge.hpp"
#include "lgraph.hpp"
#include "node.hpp"

class Fast_edge_iterator {
protected:
  Lgraph *   top_g;
  const bool visit_sub;

public:
  class Fast_iter {
  private:
    // TODO: It may be clear to have Node, not all the Node fileds here (historical reasons before Node existed)
    Lgraph *        top_g;
    Lgraph *        current_g;
    Hierarchy_index hidx;
    Index_id        nid;
    const bool      visit_sub;

    void go_next();

  public:
    constexpr Fast_iter(Lgraph *_g, Lgraph *_cg, const Hierarchy_index &_hidx, const Index_id _nid, bool _visit_sub)
        : top_g(_g), current_g(_cg), hidx(_hidx), nid(_nid), visit_sub(_visit_sub) {}
    constexpr Fast_iter(bool _visit_sub) : top_g(nullptr), current_g(nullptr), visit_sub(_visit_sub) {}

    constexpr Fast_iter(const Fast_iter &it)
        : top_g(it.top_g), current_g(it.current_g), hidx(it.hidx), nid(it.nid), visit_sub(it.visit_sub) {}

    constexpr Fast_iter &operator=(const Fast_iter &it) {
      // TO allow rewind/recover the iterator
      top_g     = it.top_g;
      current_g = it.current_g;
      hidx      = it.hidx;
      nid       = it.nid;
      assert(visit_sub == it.visit_sub);

      return *this;
    }

    void       advance_if_deleted();
    Fast_iter &operator++();

    bool operator!=(const Fast_iter &other) const {
      GI(nid && other.nid, top_g == other.top_g);  // Same or invalid
      return nid != other.nid || hidx != other.hidx;
    }
    bool operator==(const Fast_iter &other) const {
      GI(nid && other.nid, top_g == other.top_g);  // Same or invalid
      return nid == other.nid && hidx == other.hidx;
    }

    Node operator*() const { return Node(top_g, current_g, hidx, nid); }

    bool is_invalid() const { return nid == 0; }
  };

  Fast_edge_iterator() = delete;
  explicit Fast_edge_iterator(Lgraph *_g, bool _visit_sub) : top_g(_g), visit_sub(_visit_sub) {}

  Fast_iter begin() const;
  Fast_iter end() const { return Fast_iter(visit_sub); }
};

class Flow_base_iterator {
protected:
  bool                          linear_first_phase;
  bool                          linear_last_phase;
  Node                          current_node;
  Fast_edge_iterator::Fast_iter global_it;

  // State built during iteration
  const bool                              visit_sub;
  absl::flat_hash_set<Node::Compact>      unvisited;
  std::vector<Node>                       pending_stack;
  absl::flat_hash_map<Node::Compact, int> pending_loop_detect;

  Flow_base_iterator(Lgraph *lg, bool _visit_sub);
  Flow_base_iterator(bool _visit_sub);

public:
  const Node &operator*() const { return current_node; }

  void add_node(const Node &node) {
    bool all_inputs_visited = true;
    for (const auto &e : node.inp_edges()) {
      if (unvisited.contains(e.driver.get_node().get_compact())) {
        all_inputs_visited = false;
        break;
      }
    }
    if (all_inputs_visited) {
      pending_stack.push_back(node);
    }
    unvisited.insert(node.get_compact());
  }
  void del_node(const Node::Compact &node) { unvisited.erase(node); }

  void del_node(const Node &node) { del_node(node.get_compact()); }
};

class Fwd_edge_iterator {
public:
  class Fwd_iter : public Flow_base_iterator {
  protected:
    void topo_add_chain_down(const Node_pin &dst_pin);
    void topo_add_chain_fwd(const Node_pin &driver_pin);

    void fwd_get_from_linear_first(Lgraph *top);
    void fwd_get_from_pending(Lgraph *top);
    void fwd_get_from_linear_last();

    void fwd_first(Lgraph *lg);
    void fwd_next();

  public:
    Fwd_iter(Lgraph *lg, bool _visit_sub) : Flow_base_iterator(lg, _visit_sub) { fwd_first(lg); }
    Fwd_iter(bool _visit_sub) : Flow_base_iterator(_visit_sub) { I(current_node.is_invalid()); }

    bool operator!=(const Fwd_iter &other) const {
      GI(!current_node.is_invalid() && !other.current_node.is_invalid(),
         current_node.get_top_lgraph() == other.current_node.get_top_lgraph());
      return current_node != other.current_node;
    }

    Fwd_iter &operator++() {
      I(!current_node.is_invalid());  // Do not call ++ after end
      fwd_next();
      return *this;
    }
  };

protected:
  Lgraph *   top_g;
  const bool visit_sub;

public:
  Fwd_edge_iterator() = delete;
  explicit Fwd_edge_iterator(Lgraph *_g, bool _visit_sub) : top_g(_g), visit_sub(_visit_sub) {}

  Fwd_iter begin() const {
    if (top_g->is_empty())
      return end();
    return Fwd_iter(top_g, visit_sub);
  }

  Fwd_iter end() const { return Fwd_iter(visit_sub); }
};

class Bwd_edge_iterator {
public:
  class Bwd_iter : public Flow_base_iterator {
  protected:
    void bwd_first(Lgraph *lg);
    void bwd_next();

  public:
    Bwd_iter(Lgraph *lg, bool _visit_sub) : Flow_base_iterator(lg, _visit_sub) { bwd_first(lg); }
    Bwd_iter(bool _visit_sub) : Flow_base_iterator(_visit_sub) { I(current_node.is_invalid()); }

    bool operator!=(const Bwd_iter &other) const {
      GI(!current_node.is_invalid() && !other.current_node.is_invalid(),
         current_node.get_top_lgraph() == other.current_node.get_top_lgraph());
      return current_node != other.current_node;
    }

    Bwd_iter &operator++() {
      I(!current_node.is_invalid());  // Do not call ++ after end
      bwd_next();
      return *this;
    }
  };

protected:
  Lgraph *   top_g;
  const bool visit_sub;

public:
  Bwd_edge_iterator() = delete;
  explicit Bwd_edge_iterator(Lgraph *_g, bool _visit_sub) : top_g(_g), visit_sub(_visit_sub) {}

  Bwd_iter begin() const { return Bwd_iter(top_g, visit_sub); }

  Bwd_iter end() const { return Bwd_iter(visit_sub); }
};
