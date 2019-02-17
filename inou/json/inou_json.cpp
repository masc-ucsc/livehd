//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Created by birdeclipse on 12/18/17.
//

#include <fstream>

#include "absl/strings/substitute.h"

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/prettywriter.h"

#include "eprp_utils.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include "inou_json.hpp"

void setup_inou_json() {
  Inou_json p;
  p.setup();
}

Inou_json::Inou_json()
    : Pass("json") {
}

void Inou_json::setup() {
  Eprp_method m1("inou.json.tolg", "import from json to lgraph", &Inou_json::tolg);
  register_inou(m1);

  Eprp_method m2("inou.json.fromlg", "export from lgraph to json", &Inou_json::fromlg);
  register_inou(m2);
}

void Inou_json::from_json(LGraph *g, rapidjson::Document &document) {
  Index_ID last_nid = 0;
  Index_ID dst_nid  = 0;
  Port_ID  src_pid  = 0;
  Port_ID  dst_pid  = 0;

  json_remap.clear();

  if(document.HasParseError()) {
    fprintf(stderr, "\nError(offset %u): %s\n", static_cast<unsigned>(document.GetErrorOffset()),
            rapidjson::GetParseError_En(document.GetParseError()));
    // ...
  } else {
    assert(document.HasMember("nodes"));
    const rapidjson::Value &nodesArray = document["nodes"];
    assert(nodesArray.IsArray());
    for(const auto &nodes : nodesArray.GetArray()) {
      assert(nodes.IsObject());

      assert(nodes.HasMember("idx"));
      last_nid = nodes["idx"].GetUint64();
      if(json_remap.find(last_nid) == json_remap.end()) {
        json_remap[last_nid] = g->create_node().get_nid();
      }
      last_nid = json_remap[last_nid];

      if(nodes.HasMember("inputs")) {
        assert(nodes["inputs"].IsArray());
        for(const auto &input_edge : nodes["inputs"].GetArray()) {
          assert(input_edge.IsObject());
          if(input_edge.HasMember("inp_in_pid")) {
            fmt::print("DEBUG:: inp_in_pid {} \n", input_edge["inp_in_pid"].GetUint64());
          }
          if(input_edge.HasMember("inp_nid")) {
            fmt::print("DEBUG:: inp_nid {} \n", input_edge["inp_nid"].GetUint64());
          }
          // if(input_edge.HasMember("inp_out_pid")) {
          //  fmt::print("DEBUG:: inp_out_pid {} \n", input_edge["inp_out_pid"].GetUint64());
          //}
          if(input_edge.HasMember("inp_dst_pid")) {
            fmt::print("DEBUG:: inp_dst_pid {} \n", input_edge["inp_dst_pid"].GetUint64());
          }
        }
      }
      if(nodes.HasMember("op")) {
        if(nodes["op"].IsString()) {
          std::string op = nodes["op"].GetString();
          if(Node_Type::is_type(op)) {
            g->node_type_set(last_nid, Node_Type::get(op));
          } else if(is_const_op(op)) {
            fmt::print("DEBUG:: const op : {} \n", op);
            assert(op.size() > 2);
            assert(op[op.size() - 1] == '\'');
            assert(op[op.size() - 1] == '\'');
            g->node_const_type_set(last_nid, op.substr(1, op.size() - 3));
          } else {
            fmt::print("DEBUG:: HOW TO GET HERE?? \n ");
          }
        } else {
          uint32_t val = nodes["op"].GetUint();
          g->node_u32type_set(last_nid, val);
        }
      }

      if(nodes.HasMember("input_name")) {
        fmt::print("DEBUG:: input name is : {} \n", nodes["input_name"].GetString());
        g->add_graph_input(nodes["input_name"].GetString(), last_nid, 0, 0); // FIXME: set original_pos and bits
      }

      if(nodes.HasMember("output_name")) {
        fmt::print("DEBUG:: output name is : {} \n", nodes["output_name"].GetString());
        g->add_graph_output(nodes["output_name"].GetString(), last_nid, 0, 0); // FIXME: must remember original_pos and set bits
      }

      if(nodes.HasMember("outputs")) {
        assert(nodes["outputs"].IsArray());
        for(const auto &output_edge : nodes["outputs"].GetArray()) {
          assert(output_edge.IsObject());
          // if(output_edge.HasMember("out_out_pid")) {
          //  src_pid = output_edge["out_out_pid"].GetUint();
          //}
          if(output_edge.HasMember("driver_pid")) {
            src_pid = output_edge["driver_pid"].GetUint();
          }
          if(output_edge.HasMember("sink_idx")) {
            dst_nid = output_edge["sink_idx"].GetUint64();
            if(json_remap.find(dst_nid) == json_remap.end()) {
              json_remap[dst_nid] = g->create_node().get_nid();
            }
            dst_nid = json_remap[dst_nid];
          }
          // if(output_edge.HasMember("out_inp_pid")) {
          //  dst_pid = output_edge["out_inp_pid"].GetUint();
          //}
          if(output_edge.HasMember("sink_pid")) {
            dst_pid = output_edge["sink_pid"].GetUint();
          }
          Node_pin dpin = g->get_node(last_nid).setup_driver_pin(dst_pid);
          Node_pin spin = g->get_node(dst_nid).setup_sink_pin(src_pid);
          if(output_edge.HasMember("bits")) {
            g->add_edge(dpin, spin, output_edge["bits"].GetInt());
          } else {
            g->add_edge(dpin, spin);
          }
          if(output_edge.HasMember("delay")) {
            double delay = output_edge["delay"].GetDouble();
            g->node_delay_set(last_nid, static_cast<float>(delay));
          }
        }
      }
    }
  }
}

