//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Created by birdeclipse on 12/18/17.
//

#include "inou_json.hpp"

#include <fstream>

#include "absl/strings/substitute.h"
#include "eprp_utils.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/prettywriter.h"

void setup_inou_json() { Inou_json::setup(); }

void Inou_json::setup() {
  Eprp_method m1("inou.json.tolg", "import from json to lgraph", &Inou_json::tolg);
  register_inou("json", m1);

  Eprp_method m2("inou.json.fromlg", "export from lgraph to json", &Inou_json::fromlg);
  register_inou("json", m2);
}

Inou_json::Inou_json(const Eprp_var &var) : Pass("inou.json", var) {}

void Inou_json::from_json(LGraph *g, rapidjson::Document &document) {
  Index_ID last_nid = 0;
  Index_ID dst_nid  = 0;
  Port_ID  src_pid  = 0;
  Port_ID  dst_pid  = 0;
  Node last_node, dst_node;

  json_remap.clear();

  if (document.HasParseError()) {
    Pass::error("inou_json::from_json Error(offset {}): {}", static_cast<unsigned>(document.GetErrorOffset()),
                rapidjson::GetParseError_En(document.GetParseError()));
    return;
  }

  I(document.HasMember("nodes"));
  const rapidjson::Value &nodesArray = document["nodes"];
  I(nodesArray.IsArray());
  for (const auto &nodes : nodesArray.GetArray()) {
    assert(nodes.IsObject());

    assert(nodes.HasMember("nid"));
    last_nid = nodes["nid"].GetUint64();
    if (json_remap.find(last_nid) == json_remap.end()) {
      json_remap[last_nid] = g->create_node().get_compact_class();
    }
    last_node = Node(g, json_remap[last_nid]);

    if (nodes.HasMember("inputs")) {
      assert(nodes["inputs"].IsArray());
      for (const auto &input_edge : nodes["inputs"].GetArray()) {
        assert(input_edge.IsObject());
        if (input_edge.HasMember("inp_in_pid")) {
          fmt::print("DEBUG:: inp_in_pid {} \n", input_edge["inp_in_pid"].GetUint64());
        }
        if (input_edge.HasMember("inp_nid")) {
          fmt::print("DEBUG:: inp_nid {} \n", input_edge["inp_nid"].GetUint64());
        }
        // if(input_edge.HasMember("inp_out_pid")) {
        //  fmt::print("DEBUG:: inp_out_pid {} \n", input_edge["inp_out_pid"].GetUint64());
        //}
        if (input_edge.HasMember("inp_dst_pid")) {
          fmt::print("DEBUG:: inp_dst_pid {} \n", input_edge["inp_dst_pid"].GetUint64());
        }
      }
    }
    if (nodes.HasMember("op")) {
      if (nodes["op"].IsString()) {
        std::string op = nodes["op"].GetString();
        if (Node_Type::is_type(op)) {
          last_node.set_type(Node_Type::get(op));
        } else if (is_const_op(op)) {
          fmt::print("DEBUG:: const op : {} \n", op);
          assert(op.size() > 2);
          assert(op[op.size() - 1] == '\'');
          assert(op[op.size() - 1] == '\'');
          // FIXME: Ignore: Jose is in the middle of changing constants
          //g->node_const_type_set(last_nid, op.substr(1, op.size() - 3));
        } else {
          fmt::print("DEBUG:: HOW TO GET HERE?? \n ");
        }
      } else {
        uint32_t val = nodes["op"].GetUint();
        // FIXME: Ignore: Jose (ditto)
        (void) val;
        //g->node_u32type_set(last_nid, val);
      }
    }

    if (nodes.HasMember("input_name")) {
      fmt::print("DEBUG:: input name is : {} \n", nodes["input_name"].GetString());
      g->add_graph_input(nodes["input_name"].GetString(), 0, 0);  // FIXME: set original_pos and bits
    }

    if (nodes.HasMember("output_name")) {
      fmt::print("DEBUG:: output name is : {} \n", nodes["output_name"].GetString());
      g->add_graph_output(nodes["output_name"].GetString(), 0, 0);  // FIXME: must remember original_pos and set bits
    }

    if (nodes.HasMember("outputs")) {
      assert(nodes["outputs"].IsArray());
      for (const auto &output_edge : nodes["outputs"].GetArray()) {
        assert(output_edge.IsObject());
        // if(output_edge.HasMember("out_out_pid")) {
        //  src_pid = output_edge["out_out_pid"].GetUint();
        //}
        if (output_edge.HasMember("driver_pid")) {
          src_pid = output_edge["driver_pid"].GetUint();
        }
        if (output_edge.HasMember("sink_idx")) {
          dst_nid = output_edge["sink_idx"].GetUint64();
          if (json_remap.find(dst_nid) == json_remap.end()) {
            json_remap[dst_nid] = g->create_node().get_compact_class();
          }
          //dst_nid = json_remap[dst_nid];
        }
        dst_node = Node(g, json_remap[dst_nid]);
        // if(output_edge.HasMember("out_inp_pid")) {
        //  dst_pid = output_edge["out_inp_pid"].GetUint();
        //}
        if (output_edge.HasMember("sink_pid")) {
          dst_pid = output_edge["sink_pid"].GetUint();
        }
        Node_pin dpin = last_node.setup_driver_pin(dst_pid);
        Node_pin spin = dst_node.setup_sink_pin(src_pid);
        if (output_edge.HasMember("bits")) {
          g->add_edge(dpin, spin, output_edge["bits"].GetInt());
        } else {
          g->add_edge(dpin, spin);
        }
        if (output_edge.HasMember("delay")) {
          double delay = output_edge["delay"].GetDouble();
          //g->node_delay_set(last_nid, static_cast<float>(delay)); // original
          dpin.set_delay(static_cast<float>(delay));
        }
      }
    }
  }
}

