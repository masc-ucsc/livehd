#ifndef LGRAPH_H
#define LGRAPH_H

#include "graph_library.hpp"
#include "tech_library.hpp"

#include "lgedge.hpp"
#include "lgraphbase.hpp"

#include "nodeplace.hpp"
#include "nodetype.hpp"
#include "nodedelay.hpp"
#include "nodesrcloc.hpp"
#include "lgwirenames.hpp"
#include "instance_names.hpp"

class Node;
class ConstNode;
class Edge_iterator;

class  LGraph:public LGraph_Node_Type,     public LGraph_Node_Delay,
              public LGraph_Node_Src_Loc,  public LGraph_WireNames,
              public LGraph_InstanceNames, public LGraph_Node_Place{
protected:

  //FIXME: for live I need one instance per lgdb. Do it similar to library, or
  //keep references to lgraphs in the library
  static std::map<std::string, std::map<std::string,LGraph *> > name2graph;
  static uint32_t lgraph_counter;

  // singleton object, assumes all graph within a program are in the same
  // directory
  Graph_library* library;
  Tech_library*  tlibrary;

  int lgraph_id;

  Index_ID create_node_int() final;
public:
  LGraph(std::string path);
  LGraph(std::string path,  std::string name, bool clear);

  ~LGraph() {
    fmt::print("lgraph destructor\n");

  }

  int lg_id() const { return lgraph_id; }
  void clear()        override;
  void reload()       override;
  void sync()         override;
  void emplace_back() override;

  Index_ID add_graph_input(const  char *str, Index_ID nid=0, uint16_t bits = 0, uint16_t offset = 0);
  Index_ID add_graph_output(const char *str, Index_ID nid=0, uint16_t bits = 0, uint16_t offset = 0);

  Node            create_node();
  Node            get_node(Index_ID nid);
  const ConstNode get_node(Index_ID nid) const;

  ConstNode get_dest_node(const Edge &edge)  const;
  Node      get_dest_node(const Edge &edge);

  const Graph_library* get_library()  const { return library;  }

  const std::string get_subgraph_name(Index_ID nid) const {
    assert(node_type_get(nid).op == SubGraph_Op);
    return library->get_name(subgraph_id_get(nid));
  }

  const Tech_library*  get_tlibrary() const { return tlibrary; }
  Tech_library* get_tech_library() {return tlibrary;}

  std::string node_subgraph_name(Index_ID nid) const {
    assert(node_type_get(nid).op == SubGraph_Op);
    return get_library()->get_name(subgraph_id_get(nid));
  }

  static LGraph *find_graph(std::string, std::string path);

  static LGraph* open_lgraph(std::string path, std::string name) {
    char cadena[4096];
    snprintf(cadena,4096,"%s/lgraph_%s_nodes",path.c_str(),name.c_str());
    if(access(cadena, R_OK|W_OK) == -1) {
      return nullptr;
    }
    return new LGraph(path, name, false);
  }

  void dump_lgwires() {
    fmt::print("lgwires {} \n", name);
    LGraph_WireNames::dump(); }

  uint16_t get_offset(Index_ID nid) const {
    return LGraph_WireNames::get_offset(nid);
  }

  Forward_edge_iterator forward() const;
  Backward_edge_iterator backward() const;
};

// Clean interface/iterator for most operations. It must call graph
class ConstNode {
private:
  const LGraph *g;
protected:
  Index_ID nid;

public:
  ConstNode(const LGraph *_g, Index_ID _nid) {
    g   = _g;
    nid = _nid;
  };

  Index_ID get_nid() const { assert(nid); return nid; }

  Node_Type           type_get()  const { return g->node_type_get(nid);  }
  float               delay_get() const { return g->node_delay_get(nid); }
  Node_Place          place_get() const { return g->node_place_get(nid);}

  uint16_t get_bits() const { return g->get_bits(nid); }

  const Edge_iterator inp_edges() const;
  const Edge_iterator out_edges() const;

  bool is_root()         const { return g->is_root(nid);         }
  bool is_graph_input()  const { return g->is_graph_input(nid);  }
  bool is_graph_output() const { return g->is_graph_output(nid); }
};

class Node : public ConstNode {
private:
  LGraph *g;
protected:

public:
  Node(LGraph *_g, Index_ID _nid)
   :ConstNode(_g,_nid) {
    g   = _g;
  };

  void add_input(Node &src, uint16_t bits, Port_ID inp_pid=0, Port_ID out_pid=0) {
    Node_Pin src_pin(src.nid, out_pid, false);
    Node_Pin dst_pin(nid    , inp_pid, true);
    g->add_edge(src_pin, dst_pin, bits);
  };

  void set(const Node_Type_Op op) {
    g->node_type_set(nid, op);
  }

  void set_bits(uint16_t bits) {

    if(nid == 12450)
      fmt::print("foo");
    g->set_bits(nid, bits);
  }

  void delay_set(float t) {
    g->node_delay_set(nid, t);
  }

  Edge_iterator inp_edges() const;
  Edge_iterator out_edges() const;
};

#endif

