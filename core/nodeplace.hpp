#ifndef NODEPLACE_H
#define NODEPLACE_H

#include <assert.h>

#include "dense.hpp"
#include "lgraphbase.hpp"

// Sheng: have to determine the integer size of posx and posy later
class Node_Place {

private:
  friend class LGraph_Node_Place;

protected:
  uint32_t posx; // Enough for 1nm resolution and 100mm**2 die
  uint32_t posy;

public:
  Node_Place(uint32_t x = 0, uint32_t y = 0) : posx(x), posy(y){};

  void replace(uint32_t x, uint32_t y) {
    posx = x;
    posy = y;
  }

  uint32_t x() { return posx; }

  uint32_t y() { return posy; }
};

class LGraph_Node_Place : virtual public LGraph_Base {
private:
  Dense<Node_Place> node_place;

protected:
  void node_place_emplace_back();

public:
  explicit LGraph_Node_Place(const std::string& path, const std::string& name) noexcept ;

  virtual void clear();
  virtual void reload();
  virtual void sync();
  virtual void emplace_back();

  void node_place_set(Index_ID nid, uint32_t x, uint32_t y) {
    assert(nid < node_place.size());
    assert(node_internal[nid].is_node_state());
    assert(node_internal[nid].is_root());
    node_place[nid].posx = x;
    node_place[nid].posy = y;
  };
  const Node_Place &node_place_get(Index_ID nid) const {
    assert(nid < node_place.size());
    assert(node_internal[nid].is_node_state());
    assert(node_internal[nid].is_root());
    return node_place[nid];
  };

  uint32_t get_x(Index_ID nid) const {
    assert(nid < node_place.size());
    assert(node_internal[nid].is_node_state());
    assert(node_internal[nid].is_root());
    return node_place[nid].posx;
  }

  uint32_t get_y(Index_ID nid) const {
    assert(nid < node_place.size());
    assert(node_internal[nid].is_node_state());
    assert(node_internal[nid].is_root());
    return node_place[nid].posy;
  }
};

#endif
