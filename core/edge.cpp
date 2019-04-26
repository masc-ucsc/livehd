//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgraph.hpp"

#include "edge.hpp"

static_assert(sizeof(XEdge::Compact) == 16);
static_assert(sizeof(Node_pin::Compact) == 12);
static_assert(sizeof(Node_pin::Compact_class) == 4);
static_assert(sizeof(Node::Compact) == 12);
static_assert(sizeof(Node::Compact_class) == 4);

XEdge::XEdge(const Node_pin &src_, const Node_pin &dst_)
  : driver(src_)
  , sink(dst_) {

  I(sink.is_sink());
  I(driver.is_driver());

  I(driver.get_hid()        == sink.get_hid());
  I(driver.get_top_lgraph() == sink.get_top_lgraph());
}

void XEdge::del_edge() {

  I(driver.get_class_lgraph() == sink.get_class_lgraph());

  bool deleted = driver.get_class_lgraph()->del_edge(driver,sink);
  I(deleted);

}

void XEdge::add_edge() {
  driver.get_lgraph()->add_edge(driver,sink);
}

void XEdge::add_edge(uint16_t bits) {
  driver.get_lgraph()->add_edge(driver,sink,bits);
}