void Inou_json::fromlg(Eprp_var &var) {
  auto odir = var.get("odir");

  Inou_json p{var};

  bool ok = p.setup_directory(odir);
  if (!ok) return;
  for (const auto &g : var.lgs) {
    auto file = absl::StrCat(odir, "/", g->get_name(), ".json");

    p.to_json(g, file);
  }
}

void Inou_json::tolg(Eprp_var &var) {
  auto files = var.get("files");
  if (files.empty()) {
    error(fmt::format("inou.json.tolg: no files provided"));
    return;
  }

  Inou_json pinouj {var};

  auto path = var.get("path");
  bool ok   = pinouj.setup_directory(path);
  if (!ok) return;

  std::vector<LGraph *> lgs;
  for (const auto &f : absl::StrSplit(files, ',')) {
    std::string_view name = f.substr(f.find_last_of("/\\") + 1);
    if (absl::EndsWith(name, ".json")) {
      name = name.substr(0, name.size() - 5);  // remove .json
    } else {
      error(fmt::format("inou.json.tolg unknown file extension {}, expected .json", name));
      continue;
    }

    LGraph *lg = LGraph::create(path, name, f);

    std::string fname(f);
    FILE *      pFile = fopen(fname.c_str(), "rb");
    if (pFile == 0) {
      Pass::error(fmt::format("Inou_json::tolg could not open {} file", f));
      continue;
    }
    char                      buffer[65536];
    rapidjson::FileReadStream is(pFile, buffer, sizeof(buffer));
    rapidjson::Document       document;
    document.ParseStream<0, rapidjson::UTF8<>, rapidjson::FileReadStream>(is);

    pinouj.from_json(lg, document);
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

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// recursive
static void tojson_debug_print_graph(LGraph *lg) {
  fmt::print("=== from_lg to_json: {} ===\n", lg->get_name());
  ssize_t idx = 0;
  for (const auto &out_edge: lg->get_graph_input_node().out_edges()) {
    Node_pin driver_pin = out_edge.driver;
    Node sink_node = out_edge.sink.get_node();
    fmt::print("[{}] i_node = (nil)\n", idx++);
    for (auto &out: sink_node.out_edges()) {
      auto  dpin       = out.driver;
      auto  dpin_pid   = dpin.get_pid();
      auto  dnode_name = dpin.get_node().debug_name();
      auto  snode_name = out.sink.get_node().debug_name();
      auto  spin_pid   = out.sink.get_pid();
      auto  dpin_name  = dpin.has_name() ? dpin.get_name() : "";
      auto  dbits      = dpin.get_bits();

      fmt::print(" {}->{}[label=\"{}b :{} :{} :{}\"];\n"
          , dnode_name, snode_name, dbits, dpin_pid, spin_pid, dpin_name);
    }
    if (driver_pin.has_name()) {
      fmt::print("    {}\n", driver_pin.get_name());
    }
  }
  for (const auto &in_edge: lg->get_graph_output_node().inp_edges()) {
    (void) in_edge;
    fmt::print("[{}] o_node\n", idx++);
  }
  for (const auto &node: lg->forward())  {
    fmt::print("[{}] node = {}, type = {}\n", idx++, node.debug_name(), node.get_type().get_name());
    if ( node.is_type(SubGraph_Op) ) {
      LGraph *slgraph = node.ref_type_sub_lgraph();
      fmt::print(" ref_type_sub_lgraph(): {}\n", static_cast<void*>(slgraph));
      tojson_debug_print_graph(slgraph);
    }
  }
  fmt::print("=== END: {} ===\n", lg->get_name());
}

// ------------------------------------------------------------------------------------------------

using PrettySbuffWriter = rapidjson::PrettyWriter<rapidjson::StringBuffer>;
class Inou_Tojson {
  using IPair = std::pair<uint32_t, uint32_t>;
  private:
    LGraph *toplg;
    PrettySbuffWriter &writer;
    absl::flat_hash_map<Node_pin::Compact_class, IPair> indices;
    absl::flat_hash_map<Node_pin::Compact_class, uint32_t> pick_cache;
    ssize_t next_idx;
    ///
    int GetPorts(LGraph *lg);
    void ResetIndices() {
      pick_cache.clear();
      indices.clear();
      next_idx = 2;
    }
  public:
    Inou_Tojson(LGraph *toplg_, PrettySbuffWriter &writer_): toplg(toplg_), writer(writer_) {}
    int DumpGraph(Lg_type_id lgid);
};

int Inou_Tojson::GetPorts(LGraph *lg) {
  int retval = 0;
  writer.Key("ports");
  writer.StartObject();
  // === Input ports to lgraph:
  lg->each_graph_input([&](const Node_pin &out_pin) {
    if (!out_pin.has_name()) {
      fmt::print("ERROR: input pin \"{}\" for module {} has no name???\n",
                 out_pin.debug_name(), lg->get_name());
      retval = -1;
      return false;
    }
    Node_pin::Compact_class npincc = out_pin.get_compact_class();
    auto iter = indices.find(npincc);
    if (iter == indices.end()) {
      fmt::print("ERROR: input pin \"{}\" for module {} wasn't added to the map???\n",
                 out_pin.get_name(), lg->get_name());
      retval = -1;
      return false;
    }
    writer.Key(std::string(out_pin.get_name()).c_str());
    writer.StartObject();
    writer.Key("direction");
    writer.String("input");
    writer.Key("bits");
    writer.StartArray();
    IPair &ipair = iter->second;
    for (uint32_t idx = ipair.first; idx < (ipair.first + ipair.second); ++idx) {
      writer.Uint64(idx);
    }
    writer.EndArray(); // bits
    writer.EndObject();
    return true;
  });
  // === Output ports to lgraph:
  lg->each_graph_output([&](const Node_pin &out_pin) {
    if (!out_pin.has_name()) {
      fmt::print("ERROR: output pin \"{}\" for module {} has no name???\n",
                 out_pin.debug_name(), lg->get_name());
      retval = -1;
      return false;
    }
    writer.Key(std::string(out_pin.get_name()).c_str());
    writer.StartObject();
    writer.Key("direction");
    writer.String("output");
    writer.EndObject();
    return true;
  });
  writer.EndObject();
  return retval;
}

int Inou_Tojson::DumpGraph(Lg_type_id lgid) {
  LGraph *lg = LGraph::open(toplg->get_path(), lgid);
  if (lg == nullptr) {
    fmt::print("ERROR: Couldn't open LGraph for idx = {}??\n", lgid);
    return -1;
  }
  ResetIndices();
  // Step 1: Assign net indices to all "real driver pins":
  int retval = 0;
  lg->each_graph_input([&](const Node_pin &out_pin) {
    Node_pin::Compact_class npincc = out_pin.get_compact_class();
    uint32_t bsize = out_pin.get_bits();
    if (bsize == 0) {
      fmt::print("ERROR: Pin {} has bit width of zero??\n", out_pin.debug_name());
      retval = -1;
      return false;
    }
    indices[npincc] = {next_idx, bsize};
    next_idx += bsize;
    return true;
  });
  if (retval < 0) { return retval; }
  for (const auto &node: lg->forward())  {
    // Skip "fake" cell "Pick_Op": (however, cache the Pick_Op selection bits):
    if (node.is_type(Pick_Op)) {
      Node_pin offpin = node.get_sink_pin("OFFSET");
      Node_pin::Compact_class npincc = offpin.get_compact_class();
      for (auto &oedge : offpin.inp_edges()) {
        Node cnode = oedge.driver.get_node();
        pick_cache[npincc] = cnode.get_type_const_value();
      }
      continue;
    }
    // Skip "fake" cell "Join_Op":
    if (node.is_type(Join_Op)) { continue; }
    // Skip constants; they will be handled differently:
    if (node.is_type(U32Const_Op)) { continue; }
    for (auto &out_pin : node.out_connected_pins()) {
      uint32_t bsize = out_pin.get_bits();
      if (bsize == 0) { continue; }
      Node_pin::Compact_class npincc = out_pin.get_compact_class();
      indices[npincc] = {next_idx, bsize};
      next_idx += bsize;
    }
    if (false) {
      fmt::print("Node type {} not supported by inou.json.fromlg\n", node.get_type().get_name());
      return -1;
    }
  }
  writer.Key(std::string(lg->get_name()).c_str());
  writer.StartObject();
  writer.Key("attributes");
  writer.String("TODO");
  if (GetPorts(lg) < 0) {
    return -1;
  }
  writer.Key("cells");
  writer.String("TODO");
  writer.Key("netnames");
  writer.String("TODO");
  writer.EndObject();
  return 0;
}

// ------------------------------------------------------------------------------------------------

// "
void Inou_json::to_json(LGraph *lg, const std::string &filename) const {
  // TODO: Work in Progress by Clark H
  rapidjson::StringBuffer sbuff;
  PrettySbuffWriter writer(sbuff);
  writer.SetFormatOptions(rapidjson::kFormatSingleLineArray);
  writer.StartObject();
#ifdef JSON_FROMLG_DEBUG_PRINT
  fmt::print("tojson (output fname: {}) - TODO\n", filename);
  tojson_debug_print_graph(lg);
#endif
#ifndef OLD_JSON_FROMLG
  writer.Key("creator");
  writer.String("lgraph fromlg");
  writer.Key("modules");
  writer.StartObject();
  std::vector<Lg_type_id> lgids;
  //fmt::print("aaa {}\n", lg->get_lgid());
  lgids.push_back(lg->get_lgid());
  lg->each_sub_unique_fast([&](Node&, Lg_type_id lgid) {
    //fmt::print("bbb {}\n", lgtid);
    lgids.push_back(lgid);
    return true;
  });
  Inou_Tojson itj {lg, writer};
  for (auto lgid : lgids) {
    if (itj.DumpGraph(lgid) < 0) {
      fmt::print("inou.json.fromlg encountered an error, writing json aborted\n");
      return;
    }
  }
  writer.EndObject(); // "modules"
#else
  {
    writer.Key("nodes");
    writer.StartArray();

    for (const auto &nid : g->fast()) {  // Forward would not pass flops
      writer.StartObject();
      /*first print out the node nid */
      writer.Key("nid");
      writer.Uint64(nid);
#if 0
      /*next print out input nodes*/
      writer.Key("inputs");
      writer.StartArray();
      {
        for(const auto &input_edge : g->inp_edges(nid)) {
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
        auto type = g->get_node(nid).get_type();
        if (type.op == U32Const_Op) {
          writer.Uint64(g->node_value_get(nid));
        } else if (type.op == StrConst_Op) {
          std::string tmp;
          tmp.append("'");
          tmp.append(g->node_const_value_get(nid));
          tmp.append("'");
          writer.String(tmp.c_str());
        } else {
          /*normal operations*/
          if (type.op == TechMap_Op) { // New format: Subgraph
            // Does nextpnr support hierarchy, or do we have to flatten?
            // Try this out.
            // yosys
            //   help synth_ice40
            //      (there is a -noflatten option, try this out)
            auto cell_name = g->get_tlibrary().get_cell_name(g->tmap_id_get(nid));
            writer.String(std::string(cell_name).c_str());
          } else {
            writer.String((g->node_type_get(nid).get_name().c_str()));
          }
        }
      }

      auto ni_name = g->get_node_instancename(nid);
      if (!ni_name.empty()) {
        writer.Key("instance_name");
        writer.String(std::string(ni_name).c_str());  // rapidjson does not support string_view
      }

      writer.Key("outputs");
      writer.StartArray();
      {
        for (const auto &out : g->out_edges(nid)) {
          writer.StartObject();

          auto wi_name = g->get_node_wirename(nid);
          if (!wi_name.empty()) {
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
          if (out.is_root()) {
            auto  node       = g->get_dest_node(out);
            float node_delay = node.get_delay();
            int   node_width = g->get_bits(out.get_out_pin());
            if (node_delay != 0) {
              writer.Key("delay");
              writer.Double(node_delay);
            }
            if (node_width != 0) {
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
        for (const auto &inp : g->inp_edges(nid)) {
          writer.StartObject();

          auto wi_name = g->get_node_wirename(inp.get_inp_pin());
          if (!wi_name.empty()) {
            writer.Key("name");
            writer.String(std::string(wi_name).c_str());
          }

          writer.Key("sink_idx");
          writer.Uint64(inp.get_inp_pin().get_idx());
          writer.Key("sink_pid");
          writer.Uint64(inp.get_inp_pin().get_pid());
          writer.Key("driver_idx");
          writer.Uint64(inp.get_out_pin().get_idx());
          writer.Key("driver_pid");
          writer.Uint64(inp.get_out_pin().get_pid());

          writer.EndObject();
        }
      }
      writer.EndArray();
      writer.EndObject();
    }
    writer.EndArray();
  }
#endif
  writer.EndObject();
  std::ofstream fs;

  fs.open(filename, std::ios::out | std::ios::trunc);
  if (!fs.is_open()) {
    Pass::error("ERROR: could not open json file {}", filename);
    return;
  }
  fs << sbuff.GetString() << std::endl;
  fs.close();
}
