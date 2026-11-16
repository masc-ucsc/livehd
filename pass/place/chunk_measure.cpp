// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "chunk_measure.hpp"

#include <algorithm>
#include <numeric>

namespace livehd::placement {
namespace {
double area(Chunk_box b) { return std::max(0., b[2] - b[0]) * std::max(0., b[3] - b[1]); }
double overlap(const Chunk_box& a, const Chunk_box& b) {
  return area({std::max(a[0], b[0]), std::max(a[1], b[1]), std::min(a[2], b[2]), std::min(a[3], b[3])});
}
Chunk_box bounds(const rapidjson::Value& r) {
  const auto& b = r["box_dbu"];
  return {b[0].GetDouble(), b[1].GetDouble(), b[2].GetDouble(), b[3].GetDouble()};
}
}  // namespace
Chunk_measure::Chunk_measure(const rapidjson::Value& plan) {
  std::map<std::string, int> ids_by_name;
  for (const auto& c : plan["chunks"].GetArray()) {
    int j = rows.size();
    rows.push_back({c["id"].GetString(), bounds(c), 0, 0, {}, area(bounds(c))});
    ids_by_name.emplace(rows.back().id, j);
    for (const auto& m : c["members"].GetArray()) {
      if (!m["macro"].GetBool()) {
        colors.emplace(m["id"].GetString(), Color{bounds(m), j});
      }
    }
  }
  if (plan.HasMember("geometry")) {
    for (const auto& e : plan["geometry"]["overlaps"].GetArray()) {
      auto b = bounds(e);
      for (auto* key : {"a", "b"}) {
        auto& r = rows.at(ids_by_name.at(e[key].GetString()));
        r.shared.push_back(b);
        r.capacity -= .5 * area(b);
      }
    }
  }
  std::vector<int> ids(rows.size());
  std::iota(ids.begin(), ids.end(), 0);
  auto split = [&](auto&& self, auto lo, auto hi) -> int {
    Chunk_box b = rows[*lo].box;
    for (auto it = lo + 1; it != hi; ++it) {
      for (int k = 0; k < 2; ++k) {
        b[k]     = std::min(b[k], rows[*it].box[k]);
        b[k + 2] = std::max(b[k + 2], rows[*it].box[k + 2]);
      }
    }
    int i = tree.size();
    tree.push_back({b});
    if (hi - lo == 1) {
      tree[i].chunk = *lo;
    } else {
      int  k   = b[2] - b[0] >= b[3] - b[1] ? 0 : 1;
      auto mid = lo + (hi - lo) / 2;
      std::nth_element(lo, mid, hi, [&](int a, int d) {
        return rows[a].box[k] + rows[a].box[k + 2] < rows[d].box[k] + rows[d].box[k + 2];
      });
      tree[i].a = self(self, lo, mid);
      tree[i].b = self(self, mid, hi);
    }
    return i;
  };
  if (!ids.empty()) {
    split(split, ids.begin(), ids.end());
  }
}
void Chunk_measure::cell(const Chunk_box& b, bool macro, std::string_view color) {
  auto visit = [&](auto&& self, int i) -> void {
    auto&        node = tree[i];
    const double cut  = overlap(b, node.box);
    if (cut <= 0) {
      return;
    }
    if (node.chunk >= 0) {
      auto&  r        = rows[node.chunk];
      double occupied = cut;
      for (const auto& strip : r.shared) {
        occupied -= .5 * overlap(b, strip);
      }
      (macro ? r.macro : r.area) += occupied;
    } else {
      self(self, node.a);
      self(self, node.b);
    }
  };
  if (!tree.empty()) {
    visit(visit, 0);
  }
  if (!macro) {
    if (auto it = colors.find(color); it != colors.end()) {
      auto& c    = it->second;
      c.area    += area(b);
      c.in_slot += overlap(c.slot, b);
      c.in_home += overlap(rows[c.home].box, b);
    }
  }
}
rapidjson::Document Chunk_measure::record(const std::string& design) const {
  rapidjson::Document d(rapidjson::kObjectType);
  auto&               a   = d.GetAllocator();
  auto                str = [&](const std::string& s) { return rapidjson::Value(s.data(), s.size(), a); };
  d.AddMember("class", "livehd.placement.chunk_results", a).AddMember("design", str(design), a).AddMember("id", "occupancy", a);
  rapidjson::Value chunks(rapidjson::kArrayType), members(rapidjson::kArrayType);
  for (const auto& c : rows) {
    rapidjson::Value v(rapidjson::kObjectType);
    v.AddMember("id", str(c.id), a).AddMember("cell_area_dbu2", c.area, a).AddMember("macro_area_dbu2", c.macro, a);
    if (c.capacity > c.macro) {
      v.AddMember("fill", c.area / (c.capacity - c.macro), a);
    }
    chunks.PushBack(v, a);
  }
  for (const auto& [id, c] : colors) {
    rapidjson::Value v(rapidjson::kObjectType);
    v.AddMember("id", str(id), a)
        .AddMember("outside_home_dbu2", c.area - c.in_home, a)
        .AddMember("slot_fill", c.in_slot / area(c.slot), a);
    members.PushBack(v, a);
  }
  d.AddMember("chunks", chunks, a).AddMember("colors", members, a);
  return d;
}
}  // namespace livehd::placement
