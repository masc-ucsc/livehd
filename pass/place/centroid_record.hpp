// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
// Centroid helper (todo 2p-pcrs, track C): a point that pulls its members during placement.
//
// A centroid is a record of livehd.placement.hints (kind "centroid"). Its members are instances (cells, flops, memories,
// macros) named relative to the template scope, or other centroids (nesting). The record fixes where the point is (free,
// at coordinates, relative to another centroid), how the members sit around it (floating, row, column, array), how hard
// they are pulled, and the clock decisions for members that are clock sinks. The same record is written by the GUI and
// agents, by PCRS (generated records) and by hand, and every placement stage reads it. compile_centroids() turns a set of
// records into the native placer's cohesion groups: a layout is a set of target offsets from the centroid, a nested
// centroid is a member whose block keeps its own shape, and a position is a pull of the centroid toward a point.
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "dreamplace/native/placement.hpp"
#include "rapidjson/document.h"

namespace livehd::placement {
enum class Centroid_position_mode { free, at, relative };
enum class Centroid_layout_kind { floating, row, column, array };

struct Centroid_member {
  std::string name;  // qualified instance name, or the qualified id of a nested centroid
  bool        centroid                                 = false;
  // A tool that already holds the placement node (PCRS, a GUI selection) binds it here and leaves the name empty; the name is
  // asked for only when the record is written out.
  int         node                                     = -1;
  // Share of the member's area in the pull: a shared clock gate counts once over its leaves, a timing-critical flop counts less.
  double      share                                    = 1;
  bool        operator==(const Centroid_member&) const = default;
};
struct Centroid_clock {
  std::string           drive;       // leaf driver master; empty keeps the planner's choice
  std::optional<double> arrival_ps;  // wanted arrival of the members' clock pins
  bool                  operator==(const Centroid_clock&) const = default;
  bool                  empty() const { return drive.empty() && !arrival_ps; }
};
struct Centroid {
  std::string                  id;
  std::vector<Centroid_member> members;
  Centroid_position_mode       position = Centroid_position_mode::free;
  double                       x_um = 0, y_um = 0;    // position at
  std::string                  to;                    // position relative: another centroid
  double                       dx_um = 0, dy_um = 0;  // offset of this centroid from `to`
  Centroid_layout_kind         layout   = Centroid_layout_kind::floating;
  int                          columns  = 0;  // array
  double                       gap_x_um = 0, gap_y_um = 0;
  // Strength in the placer's cohesion units (inverse microns); negative takes the stage default.
  double                       strength = -1;
  Centroid_clock               clock;
  bool                         generated = false;  // formed by a tool; a field set by hand makes it authored
  std::string                  origin;             // "pcrs", "gui", "agent", "file": who wrote it last
  bool                         operator==(const Centroid&) const = default;
};
struct Centroid_issue {
  std::string id, reason;
};

// Parsing names members through two callbacks so a template scope can qualify them. The defaults keep the text.
struct Centroid_names {
  std::function<std::string(const std::vector<std::string>&)> member;
  std::function<std::string(const std::string&)>              centroid;
};
// Throws std::invalid_argument with the reason. `id` has not been qualified yet.
Centroid parse_centroid(const rapidjson::Value& record, const Centroid_names& names = {});
// The record exactly as parse_centroid reads it (ids as stored, so a qualified id is written qualified). A member bound only to
// a node is written under name_of(node).
void     write_centroid(const Centroid&, rapidjson::Value& out, rapidjson::Document::AllocatorType&,
                        const std::function<std::string(int)>& name_of = {});

class Centroid_set {
  std::vector<Centroid>         items_;
  std::map<std::string, size_t> index_;
  void                          reindex();

public:
  const std::vector<Centroid>& items() const { return items_; }
  size_t                       size() const { return items_.size(); }
  bool                         empty() const { return items_.empty(); }
  const Centroid*              find(const std::string& id) const;
  // False with the reason when the id exists or the centroid is malformed on its own (not against the others).
  bool                         add(Centroid, std::string* reason = nullptr);
  // Replace the record with the same id (authored or generated), keeping its place.
  bool                         put(Centroid, std::string* reason = nullptr);
  bool                         erase(const std::string& id);
  // Edits used by the GUI and agents. A member lives in one centroid: adding it moves it from the previous owner.
  bool                         add_member(const std::string& id, Centroid_member, std::string* reason = nullptr);
  bool                         remove_member(const std::string& id, const Centroid_member&, std::string* reason = nullptr);
  bool move_member(const std::string& from, const std::string& to, const Centroid_member&, std::string* reason = nullptr);
  // A tool-made centroid becomes authored (its members stay), as soon as someone edits a field of it.
  bool adopt(const std::string& id);
  // Replace every generated record of an origin by `fresh`. A fresh record that collides with an authored id is
  // dropped, and a fresh member already owned by an authored centroid is removed from the fresh record (authored wins).
  // Returns the ids dropped for either reason.
  // `resolve` maps an authored instance name to its node so that members compare by node (null compares names only).
  std::vector<std::string>     replace_generated(const std::string& origin, std::vector<Centroid> fresh,
                                                 const std::function<int(const std::string&)>& resolve = {});
  // Rebind generated groups after replacing the authored placement hints.
  void                         reload_authored(const Centroid_set& authored, const std::function<int(const std::string&)>& resolve);
  // Problems against the whole set: unknown nested or relative targets, cycles, a nested centroid with two parents, a member in
  // two roots, a position on a nested centroid, an empty centroid. A centroid with an issue is not compiled.
  std::vector<Centroid_issue>  validate() const;
  // Parent of a nested centroid ("" for a root).
  std::string                  parent_of(const std::string& id) const;
  // Clock fields with inheritance: a field set on a containing centroid applies to its nested ones (the nearest wins).
  Centroid_clock               effective_clock(const std::string& id) const;
  // Instances of a centroid, nested ones included, in member order.
  std::vector<Centroid_member> leaves(const std::string& id) const;
};

struct Centroid_compile_options {
  double                                 dbu              = 1;  // database units per micron
  double                                 default_strength = 0;  // used when a record has no strength; zero leaves it out
  // Instance name -> node of the design, or -1. Unknown names are reported and skipped.
  std::function<int(const std::string&)> resolve;
};
struct Centroid_result {
  std::string id;
  size_t      members = 0, unresolved = 0;
  int         group    = -1;  // first emitted group of this centroid (-1 when it emitted none)
  double      strength = 0;
  std::string reason;  // why it emitted nothing
};
struct Centroid_compiled {
  std::vector<DreamPlace::native::CohesionGroup> groups;
  std::vector<Centroid_result>                   results;
  // Largest distance (DBU) between a member's current position and the layout slot it would fill, per centroid id.
  std::map<std::string, double>                  layout_residual;
};
Centroid_compiled compile_centroids(const Centroid_set&, const DreamPlace::native::Design&, const Centroid_compile_options&);
}  // namespace livehd::placement
