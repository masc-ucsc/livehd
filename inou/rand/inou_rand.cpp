//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <random>
#include <string>
#include <time.h>

#include "bm.h"

#include "lgraph.hpp"
#include "inou_rand.hpp"

Inou_rand::Inou_rand() {
}

struct pin_pair_compare {
  bool operator()(const std::pair<Node_Pin, Node_Pin> &lhs, const std::pair<Node_Pin, Node_Pin> &rhs) const {
    if(lhs.first.get_nid() < rhs.first.get_nid())
      return true;

    if(lhs.first.get_nid() == rhs.first.get_nid() && lhs.first.get_pid() < rhs.first.get_pid())
      return true;

    if(lhs.first.get_nid() == rhs.first.get_nid() && lhs.first.get_pid() < rhs.first.get_pid() &&
       lhs.second.get_nid() < rhs.second.get_nid())
      return true;

    if(lhs.first.get_nid() == rhs.first.get_nid() && lhs.first.get_pid() < rhs.first.get_pid() &&
       lhs.second.get_nid() == rhs.second.get_nid() && lhs.second.get_pid() < rhs.second.get_pid())
      return true;

    return false;
  }
};

std::vector<LGraph *> Inou_rand::generate() {

  std::vector<LGraph *> lgs;

  LGraph *g=0;
  if (opack.graph_name.empty())
    g = new LGraph(opack.lgdb);
  else
    g = new LGraph(opack.lgdb, opack.graph_name, true); // clear graph

  std::mt19937 rnd;
  rnd.seed(opack.rand_seed);

  std::uniform_int_distribution<Index_ID> rnd_created(0, opack.rand_size - 1);
  std::uniform_int_distribution<Port_ID>  rnd_4(0, 4);
  std::uniform_int_distribution<uint16_t> rnd_bits1(1, 32);
  std::uniform_int_distribution<uint16_t> rnd_bits2(1, 512);
  std::uniform_int_distribution<uint8_t>  rnd_op(Sum_Op, SubGraph_Op - 1);
  std::uniform_int_distribution<uint32_t> rnd_u32op(0, (uint32_t)(U32ConstMax_Op - U32ConstMin_Op));
  std::uniform_int_distribution<uint8_t>  rnd_const(0, 100);

  std::vector<Index_ID> created;

  Index_ID max_nid = 0;
  for(int i = 0; i < opack.rand_size; i++) {
    Node     node = g->create_node();
    Index_ID nid  = node.get_nid();
    created.push_back(nid);
    if(nid > max_nid)
      max_nid = nid;

    if(rnd_const(rnd) < opack.rand_crate) {
      g->node_u32type_set(nid, static_cast<Node_Type_Op>(rnd_u32op(rnd)));
    } else {
      g->node_type_set(nid, static_cast<Node_Type_Op>(rnd_op(rnd)));
    }
  }

  bm::bvector<> used_port;

  std::set<std::pair<Node_Pin, Node_Pin>, struct pin_pair_compare> connections;

  int i       = 0;
  int timeout = 0;
  while(i < opack.rand_eratio * opack.rand_size) {
    uint16_t rbits = 1;
    switch(rnd_4(rnd)) {
    case 0:
      rbits = 1;
      break;
    case 1:
      rbits = 1 << rnd_4(rnd);
      break;
    case 2:
      rbits = rnd_bits2(rnd);
      break;
    default:
      rbits = rnd_bits1(rnd);
    }
    Index_ID  dst_nid  = created[rnd_created(rnd)];
    Node_Type dst_type = g->node_type_get(dst_nid);
    //if constant, we don't allow inputs to node
    if(dst_type.op > U32Const_Op && dst_type.op <= U32ConstMax_Op)
      continue;

    Port_ID dst_port = 0;
    if(used_port.get_bit(dst_nid)) {
      dst_port = rnd_4(rnd);
    }
    used_port.set_bit(dst_nid);

    Index_ID src_nid = created[rnd_created(rnd)];

    Node_Pin dst_pin(dst_nid, dst_port, true);
    Node_Pin src_pin(src_nid, rnd_4(rnd), false);

    //prevent adding same edge twice
    std::pair<Node_Pin, Node_Pin> conn(src_pin, dst_pin);
    if(connections.find(conn) == connections.end()) {
      g->add_edge(src_pin, dst_pin, rbits);
      connections.insert(conn);
      i++;
      timeout = 0;
    } else {
      timeout++;
    }

    if(timeout > 10000) {
      std::cout << "I was not able to add all the connections, giving up due to timeout." << std::endl;
      break;
    }
  }

  g->sync();

  lgs.push_back(g);

  return lgs;
}

void Inou_rand::generate(std::vector<const LGraph *> &out) {

  assert(0); // No method to randinly transform a graph, just to generate.

  out.clear();
}

Inou_rand::Inou_rand(const py::dict &dict) {
  opack.set(dict);
}

void Inou_rand::py_set(const py::dict &dict) {
  opack.set(dict);
}

void Inou_rand_options::set(const py::dict &dict) {
  for (auto item : dict) {
    const auto &key = item.first.cast<std::string>();

    try {
      if ( is_opt(key,"seed") ) {
        const auto &val = item.second.cast<int>();
        rand_seed = val;
      }else if ( is_opt(key,"size") ) {
        const auto &val = item.second.cast<int>();
        rand_size = val;
      }else if ( is_opt(key,"crate") ) {
        const auto &val = item.second.cast<int>();
        rand_crate = val;
      }else if ( is_opt(key,"eratio") ) {
        const auto &val = item.second.cast<double>();
        rand_eratio = val;
      }else{
        set_val(key,item.second);
      }
    } catch (const std::invalid_argument& ia) {
      fmt::print("ERROR: key {} has an invalid argument {}\n",key);
    }
  }
  console->info("inou_rand seed:{} size:{} crate:{} eratio:{} lgdb:{} graph_name:{}"
      ,rand_seed, rand_size, rand_crate, rand_eratio
      ,lgdb, graph_name);
}
