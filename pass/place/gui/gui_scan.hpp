// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>

#include "gui_chunks.hpp"
#include "gui_session.hpp"
#include "rapidjson/document.h"

namespace livehd::placement::gui {
// The agent's `macro_scan`: rank candidate positions (and mirrored orientations) of one macro by the wirelength of its
// connected pins to the logic around it, keeping only positions the floorplan checks accept. A pure function of an
// immutable scene and frame: it holds no session lock and moves nothing.
//   {node, positions?:[[x,y],...] | grid?:{nx,ny}, orientations?:[N|FN|FS|S,...], top?, refine?, frame?:"die"|"chunk", chunk?}  (DBU, lower-left corner)
// With "frame":"chunk" positions are relative to the lower-left corner of the macro's home chunk (or the chunk named), the
// grid spans that chunk, a candidate must keep its body inside it, and every pose is reported in both frames.
// The current pose is always a candidate, so `best` is never worse than staying. `refine` (default true) walks the best
// candidate downhill in shrinking steps (at most 600 legality checks). `orientation_editable` says whether the gate the
// session is at accepts an orientation change; when it does not, other orientations are dropped and reported.
std::string macro_scan(const Scene&, const Frame&, const rapidjson::Value& request, bool orientation_editable, const Chunk_frame* chunks = nullptr);
}  // namespace livehd::placement::gui
