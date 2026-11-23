// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// TABLE-DRIVEN attribute carrying for graph->graph reconstruction.
//
// Graph reconstruction carries operation annotations from the registry.
// Callers select whether identity and analysis metadata survive a rewrite.
//
// This header carries from the ONE tag list, LIVEHD_FOR_EACH_ATTR_TAG in
// graph/attrs.hpp -- the same list graph/cell.cpp registers from. A tag added
// there is registered and considered for carry; physical route tags explicitly
// invalidate on rebuild through carry_on_rebuild. There is no
// count to keep in step by hand.
//
// ROLE. `livehd::attrs::attr_kind<Tag>` classifies each tag as node /
// driver_pin / edge / any_pin. Node-kind tags ride the NODE, everything
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
#include "shared_text.hpp"

namespace livehd::graph_util {

// Copy ONE attribute, if present. `value_type` construction covers the string
// case (the store hands back a view, set() wants an owning string) without a
// per-tag special case; a PRESENCE-ONLY (hhds::flag) tag has no value to move,
// so carrying it is just re-setting the bit.
template <class Tag, class Src, class Dst>
inline void carry_attr(const Src& from, const Dst& to) {
  if (auto a = from.attr(Tag{}); a.has()) {
    if constexpr (hhds::attr_is_flag<Tag>()) {
      to.attr(Tag{}).set();
    } else {
      to.attr(Tag{}).set(typename Tag::value_type{a.get()});
    }
  }
}

namespace attr_carry_detail {

template <class Tag>
inline constexpr bool is_node_carried
    = livehd::attrs::carry_on_rebuild<Tag> && livehd::attrs::attr_kind<Tag> == livehd::attrs::Attr_kind::node
      && !std::is_same_v<Tag, livehd::attrs::hier_color_t>;
template <class Tag>
inline constexpr bool is_pin_carried
    = livehd::attrs::carry_on_rebuild<Tag> && livehd::attrs::attr_kind<Tag> != livehd::attrs::Attr_kind::node;

#define LIVEHD_ATTR_CARRY_COUNT_NODE(tag) +(is_node_carried<livehd::attrs::tag##_t> ? 1 : 0)
#define LIVEHD_ATTR_CARRY_COUNT_PIN(tag)  +(is_pin_carried<livehd::attrs::tag##_t> ? 1 : 0)
inline constexpr std::size_t node_tag_count = 0 LIVEHD_FOR_EACH_ATTR_TAG(LIVEHD_ATTR_CARRY_COUNT_NODE);
inline constexpr std::size_t pin_tag_count  = 0 LIVEHD_FOR_EACH_ATTR_TAG(LIVEHD_ATTR_CARRY_COUNT_PIN);
#undef LIVEHD_ATTR_CARRY_COUNT_NODE
#undef LIVEHD_ATTR_CARRY_COUNT_PIN

}  // namespace attr_carry_detail

// How many attributes each carrier moves (test/diagnostic use): every
// node-kind tag except hier_color, plus hhds::attrs::name, the NODE overload
// of `match` and the two shared texts (color group, placement block: carried
// through their text, see graph/shared_text.hpp); pin tags eligible for carry.
// Physical route tags are excluded.
inline constexpr std::size_t kNodeAttrTagCount = attr_carry_detail::node_tag_count + 4;
inline constexpr std::size_t kPinAttrTagCount  = attr_carry_detail::pin_tag_count;

struct Carry_policy {
  bool names            = true;
  bool colors           = true;
  bool placement        = true;
  bool analysis         = true;
  bool synth_policy     = true;
  bool aggregate_origin = true;
  bool matches          = true;

  template <class Tag>
  [[nodiscard]] bool allows() const {
    using Group = livehd::attrs::Rebuild_group;
    switch (livehd::attrs::Attr_traits<Tag>::rebuild) {
      case Group::always          : return true;
      case Group::names           : return names;
      case Group::colors          : return colors;
      case Group::placement       : return placement;
      case Group::analysis        : return analysis;
      case Group::synth_policy    : return synth_policy;
      case Group::aggregate_origin: return aggregate_origin;
      case Group::matches         : return matches;
      case Group::skip_raw        : return false;
    }
  }
};

// Every node-kind attribute (hier_color excepted, see above), plus the name,
// plus `match`: a DUAL-ROLE tag whose attr_kind entry classifies its pin
// overload, while semdiff stamps it on nodes too (set_match(node)) and
// `lhd tool` reads it per node -- so it is carried on both roles.
template <class Src, class Dst>
inline void carry_node_attrs(const Src& from, const Dst& to, const Carry_policy& policy = {}) {
  if (policy.names) {
    carry_attr<hhds::attrs::name_t>(from, to);
  }
  if (policy.matches) {
    carry_attr<livehd::attrs::match_t>(from, to);
  }
  if (policy.colors) {
    Color_group::copy(from, to);
  }
  if (policy.placement) {
    Placement_block::copy(from, to);
  }
#define LIVEHD_ATTR_CARRY_NODE(tag)                                           \
  if constexpr (attr_carry_detail::is_node_carried<livehd::attrs::tag##_t>) { \
    if (policy.allows<livehd::attrs::tag##_t>()) {                            \
      carry_attr<livehd::attrs::tag##_t>(from, to);                           \
    }                                                                         \
  }
  LIVEHD_FOR_EACH_ATTR_TAG(LIVEHD_ATTR_CARRY_NODE)
#undef LIVEHD_ATTR_CARRY_NODE
}

// Every pin attribute eligible for carry. Call with matching ROLES (driver->driver, sink->sink):
// the per-pin key folds the driver/sink bit, so a crossed call aliases slots.
template <class Src, class Dst>
inline void carry_pin_attrs(const Src& from, const Dst& to, const Carry_policy& policy = {}) {
#define LIVEHD_ATTR_CARRY_PIN(tag)                                                                                             \
  if constexpr (attr_carry_detail::is_pin_carried<livehd::attrs::tag##_t>) {                                                   \
    using Tag = livehd::attrs::tag##_t;                                                                                        \
    if (policy.allows<Tag>() && (livehd::attrs::attr_kind<Tag> != livehd::attrs::Attr_kind::driver_pin || from.is_driver())) { \
      carry_attr<Tag>(from, to);                                                                                               \
    }                                                                                                                          \
  }
  LIVEHD_FOR_EACH_ATTR_TAG(LIVEHD_ATTR_CARRY_PIN)
#undef LIVEHD_ATTR_CARRY_PIN
}

// Source ids are local to a locator chain. Import into the destination
// library's shared map for region bodies (one line-offset table per file), or
// into the destination graph's locator when the graph owns the copied spans.
inline void carry_srcid(const hhds::Node_class& from, const hhds::Node_class& to, const hhds::Source_locator& source,
                        hhds::Source_locator& destination) {
  if (auto id = from.attr(hhds::attrs::srcid); id.has() && id.get() != 0) {
    to.attr(hhds::attrs::srcid).set(destination.import_from(source, id.get()));
  }
}

inline void carry_srcid(const hhds::Node_class& from, const hhds::Node_class& to, hhds::Graph* src_graph = nullptr,
                        hhds::GraphLibrary* dst_lib = nullptr) {
  if (src_graph && dst_lib) {
    carry_srcid(from, to, src_graph->source_locator(), dst_lib->source_map());
  } else if (auto id = from.attr(hhds::attrs::srcid); id.has() && id.get() != 0) {
    // A same-graph or saved-library copy already resolves this id.
    to.attr(hhds::attrs::srcid).set(id.get());
  }
}

}  // namespace livehd::graph_util
