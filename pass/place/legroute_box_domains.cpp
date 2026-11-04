// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <functional>
#include <unordered_map>

#include "legroute_internal.hpp"

namespace livehd::placement::lr {
std::vector<int> box_domain_tiles(const Design& d, const Grid& g, int id, const Box_window& window, const Boxes& boxes,
                                  const std::unordered_map<int, std::vector<int>>& allowed, const std::set<int>& lifted,
                                  const std::vector<char>& pinned_tile, const std::vector<int>& T,
                                  const std::function<void()>& check) {
  check();
  const auto&      box   = boxes.boxes[size_t(id)];
  const auto       track = allowed.find(id);
  std::vector<int> result;
  for (int t : T) {
    if (g.col(t) >= window.c0 && g.col(t) <= window.c1 && g.row(t) >= window.r0 && g.row(t) <= window.r1 && !g.blocked[t]
        && (!box.fixed || t == box.tile) && (box.fixed || !pinned_tile[t]) && box_fits(d, g, box, t)
        && (track == allowed.end() || lifted.contains(id) || std::ranges::find(track->second, t) != track->second.end())) {
      result.push_back(t);
    }
  }
  return result;
}

Box_domains prepare_box_domains(const Design& d, const Legroute_options& o, const Grid& g, int region, const std::vector<int>& B,
                                const std::vector<int>& T, const Boxes& boxes,
                                const std::unordered_map<int, std::vector<int>>& allowed, const std::vector<char>& pinned_tile,
                                const std::function<void()>& check) {
  (void)region;  // T is the caller's exact ordered mesh tile list, as in the native model.
  Box_domains result;
  if (T.empty()) {
    return result;
  }
  int c0 = g.cols, c1 = -1, r0 = g.rows, r1 = -1;
  for (int t : T) {
    c0 = std::min(c0, g.col(t));
    c1 = std::max(c1, g.col(t));
    r0 = std::min(r0, g.row(t));
    r1 = std::max(r1, g.row(t));
  }
  auto& win = result.windows;
  win.resize(B.size());
  for (size_t i = 0; i < B.size(); ++i) {
    check();
    const auto& box = boxes.boxes[B[i]];
    const int   c = std::clamp(g.col_of(box.cx), c0, c1), rr = std::clamp(g.row_of(box.cy), r0, r1);
    if (box.fixed) {  // a pinned box: its tile
      win[i] = {g.col(box.tile), g.col(box.tile), g.row(box.tile), g.row(box.tile)};
    }
    const auto al = allowed.find(B[i]);
    if (!box.fixed && al != allowed.end()) {  // on an IO pre-route's track: its tiles
      win[i] = {g.cols, -1, g.rows, -1};
      for (int t : al->second) {
        win[i] = {std::min(win[i].c0, g.col(t)),
                  std::max(win[i].c1, g.col(t)),
                  std::min(win[i].r0, g.row(t)),
                  std::max(win[i].r1, g.row(t))};
      }
    }
    for (int w = box.window >= 0 ? box.window : int(o.window_tiles); !box.fixed && al == allowed.end(); ++w) {
      check();
      win[i]   = {std::max(c0, c - w), std::min(c1, c + w), std::max(r0, rr - w), std::min(r1, rr + w)};
      int fits = 0;
      for (int t : T) {
        fits += g.col(t) >= win[i].c0 && g.col(t) <= win[i].c1 && g.row(t) >= win[i].r0 && g.row(t) <= win[i].r1 && !g.blocked[t]
                && !pinned_tile[t] && box_fits(d, g, box, t);
      }
      if (fits >= 4 || (win[i].c0 == c0 && win[i].c1 == c1 && win[i].r0 == r0 && win[i].r1 == r1)) {
        break;
      }
    }
  }
  bool       repair_hall = false;
  const auto expand      = [&](size_t i) {
    check();
    if (boxes.boxes[B[i]].fixed) {
      return false;
    }
    bool       changed = allowed.contains(B[i]) && result.lifted.insert(B[i]).second;
    const auto before  = win[i];
    win[i]  = {std::max(c0, before.c0 - 1), std::min(c1, before.c1 + 1), std::max(r0, before.r0 - 1), std::min(r1, before.r1 + 1)};
    changed = changed || win[i].c0 != before.c0 || win[i].c1 != before.c1 || win[i].r0 != before.r0 || win[i].r1 != before.r1;
    ++result.relaxed_windows;
    return changed;
  };
  for (int round = 0;; ++round) {
    std::vector<std::vector<int>> pref(B.size());
    for (size_t i = 0; i < B.size(); ++i) {
      pref[i] = box_domain_tiles(d, g, B[i], win[i], boxes, allowed, result.lifted, pinned_tile, T, check);
    }
    // a maximum matching: the boxes left out grow
    std::unordered_map<int, int> owner;
    std::vector<char>            seen(g.size(), 0);
    std::vector<int>             seen_list;
    std::function<bool(int)>     augment = [&](int i) {
      check();
      for (int t : pref[i]) {
        if (seen[t]) {
          continue;
        }
        seen[t] = 1;
        seen_list.push_back(t);
        const auto it = owner.find(t);
        if (it == owner.end() || augment(it->second)) {
          owner[t] = i;
          return true;
        }
      }
      return false;
    };
    std::vector<size_t> left;
    for (size_t i = 0; i < B.size(); ++i) {
      if (!augment(int(i))) {
        left.push_back(i);
      }
      for (int t : seen_list) {
        seen[t] = 0;
      }
      seen_list.clear();
    }
    if (left.empty()) {
      result.matching.assign(B.size(), -1);
      for (const auto& [t, i] : owner) {
        result.matching[size_t(i)] = t;
      }
      break;
    }
    if (!repair_hall && round <= g.cols + g.rows) {
      // Preserve the original domain evolution whenever it makes progress.
      bool changed = false;
      for (size_t i : left) {
        changed = expand(i) || changed;
      }
      if (changed) {
        continue;
      }
    }
    repair_hall = true;
    // An unmatched box may already see the whole region while matched boxes
    // occupy every tile it can use. Grow the entire alternating deficient set,
    // not only that root. This releases a fitting tile without moving a fixed
    // box or widening unrelated domains. No-progress means incomplete fitting,
    // not a physical routability proof; the caller retains its recovery policy.
    std::vector<char>   reached(B.size());
    std::vector<size_t> pending = left;
    for (size_t i : left) {
      reached[i] = true;
    }
    for (size_t at = 0; at < pending.size(); ++at) {
      for (int t : pref[pending[at]]) {
        check();
        const auto found = owner.find(t);
        if (found != owner.end() && !reached[size_t(found->second)]) {
          reached[size_t(found->second)] = true;
          pending.push_back(size_t(found->second));
        }
      }
    }
    bool changed = false;
    for (size_t i = 0; i < B.size(); ++i) {
      if (reached[i]) {
        changed = expand(i) || changed;
      }
    }
    if (!changed) {
      break;  // finite monotone windows/track releases; deadline also bounds every attempt
    }
  }
  return result;
}
}  // namespace livehd::placement::lr
