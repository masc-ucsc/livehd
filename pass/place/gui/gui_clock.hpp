// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "gui_session.hpp"
#include "rapidjson/document.h"

// The native clock tree (pass.pcrs) as the placement GUI and its agent socket see it. Everything here is a pure
// function of an immutable scene and frame: no session lock, no flow access. The scene carries the topology by node
// index (Scene::clock_nets); wirelengths are measured at the frame's positions, electrical numbers are the handoff's
// estimates for the positions the tree was planned at.
namespace livehd::placement::gui {

// Adds the handoff's estimates (the pcrs_clock.json text) to scene.clock_nets, matched by driver instance, and records
// the leaf schedule the tree was built with. Returns the number of nets that got estimates. A handoff that does not
// parse, or names other instances, changes nothing: the topology stays usable.
size_t annotate_clock(Scene& scene, std::string_view handoff_json, const std::string& path);

// Net-to-net structure derived from Scene::clock_nets.
struct Clock_tree {
  std::vector<int> parent;   // net -> the net its driver is a load of; -1: a root (a port, a design cell, a gate fed by data)
  std::vector<int> root;     // net -> its root net
  std::vector<int> level;    // net -> driver stages between the root net and this one (a root is 0)
  std::vector<int> driven;   // scene node -> the clock net it drives; -1: an endpoint or not a clock cell
  std::vector<int> load_of;  // scene node -> the first clock net it is a load of; -1: none
};
Clock_tree clock_tree(const Scene& scene);

// The agent's `clock` command. request.what selects the report:
//   summary (default)  per clock root: endpoints, buffers, depth range, wire, estimated insertion and skew; the worst leaves
//   nets    {root?, sort?: radius|skew|slew|load|fanout|wire|delay, limit?}          one row per clock net
//   sinks   {root?, order?: late|early, limit?}                                        endpoints by estimated arrival
//   pair    {a, b}                 two endpoints (node name, "#index" or "instance:pin"): shared path and estimated skew
//   map     {metric?: skew|arrival|sinks|slew, bins?}   a grid over the die: the 2D picture of clock quality
//           (bins default to about two endpoints per bin, 2 to 64 per axis)
//   view    the whole tree by node index, for the page
std::string clock_query(const Scene& scene, const Frame& frame, const rapidjson::Value& request);

// The agent's `clock_edit`: {op: drive|arrival|option|remove|clear|show, ...}. Validates against the scene's tree and
// updates `edits`; the reply lists what is staged. `changed` reports whether edits were modified.
std::string clock_edit(const Scene& scene, Clock_edits& edits, const rapidjson::Value& request, bool& changed);
std::string clock_edits_json(const Clock_edits& edits);

// What a restart needs to apply the staged decisions: files to write under `directory` (path, contents) and the
// --set values naming them. The schedule extends the one the tree was built with.
struct Clock_restart {
  std::vector<std::pair<std::string, std::string>> files;
  std::map<std::string, std::string>               set;
};
Clock_restart clock_restart(const Scene& scene, const Clock_edits& edits, const std::string& directory);

// The clock layer of the software renderer and the page: trunk and leaf segments as (x0, y0, x1, y1, level) in DBU.
struct Clock_segment {
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  int    level = 0;
  bool   leaf  = false;
};
std::vector<Clock_segment> clock_segments(const Scene& scene, const Frame& frame);

}  // namespace livehd::placement::gui
