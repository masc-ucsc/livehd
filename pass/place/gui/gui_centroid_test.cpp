// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gui_centroid.hpp"

#include "core/metadata.hpp"
#include "gtest/gtest.h"

using namespace livehd::placement::gui;
namespace {
rapidjson::Document request(const char* text) {
  rapidjson::Document d;
  d.Parse(text);
  EXPECT_FALSE(d.HasParseError()) << text;
  return d;
}
const std::string kOther = R"([{"class":"foreign","payload":7},
  {"class":"livehd.placement.hints","schema_version":2,"id":"mine","design":"top","templates":[{"module":"top","hints":[{"id":"x","kind":"cluster","group":"g"}]}]}])";
Centroid_edit     edit(const std::string& intent, const char* text, std::vector<std::string> selection = {}) {
  const auto doc = request(text);
  return edit_centroids(intent, "top", "top", doc, selection, 0.1, [](const std::string& id) {
    return id == "pcrs:7" ? std::vector<std::string>{"u.f1", "u.f2"} : std::vector<std::string>{};
  });
}
std::vector<std::string> ids(const std::string& intent) {
  rapidjson::Document d;
  d.Parse(list_centroid_records(intent, "top").c_str());
  std::vector<std::string> out;
  for (const auto& c : d["centroids"].GetArray()) {
    out.emplace_back(c["id"].GetString());
  }
  return out;
}
}  // namespace

