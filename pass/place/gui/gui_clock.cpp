// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gui_clock.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numeric>
#include <optional>
#include <set>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::placement::gui {
namespace {
using Writer              = rapidjson::Writer<rapidjson::StringBuffer>;
constexpr double kUnknown = std::numeric_limits<double>::quiet_NaN();

std::string error(const std::string& why) {
  rapidjson::StringBuffer buf;
  Writer                  w(buf);
  w.StartObject();
  w.Key("ok");
  w.Bool(false);
  w.Key("error");
  w.String(why.c_str(), rapidjson::SizeType(why.size()));
  w.EndObject();
  return buf.GetString();
}
void text(Writer& w, const std::string& s) { w.String(s.c_str(), rapidjson::SizeType(s.size())); }
// JSON has no NaN: an unknown estimate is null.
void number(Writer& w, double v) {
  if (std::isfinite(v)) {
    w.Double(v);
  } else {
    w.Null();
  }
}
// Three decimals are below every model's accuracy and keep the large replies readable.
void rounded(Writer& w, double v) { number(w, std::isfinite(v) ? std::round(v * 1000) / 1000 : v); }

std::string string_of(const rapidjson::Value& request, const char* key, const std::string& fallback = {}) {
  return request.IsObject() && request.HasMember(key) && request[key].IsString()
             ? std::string(request[key].GetString(), request[key].GetStringLength())
             : fallback;
}
size_t limit_of(const rapidjson::Value& request, size_t fallback) {
  if (request.IsObject() && request.HasMember("limit") && request["limit"].IsUint()) {
    return request["limit"].GetUint();
  }
  return fallback;
}

// Positions of one frame, by node centre, in micrometres.
struct Places {
  const Scene& scene;
  const Frame& frame;
  double       dbu;
  Places(const Scene& s, const Frame& f) : scene(s), frame(f), dbu(s.dbu_per_um > 0 ? s.dbu_per_um : 1) {}
  bool   has(int node) const { return node >= 0 && size_t(node) < frame.x.size() && size_t(node) < scene.nodes.size(); }
  double x(int node) const { return (frame.x[size_t(node)] + scene.nodes[size_t(node)].w / 2) / dbu; }
  double y(int node) const { return (frame.y[size_t(node)] + scene.nodes[size_t(node)].h / 2) / dbu; }
};

// Geometry of one net at the frame's positions: the driver-rooted star, the pin box and the farthest load.
struct Net_geometry {
  double star_um = 0, hpwl_um = 0, radius_um = 0;
  size_t placed = 0;
};
Net_geometry geometry(const Clock_net& net, const Places& at) {
  Net_geometry g;
  double       xl = 1e300, yl = 1e300, xh = -1e300, yh = -1e300;
  const auto   grow = [&](int node) {
    xl = std::min(xl, at.x(node));
    xh = std::max(xh, at.x(node));
    yl = std::min(yl, at.y(node));
    yh = std::max(yh, at.y(node));
  };
  const bool rooted = at.has(net.driver);
  if (rooted) {
    grow(net.driver);
  }
  for (const int sink : net.sinks) {
    if (!at.has(sink)) {
      continue;
    }
    ++g.placed;
    grow(sink);
    if (rooted) {
      const double d  = std::abs(at.x(sink) - at.x(net.driver)) + std::abs(at.y(sink) - at.y(net.driver));
      g.star_um      += d;
      g.radius_um     = std::max(g.radius_um, d);
    }
  }
  if (xh >= xl) {
    g.hpwl_um = (xh - xl) + (yh - yl);
  }
  return g;
}

struct Span {
  double lo = std::numeric_limits<double>::infinity(), hi = -std::numeric_limits<double>::infinity();
  size_t n = 0;
  void   add(double v) {
    if (std::isfinite(v)) {
      lo = std::min(lo, v);
      hi = std::max(hi, v);
      ++n;
    }
  }
  void merge(const Span& o) {
    lo  = std::min(lo, o.lo);
    hi  = std::max(hi, o.hi);
    n  += o.n;
  }
  double width() const { return n ? hi - lo : kUnknown; }
};
void write(Writer& w, const Span& s) {
  w.StartObject();
  w.Key("min_ps");
  rounded(w, s.n ? s.lo : kUnknown);
  w.Key("max_ps");
  rounded(w, s.n ? s.hi : kUnknown);
  w.Key("skew_ps");
  rounded(w, s.width());
  w.Key("known");
  w.Uint64(s.n);
  w.EndObject();
}

bool endpoint(const Clock_tree& tree, int node) {
  return node < 0 || size_t(node) >= tree.driven.size() || tree.driven[size_t(node)] < 0;
}

const std::string& node_name(const Scene& scene, int node) {
  static const std::string none;
  return node >= 0 && size_t(node) < scene.nodes.size() ? scene.nodes[size_t(node)].name : none;
}

// A root named by its net label or index; -1: every root. `known` is false for a name that matches nothing.
int root_filter(const Scene& scene, const Clock_tree& tree, const rapidjson::Value& request, bool& known) {
  known = true;
  if (!request.IsObject() || !request.HasMember("root")) {
    return -1;
  }
  const auto& value = request["root"];
  if (value.IsInt()) {
    const int net = value.GetInt();
    known         = net >= 0 && size_t(net) < scene.clock_nets.size();
    return known ? tree.root[size_t(net)] : -1;
  }
  const auto name = string_of(request, "root");
  for (size_t net = 0; net < scene.clock_nets.size(); ++net) {
    if (tree.parent[net] < 0 && scene.clock_nets[net].name == name) {
      return int(net);
    }
  }
  known = false;
  return -1;
}

// An endpoint named by node name, "#index" or the handoff's "instance:pin": (net, position in its sinks).
std::optional<std::pair<int, size_t>> find_sink(const Scene& scene, const Clock_tree& tree, const std::string& what) {
  int node = scene.node_index(what);
  if (node < 0) {
    if (const auto colon = what.rfind(':'); colon != std::string::npos) {
      node = scene.node_index(what.substr(0, colon));
    }
  }
  if (node < 0 || size_t(node) >= tree.load_of.size() || tree.load_of[size_t(node)] < 0) {
    return std::nullopt;
  }
  // A node on several clock nets (a gate's clock and its latch) is addressed by pin; a bare name takes the first.
  for (size_t net = 0; net < scene.clock_nets.size(); ++net) {
    const auto& n = scene.clock_nets[net];
    for (size_t k = 0; k < n.sinks.size(); ++k) {
      if (n.sinks[k] == node && k < n.sink_pins.size() && n.sink_pins[k] == what) {
        return std::pair{int(net), k};
      }
    }
  }
  const int   net = tree.load_of[size_t(node)];
  const auto& n   = scene.clock_nets[size_t(net)];
  const auto  it  = std::ranges::find(n.sinks, node);
  return std::pair{net, size_t(it - n.sinks.begin())};
}
double             arrival_of(const Clock_net& net, size_t k) { return k < net.arrival_ps.size() ? net.arrival_ps[k] : kUnknown; }
// The name a clock decision uses for a load: the handoff's pin label. A load the handoff does not name cannot be scheduled.
const std::string& pin_of(const Clock_net& net, size_t k) {
  static const std::string none;
  return k < net.sink_pins.size() ? net.sink_pins[k] : none;
}
}  // namespace

