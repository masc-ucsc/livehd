// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Joint in-box routing probe for one exported cellbox (legroute final_box_dump), under a finite M2/M3 track menu.
//
// Input (JSON config): lefs, def (box_dump/post_f1.def, possibly with cell poses rewritten by the caller), box
// (box_c<col>_r<row>.json), menu {"M2":[y...],"M3":[x...]} (a missing layer keeps every DEF track), halo_tiles
// (maze bounds around the tile, default 1), net_ms (per net), out (result JSON).
//
// The occupancy is the DEF's (PG, every pin and obstruction) plus the exported L5 metal of other nets over the tile
// and its halo (checked/prescribed only, held fixed; fallback proposals are not metal). Each incident signal net is
// routed inside the scratch die: a net with every pin in the box is routed whole; a net that leaves the box taps
// each in-box pin to its own exported metal at the tile (its node riser, its checked segments), which also enters
// the occupancy as that net's metal. Accepted routes are committed one net at a time, so the answer is joint. Box
// M2/M3 tracks outside the menu are blocked inside the tile. The result is a witness or a bounded-search failure,
// never a proof of infeasibility.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

#include "inou/lefdef/physical.hpp"
#include "inou/lefdef/routing_connectivity.hpp"
#include "inou/lefdef/routing_geometry.hpp"
#include "inou/lefdef/routing_occupancy.hpp"
#include "inou/lefdef/tech_profile.hpp"
#include "pass/place/legroute_internal.hpp"
#include "pass/place/pin_escape_guard.hpp"
#include "pass/place/signal_router.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace ph = livehd::physical;
namespace pl = livehd::placement;
namespace lr = livehd::placement::lr;

