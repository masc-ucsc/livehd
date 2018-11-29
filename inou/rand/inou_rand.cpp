//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <random>
#include <string>
#include <time.h>

#include <set>

#include "bm.h"

#include "inou_rand.hpp"
#include "lgraph.hpp"

void setup_inou_rand() {
  Inou_rand p;
  p.setup();
}

void Inou_rand::setup() {
  Eprp_method m1("inou.rand", "generate a random lgraph", &Inou_rand::tolg);

  m1.add_label_optional("seed", "random seed");
  m1.add_label_optional("size", "lgraph size");
  m1.add_label_optional("eratio", "edge ratio for random");
  m1.add_label_required("name", "lgraph name");

  register_inou(m1);
}

Inou_rand::Inou_rand()
    : Pass("rand") {
}

void Inou_rand::tolg(Eprp_var &var) {
  Inou_rand p;

  p.opack.path = var.get("path");
  p.opack.name = var.get("name");

  if(var.has_label("seed"))
    p.opack.rand_seed = std::stoi(var.get("seed"));

  if(var.has_label("crate"))
    p.opack.rand_crate = std::stoi(var.get("crate"));

  if(var.has_label("eratio"))
    p.opack.rand_eratio = std::stod(var.get("eratio"));

  std::vector<LGraph *> lgs = p.do_tolg();

  if(lgs.empty()) {
    warn(fmt::format("inou.rand could not create a random {} lgraph in {} path", var.get("name"), var.get("path")));
  } else {
    assert(lgs.size() == 1); // rand only generated one graph at a time
    var.add(lgs[0]);
  }
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

std::vector<LGraph *> Inou_rand::do_tolg() {

  assert(!opack.name.empty());

  LGraph *g = LGraph::create(opack.path, opack.name, "inou_rand");

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
    // if constant, we don't allow inputs to node
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

    // prevent adding same edge twice
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

  std::vector<LGraph *> lgs;
  lgs.push_back(g);

  return lgs;
}