void Inou_json::fromlg(Eprp_var &var) {
  auto odir = var.get("odir");

  Inou_json p;

  bool ok = p.setup_directory(odir);
  if(!ok)
    return;

  for(const auto &g : var.lgs) {
    auto file = absl::StrCat(odir,"/",g->get_name(),".json");

    p.to_json(g, file);
  }
}

void Inou_json::tolg(Eprp_var &var) {

  auto files = var.get("files");
  if(files.empty()) {
    error(fmt::format("inou.json.tolg: no files provided"));
    return;
  }

  Inou_json p;

  auto path = var.get("path");
  bool  ok  = p.setup_directory(path);
  if(!ok)
    return;

  std::vector<LGraph *> lgs;
  for(const auto &f : absl::StrSplit(files, ',')) {

    std::string_view name = f.substr(f.find_last_of("/\\") + 1);
    if(absl::EndsWith(name, ".json")) {
      name = name.substr(0, name.size() - 5); // remove .json
    } else {
      error(fmt::format("inou.json.tolg unknown file extension {}, expected .json", name));
      continue;
    }

    LGraph *lg = LGraph::create(path, name, f);

    std::string fname(f);
    FILE *                    pFile = fopen(fname.c_str(), "rb");
    if (pFile==0) {
      Pass::error(fmt::format("Inou_json::tolg could not open {} file",f));
      continue;
    }
    char                      buffer[65536];
    rapidjson::FileReadStream is(pFile, buffer, sizeof(buffer));
    rapidjson::Document       document;
    document.ParseStream<0, rapidjson::UTF8<>, rapidjson::FileReadStream>(is);

    p.from_json(lg, document);
    lg->sync();
    lgs.push_back(lg);
  }

  var.add(lgs);
}

bool Inou_json::is_const_op(const std::string &s) const {
  return std::find_if(s.begin(), s.end(),
                      [](const char c) { return (c != '\'' && c != '0' && c != '1' && c != 'x' && c != 'z'); }) == s.end();
}

bool Inou_json::is_int(const std::string &s) const {
  return !s.empty() && std::find_if(s.begin(), s.end(), [](char c) { return !std::isdigit(c); }) == s.end();
}

