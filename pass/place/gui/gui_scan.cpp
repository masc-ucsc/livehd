// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gui_scan.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <set>
#include <tuple>

#include "gui_geometry.hpp"
#include "gui_tracks.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::placement::gui {
namespace {
std::string error(const std::string& why) {
  rapidjson::StringBuffer                    buf;
  rapidjson::Writer<rapidjson::StringBuffer> w(buf);
  w.StartObject();
  w.Key("ok");
  w.Bool(false);
  w.Key("error");
  w.String(why.c_str(), rapidjson::SizeType(why.size()));
  w.EndObject();
  return buf.GetString();
}

struct Candidate {
  double      x = 0, y = 0;
  std::string orient;
  double      wl = 0;
};

bool mirrors_x(const std::string& s) { return s == "FN" || s == "S"; }
bool mirrors_y(const std::string& s) { return s == "FS" || s == "S"; }

bool finite_number(const rapidjson::Value& v) { return v.IsNumber() && std::isfinite(v.GetDouble()); }
}  // namespace

std::string macro_scan(const Scene& scene, const Frame& frame, const rapidjson::Value& request, bool orientation_editable, const Chunk_frame* chunks) {
  const auto string_of = [&](const char* key) { return request.HasMember(key) && request[key].IsString() ? std::string(request[key].GetString(), request[key].GetStringLength()) : std::string{}; };
  auto       name      = string_of("node");
  if (name.empty()) {
    return error("macro_scan needs node (a macro name or #index)");
  }
  const int index = name[0] == '#' ? scene.node_index(name) : (scene.index.contains(name) ? scene.index.at(name) : -1);
  if (index < 0 || size_t(index) >= scene.nodes.size() || size_t(index) >= frame.x.size()) {
    return error("no such node in the current phase: " + name);
  }
  const auto& macro = scene.nodes[size_t(index)];
  if (macro.kind != Kind::macro && macro.kind != Kind::fixed) {
    return error(macro.name + " is not a macro");
  }

  // The frame of the positions: the die (DBU) or the macro's home chunk, relative to its lower-left corner.
  const auto frame_name = string_of("frame").empty() ? std::string("die") : string_of("frame");
  if (frame_name != "die" && frame_name != "chunk") {
    return error("frame must be die or chunk");
  }
  const Chunk_frame::Chunk* home = nullptr;
  if (frame_name == "chunk" || request.HasMember("chunk")) {
    if (!chunks) {
      return error("this scene has no chunk plan: the design is one home, so use die coordinates");
    }
    const auto named = string_of("chunk");
    home             = named.empty() ? chunks->of(macro.name) : chunks->find(named);
    if (!home) {
      return error(named.empty() ? macro.name + " belongs to no chunk of the plan: name one with chunk" : "unknown chunk " + named + " (export chunks lists the ids)");
    }
  } else if (chunks) {
    home = chunks->of(macro.name);  // reported, not used
  }
  const bool   in_chunk = frame_name == "chunk";
  const double ox = in_chunk ? home->box[0] : 0, oy = in_chunk ? home->box[1] : 0;  // origin of the positions given and of the grid

  // The candidate orientations: the current one unless asked; the mirrored ones a macro may take.
  std::vector<std::string> orients;
  if (request.HasMember("orientations")) {
    if (!request["orientations"].IsArray() || request["orientations"].Empty() || request["orientations"].Size() > 4) {
      return error("orientations must be 1 to 4 of N, FN, FS, S");
    }
    for (const auto& o : request["orientations"].GetArray()) {
      const std::string text = o.IsString() ? o.GetString() : "";
      if (text != "N" && text != "FN" && text != "FS" && text != "S") {
        return error("orientations must be 1 to 4 of N, FN, FS, S (rotations are not scanned)");
      }
      if (!macro.allowed_orientations.empty() && std::ranges::find(macro.allowed_orientations, text) == macro.allowed_orientations.end()) {
        return error(macro.name + " does not allow orientation " + text);
      }
      if (std::ranges::find(orients, text) == orients.end()) {
        orients.push_back(text);
      }
    }
  } else {
    orients.push_back(macro.orient);
  }

  std::vector<std::string> dropped;
  if (!orientation_editable) {  // the gate would refuse the move: do not rank poses that cannot be applied
    for (auto it = orients.begin(); it != orients.end();) {
      if (*it != macro.orient) {
        dropped.push_back(*it);
        it = orients.erase(it);
      } else {
        ++it;
      }
    }
    if (orients.empty()) {
      orients.push_back(macro.orient);
    }
  }

  // The candidate positions (lower-left, DBU): explicit ones, or a grid over the core, never both.
  std::vector<std::array<double, 2>> positions;
  double                             grid_step = 0;
  if (request.HasMember("positions")) {
    if (request.HasMember("grid")) {
      return error("give positions or grid, not both");
    }
    if (!request["positions"].IsArray() || request["positions"].Empty() || request["positions"].Size() > 20000) {
      return error("positions must be 1 to 20000 entries of [x, y] in DBU");
    }
    for (const auto& p : request["positions"].GetArray()) {
      if (p.IsArray() && p.Size() == 2 && finite_number(p[0]) && finite_number(p[1])) {
        positions.push_back({p[0].GetDouble() + ox, p[1].GetDouble() + oy});
      } else if (p.IsObject() && p.HasMember("x") && p.HasMember("y") && finite_number(p["x"]) && finite_number(p["y"])) {
        positions.push_back({p["x"].GetDouble() + ox, p["y"].GetDouble() + oy});
      } else {
        return error("positions must be entries of [x, y] or {x, y} in DBU");
      }
    }
  } else {
    int nx = 16, ny = 16;
    if (request.HasMember("grid")) {
      const auto& g = request["grid"];
      if (!g.IsObject() || !g.HasMember("nx") || !g.HasMember("ny") || !g["nx"].IsInt() || !g["ny"].IsInt() || g["nx"].GetInt() < 1 || g["ny"].GetInt() < 1
          || g["nx"].GetInt() > 200 || g["ny"].GetInt() > 200) {
        return error("grid needs integer nx and ny in [1, 200]");
      }
      nx = g["nx"].GetInt();
      ny = g["ny"].GetInt();
    }
    const Box    area = in_chunk ? Box{home->box[0], home->box[1], home->box[2], home->box[3]} : scene.die;
    const double xs = area.xh - area.xl - macro.w, ys = area.yh - area.yl - macro.h;
    if (xs < 0 || ys < 0) {
      return error(macro.name + (in_chunk ? " does not fit in its chunk " + home->id : " does not fit in the core"));
    }
    grid_step = std::min(nx > 1 ? xs / (nx - 1) : xs, ny > 1 ? ys / (ny - 1) : ys);
    for (int j = 0; j < ny; ++j) {
      for (int i = 0; i < nx; ++i) {
        positions.push_back({std::round(area.xl + (nx == 1 ? xs / 2 : xs * i / (nx - 1))), std::round(area.yl + (ny == 1 ? ys / 2 : ys * j / (ny - 1)))});
      }
    }
  }
  if (positions.size() * orients.size() > 20000) {
    return error("positions times orientations is more than 20000 candidates");
  }
  int top = 10;
  if (request.HasMember("top")) {
    if (!request["top"].IsInt() || request["top"].GetInt() < 1 || request["top"].GetInt() > 200) {
      return error("top must be an integer in [1, 200]");
    }
    top = request["top"].GetInt();
  }

  // One net per connected macro pin: the half-perimeter of the pin joined with the box of the logic cells on its net
  // (the cells stay where they are now; other macros' pins and top ports are not on the net here). A net that
  // reaches thousands of cells, like a clock or an enable, has its pin inside that box in most places and so costs
  // about the same wherever the macro goes: it does not drown the nets that pull the macro somewhere.
  struct Net {
    const Macro_pin* pin;
    double           xl = INFINITY, yl = INFINITY, xh = -INFINITY, yh = -INFINITY;
    size_t           peers = 0;
  };
  std::vector<Net> nets;
  size_t           peers = 0;
  for (const auto& pin : macro.signal_pins) {
    Net net{&pin};
    for (int peer : pin.logic) {
      if (peer == index || peer < 0 || size_t(peer) >= frame.x.size() || size_t(peer) >= scene.nodes.size()) {
        continue;
      }
      const auto& other = scene.nodes[size_t(peer)];
      const double cx = frame.x[size_t(peer)] + other.w / 2, cy = frame.y[size_t(peer)] + other.h / 2;
      net.xl = std::min(net.xl, cx);
      net.yl = std::min(net.yl, cy);
      net.xh = std::max(net.xh, cx);
      net.yh = std::max(net.yh, cy);
      ++net.peers;
    }
    if (net.peers) {
      peers += net.peers;
      nets.push_back(net);
    }
  }
  if (nets.empty()) {
    return error(macro.name + " has no pins connected to logic in this scene (no nets to measure)");
  }
  const auto pin_offset = [&](const Macro_pin& pin, const std::string& orient) {
    return std::array<double, 2>{mirrors_x(macro.orient) != mirrors_x(orient) ? macro.w - pin.x : pin.x,
                                 mirrors_y(macro.orient) != mirrors_y(orient) ? macro.h - pin.y : pin.y};
  };
  const auto wirelength = [&](double x, double y, const std::string& orient) {
    double total = 0;
    for (const auto& net : nets) {
      const auto off = pin_offset(*net.pin, orient);
      const double px = x + off[0], py = y + off[1];
      total += (std::max(net.xh, px) - std::min(net.xl, px)) + (std::max(net.yh, py) - std::min(net.yl, py));
    }
    return total;
  };

  // Legality at DEF precision is the checker the page uses (outline, halo, channel, exclusions, other macros), plus the
  // routing-track access the flow demands of a macro held where it is put. The poses offered are on the tracks.
  // The checker looks only at macros and exclusions, so it runs on a scene reduced to them: over every node of a
  // four-million-cell design one check took 66 ms, a 16 x 16 grid 17 s.
  Scene reduced;
  Frame scratch;
  int   self = -1;
  {
    reduced.die = scene.die;
    reduced.halo_x = scene.halo_x;
    reduced.halo_y = scene.halo_y;
    reduced.channel_x = scene.channel_x;
    reduced.channel_y = scene.channel_y;
    reduced.exclusions = scene.exclusions;
    for (size_t i = 0; i < scene.nodes.size() && i < frame.x.size() && i < frame.y.size(); ++i) {
      const auto& n = scene.nodes[i];
      if (n.kind != Kind::macro && n.kind != Kind::fixed && !n.exclusion) {
        continue;
      }
      if (int(i) == index) {
        self = int(reduced.nodes.size());
      }
      Node copy;  // what macro_geometry reads: not the pins, whose logic lists are long
      copy.name      = n.name;
      copy.kind      = n.kind;
      copy.movable   = n.movable;
      copy.exclusion = n.exclusion;
      copy.w         = n.w;
      copy.h         = n.h;
      reduced.nodes.push_back(std::move(copy));
      scratch.x.push_back(frame.x[i]);
      scratch.y.push_back(frame.y[i]);
    }
  }
  const auto legal = [&](double x, double y, const std::string& orient, std::string* why = nullptr) {
    scratch.x[size_t(self)] = x;
    scratch.y[size_t(self)] = y;
    bool                  ok = true;
    std::set<std::string> causes;
    for (const auto& issue : macro_geometry(reduced, scratch)) {
      if (issue.node == self || issue.other == self) {
        ok = false;
        causes.insert(issue.cause);
      }
    }
    if (!track_miss(scene, size_t(index), orient, x, y).empty()) {
      ok = false;
      causes.insert("misses_tracks");
    }
    if (in_chunk && (x < home->box[0] || y < home->box[1] || x + macro.w > home->box[2] || y + macro.h > home->box[3])) {
      ok = false;  // the macro belongs to this chunk: its body stays inside it
      causes.insert("outside_chunk");
    }
    if (why) {
      for (const auto& c : causes) {
        *why += (why->empty() ? "" : ",") + c;
      }
    }
    return ok;
  };
  // The nearest origin the flow accepts for this orientation (the position itself without track data).
  const auto snap = [&](double x, double y, const std::string& orient) -> std::optional<std::array<double, 2>> {
    return track_nearest(scene, size_t(index), orient, x, y);
  };
  // A scan that moves the macro by orientation changes its pin offsets only; the body is the same rectangle.
  const double here_x = frame.x[size_t(index)], here_y = frame.y[size_t(index)];
  std::string  here_why;
  const bool   here_legal = legal(here_x, here_y, macro.orient, &here_why);
  const double here_wl    = wirelength(here_x, here_y, macro.orient);

  std::vector<Candidate> kept;
  size_t                 evaluated = 0, illegal = 0, snapped = 0;
  positions.push_back({here_x, here_y});  // staying is a candidate: `best` is never worse than the current pose
  std::set<std::tuple<double, double, std::string>> seen;
  for (const auto& position : positions) {
    for (const auto& orient : orients) {
      ++evaluated;
      const auto at = snap(position[0], position[1], orient);
      if (!at) {
        ++illegal;
        continue;
      }
      const double x = (*at)[0], y = (*at)[1];
      if (!seen.insert({x, y, orient}).second) {
        continue;
      }
      if (x != std::round(position[0]) || y != std::round(position[1])) {
        ++snapped;
      }
      if (!legal(x, y, orient)) {
        ++illegal;
        continue;
      }
      kept.push_back({x, y, orient, wirelength(x, y, orient)});
    }
  }
  std::ranges::stable_sort(kept, [](const Candidate& a, const Candidate& b) { return std::tie(a.wl, a.y, a.x) < std::tie(b.wl, b.y, b.x); });

  // Walk the best candidate downhill: eight neighbours at a step that halves when none is better, down to a row.
  bool refine = true;
  if (request.HasMember("refine")) {
    if (!request["refine"].IsBool()) {
      return error("refine must be true or false");
    }
    refine = request["refine"].GetBool();
  }
  std::optional<Candidate> refined;
  size_t                   refine_checks = 0;
  if (refine && !kept.empty()) {
    Candidate    at   = kept.front();
    const double stop = std::max({scene.row_height, scene.site_width, 1.0});
    double       step = std::max(grid_step > 0 ? grid_step / 2 : (scene.die.xh - scene.die.xl) / 64, stop);
    while (step >= stop && refine_checks < 600) {
      Candidate better = at;
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          if ((dx || dy) && refine_checks < 600) {
            const auto moved = snap(at.x + dx * step, at.y + dy * step, at.orient);
            ++refine_checks;
            if (!moved || ((*moved)[0] == at.x && (*moved)[1] == at.y)) {
              continue;
            }
            const double x = (*moved)[0], y = (*moved)[1];
            if (!legal(x, y, at.orient)) {
              continue;
            }
            const double wl = wirelength(x, y, at.orient);
            if (wl < better.wl) {
              better = {x, y, at.orient, wl};
            }
          }
        }
      }
      if (better.wl < at.wl) {
        at = better;
      } else {
        step /= 2;
      }
    }
    if (at.x != kept.front().x || at.y != kept.front().y) {
      refined = at;
    }
  }

  rapidjson::StringBuffer                    buf;
  rapidjson::Writer<rapidjson::StringBuffer> w(buf);
  const auto                                 text = [&](const char* key, const std::string& value) {
    w.Key(key);
    w.String(value.c_str(), rapidjson::SizeType(value.size()));
  };
  w.StartObject();
  w.Key("ok");
  w.Bool(true);
  text("model", "sum over the macro's connected pins of the net half-perimeter: the pin joined with the box of the logic cells on its net, those cells staying where they are now. No timing, no congestion, no re-placement, other macros' pins and top ports not counted: verify the best few with whatif or explore");
  text("units", "dbu");
  w.Key("dbu_per_um");
  w.Double(scene.dbu_per_um);
  text("node", macro.name);
  w.Key("body_dbu");
  w.StartArray();
  w.Double(macro.w);
  w.Double(macro.h);
  w.EndArray();
  w.Key("nets");
  w.Uint64(nets.size());
  w.Key("peers");
  w.Uint64(peers);
  text("frame", frame_name);
  if (home) {
    text("chunk", home->id);
    w.Key("chunk_box_dbu");
    w.StartArray();
    for (double v : home->box) {
      w.Double(v);
    }
    w.EndArray();
  }
  const auto chunk_pose = [&](double x, double y) {
    if (home) {
      w.Key("chunk_x");
      w.Double(x - home->box[0]);
      w.Key("chunk_y");
      w.Double(y - home->box[1]);
    }
  };
  w.Key("current");
  w.StartObject();
  w.Key("x");
  w.Double(here_x);
  w.Key("y");
  w.Double(here_y);
  chunk_pose(here_x, here_y);
  text("orientation", macro.orient);
  w.Key("hpwl_dbu");
  w.Double(here_wl);
  w.Key("legal");
  w.Bool(here_legal);
  text("issues", here_why);
  w.EndObject();
  text("track_check", track_describe(scene, size_t(index), macro.orient));
  w.Key("evaluated");
  w.Uint64(evaluated);
  w.Key("illegal");
  w.Uint64(illegal);
  w.Key("snapped_to_tracks");
  w.Uint64(snapped);
  w.Key("legal_candidates");  // positions times orientations that passed
  w.Uint64(kept.size());
  if (!dropped.empty()) {
    w.Key("orientations_dropped");
    w.StartArray();
    for (const auto& o : dropped) {
      w.String(o.c_str(), rapidjson::SizeType(o.size()));
    }
    w.EndArray();
    text("orientation_note", "this gate does not accept an orientation change (only the fp and macro gates do), so those were not ranked");
  }
  if (refined) {
    w.Key("refined");
    w.StartObject();
    w.Key("x");
    w.Double(refined->x);
    w.Key("y");
    w.Double(refined->y);
    chunk_pose(refined->x, refined->y);
    text("orientation", refined->orient);
    w.Key("hpwl_dbu");
    w.Double(refined->wl);
    w.Key("delta_dbu");
    w.Double(refined->wl - here_wl);
    w.Key("delta_pct");
    w.Double(here_wl > 0 ? 100 * (refined->wl - here_wl) / here_wl : 0);
    w.Key("legality_checks");
    w.Uint64(refine_checks);
    w.EndObject();
  }
  w.Key("best");
  w.StartArray();
  for (size_t k = 0; k < kept.size() && k < size_t(top); ++k) {
    const auto& c = kept[k];
    w.StartObject();
    w.Key("x");
    w.Double(c.x);
    w.Key("y");
    w.Double(c.y);
    chunk_pose(c.x, c.y);
    text("orientation", c.orient);
    w.Key("hpwl_dbu");
    w.Double(c.wl);
    w.Key("delta_dbu");
    w.Double(c.wl - here_wl);
    w.Key("delta_pct");
    w.Double(here_wl > 0 ? 100 * (c.wl - here_wl) / here_wl : 0);
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
  return buf.GetString();
}

}  // namespace livehd::placement::gui
