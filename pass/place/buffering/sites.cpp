// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "sites.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <utility>

#include "checks.hpp"

namespace livehd::placement::buffering {
namespace {
bool     valid(Site_rect r) { return r.xl < r.xh && r.yl < r.yh; }
uint64_t checked_end(const Site_span& s) {
  require(!s.row.empty() && s.count && s.first <= std::numeric_limits<uint64_t>::max() - s.count, "invalid site span");
  return s.first + s.count;
}
bool available(const Legal_sites& sites, const Site_span& s) {
  const auto end = checked_end(s);
  const auto row = sites.find(s.row);
  if (row == sites.end()) {
    return false;
  }
  const auto& gaps = row->second.gaps;
  auto        it
      = std::upper_bound(gaps.begin(), gaps.end(), s.first, [](uint64_t first, const Site_span& gap) { return first < gap.first; });
  if (it == gaps.begin()) {
    return false;
  }
  --it;
  return s.first >= it->first && end <= it->first + it->count;
}
// Validates and orders one reservation proposal; overlapping footprints throw.
void normalize(std::vector<Site_span>& proposal) {
  for (const auto& s : proposal) {
    checked_end(s);
  }
  std::sort(proposal.begin(), proposal.end(), [](const auto& a, const auto& b) {
    return a.row == b.row ? a.first < b.first : a.row < b.row;
  });
  for (size_t i = 1; i < proposal.size(); ++i) {
    require(!spans_overlap(proposal[i - 1], proposal[i]), "overlapping footprints in one reservation");
  }
}
}  // namespace

bool spans_overlap(const Site_span& a, const Site_span& b) {
  return a.row == b.row && a.first < b.first + b.count && b.first < a.first + a.count;
}

Legal_sites build_legal_sites(const std::vector<Site_row>& rows, const std::vector<Site_rect>& exclusions, int64_t halo,
                              int64_t legalization_margin) {
  const auto limit = std::numeric_limits<int64_t>::max();
  require(halo >= 0 && legalization_margin >= 0 && halo <= limit - legalization_margin, "invalid placement halo/margin");
  const auto             grow = halo + legalization_margin;
  std::vector<Site_rect> masks;
  for (auto r : exclusions) {
    require(valid(r), "invalid site exclusion");
    require(r.xl >= std::numeric_limits<int64_t>::min() + grow && r.yl >= std::numeric_limits<int64_t>::min() + grow
                && r.xh <= limit - grow && r.yh <= limit - grow,
            "site halo coordinate overflow");
    masks.push_back({r.xl - grow, r.yl - grow, r.xh + grow, r.yh + grow});
  }
  std::sort(masks.begin(), masks.end(), [](const auto& a, const auto& b) { return a.yl < b.yl; });
  auto sorted = rows;
  std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
    if (a.body.yl != b.body.yl) {
      return a.body.yl < b.body.yl;
    }
    if (a.body.xl != b.body.xl) {
      return a.body.xl < b.body.xl;
    }
    return a.name < b.name;
  });
  Legal_sites result;
  // Existing active intervals are disjoint in x: otherwise two rows overlap.
  using Key = std::pair<int64_t, std::string>;
  std::map<Key, int64_t>         active;
  std::multimap<int64_t, Key>    endings;
  std::map<size_t, Site_rect>    active_masks;
  std::multimap<int64_t, size_t> mask_endings;
  size_t                         next_mask = 0;
  for (const auto& row : sorted) {
    const auto r = row.body;
    require(!row.name.empty() && valid(r) && row.pitch > 0, "invalid placement row");
    require(r.xl >= 0 || r.xh <= limit + r.xl, "placement row width overflow");
    const auto width = r.xh - r.xl;
    require(width % row.pitch == 0, "placement row is not site aligned");
    require(!result.contains(row.name), "duplicate placement row name");
    while (!endings.empty() && endings.begin()->first <= r.yl) {
      active.erase(endings.begin()->second);
      endings.erase(endings.begin());
    }
    const auto next = active.lower_bound({r.xl, {}});
    require(next == active.end() || next->first.first >= r.xh, "overlapping placement rows");
    require(next == active.begin() || std::prev(next)->second <= r.xl, "overlapping placement rows");
    const Key key{r.xl, row.name};
    active.emplace(key, r.xh);
    endings.emplace(r.yh, key);
    const auto                                 columns = uint64_t(width / row.pitch);
    std::vector<std::pair<uint64_t, uint64_t>> blocked;
    // Sweep physical-only inventories rather than scanning every fixed body
    // for every row. The final intersection check also handles unequal row
    // heights, where successive row tops need not be monotone.
    while (!mask_endings.empty() && mask_endings.begin()->first <= r.yl) {
      active_masks.erase(mask_endings.begin()->second);
      mask_endings.erase(mask_endings.begin());
    }
    while (next_mask < masks.size() && masks[next_mask].yl < r.yh) {
      const auto& mask = masks[next_mask];
      if (mask.yh > r.yl) {
        active_masks.emplace(next_mask, mask);
        mask_endings.emplace(mask.yh, next_mask);
      }
      ++next_mask;
    }
    for (const auto& [index, mask] : active_masks) {
      if (mask.yl >= r.yh || mask.yh <= r.yl || mask.xl >= r.xh || mask.xh <= r.xl) {
        continue;
      }
      const auto lo    = std::max(mask.xl, r.xl) - r.xl;
      const auto hi    = std::min(mask.xh, r.xh) - r.xl;
      const auto first = uint64_t(lo / row.pitch);
      const auto end   = uint64_t(hi / row.pitch + (hi % row.pitch != 0));
      blocked.emplace_back(first, end);
    }
    std::sort(blocked.begin(), blocked.end());
    Row_sites available{row, {}};
    uint64_t  cursor = 0;
    for (const auto& [first, end] : blocked) {
      if (first > cursor) {
        available.gaps.push_back({row.name, cursor, first - cursor});
      }
      cursor = std::max(cursor, end);
    }
    if (cursor < columns) {
      available.gaps.push_back({row.name, cursor, columns - cursor});
    }
    result.emplace(row.name, std::move(available));
  }
  return result;
}

