// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "physical_rc.hpp"

#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
namespace livehd::physical {
void validate_rc_tree(const Rc_net& net) {
  const auto fail = [&] { throw std::invalid_argument("placement RC must be a connected tree: " + net.driver); };
  if (net.nodes.empty()) {
    fail();
  }
  std::vector<size_t>   parent(net.nodes.size());
  std::vector<unsigned> rank(net.nodes.size());
  std::iota(parent.begin(), parent.end(), 0);
  const auto root = [&](size_t node) {
    while (parent[node] != node) {
      parent[node] = parent[parent[node]];
      node         = parent[node];
    }
    return node;
  };
  for (const auto& edge : net.edges) {
    if (edge.from >= parent.size() || edge.to >= parent.size()) {
      fail();
    }
    auto a = root(edge.from), b = root(edge.to);
    if (a == b) {
      fail();
    }
    if (rank[a] < rank[b]) {
      std::swap(a, b);
    }
    parent[b]  = a;
    rank[a]   += rank[a] == rank[b];
  }
  if (net.edges.size() != net.nodes.size() - 1) {
    fail();
  }
}
std::string serialize_parasitics(const Parasitics& rc) {
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
  w.StartObject();
  w.Key("version");
  w.Int(3);
  w.Key("identity");
  w.String(rc.identity.c_str());
  w.Key("approximate");
  w.Bool(rc.approximate);
  w.Key("timing_cuts");
  w.StartArray();
  for (const auto& driver : rc.timing_cuts) {
    w.String(driver.c_str());
  }
  w.EndArray();
  w.Key("cells");
  w.StartArray();
  for (const auto& c : rc.cells) {
    w.StartArray();
    w.String(c.name.c_str());
    w.Int64(c.x);
    w.Int64(c.y);
    w.Uint(c.orientation);
    w.String(c.master.c_str());
    w.Int64(c.width);
    w.Int64(c.height);
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
    w.Key("wire_length_um");
    w.Double(net.wire_length_um);
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
static Parasitics parse_parasitics(std::string_view text, bool full_precision) {
  const auto require = [](bool ok) {
    if (!ok) {
      throw std::runtime_error("invalid persisted placement parasitics");
    }
  };
  rapidjson::Document json;
  if (full_precision) {
    json.Parse<rapidjson::kParseFullPrecisionFlag>(text.data(), text.size());
  } else {
    json.Parse(text.data(), text.size());
  }
  require(!json.HasParseError() && json.IsObject());
  require(json.HasMember("version") && json["version"].IsInt() && json["version"].GetInt() >= 1 && json["version"].GetInt() <= 3);
  require(json.HasMember("identity") && json["identity"].IsString() && json.HasMember("approximate")
          && json["approximate"].IsBool());
  require(json.HasMember("cells") && json["cells"].IsArray() && json.HasMember("terminals") && json["terminals"].IsArray()
          && json.HasMember("nets") && json["nets"].IsArray());
  Parasitics result;
  result.identity    = json["identity"].GetString();
  result.approximate = json["approximate"].GetBool();
  if (json["version"].GetInt() >= 3 || json.HasMember("timing_cuts")) {
    require(json.HasMember("timing_cuts") && json["timing_cuts"].IsArray());
    for (const auto& driver : json["timing_cuts"].GetArray()) {
      require(driver.IsString() && driver.GetStringLength() > 0);
      result.timing_cuts.emplace(driver.GetString());
    }
  }
  for (const auto& cell : json["cells"].GetArray()) {
    require(cell.IsArray() && (cell.Size() == 4 || cell.Size() == 7) && cell[0].IsString() && cell[1].IsInt64() && cell[2].IsInt64()
            && cell[3].IsUint() && cell[3].GetUint() < 8);
    Parasitics::Cell c;
    c.name        = cell[0].GetString();
    c.x           = cell[1].GetInt64();
    c.y           = cell[2].GetInt64();
    c.orientation = cell[3].GetUint();
    if (cell.Size() == 7) {
      require(cell[4].IsString() && cell[5].IsInt64() && cell[6].IsInt64() && cell[5].GetInt64() >= 0 && cell[6].GetInt64() >= 0);
      c.master = cell[4].GetString();
      c.width  = cell[5].GetInt64();
      c.height = cell[6].GetInt64();
    }
    result.cells.push_back(std::move(c));
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
    if (net.HasMember("wire_length_um")) {
      require(net["wire_length_um"].IsNumber() && std::isfinite(net["wire_length_um"].GetDouble())
              && net["wire_length_um"].GetDouble() >= 0);
      output.wire_length_um = net["wire_length_um"].GetDouble();
    }
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
Parasitics deserialize_parasitics(std::string_view text) { return parse_parasitics(text, false); }
Parasitics deserialize_parasitics_exact(std::string_view text) { return parse_parasitics(text, true); }
}  // namespace livehd::physical
