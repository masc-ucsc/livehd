// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "chunk_topology.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>
namespace livehd::placement {
namespace {
void check(bool ok, const std::string& why) {
  if (!ok) {
    throw std::invalid_argument("chunk topology: " + why);
  }
}
std::string name(const rapidjson::Value& v) {
  check(v.IsString() && v.GetStringLength() > 0, "expected a nonempty ID");
  return v.GetString();
}
}  // namespace
std::vector<Home_edit> read_home_edits(const metadata::Bundle* input, const std::string& design) {
  std::vector<Home_edit> out;
  bool                   seen = false;
  if (!input) {
    return out;
  }
  for (auto* r : input->select(chunk_guidance_class, design)) {
    if (r->HasMember("topology")) {
      auto& ops = (*r)["topology"];
      check(!seen && ops.IsArray(), "expected one ordered topology array");
      seen = true;
      for (const auto& op : ops.GetArray()) {
        check(op.IsObject() && int(op.HasMember("split")) + op.HasMember("fuse") + op.HasMember("transfer") == 1,
              "choose split, fuse or transfer");
        Home_edit e;
        e.kind = op.HasMember("split") ? Home_edit::split : op.HasMember("transfer") ? Home_edit::transfer : Home_edit::fuse;
        if (e.kind != Home_edit::fuse) {
          const bool split = e.kind == Home_edit::split;
          check(
              op.HasMember(split ? "new_id" : "to") && op.HasMember("members") && op["members"].IsArray() && !op["members"].Empty(),
              "select members and a destination");
          e.first  = name(op[split ? "split" : "transfer"]);
          e.second = name(op[split ? "new_id" : "to"]);
          for (const auto& m : op["members"].GetArray()) {
            check(m.IsObject() && m.HasMember("id") && m.HasMember("macro") && m["macro"].IsBool(), "a member needs id and macro");
            check(e.members.emplace(m["macro"].GetBool(), name(m["id"])).second, "duplicate selected member");
          }
        } else {
          auto& pair = op["fuse"];
          check(pair.IsArray() && pair.Size() == 2, "fuse needs two physical chunk IDs");
          e.first  = name(pair[0]);
          e.second = name(pair[1]);
        }
        if (op.HasMember("before")) {
          const auto& rows = op["before"];
          check(rows.IsArray() && !rows.Empty(), "before needs a physical frame");
          for (const auto& row : rows.GetArray()) {
            check(row.IsObject() && row.HasMember("id") && row.HasMember("box_dbu") && row["box_dbu"].IsArray()
                      && row["box_dbu"].Size() == 4,
                  "invalid before rectangle");
            Chunk_box box;
            for (unsigned k = 0; k < 4; ++k) {
              const auto& v = row["box_dbu"][k];
              check(v.IsNumber() && std::isfinite(v.GetDouble()), "invalid frame coordinate");
              box[k] = v.GetDouble();
            }
            check(e.before.emplace(name(row["id"]), box).second, "duplicate before rectangle");
          }
        }
        check(e.first != e.second, "two different physical chunk IDs are required");
        out.push_back(std::move(e));
      }
    }
  }
  return out;
}
Home_history repartition(std::vector<Fp_chunk>& chunks, const std::vector<Home_edit>& edits,
                         const std::vector<Chunk_object>& objects, Chunk_box core, std::array<double, 2> pitch) {
  Home_history history;
  if (edits.empty()) {
    return history;
  }
  std::map<std::vector<int>, std::pair<std::string, std::vector<int>>> original;
  for (const auto& c : chunks) {
    auto ids = c.objects;
    std::sort(ids.begin(), ids.end());
    original.emplace(std::move(ids), std::pair{c.partition, c.objects});
  }
  auto classify = [&](Fp_chunk& c) {
    auto ids = c.objects;
    std::sort(ids.begin(), ids.end());
    if (auto it = original.find(ids); it != original.end()) {
      c.partition = it->second.first;
      c.objects   = it->second.second;
      return;
    }
    const auto&              first = objects[ids.front()];
    std::vector<std::string> roles;
    for (int i : ids) {
      const auto& o = objects[i];
      if (first.instance.empty() || first.module.empty() || o.instance != first.instance || o.module != first.module) {
        c.partition = "@home:" + c.id;
        return;
      }
      roles.push_back(std::string(o.macro ? "m:" : "c:") + o.local);
    }
    // The same cut of the same module shares a logical identity across occurrences.
    std::sort(roles.begin(), roles.end());
    uint64_t h = 14695981039346656037ULL;
    for (const auto& role : roles) {
      h = (h ^ role.size()) * 1099511628211ULL;
      for (unsigned char byte : role) {
        h = (h ^ byte) * 1099511628211ULL;
      }
    }
    c.partition = "@cut:" + std::to_string(first.module.size()) + ":" + first.module + ":" + std::to_string(h);
  };
  for (const auto& e : edits) {
    auto index = [&](const std::string& name) {
      return std::find_if(chunks.begin(), chunks.end(), [&](const auto& c) { return c.id == name; });
    };
    auto a = index(e.first), b = index(e.second);
    check(a != chunks.end(), "unknown source " + e.first);
    if (&e == &edits.back() && !e.before.empty()) {
      check(e.before.size() == chunks.size(), "before frame must cover every current home");
      std::vector<std::tuple<double, bool, double, double>> events;
      double                                                area = 0;
      for (const auto& c : chunks) {
        check(e.before.contains(c.id), "missing before rectangle for " + c.id);
        auto box = e.before.at(c.id);
        for (int k = 0; k < 2; ++k) {
          check(box[k] >= core[k] && box[k + 2] <= core[k + 2] && box[k] < box[k + 2], "before rectangle outside block");
          for (int s : {k, k + 2}) {
            check(std::abs(std::remainder(box[s] - core[k], pitch[k])) < 1e-6, "before rectangle off grid");
          }
        }
        events.emplace_back(box[0], true, box[1], box[3]);
        events.emplace_back(box[2], false, box[1], box[3]);
        area += (box[2] - box[0]) * (box[3] - box[1]);
      }
      std::sort(events.begin(), events.end());
      std::map<double, double> active;
      for (auto [x, add, lo, hi] : events) {
        if (!add) {
          active.erase(lo);
          continue;
        }
        auto it = active.lower_bound(lo);
        check((it == active.end() || it->first >= hi) && (it == active.begin() || std::prev(it)->second <= lo),
              "overlapping before rectangles");
        active.emplace(lo, hi);
      }
      check(std::abs(area - (core[2] - core[0]) * (core[3] - core[1])) <= 1e-12 * area, "before frame leaves uncovered area");
    }
    history[a->partition].insert(a->id);
    if (e.kind != Home_edit::fuse) {
      if (e.kind == Home_edit::transfer) {
        check(b != chunks.end(), "unknown destination " + e.second);
      } else {
        check(b == chunks.end(), "new chunk ID already exists: " + e.second);
      }
      Fp_chunk child;
      child.id = e.second;
      std::vector<int> keep;
      for (int i : a->objects) {
        const auto& o = objects[i];
        (e.members.contains({o.macro, o.id}) ? child.objects : keep).push_back(i);
      }
      check(child.objects.size() == e.members.size(), "selected member is outside " + e.first);
      check(!keep.empty(), "edit would leave an empty chunk; fuse the homes instead");
      a->objects = std::move(keep);
      classify(*a);
      if (e.kind == Home_edit::split) {
        classify(child);
        chunks.push_back(std::move(child));
      } else {
        history[b->partition].insert(b->id);
        b->objects.insert(b->objects.end(), child.objects.begin(), child.objects.end());
        classify(*b);
      }
    } else {
      check(b != chunks.end(), "unknown fusion peer " + e.second);
      history[b->partition].insert(b->id);
      a->objects.insert(a->objects.end(), b->objects.begin(), b->objects.end());
      std::sort(a->objects.begin(), a->objects.end(), [&](int i, int j) {
        return std::tie(objects[i].macro, objects[i].id) < std::tie(objects[j].macro, objects[j].id);
      });
      classify(*a);
      chunks.erase(b);
    }
  }
  return history;
}
}  // namespace livehd::placement
