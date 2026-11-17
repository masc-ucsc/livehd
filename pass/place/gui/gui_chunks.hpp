// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "rapidjson/document.h"

namespace livehd::placement::gui {
// The part of the floorplan chunk plan the coordinates need: each chunk's id and box, and which chunk owns each color and
// macro (the plan's member ids are the scene's node names for macros). With it a pose can be given relative to the chunk a
// macro lives in: the same relative pose stays meaningful when the chunk moves or is reshaped, and a macro's place in its
// home does not depend on where that home sits on the die.
struct Chunk_frame {
  struct Chunk {
    std::string           id;
    std::array<double, 4> box{};  // xl, yl, xh, yh, DBU
  };
  std::vector<Chunk>                  chunks;
  std::unordered_map<std::string, int> home;  // member id -> index into chunks

  const Chunk* find(std::string_view id) const {
    for (const auto& c : chunks) {
      if (c.id == id) {
        return &c;
      }
    }
    return nullptr;
  }
  // The fp gate's coarse objects are named "macro:<id>" and "color:<id>", the plan's members "<id>".
  const Chunk* of(const std::string& member) const {
    auto it = home.find(member);
    if (it == home.end() && (member.starts_with("macro:") || member.starts_with("color:"))) {
      it = home.find(member.substr(6));
    }
    return it == home.end() ? nullptr : &chunks[size_t(it->second)];
  }
  // nullptr: the text is not a chunk plan (a design that fits in one home has none).
  static std::shared_ptr<const Chunk_frame> parse(const std::string& plan_json) {
    if (plan_json.empty()) {
      return nullptr;
    }
    rapidjson::Document d;
    d.Parse<rapidjson::kParseFullPrecisionFlag>(plan_json.c_str());
    if (d.HasParseError() || !d.IsObject() || !d.HasMember("chunks") || !d["chunks"].IsArray()) {
      return nullptr;
    }
    auto out = std::make_shared<Chunk_frame>();
    for (const auto& row : d["chunks"].GetArray()) {
      if (!row.IsObject() || !row.HasMember("id") || !row["id"].IsString() || !row.HasMember("box_dbu") || !row["box_dbu"].IsArray()
          || row["box_dbu"].Size() != 4) {
        continue;
      }
      Chunk chunk;
      chunk.id = row["id"].GetString();
      bool ok  = true;
      for (rapidjson::SizeType i = 0; i < 4; ++i) {
        ok &= row["box_dbu"][i].IsNumber();
        chunk.box[i] = ok ? row["box_dbu"][i].GetDouble() : 0;
      }
      if (!ok) {
        continue;
      }
      const int index = int(out->chunks.size());
      if (row.HasMember("members") && row["members"].IsArray()) {
        for (const auto& member : row["members"].GetArray()) {
          if (member.IsObject() && member.HasMember("id") && member["id"].IsString()) {
            out->home.emplace(member["id"].GetString(), index);
          }
        }
      }
      out->chunks.push_back(std::move(chunk));
    }
    return out->chunks.empty() ? nullptr : out;
  }
};
}  // namespace livehd::placement::gui