TEST(GuiCentroid, CreateWritesOneRecordAndKeepsEveryOtherRecord) {
  const auto out
      = edit(kOther, R"({"op":"create","id":"flops","members":["u.f0","u.f1"],"layout":"row","position":[10,20],"strength":0.3})");
  livehd::metadata::Bundle bundle(out.intent);
  EXPECT_EQ(bundle.select("livehd.placement.hints", "top").size(), 2u);
  EXPECT_NE(out.intent.find("\"foreign\""), std::string::npos);
  EXPECT_NE(out.intent.find("\"kind\":\"cluster\""), std::string::npos);
  EXPECT_EQ(ids(out.intent), (std::vector<std::string>{"flops"}));
  rapidjson::Document reply;
  reply.Parse(out.reply.c_str());
  EXPECT_TRUE(reply["ok"].GetBool());
  const auto& c = reply["centroids"][0];
  EXPECT_STREQ(c["layout"]["kind"].GetString(), "row");
  EXPECT_STREQ(c["position"]["mode"].GetString(), "at");
  EXPECT_DOUBLE_EQ(c["position"]["x_um"].GetDouble(), 10);
  EXPECT_DOUBLE_EQ(c["strength"].GetDouble(), .3);
}
TEST(GuiCentroid, CreateFromTheSelectionAndAMadeId) {
  const auto out = edit("", R"({"op":"create","members":"selected"})", {"a", "b", "c"});
  EXPECT_EQ(ids(out.intent), (std::vector<std::string>{"c1"}));
  EXPECT_THROW(edit("", R"({"op":"create","members":"selected"})"), std::invalid_argument);
}
TEST(GuiCentroid, AddRemoveMoveAndDeleteKeepOneOwnerPerMember) {
  auto text = edit("", R"({"op":"create","id":"a","members":["f0","f1"]})").intent;
  text      = edit(text, R"({"op":"create","id":"b","members":["f2"]})").intent;
  text      = edit(text, R"({"op":"add","id":"b","members":["f1"]})").intent;  // f1 leaves a
  auto out  = edit(text, R"({"op":"move","to":"a","members":["f2","f1"]})");   // b is emptied and goes away
  EXPECT_EQ(ids(out.intent), (std::vector<std::string>{"a"}));
  rapidjson::Document reply;
  reply.Parse(out.reply.c_str());
  EXPECT_EQ(reply["centroids"][0]["members"].Size(), 3u);
  out = edit(out.intent, R"({"op":"remove","id":"a","members":["f0"]})");
  EXPECT_EQ(ids(out.intent), (std::vector<std::string>{"a"}));
  EXPECT_THROW(edit(out.intent, R"({"op":"remove","id":"a","members":["zzz"]})"), std::invalid_argument);
  out = edit(out.intent, R"({"op":"delete","id":"a"})");
  EXPECT_TRUE(ids(out.intent).empty());
}
TEST(GuiCentroid, SetChangesOnlyTheGivenFieldsAndScalesTheStrength) {
  auto text = edit("", R"({"op":"create","id":"a","members":["f0","f1","f2","f3"],"strength":0.2})").intent;
  auto out  = edit(text, R"({"op":"set","id":"a","layout":{"kind":"array","columns":2,"gap_x_um":1},"clock":{"arrival_ps":120}})");
  rapidjson::Document reply;
  reply.Parse(out.reply.c_str());
  auto& c = reply["centroids"][0];
  EXPECT_STREQ(c["layout"]["kind"].GetString(), "array");
  EXPECT_EQ(c["layout"]["columns"].GetInt(), 2);
  EXPECT_DOUBLE_EQ(c["strength"].GetDouble(), .2);
  EXPECT_DOUBLE_EQ(c["clock"]["arrival_ps"].GetDouble(), 120);
  out = edit(out.intent, R"({"op":"set","id":"a","strength_scale":2,"clock":{"drive":"BUFx4"}})");
  reply.Parse(out.reply.c_str());
  EXPECT_DOUBLE_EQ(reply["centroids"][0]["strength"].GetDouble(), .4);
  EXPECT_DOUBLE_EQ(reply["centroids"][0]["clock"]["arrival_ps"].GetDouble(), 120);  // the earlier field stays
  EXPECT_STREQ(reply["centroids"][0]["clock"]["drive"].GetString(), "BUFx4");
  // A scale on a centroid with no strength starts from the stage default the caller gives.
  text = edit("", R"({"op":"create","id":"b","members":["g0","g1"]})").intent;
  out  = edit(text, R"({"op":"set","id":"b","strength_scale":0.5})");
  reply.Parse(out.reply.c_str());
  EXPECT_DOUBLE_EQ(reply["centroids"][0]["strength"].GetDouble(), .05);
}
TEST(GuiCentroid, NestingAndRelativePositionsAreValidatedAsASet) {
  auto text = edit("", R"({"op":"create","id":"inner","members":["f0","f1"]})").intent;
  text      = edit(text, R"({"op":"create","id":"outer","members":["f2",{"centroid":"inner"}],"layout":"row"})").intent;
  text = edit(text, R"({"op":"create","id":"side","members":["f3"],"position":{"mode":"relative","to":"outer","dx_um":5}})").intent;
  EXPECT_EQ(ids(text).size(), 3u);
  // A position on a nested centroid, a cycle and an unknown target are refused, and the bundle is not changed.
  EXPECT_THROW(edit(text, R"({"op":"set","id":"inner","position":[1,2]})"), std::invalid_argument);
  EXPECT_THROW(edit(text, R"({"op":"set","id":"outer","position":{"mode":"relative","to":"side"}})"), std::invalid_argument);
  EXPECT_THROW(edit(text, R"({"op":"set","id":"side","position":{"mode":"relative","to":"nowhere"}})"), std::invalid_argument);
  EXPECT_THROW(edit(text, R"({"op":"create","id":"c","members":["f9"],"layout":"array"})"), std::invalid_argument);
  // Deleting the target of a relative position frees the centroid placed relative to it.
  const auto          out = edit(text, R"({"op":"delete","id":"outer"})");
  rapidjson::Document reply;
  reply.Parse(out.reply.c_str());
  for (const auto& c : reply["centroids"].GetArray()) {
    if (std::string_view(c["id"].GetString()) == "side") {
      EXPECT_FALSE(c.HasMember("position"));
    }
  }
}
TEST(GuiCentroid, AdoptWritesAGeneratedGroupAsAnAuthoredOne) {
  const auto out = edit("", R"({"op":"adopt","id":"pcrs:7"})");
  EXPECT_EQ(ids(out.intent), (std::vector<std::string>{"pcrs_7"}));
  rapidjson::Document reply;
  reply.Parse(out.reply.c_str());
  EXPECT_EQ(reply["centroids"][0]["members"].Size(), 2u);
  EXPECT_FALSE(reply["centroids"][0].HasMember("generated"));
  EXPECT_THROW(edit("", R"({"op":"adopt","id":"pcrs:99"})"), std::invalid_argument);
  EXPECT_THROW(edit(out.intent, R"({"op":"adopt","id":"pcrs:7"})"), std::invalid_argument);  // already adopted
  EXPECT_THROW(edit("", R"({"op":"spin"})"), std::invalid_argument);
}
