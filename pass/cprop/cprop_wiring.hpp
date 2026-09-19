// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "node_util.hpp"

namespace livehd {
// Persistent interval index for explicit wiring. A write copies only the paths
// to its two endpoints; shared versions retain their old roots. Coordinates
// are graph mask positions (nonnegative int32), not inferred value widths.
// The persistent treap gives expected O(log L + K) work per update/read,
// where L is the interval count and K the returned lane count. No N-lane layout is copied for
// each of N shared Set_mask versions.
class Cprop_wiring {
  using Pin = hhds::Pin_class;
  struct Source {
    Pin                     pin;
    std::shared_ptr<Source> replacement;
    bool                    retired = false;
  };
  using Ref = std::shared_ptr<Source>;
  struct Tree;
  using Root = std::shared_ptr<const Tree>;
  struct Tree {
    Ref      source;
    int64_t  lo, hi, delta;
    uint64_t priority;
    Root     left, right;
    int64_t  shift = 0;
  };
  static constexpr int64_t                     end          = int64_t{1} << 31;
  uint64_t                                     random_state = 0x243f6a8885a308d3ULL;
  // One unit per extra emitted interval. A read always gets its first piece;
  // expansions beyond that share a pass-wide edge budget, preventing many
  // shared wide reads from manufacturing quadratic operand traffic.
  size_t                                       extra_pieces = 0;
  absl::flat_hash_map<hhds::Class_index, Ref>  sources;
  absl::flat_hash_map<hhds::Class_index, Root> layouts;