Site_rect site_body(const Legal_sites& sites, const Site_span& span) {
  require(available(sites, span), "site footprint unavailable");
  const auto& row = sites.at(span.row).row;
  return {row.body.xl + int64_t(span.first) * row.pitch,
          row.body.yl,
          row.body.xl + int64_t(span.first + span.count) * row.pitch,
          row.body.yh};
}

uint64_t buffer_slots(const Legal_sites& sites, uint64_t width_in_sites) {
  require(width_in_sites > 0, "zero buffer site width");
  uint64_t count = 0;
  for (const auto& [name, row] : sites) {
    for (const auto& gap : row.gaps) {
      const auto slots = gap.count / width_in_sites;
      require(count <= std::numeric_limits<uint64_t>::max() - slots, "buffer slot count overflow");
      count += slots;
    }
  }
  return count;
}

Site_reservations::Site_reservations(Legal_sites legal_sites) : sites(std::move(legal_sites)) {
  // Verify the compressed inventory against its declared row boundaries.
  std::vector<Site_row> rows;
  for (const auto& [name, row] : sites) {
    require(name == row.row.name, "site row identity mismatch");
    rows.push_back(row.row);
  }
  const auto full = build_legal_sites(rows, {});
  for (const auto& [name, row] : sites) {
    uint64_t end = 0;
    for (const auto& gap : row.gaps) {
      require(gap.row == name && gap.first >= end && available(full, gap), "invalid compressed site inventory");
      end = checked_end(gap);
    }
  }
}

Reservation_result Site_reservations::conflict(const std::string& owner, const std::vector<Site_span>& sorted) const {
  for (const auto& s : sorted) {
    if (!available(sites, s)) {
      return {Reservation_status::unavailable, {}};
    }
    for (const auto& [other, spans] : owners) {
      if (other == owner) {
        continue;
      }
      for (const auto& used : spans) {
        if (spans_overlap(s, used)) {
          return {Reservation_status::conflict, other};
        }
      }
    }
  }
  return {};
}
Reservation_result Site_reservations::check(const std::string& owner, std::vector<Site_span> proposal) const {
  require(!owner.empty(), "empty reservation owner");
  normalize(proposal);
  return conflict(owner, proposal);
}
Reservation_result Site_reservations::replace(const std::string& owner, std::vector<Site_span> proposal) {
  require(!owner.empty(), "empty reservation owner");
  // Validate the complete proposal before inspecting/altering existing state.
  normalize(proposal);
  if (const auto result = conflict(owner, proposal); result.status != Reservation_status::reserved) {
    return result;
  }
  if (proposal.empty()) {
    owners.erase(owner);
  } else {
    owners.insert_or_assign(owner, std::move(proposal));
  }
  return {};
}
void Site_reservations::release(const std::string& owner) { owners.erase(owner); }

Legal_sites Site_reservations::available_sites(const std::string& replacing_owner) const {
  auto                                          free = sites;
  std::map<std::string, std::vector<Site_span>> used;
  for (const auto& [owner, spans] : owners) {
    if (owner == replacing_owner) {
      continue;
    }
    for (const auto& span : spans) {
      used[span.row].push_back(span);
    }
  }
  for (auto& [name, spans] : used) {
    std::sort(spans.begin(), spans.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    auto&                  row = free.at(name);
    std::vector<Site_span> gaps;
    size_t                 next = 0;
    for (const auto& gap : row.gaps) {
      const auto end    = gap.first + gap.count;
      auto       cursor = gap.first;
      while (next < spans.size() && spans[next].first < end) {
        const auto& span = spans[next++];
        if (span.first > cursor) {
          gaps.push_back({name, cursor, span.first - cursor});
        }
        cursor = span.first + span.count;
      }
      if (cursor < end) {
        gaps.push_back({name, cursor, end - cursor});
      }
    }
    row.gaps = std::move(gaps);
  }
  return free;
}
}  // namespace livehd::placement::buffering