Clock_tree clock_tree(const Scene& scene) {
  Clock_tree   tree;
  const size_t nets = scene.clock_nets.size();
  tree.parent.assign(nets, -1);
  tree.root.assign(nets, -1);
  tree.level.assign(nets, 0);
  tree.driven.assign(scene.nodes.size(), -1);
  tree.load_of.assign(scene.nodes.size(), -1);
  for (size_t net = 0; net < nets; ++net) {
    const int driver = scene.clock_nets[net].driver;
    if (driver >= 0 && size_t(driver) < tree.driven.size() && tree.driven[size_t(driver)] < 0) {
      tree.driven[size_t(driver)] = int(net);
    }
  }
  for (size_t net = 0; net < nets; ++net) {
    for (const int sink : scene.clock_nets[net].sinks) {
      if (sink >= 0 && size_t(sink) < tree.load_of.size() && tree.load_of[size_t(sink)] < 0) {
        tree.load_of[size_t(sink)] = int(net);
      }
    }
  }
  for (size_t net = 0; net < nets; ++net) {
    const int driver = scene.clock_nets[net].driver;
    if (driver >= 0 && size_t(driver) < tree.load_of.size()) {
      const int up     = tree.load_of[size_t(driver)];
      // An IO terminal or a self-loop is not an upstream stage.
      tree.parent[net] = up == int(net) ? -1 : up;
    }
  }
  for (size_t net = 0; net < nets; ++net) {
    int    at = int(net), depth = 0;
    size_t steps = 0;
    while (tree.parent[size_t(at)] >= 0 && steps++ < nets) {  // bounded: a malformed scene must not hang the controller
      at = tree.parent[size_t(at)];
      ++depth;
    }
    tree.root[net]  = at;
    tree.level[net] = depth;
  }
  return tree;
}

size_t annotate_clock(Scene& scene, std::string_view handoff_json, const std::string& path) {
  rapidjson::Document doc;
  if (doc.Parse(handoff_json.data(), handoff_json.size()).HasParseError() || !doc.IsObject() || !doc.HasMember("networks")
      || !doc["networks"].IsArray()) {
    return 0;
  }
  scene.build_index();
  std::vector<int> driven(scene.nodes.size(), -1);
  for (size_t net = 0; net < scene.clock_nets.size(); ++net) {
    const int driver = scene.clock_nets[net].driver;
    if (driver >= 0 && size_t(driver) < driven.size() && driven[size_t(driver)] < 0) {
      driven[size_t(driver)] = int(net);
    }
    auto& n = scene.clock_nets[net];
    n.sink_pins.assign(n.sinks.size(), {});
    n.arrival_ps.assign(n.sinks.size(), kUnknown);
  }
  const auto net_of = [&](const std::string& instance) -> Clock_net* {
    const int node = scene.node_index(instance);
    return node >= 0 && size_t(node) < driven.size() && driven[size_t(node)] >= 0 ? &scene.clock_nets[size_t(driven[size_t(node)])]
                                                                                  : nullptr;
  };
  const auto get
      = [](const rapidjson::Value& v, const char* key) { return v.HasMember(key) && v[key].IsNumber() ? v[key].GetDouble() : -1.; };
  // children[i] with arrivals[i]; a child is "instance:pin" on a leaf and a bare buffer instance on a trunk.
  const auto loads = [&](Clock_net& net, const rapidjson::Value& children, const rapidjson::Value* arrivals) {
    std::vector<bool> used(net.sinks.size(), false);
    for (rapidjson::SizeType i = 0; i < children.Size(); ++i) {
      if (!children[i].IsString()) {
        continue;
      }
      const std::string label(children[i].GetString(), children[i].GetStringLength());
      int               node = scene.node_index(label);
      if (node < 0) {
        if (const auto colon = label.rfind(':'); colon != std::string::npos) {
          node = scene.node_index(label.substr(0, colon));
        }
      }
      for (size_t k = 0; node >= 0 && k < net.sinks.size(); ++k) {
        if (net.sinks[k] == node && !used[k]) {
          used[k]          = true;
          net.sink_pins[k] = label;
          if (arrivals && i < arrivals->Size() && (*arrivals)[i].IsNumber()) {
            net.arrival_ps[k] = (*arrivals)[i].GetDouble();
          }
          break;
        }
      }
    }
  };
  size_t annotated = 0;
  for (const auto& network : doc["networks"].GetArray()) {
    if (!network.IsObject()) {
      continue;
    }
    if (network.HasMember("branches") && network["branches"].IsArray()) {
      for (const auto& branch : network["branches"].GetArray()) {
        if (!branch.IsObject() || !branch.HasMember("instance") || !branch["instance"].IsString()) {
          continue;
        }
        auto* net = net_of(branch["instance"].GetString());
        if (!net) {
          continue;
        }
        ++annotated;
        net->stage_delay_ps = get(branch, "estimated_stage_delay_ps");
        net->far_slew_ps    = get(branch, "estimated_far_slew_ps");
        net->load_ff        = get(branch, "load_ff");
        net->leaf           = branch.HasMember("leaf") && branch["leaf"].IsBool() && branch["leaf"].GetBool();
        net->drive_steps    = branch.HasMember("drive_steps") && branch["drive_steps"].IsInt() ? branch["drive_steps"].GetInt() : 0;
        if (branch.HasMember("children") && branch["children"].IsArray()) {
          const bool timed = branch.HasMember("estimated_sink_arrivals_ps") && branch["estimated_sink_arrivals_ps"].IsArray();
          loads(*net, branch["children"], timed ? &branch["estimated_sink_arrivals_ps"] : nullptr);
        }
      }
    }
    // Loads the source drives without a buffer.
    for (const char* key : {"retained_sinks", "gate_leaf_sinks"}) {
      if (!network.HasMember(key) || !network[key].IsArray() || network[key].Empty() || !network.HasMember("source_instance")
          || !network["source_instance"].IsString()) {
        continue;
      }
      const std::string instance = network["source_instance"].GetString();
      auto*             net      = net_of(instance == "PIN" && network.HasMember("source_pin") && network["source_pin"].IsString()
                             ? network["source_pin"].GetString()
                             : instance);
      if (net) {
        const bool timed
            = network.HasMember("estimated_direct_sink_arrivals_ps") && network["estimated_direct_sink_arrivals_ps"].IsArray();
        loads(*net, network[key], timed ? &network["estimated_direct_sink_arrivals_ps"] : nullptr);
      }
    }
  }
  scene.clock_schedule.clear();
  if (doc.HasMember("leaf_schedule") && doc["leaf_schedule"].IsObject()) {
    for (const auto& member : doc["leaf_schedule"].GetObject()) {
      if (member.value.IsInt()) {
        scene.clock_schedule[member.name.GetString()] = member.value.GetInt();
      }
    }
  }
  if (annotated) {
    scene.clock_report = path;
  }
  return annotated;
}