  Ref source(Pin pin) {
    auto [it, fresh] = sources.try_emplace(pin.get_class_index());
    if (fresh) {
      it->second = std::make_shared<Source>(Source{pin, {}, false});
    }
    return it->second;
  }
  static Pin resolve(Ref ref) {
    std::vector<Ref> path;
    while (ref->replacement) {
      path.push_back(ref);
      ref = ref->replacement;
    }
    for (auto& item : path) {
      item->replacement = ref;
    }
    return ref->retired ? Pin{} : ref->pin;
  }
  uint64_t priority() {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return random_state;
  }
  Root interval(Ref ref, int64_t lo, int64_t hi, int64_t delta) {
    return std::make_shared<Tree>(Tree{std::move(ref), lo, hi, delta, priority(), {}, {}, 0});
  }
  Root        uniform(Pin pin, int64_t delta = 0) { return interval(source(pin), 0, end, delta); }
  static Root shifted(const Root& root, int64_t shift) {
    if (!root || shift == 0) {
      return root;
    }
    auto copy    = std::make_shared<Tree>(*root);
    copy->lo    += shift;
    copy->hi    += shift;
    copy->delta -= shift;
    copy->shift += shift;
    return copy;
  }
  static std::shared_ptr<Tree> pushed(const Root& root) {
    auto copy = std::make_shared<Tree>(*root);
    if (copy->shift) {
      copy->left  = shifted(copy->left, copy->shift);
      copy->right = shifted(copy->right, copy->shift);
      copy->shift = 0;
    }
    return copy;
  }
  static Root merge(const Root& a, const Root& b) {
    if (!a) {
      return b;
    }
    if (!b) {
      return a;
    }
    if (a->priority >= b->priority) {
      auto root   = pushed(a);
      root->right = merge(root->right, b);
      return root;
    }
    auto root  = pushed(b);
    root->left = merge(a, root->left);
    return root;
  }
  std::pair<Root, Root> split(const Root& tree, int64_t position) {
    if (!tree) {
      return {};
    }
    auto root = pushed(tree);
    if (position <= root->lo) {
      auto [a, b] = split(root->left, position);
      root->left  = b;
      return {a, root};
    }
    if (position >= root->hi) {
      auto [a, b] = split(root->right, position);
      root->right = a;
      return {root, b};
    }
    auto left  = interval(root->source, root->lo, position, root->delta);
    auto right = interval(root->source, position, root->hi, root->delta);
    return {merge(root->left, left), merge(right, root->right)};
  }
  Root select(const Root& root, int64_t lo, int64_t hi, int64_t destination) {
    auto [before, tail]  = split(root, lo);
    auto [inside, after] = split(tail, hi);
    (void)before;
    (void)after;
    return shifted(inside, destination - lo);
  }
  Root write(const Root& root, int64_t lo, int64_t hi, Root value) {
    auto [before, tail]  = split(root, lo);
    auto [inside, after] = split(tail, hi);
    (void)inside;
    return merge(merge(before, value), after);
  }
  Root known(Pin pin) {
    auto it = layouts.find(pin.get_class_index());
    return it == layouts.end() ? uniform(pin) : it->second;
  }
  Root wiring(const hhds::Node_class& node) {
    namespace gu   = graph_util;
    const auto op  = gu::type_op_of(node);
    auto       pin = node.get_driver_pin(0);
    if (op != Ntype_op::Concat && op != Ntype_op::Get_mask && op != Ntype_op::SHL && op != Ntype_op::SRA) {
      return uniform(pin);
    }
    auto zero = [&] { return uniform(gu::create_const(*node.get_graph(), *Dlop::create_integer(0))); };
    if (op == Ntype_op::Concat) {
      auto       root  = zero();
      const auto lanes = gu::concat_lanes(node);
      if (lanes.empty()) {
        return uniform(pin);
      }
      for (const auto& lane : lanes) {
        const int64_t hi = int64_t{lane.offset} + lane.width;
        if (lane.offset < 0 || hi > end || hi <= lane.offset) {
          return uniform(pin);
        }
        root = write(root, lane.offset, hi, select(known(lane.value), 0, lane.width, lane.offset));
      }
      return root;
    }
    const auto input = gu::get_driver_of_sink_name(node, "a");
    if (input.is_invalid()) {
      return uniform(pin);
    }
    if (op == Ntype_op::Get_mask) {
      const auto mask   = gu::get_driver_of_sink_name(node, "mask");
      const auto window = mask.is_const() ? gu::mask_window_of(gu::const_of(mask)) : std::nullopt;
      if (!window || window->first < 0 || window->second <= window->first) {
        return uniform(pin);
      }
      const auto width = window->second - window->first;
      return write(zero(), 0, width, select(known(input), window->first, window->second, 0));
    }
    const auto amount = gu::get_driver_of_sink_name(node, "b");
    if (!amount.is_const() || !gu::const_of(amount).is_just_i64() || gu::const_of(amount).has_unknowns()) {
      return uniform(pin);
    }
    const auto shift = gu::const_of(amount).to_just_i64();
    if (shift < 0 || shift >= end) {
      return uniform(pin);
    }
    if (op == Ntype_op::SHL) {
      return write(zero(), shift, end, select(known(input), 0, end - shift, shift));
    }
    return write(uniform(pin), 0, end - shift, select(known(input), shift, end, 0));
  }

public:
  using Reference = Ref;
  bool       has_layout(Pin pin) const { return layouts.contains(pin.get_class_index()); }
  Reference  reference(Pin pin) { return source(pin); }
  static Pin resolve_reference(Reference ref) { return resolve(std::move(ref)); }
  struct Piece {
    Pin     source;
    int64_t source_lo;
    int64_t lo, hi;
  };
  void set_edge_budget(size_t edges) { extra_pieces = edges; }
  void invalidate(Pin pin) { layouts.erase(pin.get_class_index()); }
  void fresh(Pin pin) {
    sources.erase(pin.get_class_index());
    layouts.erase(pin.get_class_index());
  }
  void retire(Pin pin) {
    if (auto it = sources.find(pin.get_class_index()); it != sources.end()) {
      it->second->retired = true;
    }
    layouts.erase(pin.get_class_index());
  }
  void forward(Pin from, Pin to) {
    if (from == to) {
      return;
    }
    auto it = sources.find(from.get_class_index());
    if (it != sources.end()) {
      auto       original = it->second;  // source(to) may rehash the flat map
      auto       target   = source(to);
      const auto resolved = resolve(target);
      if (resolved == from) {
        original->retired = true;
        return;
      }
      original->replacement = std::move(target);
    }
  }
  // Ordinary forward visits have a cached base already. HHDS's cycle tail
  // can emit a packed read before its writer: bootstrap only the base spine,
  // whose explicit layout is independent of lane-value computation. Each
  // version is cached once, shared by every later read in this invocation.
  void remember(Pin pin) {
    namespace gu = graph_util;
    if (pin.is_invalid() || pin.is_const() || layouts.contains(pin.get_class_index())) {
      return;
    }
    std::vector<Pin>                       chain;
    absl::flat_hash_set<hhds::Class_index> seen;
    auto                                   cur = pin;
    Root                                   root;
    for (;;) {
      if (auto it = layouts.find(cur.get_class_index()); it != layouts.end()) {
        root = it->second;
        break;
      }
      auto node = cur.get_master_node();
      if (cur.is_const() || gu::type_op_of(node) != Ntype_op::Set_mask) {
        const auto op = gu::type_op_of(node);
        const bool explicit_wiring
            = op == Ntype_op::Get_mask || op == Ntype_op::Concat || op == Ntype_op::SHL || op == Ntype_op::SRA;
        root = explicit_wiring && !cur.is_const() ? wiring(node) : uniform(cur);
        break;
      }
      if (!seen.insert(cur.get_class_index()).second) {
        root = uniform(cur);
        break;
      }
      auto base = gu::get_driver_of_sink_name(node, "a");
      if (base.is_invalid()) {
        root = uniform(cur);
        break;
      }
      chain.push_back(cur);
      cur = base;
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
      auto       node   = it->get_master_node();
      const auto mask   = gu::get_driver_of_sink_name(node, "mask");
      const auto value  = gu::get_driver_of_sink_name(node, "value");
      const auto window = mask.is_const() ? gu::mask_window_of(gu::const_of(mask)) : std::nullopt;
      if (!value.is_invalid() && window && window->first >= 0 && window->second > window->first) {
        root = write(root, window->first, window->second, select(known(value), 0, window->second - window->first, window->first));
      } else {
        root = uniform(*it);
      }
      layouts.insert_or_assign(it->get_class_index(), root);
    }
    layouts.try_emplace(pin.get_class_index(), root);
  }
  // Build the intervals with exactly one possibly-nonzero Or operand once.
  // Unknown support covers the entire coordinate space. Overlaps remain an
  // opaque Or source; their readers cannot speculate that a lane is disjoint.
  struct Support {
    Pin pin;
    int lo, hi;
  };
  void remember_or(Pin pin, const std::vector<Support>& inputs) {
    if (layouts.contains(pin.get_class_index())) {
      return;
    }
    struct Event {
      int64_t position;
      size_t  index;
      bool    enter;
    };
    std::vector<Event> events;
    for (size_t i = 0; i < inputs.size(); ++i) {
      const auto lo = inputs[i].lo < 0 ? 0 : inputs[i].lo;
      const auto hi = inputs[i].lo < 0 ? end : inputs[i].hi;
      if (hi <= lo) {
        continue;
      }
      events.push_back({lo, i, true});
      events.push_back({hi, i, false});
    }
    std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.position < b.position; });
    size_t     count = 0, identity = 0;
    int64_t    position = 0;
    Root       root;
    auto       zero   = graph_util::create_const(*pin.get_master_node().get_graph(), *Dlop::create_integer(0));
    const auto append = [&](int64_t limit) {
      if (limit <= position) {
        return;
      }
      auto value = count == 0 ? zero : count == 1 ? inputs[identity].pin : pin;
      auto piece = count == 1 ? select(known(value), position, limit, position) : interval(source(value), position, limit, 0);
      root       = merge(root, piece);
    };
    for (size_t i = 0; i < events.size();) {
      const auto next = events[i].position;
      append(next);
      position = next;
      while (i < events.size() && events[i].position == next) {
        const auto& event  = events[i++];
        identity          ^= event.index;
        if (event.enter) {
          ++count;
        } else {
          --count;
        }
      }
    }
    append(end);
    layouts.emplace(pin.get_class_index(), std::move(root));
  }
  std::optional<std::vector<Piece>> read(Pin pin, int64_t lo, int64_t hi) {
    if (lo < 0 || hi <= lo || hi > end) {
      return std::nullopt;
    }
    remember(pin);
    auto found = layouts.find(pin.get_class_index());
    if (found == layouts.end()) {
      return std::nullopt;
    }
    struct Visit {
      Root    root;
      int64_t shift;
      bool    emit;
    };
    std::vector<Visit> work{
        {found->second, 0, false}
    };
    std::vector<Piece> pieces;
    while (!work.empty()) {
      auto item = work.back();
      work.pop_back();
      if (!item.root) {
        continue;
      }
      const int64_t a = item.root->lo + item.shift, b = item.root->hi + item.shift;
      const int64_t child_shift = item.shift + item.root->shift;
      if (!item.emit) {
        if (b < hi) {
          work.push_back({item.root->right, child_shift, false});
        }
        if (b > lo && a < hi) {
          work.push_back({item.root, item.shift, true});
        }
        if (a > lo) {
          work.push_back({item.root->left, child_shift, false});
        }
        continue;
      }
      auto value = resolve(item.root->source);
      if (value.is_invalid()) {
        return std::nullopt;
      }
      const int64_t begin = std::max(a, lo), limit = std::min(b, hi);
      const int64_t offset = begin + item.root->delta - item.shift;
      if (!pieces.empty()) {
        if (extra_pieces == 0) {
          return std::nullopt;
        }
        --extra_pieces;
      }
      if (!pieces.empty() && pieces.back().source == value && pieces.back().hi == begin
          && pieces.back().source_lo + (pieces.back().hi - pieces.back().lo) == offset) {
        pieces.back().hi = limit;
      } else {
        pieces.push_back({value, offset, begin, limit});
      }
    }
    return pieces;
  }
};
}  // namespace livehd
