#ifndef INVARIANT_H_
#define INVARIANT_H_

#include "lgraph.hpp"
#include <boost/serialization/access.hpp>
#include <map>
#include <set>

namespace Live_synth {

//FIXME: can I reduce the dependency on string by using IDs from LGraph?

//typedef std::string Net_ID;
typedef std::pair<WireName_ID, uint32_t> Net_ID;
typedef std::string                      Graph_ID;
typedef std::string                      Instance_name;

typedef Index_ID Gate_ID;

typedef std::set<Net_ID>        Net_set;
typedef std::set<Instance_name> Instance_set;
typedef std::set<Graph_ID>      Graph_set;

typedef std::set<Gate_ID> Gate_set;
} // namespace Live_synth

using namespace Live_synth;
class Invariant_boundaries {
private:
  friend class boost::serialization::access;

  template <class Archive>
  void serialize(Archive &ar, const unsigned int version);

public:
  std::map<Net_ID, Net_set>         invariant_cones;      //sips
  std::map<Instance_name, Graph_ID> instance_type_map;    //all_instances
  std::map<Graph_ID, Instance_set>  instance_collection;  //instances
  std::map<Net_ID, Gate_set>        invariant_cone_cells; // gate_count
  std::map<Graph_ID, Graph_set>     hierarchy_tree;       //tree

  std::map<Gate_ID, uint32_t> gate_appearances; //shared_gates

  std::string top;

  Invariant_boundaries() {}

  static void                  serialize(Invariant_boundaries *ib, std::ostream &ofs);
  static Invariant_boundaries *deserialize(std::istream &ifs);

  static Graph_ID get_graphID(LGraph *g) {
    if(g->get_name().substr(0, 7) == "lgraph_")
      return g->get_name().substr(7);

    return g->get_name();
  }

  static LGraph *get_graph(Graph_ID id, std::string lgdb) {
    return LGraph::find_graph(id, lgdb);
  }

  bool is_invariant_boundary(Net_ID net) {
    if(net.first == 0)
      return false;

    return invariant_cones.find(net) != invariant_cones.end();
  }
};

#endif
