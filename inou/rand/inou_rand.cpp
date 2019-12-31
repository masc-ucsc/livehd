//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_rand.hpp"

#include <charconv>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "lbench.hpp"
#include "lgraph.hpp"
#include "lrand.hpp"

void setup_inou_rand() { Inou_rand::setup(); }

void Inou_rand::setup() {
  Eprp_method m1("inou.rand", "generate a random lgraph", &Inou_rand::tolg);

  m1.add_label_optional("size", "lgraph size");
  m1.add_label_optional("crate", "crate size for random");
  m1.add_label_optional("eratio", "edge ratio for random");
  m1.add_label_required("name", "lgraph name");

  register_inou("rand", m1);
}

Inou_rand::Inou_rand(const Eprp_var &var) : Pass("rand", var) {
  rand_size   = 8192;
  rand_crate  = 10;
  rand_eratio = 4;

  if (var.has_label("crate")) {
    auto sv = var.get("crate");
    std::from_chars(sv.data(), sv.data() + sv.size(), rand_crate);
  }

  if (var.has_label("eratio")) {
    auto sv = var.get("eratio");
    int  val;
    std::from_chars(sv.data(), sv.data() + sv.size(), val);
    rand_eratio = val;
  }

  name = var.get("name");
  I(!name.empty());
}

void Inou_rand::tolg(Eprp_var &var) {
  Inou_rand p(var);

  LGraph *g = p.do_tolg();

  var.add(g);
}

LGraph *Inou_rand::do_tolg() {
  assert(!name.empty());

  LGraph *g = LGraph::create(path, name, "inou_rand");

  Lrand_range<uint64_t> rnd_created(0, rand_size - 1);
  Lrand_range<Port_ID>  rnd_4(0, 4);
  Lrand_range<Port_ID>  rnd_port(0, (1 << Port_bits) - 1);

  Lrand_range<uint16_t> rnd_bits1(1, 32);
  Lrand_range<uint16_t> rnd_bits2(1, 512);
  Lrand_range<uint8_t>  rnd_op(Sum_Op, SubGraph_Op - 1);
  Lrand_range<uint32_t> rnd_u32op(0, (uint32_t)(U32ConstMax_Op - U32ConstMin_Op));
  Lrand_range<uint8_t>  rnd_const(0, 100);

  std::vector<Node> created;

  for (int i = 0; i < rand_size; i++) {
    if (rnd_const.any() < rand_crate) {
      created.emplace_back(g->create_node_const(rnd_u32op.any()));
    } else {
      created.emplace_back(g->create_node(static_cast<Node_Type_Op>(rnd_op.any())));
    }
  }

  absl::flat_hash_set<Node_pin::Compact> used_port;
  absl::flat_hash_set<XEdge::Compact>    connections;

  int i       = 0;
  int timeout = 0;
  while (i < rand_eratio * rand_size) {
    uint16_t rbits = 1;
    switch (rnd_4.any()) {
      case 0: rbits = 1; break;
      case 1: rbits = 1 << rnd_4.any(); break;
      case 2: rbits = rnd_bits2.any(); break;
      default: rbits = rnd_bits1.any();
    }
    auto &sink_node = created[rnd_created.any()];
    auto  dst_type  = sink_node.get_type();
    // if constant, we don't allow inputs to sink_node
    if (dst_type.op > U32Const_Op && dst_type.op <= U32ConstMax_Op) continue;

    auto spin = sink_node.setup_sink_pin(rnd_port.any() % dst_type.get_num_inputs());
    if (used_port.count(spin.get_compact())) continue;

    used_port.insert(spin.get_compact());

    auto &driver_node = created[rnd_created.any()];
    auto  dpin        = driver_node.setup_driver_pin(rnd_port.any() % driver_node.get_type().get_num_outputs());

    XEdge e(dpin, spin);
    // prevent adding same edge twice
    if (!connections.count(e.get_compact())) {
      e.add_edge(rbits);
      connections.insert(e.get_compact());
      i++;
      timeout = 0;
    } else {
      timeout++;
    }

    if (timeout > 10000) {
      std::cout << "I was not able to add all the connections, giving up due to timeout." << std::endl;
      break;
    }
  }

  g->sync();

  return g;
}
