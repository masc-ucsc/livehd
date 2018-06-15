
#include "graph_library.hpp"
#include "lgraph.hpp"

LGraph *Graph_library::get_graph(int id) const {
  assert(id2name.size() > id);
  return LGraph::find_graph(id2name[id], path);
}
