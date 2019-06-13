//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <random>
#include <string>
#include <time.h>

#include <set>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

#include "inou_rand.hpp"
#include "lgraph.hpp"

void setup_inou_rand() {
  Inou_rand p;
  p.setup();
}

void Inou_rand::setup() {
  Eprp_method m1("inou.rand", "generate a random lgraph", &Inou_rand::tolg);

  m1.add_label_optional("seed",   "random seed");
  m1.add_label_optional("size",   "lgraph size");
  m1.add_label_optional("eratio", "edge ratio for random");
  m1.add_label_required("name",   "lgraph name");

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
    p.opack.rand_seed = std::stoi(std::string(var.get("seed")));

  if(var.has_label("crate"))
    p.opack.rand_crate = std::stoi(std::string(var.get("crate")));

  if(var.has_label("eratio"))
    p.opack.rand_eratio = std::stod(std::string(var.get("eratio")));

  std::vector<LGraph *> lgs = p.do_tolg();

  if(lgs.empty()) {
    warn(fmt::format("inou.rand could not create a random {} lgraph in {} path", var.get("name"), var.get("path")));
  } else {
    assert(lgs.size() == 1); // rand only generated one graph at a time
    var.add(lgs[0]);
  }
}

std::vector<LGraph *> Inou_rand::do_tolg() {

  assert(!opack.name.empty());

  LGraph *g = LGraph::create(opack.path, opack.name, "inou_rand");

  std::mt19937 rnd;
  rnd.seed(opack.rand_seed);

  std::uniform_int_distribution<uint64_t> rnd_created(0, opack.rand_size - 1);
  std::uniform_int_distribution<Port_ID>  rnd_4(0, 4);
  std::uniform_int_distribution<Port_ID>  rnd_port(0, (1<<Port_bits)-1);
  std::uniform_int_distribution<uint16_t> rnd_bits1(1, 32);
  std::uniform_int_distribution<uint16_t> rnd_bits2(1, 512);
  std::uniform_int_distribution<uint8_t>  rnd_op(Sum_Op, SubGraph_Op - 1);
  std::uniform_int_distribution<uint32_t> rnd_u32op(0, (uint32_t)(U32ConstMax_Op - U32ConstMin_Op));
  std::uniform_int_distribution<uint8_t>  rnd_const(0, 100);

  std::vector<Node> created;

  for(int i = 0; i < opack.rand_size; i++) {
    if(rnd_const(rnd) < opack.rand_crate) {
      created.emplace_back(g->create_node_const(rnd_u32op(rnd),rnd_bits1(rnd)));
    } else {
      created.emplace_back(g->create_node(static_cast<Node_Type_Op>(rnd_op(rnd))));
    }
  }

  absl::flat_hash_set<Node_pin::Compact> used_port;
  absl::flat_hash_set<XEdge::Compact>    connections;

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
    auto &sink_node = created[rnd_created(rnd)];
    auto dst_type = sink_node.get_type();
    // if constant, we don't allow inputs to sink_node
    if(dst_type.op > U32Const_Op && dst_type.op <= U32ConstMax_Op)
      continue;

    auto spin = sink_node.setup_sink_pin(rnd_port(rnd) % dst_type.get_num_inputs());
    if(used_port.count(spin.get_compact()))
      continue;

    used_port.insert(spin.get_compact());

    auto &driver_node = created[rnd_created(rnd)];
    auto dpin = driver_node.setup_driver_pin(rnd_port(rnd) % driver_node.get_type().get_num_outputs());

    XEdge e(dpin,spin);
    // prevent adding same edge twice
    if(!connections.count(e.get_compact())) {
      e.add_edge(rbits);
      connections.insert(e.get_compact());
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
