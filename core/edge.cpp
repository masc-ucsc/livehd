//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "edge.hpp"

#include "lgraph.hpp"
#include "node.hpp"
#include "node_pin.hpp"

static_assert(sizeof(XEdge::Compact) == 40);
static_assert(sizeof(Node_pin::Compact) == 20); // 16 hier + 4 compact
static_assert(sizeof(Node_pin::Compact_class) == 4);
static_assert(sizeof(Node::Compact) == 20);
static_assert(sizeof(Node::Compact_class) == 4);

XEdge::XEdge(Lgraph *g, const Compact &c)
    : driver(g, Node_pin::Compact_class(c.driver_idx, false)), sink(g, Node_pin::Compact_class(c.sink_idx, true)) {
  I(sink.is_sink());
  I(driver.is_driver());
}

XEdge::XEdge(const Node_pin &src_, const Node_pin &dst_) : driver(src_), sink(dst_) {
  I(sink.is_sink());
  I(driver.is_driver());

  I(driver.get_top_lgraph() == sink.get_top_lgraph());
}

void XEdge::del_edge() {
  I(driver.get_class_lgraph() == sink.get_class_lgraph());

  driver.get_class_lgraph()->del_edge(driver, sink);
}

void XEdge::del_edge(Node_pin &dpin, Node_pin &spin) {
  I(dpin.get_class_lgraph() == spin.get_class_lgraph());

  I(dpin.is_driver());
  I(spin.is_sink());

  dpin.get_class_lgraph()->del_edge(dpin, spin);
}

void XEdge::add_edge() { driver.get_class_lgraph()->add_edge(driver, sink); }

void XEdge::add_edge(uint32_t bits) { driver.get_class_lgraph()->add_edge(driver, sink, bits); }
