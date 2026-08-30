// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// TABLE-DRIVEN attribute carrying for graph->graph reconstruction.
//
// Every transform that builds a new node from an old one has to move that
// node's attributes across. Doing it by hand means an if-chain per attribute,
// and there are four such chains today (pass/partition/flatten.cpp,
// pass/partition/pass_partition.cpp, graph/inline_sub.cpp,
// graph/occurrence_materialize.cpp). They have already drifted from each other
// -- flatten drops `memory_async_reset`, inline_sub keeps it -- and a new
// attribute is silently dropped by whichever chain nobody remembered to update:
// the failure is a zero-width pin or a lost async reset a long way downstream,
// not a compile error. Those chains also carry POLICY (name prefixing, a
// deliberate color drop) the plain copy below does not, which is why they have
// not been folded onto it yet.
//
// So this header carries from the ONE tag list, LIVEHD_FOR_EACH_ATTR_TAG in
// graph/attrs.hpp -- the same list graph/cell.cpp registers from. A tag added
// there is registered and carried; a tag not on it is neither, and there is no
// count to keep in step by hand.
//
// ROLE. `livehd::attrs::attr_kind<Tag>` classifies each tag as node /
// driver_pin / edge / sink / any_pin. Node-kind tags ride the NODE, everything
// else rides a PIN, and the two carriers below are that partition, decided at
// compile time. Stamping a pin attr on the wrong role silently aliases the other
// role's slot (the per-pin key folds the driver/sink bit), which is exactly what
// the graph_util setters assert against -- so the split is not cosmetic.
//
// HIER_COLOR is node-kind but HIERARCHY-KEYED: it is stored per OCCURRENCE, so
// reading or writing it through a plain Node_class asserts (`AttrRef: hier
// attribute requires hierarchy context`) on a graph with no hierarchy. It is
// therefore NOT part of the per-def node carry -- pass/partition/flatten.cpp
// skips it for the same reason. An occurrence-aware caller carries it
// explicitly, per occurrence.
//
// SRCID is deliberately NOT carried by the generic copy. A source id is an
// index into the owning library's source map; see carry_srcid below.

#include <cstdint>
#include <string>
#include <type_traits>

#include "attrs.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/attrs/srcid.hpp"
#include "hhds/graph.hpp"

namespace livehd::graph_util {

// Copy ONE attribute, if present. `value_type` construction covers the string
// case (the store hands back a view, set() wants an owning string) without a
// per-tag special case.
template <class Tag, class Src, class Dst>
inline void carry_attr(const Src& from, const Dst& to) {
  if (auto a = from.attr(Tag{}); a.has()) {
    to.attr(Tag{}).set(typename Tag::value_type{a.get()});
  }
}

namespace attr_carry_detail {

template <class Tag>
inline constexpr bool is_node_carried
    = livehd::attrs::attr_kind<Tag> == livehd::attrs::Attr_kind::node && !std::is_same_v<Tag, livehd::attrs::hier_color_t>;
template <class Tag>
inline constexpr bool is_pin_carried = livehd::attrs::attr_kind<Tag> != livehd::attrs::Attr_kind::node;

#define LIVEHD_ATTR_CARRY_COUNT_NODE(tag) +(is_node_carried<livehd::attrs::tag##_t> ? 1 : 0)
#define LIVEHD_ATTR_CARRY_COUNT_PIN(tag)  +(is_pin_carried<livehd::attrs::tag##_t> ? 1 : 0)
inline constexpr std::size_t node_tag_count = 0 LIVEHD_FOR_EACH_ATTR_TAG(LIVEHD_ATTR_CARRY_COUNT_NODE);
inline constexpr std::size_t pin_tag_count  = 0 LIVEHD_FOR_EACH_ATTR_TAG(LIVEHD_ATTR_CARRY_COUNT_PIN);
#undef LIVEHD_ATTR_CARRY_COUNT_NODE
#undef LIVEHD_ATTR_CARRY_COUNT_PIN

}  // namespace attr_carry_detail

// How many attributes each carrier moves (test/diagnostic use): every
// node-kind tag except hier_color, plus hhds::attrs::name and the NODE overload
// of `match`; every other tag.
inline constexpr std::size_t kNodeAttrTagCount = attr_carry_detail::node_tag_count + 2;
inline constexpr std::size_t kPinAttrTagCount  = attr_carry_detail::pin_tag_count;

// Every node-kind attribute (hier_color excepted, see above), plus the name,
// plus `match`: a DUAL-ROLE tag whose attr_kind entry classifies its pin
// overload, while semdiff stamps it on nodes too (set_match(node)) and
// `lhd tool` reads it per node -- so it is carried on both roles.
template <class Src, class Dst>
inline void carry_node_attrs(const Src& from, const Dst& to) {
  carry_attr<hhds::attrs::name_t>(from, to);
  carry_attr<livehd::attrs::match_t>(from, to);
#define LIVEHD_ATTR_CARRY_NODE(tag)                                           \
  if constexpr (attr_carry_detail::is_node_carried<livehd::attrs::tag##_t>) { \
    carry_attr<livehd::attrs::tag##_t>(from, to);                             \
  }
  LIVEHD_FOR_EACH_ATTR_TAG(LIVEHD_ATTR_CARRY_NODE)
#undef LIVEHD_ATTR_CARRY_NODE
}

// Every pin attribute. Call with matching ROLES (driver->driver, sink->sink):
// the per-pin key folds the driver/sink bit, so a crossed call aliases slots.
template <class Src, class Dst>
inline void carry_pin_attrs(const Src& from, const Dst& to) {
#define LIVEHD_ATTR_CARRY_PIN(tag)                                           \
  if constexpr (attr_carry_detail::is_pin_carried<livehd::attrs::tag##_t>) { \
    carry_attr<livehd::attrs::tag##_t>(from, to);                            \
  }
  LIVEHD_FOR_EACH_ATTR_TAG(LIVEHD_ATTR_CARRY_PIN)
#undef LIVEHD_ATTR_CARRY_PIN
}

// Source provenance. A source id is minted by a Source_locator; `to` must
// resolve it through ITS graph's locator chain, so the id is re-imported into
// `dst_lib`'s map from `src_graph`'s locator -- which, within one library,
// re-mints an unsaved per-graph id into the shared base map (hhds allows that
// only single-threaded, which is how every rebuild here runs). Both null:
// the caller vouches that the id is already resolvable from `to` (same graph,
// or a saved library), so it is copied verbatim.
inline void carry_srcid(const hhds::Node_class& from, const hhds::Node_class& to, hhds::Graph* src_graph = nullptr,
                        hhds::GraphLibrary* dst_lib = nullptr) {
  auto a = from.attr(hhds::attrs::srcid);
  if (!a.has() || a.get() == 0) {
    return;
  }
  if (dst_lib == nullptr || src_graph == nullptr) {
    to.attr(hhds::attrs::srcid).set(a.get());
    return;
  }
  to.attr(hhds::attrs::srcid).set(dst_lib->source_map().import_from(src_graph->source_locator(), a.get()));
}

}  // namespace livehd::graph_util
