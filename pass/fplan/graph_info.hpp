#pragma once

#include <string>

#include "i_resolve_header.hpp"
#include "lgraph_base_core.hpp"

// specific kind of graph used elsewhere
// Bi_adjacency_list: works
// Out_adjacency_list: doesn't return 0 when you ask for the weight of a null edge,
// and if a vertex object goes out of scope after being created, it can't be deleted from the graph without segfaulting.
// Atomic_out_adjacency_list: no method provided to remove vertices or edges, which I need.

using g_type = graph::Bi_adjacency_list;

using vertex_t  = g_type::Vert;
using edge_t    = g_type::Edge;
using set_t     = g_type::Vert_set;
using set_vec_t = std::vector<set_t>;

// this holds a graph and all the related map information.
template <typename GImp>
class Graph_info {
public:
  GImp                                 al;
  graph::Vert_map<GImp, std::string>   debug_names;  // can remove later, mostly for debugging
  graph::Vert_map<GImp, unsigned long> ids;          // TODO: might be useful to uniquely identify a node without debug_names
  graph::Vert_map<GImp, double>        areas;        // area of the node
  graph::Vert_map<GImp, Lg_type_id>    labels;       // what LGraph a node represents
  graph::Edge_map<GImp, unsigned int>  weights;      // number of wires in a connection between two nodes

  // We have to use the "template" keyword here because the compiler doesn't know how to compile Graph_info since al is not an
  // explicit type.  The type given at instantiation time could drastically change the behavior of how the class behaves, so we use
  // "template" to indicate that we're working with template member functions.

  // Note that vert maps and vert sets have no default constructor and need to be created using al.

  unsigned int unique_label_counter = 0;

  Graph_info()
      : al()
      , debug_names(al.template vert_map<std::string>())
      , ids(al.template vert_map<unsigned long>())
      , areas(al.template vert_map<double>())
      , labels(al.template vert_map<Lg_type_id>())
      , weights(al.template edge_map<unsigned int>()) {}

  Graph_info(Graph_info&& other) noexcept
      : al(std::move(other.al))
      , debug_names(std::move(other.debug_names))
      , ids(std::move(other.ids))
      , areas(std::move(other.areas))
      , labels(std::move(other.labels))
      , weights(std::move(other.weights)) {}

  // super expensive copy constructor
  Graph_info(const Graph_info& other)
      : al()
      , debug_names(al.template vert_map<std::string>())
      , ids(al.template vert_map<unsigned long>())
      , areas(al.template vert_map<double>())
      , labels(al.template vert_map<Lg_type_id>())
      , weights(al.template edge_map<unsigned int>()) {
    // make a new graph and fill out all the maps

    // vert map makes it way easier to map edges
    auto ov_v_map = other.al.template vert_map<vertex_t>();

    // assign content
    for (auto ov : other.al.verts()) {
      auto v         = al.insert_vert();
      debug_names[v] = other.debug_names(ov);
      ids[v]         = other.ids(ov);
      areas[v]       = other.ids(ov);
      labels[v]      = other.labels(ov);

      ov_v_map[ov] = v;
    }

    for (auto oe : other.al.edges()) {
      auto e     = al.insert_edge(ov_v_map[other.al.tail(oe)], ov_v_map[other.al.head(oe)]);
      weights[e] = other.weights(oe);
    }
  }

  // set will be modified interally by graph I think
  vertex_t collapse_to_vertex(set_t& set) {
    I(set.size() >= 1);

    auto nv = al.insert_vert();

    ids[nv]    = ++unique_id_counter;
    labels[nv] = ++unique_label_counter;

    std::string dname = std::string("cp_vert_");
    for (auto v : set) {
      dname.append(std::to_string(ids(v))).append("_");
    }

    debug_names[nv] = dname;

    for (auto v : set) {
      areas[nv] += areas(v);
    }

    auto marked   = al.template vert_map<bool>();

    for (auto v : set) {
      for (auto e : al.out_edges(v)) {
        al.insert_edge(nv, al.head(e));
      }

      for (auto e : al.in_edges(v)) {
        al.insert_edge(al.tail(e), nv);
      }

      marked[v] = true;
    }

    // we have to be extremely careful about how we delete verts here, because otherwise graph will segfault
    // trying to keep track of verts in the set that have already been deleted.
    
    set.clear();

    for (auto it = al.verts().begin(); it != al.verts().end(); it++) {
      if (marked(*it)) {
        al.erase_vert(*it);
        it = al.verts().begin();
      }
    }

    return nv;
  }

  vertex_t make_temp_vertex(const std::string& debug_name, double area) {
    auto nv = al.insert_vert();

    ids[nv]         = ++unique_id_counter;
    areas[nv]       = area;
    debug_names[nv] = debug_name;

    for (auto other_v : al.verts()) {
      edge_t temp_edge_t   = al.insert_edge(nv, other_v);
      weights[temp_edge_t] = 0;

      temp_edge_t          = al.insert_edge(other_v, nv);
      weights[temp_edge_t] = 0;
    }

    return nv;
  }

  vertex_t make_vertex(const std::string& debug_name, double area, const Lg_type_id label) {
    auto nv = al.insert_vert();

    ids[nv]         = ++unique_id_counter;
    areas[nv]       = area;
    debug_names[nv] = debug_name;
    labels[nv]      = label;

    return nv;
  }

  vertex_t make_bare_vertex(const std::string& debug_name, const double area) {
    auto nv = al.insert_vert();

    ids[nv]         = ++unique_id_counter;
    areas[nv]       = area;
    debug_names[nv] = debug_name;

    return nv;
  }

  edge_t find_edge(const vertex_t& v_src, const vertex_t& v_dst) const {
    for (auto e : al.out_edges(v_src)) {
      if (al.head(e) == v_dst) {
        return e;
      }
    }

    return al.null_edge();
  }

private:
  unsigned long unique_id_counter = 0;
};