void Inou_json::to_json(const LGraph *g, const std::string &filename) const {
  rapidjson::StringBuffer                          s;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(s);
  {
    writer.StartObject();
    writer.Key("nodes");
    writer.StartArray();

    for(auto &idx : g->fast()) { // Forward would not pass flops
      writer.StartObject();
      /*first print out the node Idx*/
      writer.Key("idx");
      writer.Uint64(idx);
#if 0
      /*next print out input nodes*/
      writer.Key("inputs");
      writer.StartArray();
      {
        for(const auto &input_edge : g->inp_edges(idx)) {
          writer.StartObject();
          writer.Key("inp_nid ");
          writer.Uint64((input_edge.get_idx()));
          // writer.Key("inp_out_pid");
          writer.Key("inp_dst_pid ");
          writer.Uint64(input_edge.get_inp_pin().get_pid());
          writer.EndObject();
        }
        writer.EndArray();
      }
#endif

      writer.Key("op");
      {
        if(g->node_type_get(idx).op == U32Const_Op) {
          writer.Uint64(g->node_value_get(idx));
        } else if(g->node_type_get(idx).op == StrConst_Op) {
          std::string tmp;
          tmp.append("'");
          tmp.append(g->node_const_value_get(idx));
          tmp.append("'");
          writer.String(tmp.c_str());
        } else {
          /*normal operations*/
          if(g->node_type_get(idx).op == TechMap_Op) {
            const Tech_cell *tcell = g->get_tlibrary().get_const_cell(g->tmap_id_get(idx));
            writer.String(std::string(tcell->get_name()).c_str());
          } else {
            writer.String((g->node_type_get(idx).get_name().c_str()));
          }
        }
      }

      auto ni_name = g->get_node_instancename(idx);
      if(!ni_name.empty()) {
        writer.Key("instance_name");
        writer.String(std::string(ni_name).c_str()); // rapidjson does not support string_view
      }

      writer.Key("outputs");
      writer.StartArray();
      {
        for(const auto &out : g->out_edges(idx)) {
          writer.StartObject();

          auto wi_name = g->get_node_wirename(idx);
          if(!wi_name.empty()) {
            writer.Key("name");
            writer.String(std::string(wi_name).c_str());
          }

          writer.Key("driver_idx");
          writer.Uint64(out.get_out_pin().get_idx());
          writer.Key("driver_pid");
          writer.Uint64(out.get_out_pin().get_pid());
          writer.Key("sink_idx");
          writer.Uint64(out.get_idx());
          // writer.Key("out_inp_pid");
          writer.Key("sink_pid");
          writer.Uint64(out.get_inp_pin().get_pid());
          if(out.is_root()) {
            auto  node       = g->get_dest_node(out);
            float node_delay = node.get_delay();
            int   node_width = g->get_bits(out.get_out_pin());
            if(node_delay != 0) {
              writer.Key("delay");
              writer.Double(node_delay);
            }
            if(node_width != 0) {
              writer.Key("bits");
              writer.Uint(node_width);
            }
          }
          writer.EndObject();
        }
      }
      writer.EndArray();
      writer.Key("inputs");
      writer.StartArray();
      {
        for(const auto &inp : g->inp_edges(idx)) {
          writer.StartObject();

          auto wi_name = g->get_node_wirename(idx);
          if(!wi_name.empty()) {
            writer.Key("name");
            writer.String(std::string(wi_name).c_str());
          }

          writer.Key("sink_idx");
          writer.Uint64(inp.get_inp_pin().get_idx());
          writer.Key("sink_pid");
          writer.Uint64(inp.get_inp_pin().get_pid());

          writer.EndObject();
        }
      }
      writer.EndArray();
      writer.EndObject();
    }
    writer.EndArray();
    writer.EndObject();
  }

  std::ofstream fs;

  fs.open(filename, std::ios::out | std::ios::trunc);
  if(!fs.is_open()) {
    Pass::error("ERROR: could not open json file {}", filename);
    return;
  }
  fs << s.GetString() << std::endl;
  fs.close();
}