namespace {
rapidjson::Document read_json(const std::string& path) {
  std::ifstream      in(path);
  std::ostringstream text;
  text << in.rdbuf();
  rapidjson::Document doc;
  doc.Parse(text.str().c_str());
  if (!in || doc.HasParseError()) {
    throw std::invalid_argument("cannot parse JSON " + path);
  }
  return doc;
}
// Track centerline coordinates of a layer inside [lo, hi) from the DEF TRACKS (axis Y: horizontal wires at y).
std::vector<int64_t> track_coords(const ph::Floorplan& fp, const std::string& layer, bool horizontal, int64_t lo, int64_t hi) {
  std::set<int64_t> out;
  for (const auto& t : fp.tracks) {
    if (t.layer != layer || (t.axis == "Y") != horizontal || t.step <= 0) {
      continue;
    }
    for (int64_t k = std::max<int64_t>(0, (lo - t.start + t.step - 1) / t.step); k < t.count; ++k) {
      const int64_t c = t.start + k * t.step;
      if (c >= hi) {
        break;
      }
      if (c >= lo) {
        out.insert(c);
      }
    }
  }
  return {out.begin(), out.end()};
}
// Edge spacing (DBU) a nominal parallel wire needs from a shape of `width` running `prl` alongside: the layer's plain
// SPACING values (no EOL/range qualifiers) and every parallel-run-length table, looked up at the wider width.
int64_t parallel_spacing(const ph::Layer& layer, double width_um, double prl_um, int64_t dbu) {
  double s = 0;
  for (const auto& m : layer.rules.spacing) {
    if (!m.eol && !m.range) {
      s = std::max(s, m.distance);
    }
  }
  for (const auto& t : layer.rules.parallel) {
    size_t wi = 0, li = 0;
    for (size_t i = 0; i < t.widths.size(); ++i) {
      if (t.widths[i] <= width_um + 1e-12) {
        wi = i;
      }
    }
    for (size_t j = 0; j < t.lengths.size(); ++j) {
      if (t.lengths[j] <= prl_um + 1e-12) {
        li = j;
      }
    }
    if (wi < t.spacing.size() && li < t.spacing[wi].size()) {
      s = std::max(s, t.spacing[wi][li]);
    }
  }
  return int64_t(std::ceil(s * double(dbu) - 1e-9));
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::invalid_argument("usage: legroute_box_probe config.json");
    }
    const auto config = read_json(argv[1]);
    const auto str    = [&](const char* key) -> std::string {
      if (!config.HasMember(key) || !config[key].IsString()) {
        throw std::invalid_argument(std::string("missing string: ") + key);
      }
      return config[key].GetString();
    };
    std::vector<std::string> lefs;
    for (const auto& lef : config["lefs"].GetArray()) {
      lefs.emplace_back(lef.GetString());
    }
    auto tech = ph::read_lef(lefs);
    auto fp   = ph::read_def(str("def"));
    tech.dbu  = fp.dbu;
    for (auto& c : fp.components) {
      const auto& master = tech.macros.at(c.macro);
      c.place.width      = std::llround(master.width * fp.dbu);
      c.place.height     = std::llround(master.height * fp.dbu);
      if (ph::swaps_axes(c.place.orientation)) {
        std::swap(c.place.width, c.place.height);
      }
    }
    for (auto& net : fp.nets) {  // signal wiring comes only from the export (and this probe's routes)
      if (net.use != "POWER" && net.use != "GROUND") {
        net.wires.clear();
      }
    }
    const auto               box  = read_json(str("box"));
    // scratch die: the tile itself by default, so no new wiring leaves the box
    const int                halo = config.HasMember("halo_tiles") ? config["halo_tiles"].GetInt() : 0;
    std::vector<std::string> signal_layers{"M2", "M3"};  // new wiring may use only these (config "layers")
    // planar layers (config "planar", default M2/M3): any other allowed layer costs upper_cost per unit length, so the
    // maze uses it for access stacks to higher anchors; its wire length is reported per net
    std::set<std::string>    planar{"M2", "M3"};
    if (config.HasMember("planar")) {
      planar.clear();
      for (const auto& l : config["planar"].GetArray()) {
        planar.insert(l.GetString());
      }
    }
    const double upper_cost = config.HasMember("upper_cost") ? config["upper_cost"].GetDouble() : 1000.;
    if (config.HasMember("layers")) {
      signal_layers.clear();
      for (const auto& l : config["layers"].GetArray()) {
        signal_layers.emplace_back(l.GetString());
      }
    }
    const auto net_ms       = config.HasMember("net_ms") ? config["net_ms"].GetUint() : 5000u;
    const auto patterns     = config.HasMember("patterns") ? config["patterns"].GetUint() : 8192u;     // per-search state budget
    const auto maze_labels  = config.HasMember("maze_labels") ? config["maze_labels"].GetUint() : 1u;  // live labels per coordinate
    // same-context retry: a request that ends "maze exhausted" is retried once with this many labels (0: off). A
    // failed request commits nothing, so the retry sees exactly the context the first attempt saw.
    const auto retry_labels = config.HasMember("retry_labels") ? config["retry_labels"].GetUint() : 0u;
    // strict witness mode: an off-menu or outside-box shape fails its net before commit (default: tallied only)
    const bool strict       = config.HasMember("strict") && config["strict"].GetBool();
    // fixed-context domain diagnostic: every job routes in the halo_tiles window; the job named focus_net is attempted
    // first there, then again with a focus_halo window and its anchors clipped to the tile, so the wider domain adds
    // search space but no easier outside target. Its context (earlier commits) is identical in both attempts.
    const std::string focus_net    = config.HasMember("focus_net") ? config["focus_net"].GetString() : "";
    // Alternate-cost experiment (external taps only: layer_cost does not reach the internal-net search path). A boundary
    // net that fails its taps under the configured costs is retried once from the identical committed prefix with flat
    // costs. With job_patterns > 0 every job shares ONE allowance (that many patterns and job_ms of wall time, charged
    // for every call, successful or not, no refunds) across both attempts.
    const bool        alt_flat     = config.HasMember("alt_flat") && config["alt_flat"].GetBool();
    const auto        job_patterns = config.HasMember("job_patterns") ? config["job_patterns"].GetUint64() : uint64_t(0);
    const auto        job_ms       = config.HasMember("job_ms") ? config["job_ms"].GetUint() : 0u;
    const int         focus_halo   = config.HasMember("focus_halo") ? config["focus_halo"].GetInt() : 1;
    const auto        box_ms       = config.HasMember("box_ms") ? config["box_ms"].GetUint() : 0u;  // whole-box deadline (0: none)
    const auto        box_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(box_ms ? box_ms : 1u << 30);
    // Conditional model (off by default): a net that leaves the box may tap its OWN fallback-proposed L5 metal at the
    // tile (the boundary the global route chose but L5 could not check), as if that track were made available. Foreign
    // fallback proposals are never obstacles either way.
    const bool        anchor_fallback = config.HasMember("anchor_fallback") && config["anchor_fallback"].GetBool();
    const auto&       rect            = box["rect_dbu"].GetArray();
    const int64_t     xl = rect[0].GetInt64(), yl = rect[1].GetInt64(), xh = rect[2].GetInt64(), yh = rect[3].GetInt64();
    const int64_t     tw = xh - xl, th = yh - yl;
    const int         col = box["col"].GetInt(), row = box["row"].GetInt();
    const auto        tile_x  = [&](int c) { return xl + int64_t(c - col) * tw; };
    const auto        tile_y  = [&](int r) { return yl + int64_t(r - row) * th; };
    const auto*       profile = ph::detect_tech_profile(tech);
    if (!profile) {
      throw std::invalid_argument("no technology profile for these LEFs");
    }
    const auto layer_width = [&](const std::string& layer) {
      const auto it = tech.layers.find(layer);
      return it == tech.layers.end() ? int64_t(0) : std::llround(it->second.width * fp.dbu);
    };
    // the occupancy: DEF (PG, pins, obstructions), then exported foreign metal and the incident nets' anchors
    auto                       occ = ph::routing_occupancy(tech, fp, std::max<int64_t>(1, tw));
    std::set<int>              incident;
    std::map<int, std::string> net_name;
    for (const auto& n : box["nets"].GetArray()) {
      incident.insert(n["net"].GetInt());
      net_name[n["net"].GetInt()] = n["name"].GetString();
    }
    std::map<std::string, std::vector<ph::Shape>> anchors;  // incident net -> its own exported metal at the tile
    size_t foreign = 0, fallback_skipped = 0, joins_unrealized = 0, foreign_joins = 0, foreign_join_unresolved = 0,
           foreign_joins_skipped = 0;
    std::vector<ph::Occupied_shape> add;
    // Exact planned geometry (Final's own materialization) when the dump has it; else the older track/tile-span records
    const bool exact  = box.HasMember("foreign_planned") && box["nets"].Size() > 0 && box["nets"][0].HasMember("planned_shapes");
    const auto usable = [&](const std::string& state, bool own) {
      return state == "checked" || state == "prescribed" || (own && anchor_fallback && (state == "fallback" || state == "none"));
    };
    const auto rect_of = [](const rapidjson::Value& v) {
      return ph::Rect{v[0].GetDouble(), v[1].GetDouble(), v[2].GetDouble(), v[3].GetDouble()};
    };
    if (exact) {
      for (const auto& f : box["foreign_planned"].GetArray()) {
        if (!usable(f["state"].GetString(), false)) {
          fallback_skipped += std::string(f["state"].GetString()) == "fallback";
          continue;
        }
        add.push_back({
            {f["layer"].GetString(), rect_of(f["rect"])},
            f["name"].GetString(),
            true
        });
        ++foreign;
      }
      for (const auto& n : box["nets"].GetArray()) {
        for (const auto& ps : n["planned_shapes"].GetArray()) {
          if (!usable(ps["state"].GetString(), true)) {
            continue;
          }
          ph::Shape sh{ps["layer"].GetString(), rect_of(ps["rect"])};
          anchors[n["name"].GetString()].push_back(sh);
          add.push_back({sh, n["name"].GetString(), true});
        }
        joins_unrealized += n["planned_joins"].Size();
      }
      // foreign joins: for each layer step of the stack, the union of every default LEF via defined for that pair (pads
      // and cut, as Final's router may pick any of them); a step with no such via fails the box closed
      if (box.HasMember("foreign_joins")) {
        const auto&                                   metals = tech.routing_layers;
        // joins follow their wires: a join counts only where the same net has a usable (checked/prescribed) foreign
        // wire under its point on one of its two layers; others belong to omitted fallback geometry
        std::map<std::string, std::vector<ph::Shape>> usable_foreign;
        for (const auto& f : box["foreign_planned"].GetArray()) {
          if (usable(f["state"].GetString(), false)) {
            usable_foreign[f["name"].GetString()].push_back({f["layer"].GetString(), rect_of(f["rect"])});
          }
        }
        for (const auto& j : box["foreign_joins"].GetArray()) {
          const double jx = j["x"].GetDouble(), jy = j["y"].GetDouble();
          const auto&  wires = usable_foreign[j["name"].GetString()];
          if (std::ranges::none_of(wires, [&](const ph::Shape& w) {
                return (w.layer == j["from"].GetString() || w.layer == j["to"].GetString()) && w.rect.xl <= jx && jx <= w.rect.xh
                       && w.rect.yl <= jy && jy <= w.rect.yh;
              })) {
            ++foreign_joins_skipped;
            continue;
          }
          const auto a = std::ranges::find(metals, std::string(j["from"].GetString())),
                     b = std::ranges::find(metals, std::string(j["to"].GetString()));
          if (a == metals.end() || b == metals.end()) {
            ++foreign_join_unresolved;
            continue;
          }
          const double x = j["x"].GetDouble(), y = j["y"].GetDouble();
          for (auto it = std::min(a, b); it < std::max(a, b); ++it) {
            bool found = false;
            for (const auto& [vname, via] : tech.vias) {
              if (!via.is_default) {
                continue;
              }
              const bool lower = std::ranges::any_of(via.shapes, [&](const ph::Shape& v) { return v.layer == *it; });
              const bool upper = std::ranges::any_of(via.shapes, [&](const ph::Shape& v) { return v.layer == *(it + 1); });
              if (!lower || !upper) {
                continue;
              }
              found = true;
              for (const auto& v : via.shapes) {
                add.push_back({
                    {v.layer,
                     ph::Rect{x + v.rect.xl * fp.dbu, y + v.rect.yl * fp.dbu, x + v.rect.xh * fp.dbu, y + v.rect.yh * fp.dbu}},
                    j["name"].GetString(),
                    true
                });
              }
            }
            foreign_join_unresolved += !found;
          }
          ++foreign_joins;
        }
      }
    }
    for (const auto& m : box["metal"].GetArray()) {  // approximate dumps: tracks over tile spans
      if (exact) {
        break;
      }
      const std::string state        = m["state"].GetString();
      const bool        own_fallback = anchor_fallback && state == "fallback" && m["incident"].GetInt() != 0;
      if (state != "checked" && state != "prescribed" && !own_fallback) {
        fallback_skipped += state == "fallback";
        continue;
      }
      const std::string layer = m["layer"].GetString();
      const bool        h     = m["horizontal"].GetInt() != 0;
      const int64_t     c = m["track_coord"].GetInt64(), half = layer_width(layer) / 2;
      const int         lo = m["tiles"][0].GetInt(), hi = m["tiles"][1].GetInt();
      ph::Shape         s{
          layer,
          h ? ph::Rect{double(tile_x(lo)),   double(c - half), double(tile_x(hi) + tw),        double(c + half)}
            : ph::Rect{  double(c - half), double(tile_y(lo)),        double(c + half), double(tile_y(hi) + th)}
      };
      const int         net  = m["net"].GetInt();
      const bool        own  = m["incident"].GetInt() != 0;
      const std::string name = own ? net_name.at(net) : "legroute_net_" + std::to_string(net);
      if (own) {  // the anchor is the part over this tile
        ph::Shape clip = s;
        clip.rect.xl   = std::max(clip.rect.xl, double(xl));
        clip.rect.xh   = std::min(clip.rect.xh, double(xh));
        clip.rect.yl   = std::max(clip.rect.yl, double(yl));
        clip.rect.yh   = std::min(clip.rect.yh, double(yh));
        if (clip.rect.xl < clip.rect.xh && clip.rect.yl < clip.rect.yh) {
          anchors[name].push_back(clip);
        }
      } else {
        ++foreign;
      }
      add.push_back({s, name, true});
    }
    for (const auto& n : box["nets"].GetArray()) {  // node risers over the tile (approximate dumps only)
      if (exact) {
        break;
      }
      for (const auto& node : n["nodes"].GetArray()) {
        const std::string layer = node["riser_layer"].GetString();
        const std::string state = node["riser_state"].GetString();
        if (layer.empty() || node["riser_coord"].GetInt64() < 0
            || (state != "checked" && state != "prescribed" && !(anchor_fallback && state == "fallback"))) {
          continue;
        }
        const int64_t c = node["riser_coord"].GetInt64(), half = layer_width(layer) / 2;
        ph::Shape     s{
            layer,
            ph::Rect{double(c - half), double(yl), double(c + half), double(yh)}
        };
        anchors[n["name"].GetString()].push_back(s);
        add.push_back({s, n["name"].GetString(), true});
      }
    }
    // the menu: box M2/M3 tracks not offered are blocked inside the tile
    std::map<std::string, std::set<int64_t>> menu;
    std::map<std::string, size_t>            blocked_tracks;
    for (const std::string layer : {"M2", "M3"}) {
      const bool h      = layer == "M2";
      const auto coords = track_coords(fp, layer, h, h ? yl : xl, h ? yh : xh);
      if (!config.HasMember("menu") || !config["menu"].HasMember(layer.c_str())) {
        menu[layer].insert(coords.begin(), coords.end());
        continue;
      }
      for (const auto& v : config["menu"][layer.c_str()].GetArray()) {
        menu[layer].insert(v.GetInt64());
      }
      const int64_t half = layer_width(layer) / 2;
      for (const int64_t c : coords) {
        if (!menu[layer].contains(c)) {
          ++blocked_tracks[layer];
          add.push_back({
              {layer,
               h ? ph::Rect{double(xl), double(c - half), double(xh), double(c + half)}
                 : ph::Rect{double(c - half), double(yl), double(c + half), double(yh)}},
              {},
              false
          });
        }
      }
    }
    occ.insert_many(add);
    // The guard and the rules see the whole physical die (cropping it would turn every outside pin into an
    // unresolved access reservation); only the router's search frame is bounded to the box (plus halo).
    const ph::Signal_metal_rules rules(tech, fp);
    pl::Pin_escape_guard         escapes(tech, fp, occ, 2048, {}, profile->route.access, &rules, true);
    auto                         search = fp;
    search.die           = {double(xl - halo * tw), double(yl - halo * th), double(xh + halo * tw), double(yh + halo * th)};
    auto policy          = pl::signal_route_profile(*profile, tech);
    policy.signal_layers = signal_layers;
    pl::Signal_router router(tech, search, occ, rules, policy, &escapes);
    auto              wide = fp;  // the focus window (same occupancy, rules and escape guard)
    wide.die
        = {double(xl - focus_halo * tw), double(yl - focus_halo * th), double(xh + focus_halo * tw), double(yh + focus_halo * th)};
    pl::Signal_router             wide_router(tech, wide, occ, rules, policy, &escapes);
    std::map<std::string, double> layer_cost;
    for (const auto& l : signal_layers) {
      if (!planar.contains(l)) {
        layer_cost[l] = upper_cost;
      }
    }
    // incident signal nets, most in-box pins first
    struct Job {
      std::string name;
      ph::Net     inside;
      bool        leaves = false;
    };
    std::vector<Job>         jobs;
    std::vector<std::string> unfulfilled;  // box obligations this probe does not route (clock attachments)
    for (const auto& n : box["nets"].GetArray()) {
      if (n["power"].GetInt()) {
        continue;
      }
      const bool clock = n["clock"].GetInt() != 0;
      if (clock) {
        // a clock attachment is diagnostic until its designated spine tap continuity is validated: route it when tap
        // metal exists, but keep the obligation listed as unfulfilled
        unfulfilled.emplace_back(n["name"].GetString());
        if (!(exact && !anchors[n["name"].GetString()].empty())) {
          continue;
        }
      }
      if (!clock && !n["routed"].GetInt()) {
        continue;
      }
      Job job;
      job.name        = n["name"].GetString();
      job.inside.name = job.name;
      for (const auto& p : n["pins"].GetArray()) {
        if (p["in_box"].GetInt()) {
          job.inside.connections.push_back({p["owner"].GetString(), p["pin"].GetString()});
        } else {
          job.leaves = true;
        }
      }
      if (!job.inside.connections.empty() && (job.leaves || job.inside.connections.size() >= 2)) {
        jobs.push_back(std::move(job));
      }
    }
    std::ranges::stable_sort(jobs,
                             [](const Job& a, const Job& b) { return a.inside.connections.size() > b.inside.connections.size(); });
    rapidjson::StringBuffer                    buffer;
    rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
    std::map<std::string, std::set<int64_t>>   used, owned_used;
    size_t                                     routed = 0, off_menu = 0, outside_box = 0;
    std::map<std::string, std::set<int64_t>>   excluded;              // layer -> track centres new metal excludes (rule-expanded)
    std::map<std::string, std::set<int64_t>>   excluded_incremental;  // ... beyond the net's owned metal on that track
    std::string                                focus_report;          // the focus net's fixed-context comparison (never committed)
    std::vector<ph::Route_path>                focus_paths;
    ph::Route_width_rules                      focus_rules;  // the witness's own width rules (decoding only)
    std::map<std::string, std::set<int64_t>>   focus_excluded;
    std::string                                first_failure;
    w.StartObject();
    w.Key("nets");
    w.StartArray();
    for (auto& job : jobs) {
      std::vector<ph::Route_path>        paths;
      std::string                        why;
      std::string                        retry;  // "", "recovered" or the retry's own failure reason
      auto&                              own          = anchors[job.name];
      // One pending transaction per net: its cut state (every accepted piece's vias, exact reuse allowed, distinct vias
      // spaced), its width rules (staged; restored if the net fails) and its paths. Nothing is shared until commit.
      const auto                         rules_before = fp.nondefault_rules;
      std::optional<lr::Final_cut_state> cut_state;
      cut_state.emplace(tech, fp);
      const auto stage_rules = [&](const ph::Route_width_rules& staged) -> std::string {
        for (const auto& [name, widths] : staged) {
          if (const auto it = fp.nondefault_rules.find(name); it != fp.nondefault_rules.end() && it->second != widths) {
            return "conflicting width rule " + name;
          }
        }
        for (auto* frame : {&fp, &search}) {
          frame->nondefault_rules.insert(staged.begin(), staged.end());
        }
        return {};
      };
      // the job ledger (job_patterns > 0): remaining work and one absolute deadline for every call of this net
      uint64_t   job_left        = job_patterns;
      uint64_t   job_access_left = 2048;  // access enumeration shared by the job's calls (with job_patterns > 0)
      const auto job_deadline
          = std::min(box_deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(job_ms ? job_ms : net_ms));
      const auto limits = [&](size_t labels = 0) {
        pl::Signal_route_limits l;
        l.access_candidates = job_patterns ? size_t(job_access_left) : 2048;
        l.patterns          = job_patterns ? size_t(job_left) : patterns;
        l.maze_labels       = labels ? labels : maze_labels;
        l.deadline = job_patterns ? job_deadline
                                  : std::min(box_deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(net_ms));
        return l;
      };
      const auto charge = [&](size_t work, size_t access = 0) {
        job_left        = work >= job_left ? 0 : job_left - work;
        job_access_left = access >= job_access_left ? 0 : job_access_left - access;
      };
      std::vector<ph::Shape> shapes;
      size_t                 job_off_menu = 0, job_outside = 0;
      const auto             on_own_track = [&](const ph::Shape& sh) {
        const auto h = [&](const std::string& layer) {
          const auto it = tech.layers.find(layer);
          return it != tech.layers.end() && it->second.direction == "HORIZONTAL";
        };
        const auto cc = [&](const ph::Shape& x) {
          return std::llround(h(x.layer) ? (x.rect.yl + x.rect.yh) / 2 : (x.rect.xl + x.rect.xh) / 2);
        };
        return std::ranges::any_of(own, [&](const ph::Shape& a) {
          return a.layer == sh.layer && cc(a) == cc(sh) && a.rect.xl <= sh.rect.xh && sh.rect.xl <= a.rect.xh
                 && a.rect.yl <= sh.rect.yh && sh.rect.yl <= a.rect.yh;
        });
      };
      // the non-mutating whole-attempt acceptance: menu/box gates (strict), then the pin-escape guard under the job
      // deadline; the physical commit happens once, after the attempts
      std::optional<pl::Pin_escape_trial> accepted;
      const auto                          accept = [&] {
        job_off_menu = job_outside = 0;
        accepted.reset();
        ph::Net whole{job.name, {}, {ph::Wire{"ROUTED", paths}}};
        shapes = ph::route_geometry(tech, fp, whole);
        // the menu and box gates come before any commit
        for (const auto& sh : shapes) {
          if (!tech.layers.contains(sh.layer)) {
            continue;
          }
          const bool hz  = tech.layers.at(sh.layer).direction == "HORIZONTAL";
          const auto c   = std::llround(hz ? (sh.rect.yl + sh.rect.yh) / 2 : (sh.rect.xl + sh.rect.xh) / 2);
          job_off_menu  += (sh.layer == "M2" || sh.layer == "M3") && !menu[sh.layer].contains(c) && !on_own_track(sh);
          job_outside   += sh.rect.xl < double(xl) || sh.rect.xh > double(xh) || sh.rect.yl < double(yl) || sh.rect.yh > double(yh);
        }
        if (strict && (job_off_menu || job_outside)) {
          why = std::format("strict: {} off-menu and {} outside-box shapes", job_off_menu, job_outside);
          return;
        }
        const auto deadline = job_patterns ? job_deadline : box_deadline;
        auto       esc      = escapes.check(shapes, job.name, [&] { return std::chrono::steady_clock::now() >= deadline; });
        if (!esc.legal) {
          why = "escape: " + esc.reason;
          return;
        }
        accepted = std::move(esc);
      };

      std::string alt_note;  // the alternate attempt's record
      if (!job.leaves) {
        pl::Signal_route_request request;
        request.limits         = limits();
        request.search_detours = true;
        request.try_hard       = true;  // relaxes only the coupling screen, never DRC or access
        request.layer_cost     = layer_cost;
        auto result            = router.try_route(job.inside, request);
        charge(result.patterns_examined, result.access_examined);
        if (result.candidates.empty() && retry_labels > 1 && result.reason.find("maze exhausted") != std::string::npos) {
          request.limits = limits(retry_labels);
          auto again     = router.try_route(job.inside, request);
          charge(again.patterns_examined, again.access_examined);
          retry = again.candidates.empty() ? (again.reason.empty() ? "no candidate" : again.reason) : "recovered";
          if (!again.candidates.empty()) {
            result = std::move(again);
          }
        }
        if (result.candidates.empty()) {
          why = result.reason.empty() ? "no candidate" : result.reason;
        } else {
          paths = result.candidates.front().paths;
          why   = stage_rules(result.candidates.front().nondefault_rules);
          if (why.empty()) {
            if (auto e = cut_state->add(paths); !e.empty()) {
              why = "cuts: " + e;
            }
          }
        }
      } else if (own.empty()) {
        why = "boundary: no exported metal of this net at the tile to tap";
      } else if (std::ranges::none_of(own, [&](const ph::Shape& a) {
                   return std::ranges::find(signal_layers, a.layer) != signal_layers.end() && a.rect.xl < search.die.xh
                          && a.rect.xh > search.die.xl && a.rect.yl < search.die.yh && a.rect.yh > search.die.yl;
                 })) {
        // preflight: no anchor of this net on an allowed layer overlaps the search window, so no maze goal exists
        why = "boundary: no anchor inside the search window";
      } else {
        std::vector<std::map<std::string, double>> policies{layer_cost};
        if (alt_flat) {
          policies.push_back({});  // flat: every allowed layer at unit cost
        }
        for (size_t attempt = 0; attempt < policies.size(); ++attempt) {
          if (attempt) {  // restart from the identical committed prefix: nothing pending survives the first attempt
            if (job_patterns && job_left == 0) {
              alt_note = "alternate skipped: job allowance spent";
              break;
            }
            alt_note = "alternate (flat) after: " + why;
            why.clear();
            paths.clear();
            fp.nondefault_rules     = rules_before;
            search.nondefault_rules = rules_before;
            cut_state.emplace(tech, fp);
          }
          const auto& cost = policies[attempt];
          auto        taps = own;
          for (const auto& c : job.inside.connections) {
            ph::Net one;
            one.name                     = job.name;
            one.connections              = {c};
            // every tap sees the net's pending vias (exact reuse allowed, new cuts spaced against them)
            const auto             prior = cut_state->vias();
            // the focus net's targets are clipped to the tile once, for both its narrow and wide attempts
            std::vector<ph::Shape> clipped;
            if (job.name == focus_net) {
              for (auto a : taps) {
                a.rect.xl = std::max(a.rect.xl, double(xl));
                a.rect.xh = std::min(a.rect.xh, double(xh));
                a.rect.yl = std::max(a.rect.yl, double(yl));
                a.rect.yh = std::min(a.rect.yh, double(yh));
                if (a.rect.xl < a.rect.xh && a.rect.yl < a.rect.yh) {
                  clipped.push_back(a);
                }
              }
            }
            const auto& targets = job.name == focus_net ? clipped : taps;
            auto        trial   = router.propose_tap(one, limits(), targets, cost, prior);
            charge(trial.patterns_examined, trial.access_examined);
            if (!trial.geometry && retry_labels > 1 && trial.reason == "maze exhausted") {
              auto again = router.propose_tap(one, limits(retry_labels), targets, cost, prior);
              charge(again.patterns_examined, again.access_examined);
              retry = again.geometry ? "recovered" : (again.reason.empty() ? "no witness" : again.reason);
              if (again.geometry) {
                trial = std::move(again);
              }
            }
            if (!trial.geometry && job.name == focus_net) {
              wide.nondefault_rules = fp.nondefault_rules;  // the job's current staged rules
              auto again            = wide_router.propose_tap(one, limits(), clipped, cost, prior);
              charge(again.patterns_examined, again.access_examined);
              focus_report = std::format("{}/{}: tile window: {}; focus_halo {} window (in-tile targets {}): {} (work {})",
                                         c.instance,
                                         c.pin,
                                         trial.reason,
                                         focus_halo,
                                         clipped.size(),
                                         again.geometry ? "found" : again.reason,
                                         again.patterns_examined);
              if (again.geometry) {
                focus_paths = again.geometry->paths;
                focus_rules = again.geometry->nondefault_rules;
                // non-mutating acceptance of pending + witness: rule conflicts, the cut union, then the pin-escape guard
                std::string verdict;
                for (const auto& [name, widths] : focus_rules) {
                  if (const auto it = fp.nondefault_rules.find(name); it != fp.nondefault_rules.end() && it->second != widths) {
                    verdict = "conflicting width rule " + name;
                  }
                }
                auto all = paths;
                all.insert(all.end(), focus_paths.begin(), focus_paths.end());
                if (verdict.empty()) {
                  lr::Final_cut_state check(tech, fp);
                  if (auto e = check.add(all); !e.empty()) {
                    verdict = "cuts: " + e;
                  }
                }
                if (verdict.empty()) {
                  auto decode = fp;
                  decode.nondefault_rules.insert(focus_rules.begin(), focus_rules.end());
                  ph::Net    whole{job.name, {}, {ph::Wire{"ROUTED", all}}};
                  const auto esc = escapes.check(ph::route_geometry(tech, decode, whole), job.name);
                  verdict        = esc.legal ? "accepted" : "escape: " + esc.reason;
                }
                // connectivity: the real cell pin to ONE target rectangle at a time (a synthetic fixed terminal holding
                // that rectangle only, so unrelated same-net islands are never made equivalent)
                if (verdict == "accepted") {
                  size_t reached = 0;
                  for (size_t ti = 0; ti < clipped.size(); ++ti) {
                    auto trial_fp = fp;
                    trial_fp.nondefault_rules.insert(focus_rules.begin(), focus_rules.end());
                    ph::Terminal target;
                    target.name   = "__probe_target__";
                    target.net    = job.name;
                    target.status = ph::Status::FIXED;
                    target.shapes = {clipped[ti]};
                    trial_fp.terminals.push_back(target);
                    ph::Net routed_net{
                        job.name,
                        {c, {"PIN", "__probe_target__"}},
                        {ph::Wire{"ROUTED", all}}
                    };
                    if (ph::check_route_connectivity(tech, trial_fp, routed_net, nullptr, &trial_fp.nondefault_rules).connected) {
                      ++reached;
                      focus_report += std::format("; pin connects to target {} {}[{},{},{},{}]",
                                                  ti,
                                                  clipped[ti].layer,
                                                  clipped[ti].rect.xl,
                                                  clipped[ti].rect.yl,
                                                  clipped[ti].rect.xh,
                                                  clipped[ti].rect.yh);
                    }
                  }
                  if (!reached) {
                    focus_report += "; connectivity: the pin reaches no single target rectangle";
                  }
                }
                focus_report += "; acceptance (cut union, escape guard): " + verdict;
              }
            }
            if (!trial.geometry) {
              why = std::format("tap {}/{}: {}", c.instance, c.pin, trial.reason.empty() ? "no witness" : trial.reason);
              break;
            }
            if (why = stage_rules(trial.geometry->nondefault_rules); !why.empty()) {
              break;
            }
            if (auto e = cut_state->add(trial.geometry->paths); !e.empty()) {
              why = std::format("tap {}/{}: cuts: {}", c.instance, c.pin, e);
              break;
            }
            ph::Net piece{job.name, {}, {ph::Wire{"ROUTED", trial.geometry->paths}}};
            for (const auto& s : ph::route_geometry(tech, fp, piece)) {
              taps.push_back(s);
            }
            paths.insert(paths.end(), trial.geometry->paths.begin(), trial.geometry->paths.end());
          }
          if (why.empty()) {
            accept();  // gates and escape guard per attempt, so a rejected attempt can still take the alternate policy
          }
          if (why.empty()) {
            if (attempt) {
              alt_note += "; recovered";
            }
            break;
          }
        }
      }
      if (why.empty() && !accepted) {  // an internal net's acceptance (external nets were accepted per attempt)
        accept();
      }
      if (why.empty()) {  // commit the metal: the next nets route around this one
        std::vector<ph::Occupied_shape> mine;
        for (const auto& sh : shapes) {
          mine.push_back({sh, job.name, true});
        }
        occ.insert_many(mine);
        // Every accepted net updates the consumed access choices; only a whole in-box net retires its pin protection
        // (a boundary net's outside pins are not shown connected here).
        escapes.commit(*accepted, !job.leaves);
      }
      if (!why.empty()) {  // the failed net leaves nothing behind
        fp.nondefault_rules     = rules_before;
        search.nondefault_rules = rules_before;
      } else {
        off_menu    += job_off_menu;
        outside_box += job_outside;
      }
      w.StartObject();
      w.Key("name");
      w.String(job.name.c_str());
      w.Key("in_box_pins");
      w.Uint(unsigned(job.inside.connections.size()));
      w.Key("leaves_box");
      w.Bool(job.leaves);
      w.Key("ok");
      w.Bool(why.empty());
      w.Key("reason");
      w.String(why.c_str());
      w.Key("retry");
      w.String(retry.c_str());
      w.Key("alternate");
      w.String(alt_note.c_str());
      w.Key("job_patterns_left");
      w.Uint64(job_left);
      std::map<std::string, std::set<int64_t>> mine;
      std::map<std::string, double>            upper_length;  // DBU of metal on non-planar layers (M1 pin metal included)
      if (why.empty()) {
        ++routed;
        // this net's anchors by (layer, coordinate): reused, not newly allocated, when the new metal overlaps one
        const auto horizontal_layer = [&](const std::string& layer) {
          const auto it = tech.layers.find(layer);
          return it != tech.layers.end() && it->second.direction == "HORIZONTAL";
        };
        const auto coord = [&](const ph::Shape& sh) {
          return std::llround(horizontal_layer(sh.layer) ? (sh.rect.yl + sh.rect.yh) / 2 : (sh.rect.xl + sh.rect.xh) / 2);
        };
        const auto overlaps_own = [&](const ph::Shape& sh) {
          return std::ranges::any_of(own, [&](const ph::Shape& a) {
            return a.layer == sh.layer && coord(a) == coord(sh) && a.rect.xl <= sh.rect.xh && sh.rect.xl <= a.rect.xh
                   && a.rect.yl <= sh.rect.yh && sh.rect.yl <= a.rect.yh;
          });
        };
        // Track exclusion as intervals along each track (width + PRL spacing): total for the new metal, and incremental
        // = what (owned + new) covers beyond what the net's owned metal already covers, so widening or extending an
        // owned track still counts
        {
          using Intervals    = std::map<std::pair<std::string, int64_t>, std::vector<std::pair<double, double>>>;
          const auto project = [&](const std::vector<ph::Shape>& from, Intervals& into) {
            for (const auto& sh : from) {
              if (!tech.layers.contains(sh.layer)) {
                continue;
              }
              const auto&  L       = tech.layers.at(sh.layer);
              const bool   hz      = L.direction == "HORIZONTAL";
              const double across  = hz ? sh.rect.yh - sh.rect.yl : sh.rect.xh - sh.rect.xl;
              const double along   = hz ? sh.rect.xh - sh.rect.xl : sh.rect.yh - sh.rect.yl;
              const double nominal = L.width * double(fp.dbu);
              const double centre  = hz ? (sh.rect.yl + sh.rect.yh) / 2 : (sh.rect.xl + sh.rect.xh) / 2;
              const auto   space = parallel_spacing(L, std::max(across, nominal) / double(fp.dbu), along / double(fp.dbu), fp.dbu);
              const double reach = across / 2 + nominal / 2 + double(space);
              for (const auto t :
                   track_coords(fp, sh.layer, hz, int64_t(std::floor(centre - reach)), int64_t(std::ceil(centre + reach)) + 1)) {
                if (std::abs(double(t) - centre) < reach) {
                  into[{sh.layer, t}].emplace_back(hz ? sh.rect.xl : sh.rect.yl, hz ? sh.rect.xh : sh.rect.yh);
                }
              }
            }
          };
          const auto covered = [](std::vector<std::pair<double, double>> v) {
            std::ranges::sort(v);
            double total = 0, lo = -1e300, hi = -1e300;
            for (const auto& [a, b] : v) {
              if (a > hi) {
                total += hi > lo ? hi - lo : 0;
                lo     = a;
                hi     = b;
              } else {
                hi = std::max(hi, b);
              }
            }
            return total + (hi > lo ? hi - lo : 0);
          };
          Intervals owned_iv, new_iv;
          project(own, owned_iv);
          std::vector<ph::Shape> metal;
          for (const auto& sh : shapes) {
            if (tech.layers.contains(sh.layer)) {
              metal.push_back(sh);
            }
          }
          project(metal, new_iv);
          for (const auto& [key, iv] : new_iv) {
            excluded[key.first].insert(key.second);
            auto       both = iv;
            const auto ow   = owned_iv.find(key);
            if (ow != owned_iv.end()) {
              both.insert(both.end(), ow->second.begin(), ow->second.end());
            }
            if (covered(both) > (ow == owned_iv.end() ? 0. : covered(ow->second)) + 1e-9) {
              excluded_incremental[key.first].insert(key.second);
            }
          }
        }
        for (const auto& sh : shapes) {
          if (!tech.layers.contains(sh.layer)) {
            continue;  // cut shapes
          }
          const int64_t c = coord(sh);  // any metal, square via landings included, claims its track coordinate
          mine[sh.layer].insert(c);
          (overlaps_own(sh) ? owned_used : used)[sh.layer].insert(c);
          if (!planar.contains(sh.layer)) {  // upper-layer metal: an access stack, or planar use to report
            upper_length[sh.layer] += std::max(sh.rect.xh - sh.rect.xl, sh.rect.yh - sh.rect.yl);
          }
        }
      } else if (first_failure.empty()) {
        first_failure = job.name + ": " + why;
      }
      w.Key("non_planar_length_dbu");
      w.StartObject();
      for (const auto& [layer, len] : upper_length) {
        w.Key(layer.c_str());
        w.Double(len);
      }
      w.EndObject();
      w.Key("coords");  // layer -> claimed track coordinates of this net's new metal
      w.StartObject();
      for (const auto& [layer, cs] : mine) {
        w.Key(layer.c_str());
        w.StartArray();
        for (const auto c : cs) {
          w.Int64(c);
        }
        w.EndArray();
      }
      w.EndObject();
      w.EndObject();
    }
    w.EndArray();
    // a full-box witness needs every obligation (clocks included) and both validations clean
    // with exact geometry the planned joins are not realized here, so anchors may be disconnected islands: until they
    // are, a routed box is an attachment-only result (signal_jobs_ok), never a full-box witness
    const bool all_ok = routed == jobs.size() && unfulfilled.empty() && off_menu == 0 && outside_box == 0 && joins_unrealized == 0
                        && foreign_join_unresolved == 0;
    w.Key("all_ok");
    w.Bool(all_ok);
    w.Key("signal_jobs_ok");
    w.Bool(routed == jobs.size());
    w.Key("unfulfilled_clock_nets");
    w.StartArray();
    for (const auto& n : unfulfilled) {
      w.String(n.c_str());
    }
    w.EndArray();
    w.Key("off_menu_shapes");  // validation: new M2/M3 metal on a coordinate outside the menu (must be 0)
    w.Uint(unsigned(off_menu));
    w.Key("shapes_outside_box");  // validation: new metal past the tile (pin-access stacks; 0 with halo 0)
    w.Uint(unsigned(outside_box));
    w.Key("track_exclusions");  // every layer: [total, incremental] distinct DEF track centres excluded by new metal
    w.StartObject();
    for (const auto& [l, ts] : excluded) {
      w.Key(l.c_str());
      w.StartArray();
      w.Uint(unsigned(ts.size()));
      w.Uint(unsigned(excluded_incremental[l].size()));
      w.EndArray();
    }
    w.EndObject();
    w.Key("layers_used");  // every layer: distinct new / owned-reused coordinates
    w.StartObject();
    std::set<std::string> layers_seen;
    for (const auto& [l, _] : used) {
      layers_seen.insert(l);
    }
    for (const auto& [l, _] : owned_used) {
      layers_seen.insert(l);
    }
    for (const auto& l : layers_seen) {
      w.Key(l.c_str());
      w.StartArray();
      w.Uint(unsigned(used[l].size()));
      w.Uint(unsigned(owned_used[l].size()));
      w.EndArray();
    }
    w.EndObject();
    w.Key("nets_routed");
    w.Uint(unsigned(routed));
    w.Key("nets_total");
    w.Uint(unsigned(jobs.size()));
    w.Key("first_failure");
    w.String(first_failure.c_str());
    for (const std::string layer : {"M2", "M3"}) {
      w.Key((layer + "_tracks_used").c_str());  // distinct track coordinates newly used in the tile (owned anchors excluded)
      w.Uint(unsigned(used[layer].size()));
      w.Key((layer + "_owned_reused").c_str());
      w.Uint(unsigned(owned_used[layer].size()));
      w.Key((layer + "_menu").c_str());
      w.Uint(unsigned(menu[layer].size()));
      w.Key((layer + "_blocked").c_str());
      w.Uint(unsigned(blocked_tracks[layer]));
    }
    w.Key("anchor_fallback");
    w.Bool(anchor_fallback);
    w.Key("maze_labels");
    w.Uint(maze_labels);
    w.Key("retry_labels");
    w.Uint(retry_labels);
    w.Key("strict");
    w.Bool(strict);
    w.Key("alt_flat");
    w.Bool(alt_flat);
    w.Key("job_patterns");
    w.Uint64(job_patterns);
    w.Key("focus");
    w.String(focus_report.c_str());
    w.Key("focus_wide_shapes");  // the wide-window witness (diagnostic only, not committed): layer and rectangle
    w.StartArray();
    if (!focus_paths.empty()) {
      ph::Net piece{focus_net, {}, {ph::Wire{"ROUTED", focus_paths}}};
      auto    decode = fp;  // never committed: its rules stay out of the shared frame
      decode.nondefault_rules.insert(focus_rules.begin(), focus_rules.end());
      for (const auto& sh : ph::route_geometry(tech, decode, piece)) {
        if (tech.layers.contains(sh.layer)) {  // the same width/rule-expanded projection, for the witness
          const auto&  L       = tech.layers.at(sh.layer);
          const bool   hz      = L.direction == "HORIZONTAL";
          const double across  = hz ? sh.rect.yh - sh.rect.yl : sh.rect.xh - sh.rect.xl;
          const double along   = hz ? sh.rect.xh - sh.rect.xl : sh.rect.yh - sh.rect.yl;
          const double nominal = L.width * double(fp.dbu);
          const double centre  = hz ? (sh.rect.yl + sh.rect.yh) / 2 : (sh.rect.xl + sh.rect.xh) / 2;
          const auto   space   = parallel_spacing(L, std::max(across, nominal) / double(fp.dbu), along / double(fp.dbu), fp.dbu);
          const double reach   = across / 2 + nominal / 2 + double(space);
          for (const auto t :
               track_coords(fp, sh.layer, hz, int64_t(std::floor(centre - reach)), int64_t(std::ceil(centre + reach)) + 1)) {
            if (std::abs(double(t) - centre) < reach) {
              focus_excluded[sh.layer].insert(t);
            }
          }
        }
        w.StartArray();
        w.String(sh.layer.c_str());
        w.Double(sh.rect.xl);
        w.Double(sh.rect.yl);
        w.Double(sh.rect.xh);
        w.Double(sh.rect.yh);
        w.EndArray();
      }
    }
    w.EndArray();
    w.Key("focus_track_exclusions");
    w.StartObject();
    for (const auto& [l, ts] : focus_excluded) {
      w.Key(l.c_str());
      w.StartArray();
      for (const auto t : ts) {
        w.Int64(t);
      }
      w.EndArray();
    }
    w.EndObject();
    w.Key("exact_planned_geometry");
    w.Bool(exact);
    w.Key("foreign_join_vias");  // foreign joins near the tile occupied by the union of their default LEF vias
    w.Uint(unsigned(foreign_joins));
    w.Key("foreign_joins_skipped");  // joins of omitted (fallback/derived-only) foreign geometry: not occupancy
    w.Uint(unsigned(foreign_joins_skipped));
    w.Key("foreign_join_unresolved");  // join steps with no default LEF via: the box cannot pass
    w.Uint(unsigned(foreign_join_unresolved));
    w.Key("planned_joins_unrealized");  // layer joins of the planned metal near the tile: not built, so anchors may be islands
    w.Uint(unsigned(joins_unrealized));
    w.Key("foreign_metal_shapes");
    w.Uint(unsigned(foreign));
    w.Key("fallback_proposals_ignored");
    w.Uint(unsigned(fallback_skipped));
    w.EndObject();
    std::ofstream out(str("out"));
    out << buffer.GetString() << '\n';
    std::cout << buffer.GetString() << '\n';
    return all_ok ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
  }
}
