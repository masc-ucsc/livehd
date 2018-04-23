
#include "nodeplace.hpp"
#include "lgraph.hpp"

LGraph_Node_Place::LGraph_Node_Place(std::string path, std::string name) 
  : node_place(path + "/" + name + "_place") {
}

void LGraph_Node_Place::clear() { node_place.clear(); }

void LGraph_Node_Place::reload() { node_place.reload(); }

void LGraph_Node_Place::sync() { node_place.sync(); }

void LGraph_Node_Place::emplace_back() {
  node_place.emplace_back();
  node_place[node_place.size() - 1] = Node_Place();
}

void LGraph_Node_Place::node_place_emplace_back() { node_place.emplace_back(); }
