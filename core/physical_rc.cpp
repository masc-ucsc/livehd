// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "physical_rc.hpp"

#include <cmath>
#include <stdexcept>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
namespace livehd::physical {
std::string serialize_parasitics(const Parasitics& rc) {
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
  w.StartObject();
  w.Key("version");
  w.Int(1);
  w.Key("identity");
  w.String(rc.identity.c_str());
  w.Key("approximate");
  w.Bool(rc.approximate);
  w.Key("cells");
  w.StartArray();
  for (const auto& c : rc.cells) {
    w.StartArray();
    w.String(c.name.c_str());
    w.Int64(c.x);
    w.Int64(c.y);
    w.Uint(c.orientation);
    w.EndArray();
  }
  w.EndArray();
  w.Key("terminals");
  w.StartArray();
  for (const auto& t : rc.terminals) {
    w.StartArray();
    w.String(t.name.c_str());
    w.Int64(t.x);
    w.Int64(t.y);
    w.EndArray();
  }
  w.EndArray();
  w.Key("nets");
  w.StartArray();
  for (const auto& net : rc.nets) {
    w.StartObject();
    w.Key("driver");
    w.String(net.driver.c_str());
    w.Key("nodes");
    w.StartArray();
    for (const auto& n : net.nodes) {
      w.StartArray();
      w.String(n.pin.c_str());
      w.Double(n.capacitance_ff);
      w.EndArray();
    }
    w.EndArray();
    w.Key("edges");
    w.StartArray();
    for (const auto& e : net.edges) {
      w.StartArray();
      w.Uint(e.from);
      w.Uint(e.to);
      w.Double(e.resistance_ohm);
      w.EndArray();
    }
    w.EndArray();
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
  return buffer.GetString();
}
Parasitics deserialize_parasitics(std::string_view text) {
  const auto require = [](bool ok) {
    if (!ok) {
      throw std::runtime_error("invalid persisted placement parasitics");
    }
  };
  rapidjson::Document json;
  json.Parse(text.data(), text.size());
  require(!json.HasParseError() && json.IsObject());
  require(json.HasMember("version") && json["version"].IsInt() && json["version"].GetInt() == 1);
  require(json.HasMember("identity") && json["identity"].IsString() && json.HasMember("approximate")
          && json["approximate"].IsBool());
  require(json.HasMember("cells") && json["cells"].IsArray() && json.HasMember("terminals") && json["terminals"].IsArray()
          && json.HasMember("nets") && json["nets"].IsArray());
  Parasitics result;
  result.identity    = json["identity"].GetString();
  result.approximate = json["approximate"].GetBool();
  for (const auto& cell : json["cells"].GetArray()) {
    require(cell.IsArray() && cell.Size() == 4 && cell[0].IsString() && cell[1].IsInt64() && cell[2].IsInt64() && cell[3].IsUint()
            && cell[3].GetUint() < 8);
    result.cells.push_back({cell[0].GetString(), cell[1].GetInt64(), cell[2].GetInt64(), cell[3].GetUint()});
  }
  for (const auto& terminal : json["terminals"].GetArray()) {
    require(terminal.IsArray() && terminal.Size() == 3 && terminal[0].IsString() && terminal[1].IsInt64() && terminal[2].IsInt64());
    result.terminals.push_back({terminal[0].GetString(), terminal[1].GetInt64(), terminal[2].GetInt64()});
  }
  for (const auto& net : json["nets"].GetArray()) {
    require(net.IsObject() && net.HasMember("driver") && net["driver"].IsString() && net.HasMember("nodes")
            && net["nodes"].IsArray() && net.HasMember("edges") && net["edges"].IsArray());
    Rc_net output;
    output.driver = net["driver"].GetString();
    for (const auto& node : net["nodes"].GetArray()) {
      require(node.IsArray() && node.Size() == 2 && node[0].IsString() && node[1].IsNumber() && node[1].GetDouble() >= 0
              && std::isfinite(node[1].GetDouble()));
      output.nodes.push_back({node[0].GetString(), node[1].GetDouble()});
    }
    for (const auto& edge : net["edges"].GetArray()) {
      require(edge.IsArray() && edge.Size() == 3 && edge[0].IsUint() && edge[1].IsUint() && edge[2].IsNumber()
              && edge[0].GetUint() < output.nodes.size() && edge[1].GetUint() < output.nodes.size() && edge[2].GetDouble() >= 0
              && std::isfinite(edge[2].GetDouble()));
      output.edges.push_back({edge[0].GetUint(), edge[1].GetUint(), edge[2].GetDouble()});
    }
    result.nets.push_back(std::move(output));
  }
  return result;
}
}  // namespace livehd::physical
