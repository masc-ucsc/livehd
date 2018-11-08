//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Created by birdeclipse on 12/18/17.
//

#include <fstream>

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/prettywriter.h"

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "eprp_utils.hpp"

#include "inou_json.hpp"

void setup_inou_json() {
  Inou_json p;
  p.setup();
}

Inou_json::Inou_json()
  :Pass("json") {

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

  if(document.HasParseError()) {
    fprintf(stderr, "\nError(offset %u): %s\n",
            static_cast<unsigned>(document.GetErrorOffset()),
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
          //if(input_edge.HasMember("inp_out_pid")) {
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
            g->node_const_type_set(last_nid, op);
          } else {
            fmt::print("DEBUG:: HOW TO GET HERE?? \n ");
          }
        } else {
          uint32_t val = nodes["op"].GetUint();
          g->node_u32type_set(last_nid, val);
        }
      }

      if(nodes.HasMember("node_bits")){
        g->set_bits(last_nid, nodes["node_bits"].GetUint());
      }

      if(nodes.HasMember("input_name")) {
        fmt::print("DEBUG:: input name is : {} \n", nodes["input_name"].GetString());
        g->add_graph_input(nodes["input_name"].GetString(), last_nid);
      }

      if(nodes.HasMember("output_name")) {
        fmt::print("DEBUG:: output name is : {} \n", nodes["output_name"].GetString());
        g->add_graph_output(nodes["output_name"].GetString(), last_nid);
      }

      if(nodes.HasMember("outputs")) {
        assert(nodes["outputs"].IsArray());
        for(const auto &output_edge : nodes["outputs"].GetArray()) {
          assert(output_edge.IsObject());
          //if(output_edge.HasMember("out_out_pid")) {
          //  src_pid = output_edge["out_out_pid"].GetUint();
          //}
          if(output_edge.HasMember("out_src_pid")) {
            src_pid = output_edge["out_src_pid"].GetUint();
          }
          if(output_edge.HasMember("out_dst_nid")) {
            dst_nid = output_edge["out_dst_nid"].GetUint64();
            if(json_remap.find(dst_nid) == json_remap.end()) {
              json_remap[dst_nid] = g->create_node().get_nid();
            }
            dst_nid = json_remap[dst_nid];
          }
          //if(output_edge.HasMember("out_inp_pid")) {
          //  dst_pid = output_edge["out_inp_pid"].GetUint();
          //}
          if(output_edge.HasMember("out_dst_pid")) {
            dst_pid = output_edge["out_dst_pid"].GetUint();
          }
          Node_Pin src_pin(last_nid, dst_pid, false);
          Node_Pin dst_pin(dst_nid, src_pid, true);
          if(output_edge.HasMember("out_src_bits")) {
            int bits = output_edge["out_src_bits"].GetInt();
            g->add_edge(src_pin, dst_pin, bits);
          } else {
            g->add_edge(src_pin, dst_pin);
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
  const std::string path    = var.get("path");
  const std::string odir    = var.get("odir");

  Inou_json p;

  bool ok = p.setup_directory(odir);
  if (!ok)
    return;

  for(const auto &g:var.lgs) {
    const std::string file = odir + "/" + g->get_name() + ".json";
    p.to_json(g, file);
  }
}

void Inou_json::tolg(Eprp_var &var) {

  const std::string files   = var.get("files");
  if (files.empty()) {
    error(fmt::format("inou.json.tolg: no files provided"));
    return;
  }

  Inou_json p;

  const std::string path   = var.get("path");
  bool ok = p.setup_directory(path);
  if (!ok)
    return;

  std::vector<LGraph *> lgs;
  for(const auto &f:Eprp_utils::parse_files(files,"inou.yosys.tolg")) {

    const std::string name = f.substr(f.find_last_of("/\\") + 1);

    LGraph *lg = LGraph::create(path, name);

    FILE *                    pFile = fopen(f.c_str(), "rb");
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
  return std::find_if(s.begin(), s.end(), [](const char c) {
      return (c != '\'' && c != '0' && c != '1' && c != 'x' && c != 'z');
      }) == s.end();
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
      /*next print out input nodes*/
      writer.Key("inputs");
      writer.StartArray();
      {
        for(const auto &input_edge : g->inp_edges(idx)) {
          writer.StartObject();
          writer.Key("inp_nid ");
          writer.Uint64((input_edge.get_idx()));
          //writer.Key("inp_out_pid");
          writer.Key("inp_dst_pid ");
          writer.Uint64(input_edge.get_inp_pin().get_pid());
          writer.EndObject();
        }
        writer.EndArray();
      }


      writer.Key("op");
      {
        if(g->node_type_get(idx).op == U32Const_Op) {
          writer.Uint64(g->node_value_get(idx));
        } else if(g->node_type_get(idx).op == StrConst_Op) {
          writer.String("'");
          writer.String(g->node_const_value_get(idx));
          writer.String("'");
        } else {
          /*normal operations*/
          if(g->node_type_get(idx).op == TechMap_Op) {
            const Tech_cell *tcell = g->get_tlibrary()->get_const_cell(g->tmap_id_get(idx));
            writer.String(tcell->get_name().c_str());
          } else {
            writer.String((g->node_type_get(idx).get_name().c_str()));
          }
        }
      }

      writer.Key("node_bits");
      {
        int node_width = g->get_bits(idx);
        writer.Uint64(node_width);
      }

      if(g->get_node_instancename(idx) != nullptr) {
        writer.Key("instance_name");
        writer.String(g->get_node_instancename(idx));
      }

      if(g->get_node_wirename(idx) != nullptr) {
        writer.Key("node_wirename");
        writer.String(g->get_node_wirename(idx));
      }

      if(g->is_graph_input(idx)) {
        writer.Key("input_name");
        writer.String(g->get_graph_input_name(idx));
      }
      if(g->is_graph_output(idx)) {
        writer.Key("output_name");
        writer.String(g->get_graph_output_name(idx));
      }

      writer.Key("outputs");
      writer.StartArray();
      {
        for(const auto &out : g->out_edges(idx)) {
          writer.StartObject();
          //writer.Key("out_out_pid");
          writer.Key("out_src_pid");
          writer.Uint64(out.get_out_pin().get_pid());
          writer.Key("out_dst_nid");
          writer.Uint64(out.get_idx());
          //writer.Key("out_inp_pid");
          writer.Key("out_dst_pid");
          writer.Uint64(out.get_inp_pin().get_pid());
          if(out.is_root()) {
            auto  node_idx   = out.get_out_pin().get_nid();
            auto  node       = g->get_dest_node(out);
            float node_delay = node.delay_get();
            int   node_bits  = node.get_bits();
            int node_width = g->get_bits(node_idx);
            if(node_delay != 0) {
              writer.Key("delay");
              writer.Double(node_delay);
            }
            if(node_bits != 0) {
              writer.Key("out_src_bits");
              writer.Uint(node_bits);
            } else if(node_width != 0) {
              writer.Key("out_src_bits");
              writer.Uint(node_width);
            }
          }
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
    console->error("ERROR: could not open json file [{}]\n", filename);
    exit(-4);
  }
  fs << s.GetString() << std::endl;
  fs.close();
}
