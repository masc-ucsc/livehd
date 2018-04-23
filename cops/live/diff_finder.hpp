#ifndef DIFF_FINDER_H_
#define DIFF_FINDER_H_

#include "lgraph.hpp"
#include "invariant.hpp"

class Diff_finder {

  private:
    LGraph*                original;
    LGraph*                synth;
    Invariant_boundaries*  boundaries;

    std::string            hier_sep;

    class Graph_Node {
      public:
      LGraph*      module;
      Index_ID    idx;
      uint32_t    bit;
      std::string instance;
      //we need to take into account PIDs for subgraphs
      //for primitives, it doesn't matter since most have a single output
      Port_ID     pid;

      Graph_Node(LGraph* module, Index_ID idx, uint32_t bit, std::string instance, Port_ID pid) :
        module(module), idx(idx), bit(bit), instance(instance), pid(pid) {
        }

      Graph_Node() {}

      bool operator< (const Graph_Node rhs) const {
        return (module < rhs.module) ||
          (module == rhs.module && idx < rhs.idx) ||
          (module == rhs.module && idx == rhs.idx && pid < rhs.pid) ||
          (module == rhs.module && idx == rhs.idx && pid == rhs.pid && bit < rhs.bit) ||
          (module == rhs.module && idx == rhs.idx && pid == rhs.pid && bit == rhs.bit && instance < rhs.instance);
      }
    };

    std::map<Graph_Node, std::set<Graph_Node>>  cones;
    std::map<Graph_Node, bool>                  different;
    std::map<Graph_Node, std::set<Graph_Node> > endpoints;
    std::set<Graph_Node>                        stack;
    std::map<Graph_Node, Net_ID>                synth_map;
    std::map<Graph_Node, std::string>           bound2net;

    bool is_user_def(LGraph* current, Index_ID idx, Port_ID pid) const;
    bool is_invariant(Graph_Node node);

    void find_fwd_boundaries(Graph_Node start_boundary, std::set<Graph_Node>& discovered);
    bool compare_cone(Graph_Node start_boundary, Graph_Node original_boundary, bool went_up = false);

    auto go_up(Graph_Node boundary);
    auto go_down(Graph_Node boundary, bool output);

    void generate_modules(std::set<Graph_Node> different_nodes, std::string out_lgdb);

    void add_ios_up(LGraph* module, Index_ID nid, std::map<std::string, LGraph*>& name2graph);

  public:
    //FIXME: can we remove the dependency on the synthesized graph?
    Diff_finder(LGraph* original, LGraph* synth, Invariant_boundaries* boundaries, std::string hier_sep = ".") :
      original(original), synth(synth), boundaries(boundaries), hier_sep(hier_sep) {
    }


    void generate_delta(std::string mod_lgdb, std::string out_lgdb, std::set<Net_ID>& diffs);
};

#endif
