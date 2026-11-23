// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "rapidjson/document.h"

// Centroid records as the GUI page and the agent socket edit them (todo 2p-pcrs, G3). A centroid is a record of the placement
// hints (livehd.placement.hints, kind "centroid"); the GUI keeps the ones it creates in one hints record, "gui-centroids",
// and leaves every other record of the bundle exactly as it was. Everything here is a pure function of the bundle text: the
// session applies the returned bundle through the same intent path as any other hint edit, so a rejected record is reported
// by the same preview and the same collateral is what batch runs read.
namespace livehd::placement::gui {

inline constexpr const char* kCentroidRecord = "gui-centroids";

struct Centroid_edit {
  std::string intent;  // the whole updated metadata bundle (JSON text)
  std::string reply;   // JSON: {"ok":true,"op":...,"id":...,"centroids":[...]} with the record as written
};

// Applies one edit to the GUI's centroid record in `intent` (a metadata bundle) for `design`; `module` is the template module
// (the design's top module). Throws std::invalid_argument with the reason when the request is not valid; nothing is changed
// then. request.op:
//   create  {id?, members, position?, layout?, strength?, clock?}   a new centroid (an id is made when absent)
//   delete  {id}                                                    the centroid, and its nesting and relative positions
//   add     {id, members}                                           members join; a member leaves its previous centroid
//   remove  {id, members}
//   move    {to, members}                                           members join `to` from wherever they are
//   set     {id, position?, layout?, strength? | strength_scale?, clock?, members?}   change fields; a field absent stays
//   adopt   {id, as?}                                               write a generated centroid (a PCRS leaf group) as an
//                                                                   authored one with the same members, so it can be edited
// members: instance names, {"centroid": id}, the string "selected" (the page selection), or {"regex": ...} is not accepted
// here (the agent resolves it with `find` first). position: {mode: free|at|relative, x_um, y_um | to, dx_um, dy_um} or
// "at": [x_um, y_um]. layout: {kind: floating|row|column|array, columns?, gap_x_um?, gap_y_um?} or a kind name.
// strength_scale multiplies the centroid's strength (or `fallback_strength` when it has none) — "stronger" is 2, "weaker" 0.5.
Centroid_edit edit_centroids(const std::string& intent, const std::string& design, const std::string& module,
                             const rapidjson::Value& request, const std::vector<std::string>& selection, double fallback_strength,
                             const std::function<std::vector<std::string>(const std::string&)>& generated_members);

// The authored centroid records of the bundle for `design`: {"centroids":[{...record..., "record": owner id}]}.
std::string list_centroid_records(const std::string& intent, const std::string& design);

}  // namespace livehd::placement::gui