std::vector<Clock_segment> clock_segments(const Scene& scene, const Frame& frame) {
  std::vector<Clock_segment> out;
  const auto                 tree = clock_tree(scene);
  const Places               at(scene, frame);
  for (size_t net = 0; net < scene.clock_nets.size(); ++net) {
    const auto& n = scene.clock_nets[net];
    if (!at.has(n.driver)) {
      continue;
    }
    for (const int sink : n.sinks) {
      if (at.has(sink)) {
        out.push_back({at.x(n.driver) * at.dbu,
                       at.y(n.driver) * at.dbu,
                       at.x(sink) * at.dbu,
                       at.y(sink) * at.dbu,
                       tree.level[net],
                       endpoint(tree, sink)});
      }
    }
  }
  return out;
}

namespace {
struct Net_row {
  int          net = 0;
  Net_geometry geo;
  Span         arrival;
  size_t       endpoints = 0;
};
std::vector<Net_row> net_rows(const Scene& scene, const Clock_tree& tree, const Places& at, int root) {
  std::vector<Net_row> rows;
  for (size_t net = 0; net < scene.clock_nets.size(); ++net) {
    if (root >= 0 && tree.root[net] != root) {
      continue;
    }
    const auto& n = scene.clock_nets[net];
    Net_row     row;
    row.net = int(net);
    row.geo = geometry(n, at);
    for (size_t k = 0; k < n.sinks.size(); ++k) {
      if (endpoint(tree, n.sinks[k])) {
        ++row.endpoints;
        row.arrival.add(arrival_of(n, k));
      }
    }
    rows.push_back(row);
  }
  return rows;
}
void write_net(Writer& w, const Scene& scene, const Clock_tree& tree, const Net_row& row) {
  const auto& n = scene.clock_nets[size_t(row.net)];
  w.StartObject();
  w.Key("net");
  w.Int(row.net);
  w.Key("name");
  text(w, n.name);
  w.Key("driver");
  text(w, node_name(scene, n.driver));
  if (n.driver >= 0 && size_t(n.driver) < scene.nodes.size()) {
    w.Key("master");
    text(w, scene.nodes[size_t(n.driver)].master);
  }
  w.Key("root");
  w.Int(tree.root[size_t(row.net)]);
  w.Key("parent");
  w.Int(tree.parent[size_t(row.net)]);
  w.Key("level");
  w.Int(tree.level[size_t(row.net)]);
  w.Key("leaf");
  w.Bool(row.endpoints == n.sinks.size() && !n.sinks.empty());
  w.Key("fanout");
  w.Uint64(n.sinks.size());
  w.Key("endpoints");
  w.Uint64(row.endpoints);
  w.Key("star_um");
  rounded(w, row.geo.star_um);
  w.Key("hpwl_um");
  rounded(w, row.geo.hpwl_um);
  w.Key("radius_um");
  rounded(w, row.geo.radius_um);
  w.Key("stage_delay_ps");
  rounded(w, n.stage_delay_ps < 0 ? kUnknown : n.stage_delay_ps);
  w.Key("far_slew_ps");
  rounded(w, n.far_slew_ps < 0 ? kUnknown : n.far_slew_ps);
  w.Key("load_ff");
  rounded(w, n.load_ff < 0 ? kUnknown : n.load_ff);
  w.Key("drive_steps");
  w.Int(n.drive_steps);
  w.Key("arrival");
  write(w, row.arrival);
  w.EndObject();
}

std::string summary(const Scene& scene, const Frame& frame, const rapidjson::Value& request) {
  const auto   tree = clock_tree(scene);
  const Places at(scene, frame);
  const auto   rows = net_rows(scene, tree, at, -1);
  struct Domain {
    size_t                        nets = 0, buffers = 0, endpoints = 0, leaves = 0, fanout_max = 0, fanout_sum = 0;
    int                           depth_min = std::numeric_limits<int>::max(), depth_max = 0;
    double                        star_um = 0, hpwl_um = 0, slew_ps = -1, load_ff = -1, radius_um = 0;
    int                           slew_net = -1, radius_net = -1;
    Span                          arrival;
    std::map<std::string, size_t> masters;
  };
  std::map<int, Domain> domains;
  for (const auto& row : rows) {
    const auto& n = scene.clock_nets[size_t(row.net)];
    auto&       d = domains[tree.root[size_t(row.net)]];
    ++d.nets;
    d.star_um += row.geo.star_um;
    d.hpwl_um += row.geo.hpwl_um;
    if (tree.parent[size_t(row.net)] >= 0 && n.driver >= 0 && size_t(n.driver) < scene.nodes.size()) {
      ++d.buffers;
      ++d.masters[scene.nodes[size_t(n.driver)].master];
    }
    if (row.endpoints) {
      // Depth counts the driver stages an endpoint sees: unequal depth is skew the tree has by construction.
      d.depth_min  = std::min(d.depth_min, tree.level[size_t(row.net)]);
      d.depth_max  = std::max(d.depth_max, tree.level[size_t(row.net)]);
      d.endpoints += row.endpoints;
      if (row.endpoints == n.sinks.size()) {
        ++d.leaves;
        d.fanout_max  = std::max(d.fanout_max, n.sinks.size());
        d.fanout_sum += n.sinks.size();
      }
    }
    if (n.far_slew_ps > d.slew_ps) {
      d.slew_ps  = n.far_slew_ps;
      d.slew_net = row.net;
    }
    d.load_ff = std::max(d.load_ff, n.load_ff);
    if (row.geo.radius_um > d.radius_um) {
      d.radius_um  = row.geo.radius_um;
      d.radius_net = row.net;
    }
    d.arrival.merge(row.arrival);
  }
  rapidjson::StringBuffer buf;
  Writer                  w(buf);
  w.StartObject();
  w.Key("ok");
  w.Bool(true);
  w.Key("built");
  w.Bool(!scene.clock_nets.empty());
  w.Key("phase");
  text(w, scene.phase);
  w.Key("frame");
  w.Uint64(frame.id);
  w.Key("handoff");
  text(w, scene.clock_report);
  w.Key("note");
  text(w,
       scene.clock_nets.empty()
           ? "no native clock tree in this phase (synth.place.pcrs.clock=true builds one after placement optimization)"
       : scene.clock_report.empty()
           ? "topology only: no clock handoff was found, so delays, slews and arrivals are unknown"
           : "wire lengths are measured at this frame; delays, slews and arrivals are the handoff's estimates at "
             "the positions the tree was planned at");
  w.Key("domains");
  w.StartArray();
  for (const auto& [root, d] : domains) {
    w.StartObject();
    w.Key("root");
    text(w, scene.clock_nets[size_t(root)].name);
    w.Key("root_net");
    w.Int(root);
    w.Key("nets");
    w.Uint64(d.nets);
    w.Key("buffers");
    w.Uint64(d.buffers);
    w.Key("endpoints");
    w.Uint64(d.endpoints);
    w.Key("leaves");
    w.Uint64(d.leaves);
    w.Key("leaf_fanout");
    w.StartObject();
    w.Key("mean");
    rounded(w, d.leaves ? double(d.fanout_sum) / double(d.leaves) : kUnknown);
    w.Key("max");
    w.Uint64(d.fanout_max);
    w.EndObject();
    w.Key("depth");
    w.StartObject();
    w.Key("min");
    w.Int(d.endpoints ? d.depth_min : 0);
    w.Key("max");
    w.Int(d.depth_max);
    w.EndObject();
    w.Key("star_um");
    rounded(w, d.star_um);
    w.Key("hpwl_um");
    rounded(w, d.hpwl_um);
    w.Key("insertion");
    write(w, d.arrival);
    w.Key("worst_slew_ps");
    rounded(w, d.slew_ps < 0 ? kUnknown : d.slew_ps);
    w.Key("worst_slew_net");
    w.Int(d.slew_net);
    w.Key("max_load_ff");
    rounded(w, d.load_ff < 0 ? kUnknown : d.load_ff);
    w.Key("max_radius_um");
    rounded(w, d.radius_um);
    w.Key("max_radius_net");
    w.Int(d.radius_net);
    w.Key("masters");
    w.StartObject();
    for (const auto& [master, count] : d.masters) {
      w.Key(master.c_str());
      w.Uint64(count);
    }
    w.EndObject();
    w.EndObject();
  }
  w.EndArray();
  // The stages most likely to hurt: by estimated far slew when known, by radius otherwise.
  auto worst = rows;
  std::ranges::stable_sort(worst, [&](const Net_row& a, const Net_row& b) {
    const double sa = scene.clock_nets[size_t(a.net)].far_slew_ps, sb = scene.clock_nets[size_t(b.net)].far_slew_ps;
    return sa != sb ? sa > sb : a.geo.radius_um > b.geo.radius_um;
  });
  worst.resize(std::min(worst.size(), limit_of(request, 8)));
  w.Key("worst_nets");
  w.StartArray();
  for (const auto& row : worst) {
    write_net(w, scene, tree, row);
  }
  w.EndArray();
  w.EndObject();
  return buf.GetString();
}

std::string nets(const Scene& scene, const Frame& frame, const rapidjson::Value& request) {
  const auto tree  = clock_tree(scene);
  bool       known = true;
  const int  root  = root_filter(scene, tree, request, known);
  if (!known) {
    return error("no such clock root (see clock what=summary: domains[].root)");
  }
  auto       rows = net_rows(scene, tree, Places(scene, frame), root);
  const auto sort = string_of(request, "sort", "slew");
  const auto key  = [&](const Net_row& r) -> double {
    const auto& n = scene.clock_nets[size_t(r.net)];
    if (sort == "radius") {
      return r.geo.radius_um;
    }
    if (sort == "skew") {
      return r.arrival.n ? r.arrival.width() : -1;
    }
    if (sort == "load") {
      return n.load_ff;
    }
    if (sort == "fanout") {
      return double(n.sinks.size());
    }
    if (sort == "wire") {
      return r.geo.star_um;
    }
    if (sort == "delay") {
      return n.stage_delay_ps;
    }
    return n.far_slew_ps;
  };
  if (sort != "radius" && sort != "skew" && sort != "load" && sort != "fanout" && sort != "wire" && sort != "delay"
      && sort != "slew") {
    return error("sort is one of radius, skew, slew, load, fanout, wire, delay");
  }
  std::ranges::stable_sort(rows, [&](const Net_row& a, const Net_row& b) { return key(a) > key(b); });
  const size_t total = rows.size();
  rows.resize(std::min(rows.size(), limit_of(request, 50)));
  rapidjson::StringBuffer buf;
  Writer                  w(buf);
  w.StartObject();
  w.Key("ok");
  w.Bool(true);
  w.Key("sort");
  text(w, sort);
  w.Key("total");
  w.Uint64(total);
  w.Key("nets");
  w.StartArray();
  for (const auto& row : rows) {
    write_net(w, scene, tree, row);
  }
  w.EndArray();
  w.EndObject();
  return buf.GetString();
}

std::string sinks(const Scene& scene, const Frame& frame, const rapidjson::Value& request) {
  const auto tree  = clock_tree(scene);
  bool       known = true;
  const int  root  = root_filter(scene, tree, request, known);
  if (!known) {
    return error("no such clock root (see clock what=summary: domains[].root)");
  }
  const auto order = string_of(request, "order", "late");
  if (order != "late" && order != "early") {
    return error("order is late or early");
  }
  struct Row {
    int    net;
    size_t k;
    double arrival;
  };
  std::vector<Row> rows;
  size_t           unknown = 0;
  for (size_t net = 0; net < scene.clock_nets.size(); ++net) {
    if (root >= 0 && tree.root[net] != root) {
      continue;
    }
    const auto& n = scene.clock_nets[net];
    for (size_t k = 0; k < n.sinks.size(); ++k) {
      if (!endpoint(tree, n.sinks[k])) {
        continue;
      }
      if (std::isfinite(arrival_of(n, k))) {
        rows.push_back({int(net), k, arrival_of(n, k)});
      } else {
        ++unknown;
      }
    }
  }
  std::ranges::stable_sort(rows, [&](const Row& a, const Row& b) {
    return order == "late" ? a.arrival > b.arrival : a.arrival < b.arrival;
  });
  const size_t total = rows.size();
  rows.resize(std::min(rows.size(), limit_of(request, 50)));
  const Places            at(scene, frame);
  rapidjson::StringBuffer buf;
  Writer                  w(buf);
  w.StartObject();
  w.Key("ok");
  w.Bool(true);
  w.Key("order");
  text(w, order);
  w.Key("timed");
  w.Uint64(total);
  w.Key("untimed");
  w.Uint64(unknown);
  w.Key("sinks");
  w.StartArray();
  for (const auto& row : rows) {
    const auto& n    = scene.clock_nets[size_t(row.net)];
    const int   node = n.sinks[row.k];
    w.StartObject();
    w.Key("name");
    text(w, node_name(scene, node));
    w.Key("pin");
    text(w, pin_of(n, row.k));
    w.Key("net");
    w.Int(row.net);
    w.Key("leaf_driver");
    text(w, node_name(scene, n.driver));
    w.Key("level");
    w.Int(tree.level[size_t(row.net)]);
    w.Key("arrival_ps");
    rounded(w, row.arrival);
    if (at.has(node)) {
      w.Key("x_um");
      rounded(w, at.x(node));
      w.Key("y_um");
      rounded(w, at.y(node));
      if (at.has(n.driver)) {
        w.Key("driver_distance_um");
        rounded(w, std::abs(at.x(node) - at.x(n.driver)) + std::abs(at.y(node) - at.y(n.driver)));
      }
    }
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
  return buf.GetString();
}

std::string pair(const Scene& scene, const Frame& frame, const rapidjson::Value& request) {
  const auto tree = clock_tree(scene);
  const auto a = find_sink(scene, tree, string_of(request, "a")), b = find_sink(scene, tree, string_of(request, "b"));
  if (!a || !b) {
    return error("pair needs a and b: clock loads by node name, #index or instance:pin");
  }
  // Walk both up to the shared net: the stages below it are where the two arrivals can differ.
  int na = a->first, nb = b->first;
  int stages_a = 0, stages_b = 0;
  while (na != nb && na >= 0 && nb >= 0) {
    if (tree.level[size_t(na)] >= tree.level[size_t(nb)]) {
      na = tree.parent[size_t(na)];
      ++stages_a;
    } else {
      nb = tree.parent[size_t(nb)];
      ++stages_b;
    }
  }
  const bool   common = na == nb && na >= 0;
  const Places at(scene, frame);
  const auto   side = [&](Writer& w, const std::pair<int, size_t>& s, int stages) {
    const auto& n    = scene.clock_nets[size_t(s.first)];
    const int   node = n.sinks[s.second];
    w.StartObject();
    w.Key("name");
    text(w, node_name(scene, node));
    w.Key("pin");
    text(w, pin_of(n, s.second));
    w.Key("net");
    w.Int(s.first);
    w.Key("leaf_driver");
    text(w, node_name(scene, n.driver));
    w.Key("level");
    w.Int(tree.level[size_t(s.first)]);
    w.Key("private_stages");
    w.Int(stages);
    w.Key("arrival_ps");
    rounded(w, arrival_of(n, s.second));
    w.EndObject();
  };
  rapidjson::StringBuffer buf;
  Writer                  w(buf);
  w.StartObject();
  w.Key("ok");
  w.Bool(true);
  w.Key("a");
  side(w, *a, stages_a);
  w.Key("b");
  side(w, *b, stages_b);
  w.Key("same_root");
  w.Bool(common);
  w.Key("common_net");
  w.Int(common ? na : -1);
  if (common) {
    w.Key("common_driver");
    text(w, node_name(scene, scene.clock_nets[size_t(na)].driver));
    w.Key("common_level");
    w.Int(tree.level[size_t(na)]);
  }
  const double ta = arrival_of(scene.clock_nets[size_t(a->first)], a->second),
               tb = arrival_of(scene.clock_nets[size_t(b->first)], b->second);
  w.Key("skew_ps");  // b later than a is positive: setup margin for a path a -> b, hold risk for it
  rounded(w, common ? tb - ta : kUnknown);
  const int node_a = scene.clock_nets[size_t(a->first)].sinks[a->second],
            node_b = scene.clock_nets[size_t(b->first)].sinks[b->second];
  if (at.has(node_a) && at.has(node_b)) {
    w.Key("distance_um");
    rounded(w, std::abs(at.x(node_a) - at.x(node_b)) + std::abs(at.y(node_a) - at.y(node_b)));
  }
  w.EndObject();
  return buf.GetString();
}

// A grid over the die. v[x * by + y] like Grid; a bin without endpoints is null.
std::string map(const Scene& scene, const Frame& frame, const rapidjson::Value& request) {
  const auto metric = string_of(request, "metric", "skew");
  if (metric != "skew" && metric != "arrival" && metric != "sinks" && metric != "slew") {
    return error("metric is one of skew, arrival, sinks, slew");
  }
  const auto tree = clock_tree(scene);
  int        bins = 0;
  if (request.IsObject() && request.HasMember("bins") && request["bins"].IsInt()) {
    bins = std::clamp(request["bins"].GetInt(), 2, 256);
  } else {
    // A bin and its neighbours should hold a few leaves' worth of endpoints: finer, and no bin sees a spread at all.
    size_t endpoints = 0;
    for (const auto& n : scene.clock_nets) {
      endpoints += size_t(std::ranges::count_if(n.sinks, [&](int sink) { return endpoint(tree, sink); }));
    }
    bins = std::clamp(int(std::lround(std::sqrt(double(endpoints) / 2))), 2, 64);
  }
  const Places at(scene, frame);
  const double xl = scene.die.xl / at.dbu, yl = scene.die.yl / at.dbu, xh = scene.die.xh / at.dbu, yh = scene.die.yh / at.dbu;
  if (!(xh > xl) || !(yh > yl)) {
    return error("the scene has no die");
  }
  const int           bx = bins, by = bins;
  std::vector<Span>   spans(size_t(bx) * size_t(by));
  std::vector<double> slew(size_t(bx) * size_t(by), -1);
  std::vector<size_t> count(size_t(bx) * size_t(by), 0);
  Span                all;
  for (size_t net = 0; net < scene.clock_nets.size(); ++net) {
    const auto& n = scene.clock_nets[net];
    for (size_t k = 0; k < n.sinks.size(); ++k) {
      const int node = n.sinks[k];
      if (!endpoint(tree, node) || !at.has(node)) {
        continue;
      }
      const int    i   = std::clamp(int((at.x(node) - xl) / (xh - xl) * bx), 0, bx - 1);
      const int    j   = std::clamp(int((at.y(node) - yl) / (yh - yl) * by), 0, by - 1);
      const size_t bin = size_t(i) * size_t(by) + size_t(j);
      ++count[bin];
      spans[bin].add(arrival_of(n, k));
      all.add(arrival_of(n, k));
      slew[bin] = std::max(slew[bin], n.far_slew_ps);
    }
  }
  std::vector<double> v(spans.size(), kUnknown);
  for (int i = 0; i < bx; ++i) {
    for (int j = 0; j < by; ++j) {
      const size_t bin = size_t(i) * size_t(by) + size_t(j);
      if (!count[bin]) {
        continue;
      }
      if (metric == "sinks") {
        v[bin] = double(count[bin]);
      } else if (metric == "slew") {
        v[bin] = slew[bin] < 0 ? kUnknown : slew[bin];
      } else if (metric == "arrival") {
        // Mean would hide a late leaf sharing the bin: the latest endpoint, relative to the earliest in the design.
        v[bin] = spans[bin].n ? spans[bin].hi - all.lo : kUnknown;
      } else {
        // Local skew: the arrival spread among endpoints of this bin and the eight around it. Neighbours exchange data,
        // so this is the skew a short path sees; the global number mostly compares flops that never talk.
        Span local;
        for (int di = -1; di <= 1; ++di) {
          for (int dj = -1; dj <= 1; ++dj) {
            if (i + di < 0 || j + dj < 0 || i + di >= bx || j + dj >= by) {
              continue;
            }
            const auto& s = spans[size_t(i + di) * size_t(by) + size_t(j + dj)];
            if (s.n) {
              local.add(s.lo);
              local.add(s.hi);
            }
          }
        }
        v[bin] = local.width();
      }
    }
  }
  Span range;
  for (const double value : v) {
    range.add(value);
  }
  rapidjson::StringBuffer buf;
  Writer                  w(buf);
  w.StartObject();
  w.Key("ok");
  w.Bool(true);
  w.Key("metric");
  text(w, metric);
  w.Key("unit");
  w.String(metric == "sinks" ? "endpoints" : "ps");
  w.Key("bx");
  w.Int(bx);
  w.Key("by");
  w.Int(by);
  w.Key("die_um");
  w.StartArray();
  for (const double c : {xl, yl, xh, yh}) {
    rounded(w, c);
  }
  w.EndArray();
  w.Key("min");
  rounded(w, range.n ? range.lo : kUnknown);
  w.Key("max");
  rounded(w, range.n ? range.hi : kUnknown);
  if (range.n) {
    const auto   peak = std::ranges::max_element(v, [](double a, double b) {
      return (std::isfinite(a) ? a : -1e300) < (std::isfinite(b) ? b : -1e300);
    });
    const size_t bin  = size_t(peak - v.begin());
    w.Key("peak_um");  // the centre of the worst bin
    w.StartArray();
    rounded(w, xl + (double(bin / size_t(by)) + .5) * (xh - xl) / bx);
    rounded(w, yl + (double(bin % size_t(by)) + .5) * (yh - yl) / by);
    w.EndArray();
  }
  w.Key("v");
  w.StartArray();
  for (const double value : v) {
    rounded(w, value);
  }
  w.EndArray();
  w.EndObject();
  return buf.GetString();
}

// The page's copy of the tree: per net [driver, level, [sinks], [arrival or null], stage_delay, far_slew, load, drive_steps].
std::string view(const Scene& scene, const Frame& frame) {
  const auto              tree = clock_tree(scene);
  Span                    all, slews;
  int                     levels = 0;
  rapidjson::StringBuffer buf;
  Writer                  w(buf);
  w.StartObject();
  w.Key("ok");
  w.Bool(true);
  w.Key("frame");
  w.Uint64(frame.id);
  w.Key("handoff");
  text(w, scene.clock_report);
  w.Key("nodes");  // the scene these indices belong to: a page showing another design must not draw them
  w.Uint64(scene.nodes.size());
  w.Key("nets");
  w.StartArray();
  for (size_t net = 0; net < scene.clock_nets.size(); ++net) {
    const auto& n = scene.clock_nets[net];
    levels        = std::max(levels, tree.level[net] + 1);
    w.StartObject();
    w.Key("d");
    w.Int(n.driver);
    w.Key("l");
    w.Int(tree.level[net]);
    w.Key("r");
    w.Int(tree.root[net]);
    w.Key("s");
    w.StartArray();
    for (const int sink : n.sinks) {
      w.Int(sink);
    }
    w.EndArray();
    w.Key("e");  // 1: the load is an endpoint, 0: it drives another clock net
    w.StartArray();
    for (const int sink : n.sinks) {
      w.Int(endpoint(tree, sink) ? 1 : 0);
    }
    w.EndArray();
    w.Key("a");
    w.StartArray();
    for (size_t k = 0; k < n.sinks.size(); ++k) {
      const double t = arrival_of(n, k);
      if (endpoint(tree, n.sinks[k])) {
        all.add(t);
      }
      if (std::isfinite(t)) {
        w.Double(std::round(t * 10) / 10);
      } else {
        w.Null();
      }
    }
    w.EndArray();
    w.Key("dl");
    rounded(w, n.stage_delay_ps < 0 ? kUnknown : n.stage_delay_ps);
    w.Key("sl");
    rounded(w, n.far_slew_ps < 0 ? kUnknown : n.far_slew_ps);
    w.Key("ld");
    rounded(w, n.load_ff < 0 ? kUnknown : n.load_ff);
    w.Key("ds");
    w.Int(n.drive_steps);
    w.EndObject();
    if (n.far_slew_ps >= 0) {
      slews.add(n.far_slew_ps);
    }
  }
  w.EndArray();
  w.Key("levels");
  w.Int(levels);
  w.Key("arrival");
  write(w, all);
  w.Key("slew_max_ps");
  rounded(w, slews.n ? slews.hi : kUnknown);
  w.EndObject();
  return buf.GetString();
}
}  // namespace

std::string clock_query(const Scene& scene, const Frame& frame, const rapidjson::Value& request) {
  const auto what = string_of(request, "what", "summary");
  if (what == "summary") {
    return summary(scene, frame, request);
  }
  if (scene.clock_nets.empty()) {
    return error("no native clock tree in this phase (clock what=summary says why)");
  }
  if (what == "nets") {
    return nets(scene, frame, request);
  }
  if (what == "sinks") {
    return sinks(scene, frame, request);
  }
  if (what == "pair") {
    return pair(scene, frame, request);
  }
  if (what == "map") {
    return map(scene, frame, request);
  }
  if (what == "view") {
    return view(scene, frame);
  }
  return error("clock what= is one of summary, nets, sinks, pair, map, view");
}

std::string clock_edits_json(const Clock_edits& edits) {
  rapidjson::StringBuffer buf;
  Writer                  w(buf);
  w.StartObject();
  w.Key("drive");
  w.StartObject();
  for (const auto& [pin, step] : edits.drive) {
    w.Key(pin.c_str());
    w.Int(step);
  }
  w.EndObject();
  w.Key("wanted_ps");
  w.StartObject();
  for (const auto& [pin, ps] : edits.wanted_ps) {
    w.Key(pin.c_str());
    w.Double(ps);
  }
  w.EndObject();
  w.Key("options");
  w.StartObject();
  for (const auto& [key, value] : edits.options) {
    w.Key(key.c_str());
    text(w, value);
  }
  w.EndObject();
  w.EndObject();
  return buf.GetString();
}

std::string clock_edit(const Scene& scene, Clock_edits& edits, const rapidjson::Value& request, bool& changed) {
  changed       = false;
  const auto op = string_of(request, "op", "show");
  const auto ok = [&](const std::string& note = {}) {
    return std::format(R"({{"ok":true,"staged":{},"note":"{}"}})",
                       clock_edits_json(edits),
                       note.empty() ? "staged only: clock_edit op=apply restarts the flow with these decisions" : note);
  };
  if (op == "show") {
    return ok();
  }
  if (op == "clear") {
    changed = !edits.empty();
    edits   = {};
    return ok("cleared");
  }
  if (op == "option") {
    // The scalar knobs of the tree build. File-valued inputs are written from the staged decisions at apply.
    static const std::set<std::string> knobs{"balance",
                                             "source_fork",
                                             "leaf_fanout",
                                             "leaf_radius_um",
                                             "max_slew_ps",
                                             "leaf_drive_steps",
                                             "skew_rounds",
                                             "skew_period_fraction",
                                             "skew_hold_margin_ps",
                                             "hold_area_um2",
                                             "cluster_strength",
                                             "gate_merge_trials",
                                             "gate_leaf_trials",
                                             "gate_clone_trials",
                                             "gate_raise_trials"};
    auto                               key = string_of(request, "key");
    for (const char* prefix : {"synth.place.pcrs.", "place.pcrs.", "pcrs."}) {
      if (key.starts_with(prefix)) {
        key = key.substr(std::string_view(prefix).size());
        break;
      }
    }
    if (!knobs.contains(key)) {
      std::string list;
      for (const auto& k : knobs) {
        list += (list.empty() ? "" : ", ") + k;
      }
      return error("option needs key, one of: " + list);
    }
    if (!request.HasMember("value")) {
      changed = edits.options.erase("synth.place.pcrs." + key) > 0;
      return ok("removed");
    }
    const auto& value = request["value"];
    std::string spelled;
    if (value.IsBool()) {
      spelled = value.GetBool() ? "true" : "false";
    } else if (value.IsInt64()) {
      spelled = std::to_string(value.GetInt64());
    } else if (value.IsNumber() && std::isfinite(value.GetDouble())) {
      spelled = std::format("{}", value.GetDouble());
    } else if (value.IsString() && value.GetStringLength()
               && std::string_view(value.GetString()).find_first_of(" \t\n=") == std::string_view::npos) {
      spelled = value.GetString();
    } else {
      return error("option value must be a boolean, a number or a plain word");
    }
    edits.options["synth.place.pcrs." + key] = spelled;
    changed                                  = true;
    return ok();
  }
  if (scene.clock_nets.empty()) {
    return error("no native clock tree in this phase: there is no leaf or pin to schedule");
  }
  const auto                          tree = clock_tree(scene);
  // The loads an edit names: `pins` (clock loads) or `leaf` (a leaf driver instance or a net index: all its loads).
  std::vector<std::pair<int, size_t>> targets;
  if (request.HasMember("leaf")) {
    int net = -1;
    if (request["leaf"].IsInt()) {
      net = request["leaf"].GetInt();
    } else if (request["leaf"].IsString()) {
      const int node = scene.node_index(request["leaf"].GetString());
      net            = node >= 0 && size_t(node) < tree.driven.size() ? tree.driven[size_t(node)] : -1;
    }
    if (net < 0 || size_t(net) >= scene.clock_nets.size()) {
      return error("leaf names no clock net (a leaf driver instance, or the net index from clock what=nets)");
    }
    for (size_t k = 0; k < scene.clock_nets[size_t(net)].sinks.size(); ++k) {
      if (endpoint(tree, scene.clock_nets[size_t(net)].sinks[k])) {
        targets.emplace_back(net, k);
      }
    }
    if (targets.empty()) {
      return error("that net drives clock buffers only: schedule a leaf");
    }
  } else if (request.HasMember("pins") && request["pins"].IsArray()) {
    for (const auto& pin : request["pins"].GetArray()) {
      const auto found = pin.IsString() ? find_sink(scene, tree, pin.GetString()) : std::nullopt;
      if (!found) {
        return error(std::format("not a clock load in this tree: {}", pin.IsString() ? pin.GetString() : "(not a string)"));
      }
      targets.push_back(*found);
    }
  }
  if (targets.empty()) {
    return error("clock_edit needs leaf (a leaf driver or net index) or pins (clock loads)");
  }
  for (const auto& [net, k] : targets) {
    if (pin_of(scene.clock_nets[size_t(net)], k).empty()) {
      return error("the clock handoff does not name " + node_name(scene, scene.clock_nets[size_t(net)].sinks[k])
                   + ": it cannot be scheduled (retained and gate-driven loads keep their source)");
    }
  }
  // Every staged pin of the leaves an edit touches: one request replaces what was staged for the same leaf.
  const auto drop_leaf = [&](auto& staged, int net) {
    for (size_t k = 0; k < scene.clock_nets[size_t(net)].sinks.size(); ++k) {
      staged.erase(pin_of(scene.clock_nets[size_t(net)], k));
    }
  };
  if (op == "drive") {
    if (!request.HasMember("step") || !request["step"].IsInt() || request["step"].GetInt() < 0 || request["step"].GetInt() > 16) {
      return error("drive needs step: the absolute leaf drive step, 0 (the default driver) to 16");
    }
    // A leaf has one driver: one pin stands for it, and a second request on the same leaf replaces the first.
    std::set<int> leaves;
    for (const auto& [net, k] : targets) {
      leaves.insert(net);
    }
    for (const int net : leaves) {
      drop_leaf(edits.drive, net);
      const auto& n = scene.clock_nets[size_t(net)];
      for (size_t k = 0; k < n.sinks.size(); ++k) {
        if (endpoint(tree, n.sinks[k]) && !pin_of(n, k).empty()) {
          edits.drive[pin_of(n, k)] = request["step"].GetInt();
          break;
        }
      }
    }
    changed = true;
    return ok();
  }
  if (op == "arrival") {
    if (!request.HasMember("wanted_ps") || !request["wanted_ps"].IsNumber() || !std::isfinite(request["wanted_ps"].GetDouble())) {
      return error("arrival needs wanted_ps: the arrival wanted for these loads relative to the rest of their clock, in ps");
    }
    for (const auto& [net, k] : targets) {
      edits.wanted_ps[pin_of(scene.clock_nets[size_t(net)], k)] = request["wanted_ps"].GetDouble();
    }
    changed = true;
    return ok(edits.options.contains("synth.place.pcrs.balance")
                  ? ""
                  : "staged; wanted arrivals shape the tree only with balancing on (clock_edit op=option key=balance value=true)");
  }
  if (op == "remove") {
    for (const auto& [net, k] : targets) {
      const auto& pin  = pin_of(scene.clock_nets[size_t(net)], k);
      changed         |= edits.wanted_ps.erase(pin) > 0;
    }
    std::set<int> leaves;
    for (const auto& [net, k] : targets) {
      leaves.insert(net);
    }
    for (const int net : leaves) {
      const auto before = edits.drive.size();
      drop_leaf(edits.drive, net);
      changed |= edits.drive.size() != before;
    }
    return ok("removed");
  }
  return error("clock_edit op= is one of drive, arrival, option, remove, clear, show, apply");
}

Clock_restart clock_restart(const Scene& scene, const Clock_edits& edits, const std::string& directory) {
  Clock_restart out;
  out.set = edits.options;
  if (!edits.drive.empty()) {
    // The schedule the tree was built with stays, except on leaves the staged edits name.
    auto       schedule = scene.clock_schedule;
    const auto tree     = clock_tree(scene);
    for (const auto& [pin, step] : edits.drive) {
      if (const auto found = find_sink(scene, tree, pin)) {
        const auto& n = scene.clock_nets[size_t(found->first)];
        for (size_t k = 0; k < n.sinks.size(); ++k) {
          schedule.erase(pin_of(n, k));
        }
      }
    }
    for (const auto& [pin, step] : edits.drive) {
      schedule[pin] = step;
    }
    rapidjson::StringBuffer buf;
    Writer                  w(buf);
    w.StartObject();
    for (const auto& [pin, step] : schedule) {
      w.Key(pin.c_str());
      w.Uint(unsigned(std::max(step, 0)));
    }
    w.EndObject();
    const auto path = directory + "/gui_clock_leaf_schedule.json";
    out.files.emplace_back(path, buf.GetString());
    out.set["synth.place.pcrs.leaf_schedule"] = path;
  }
  if (!edits.wanted_ps.empty()) {
    rapidjson::StringBuffer buf;
    Writer                  w(buf);
    w.StartObject();
    for (const auto& [pin, ps] : edits.wanted_ps) {
      w.Key(pin.c_str());
      w.StartObject();
      w.Key("wanted_ps");
      w.Double(ps);
      w.EndObject();
    }
    w.EndObject();
    const auto path = directory + "/gui_clock_arrival_targets.json";
    out.files.emplace_back(path, buf.GetString());
    out.set["synth.place.pcrs.arrival_targets"] = path;
  }
  return out;
}

}  // namespace livehd::placement::gui
