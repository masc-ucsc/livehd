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

/***************************************

 === TODO ===
fromlg to_json:
  1. Setting pin names for nodes that perform commutative operations (e.g. ADD, AND, etc.)
  2. Setting attributes (e.g. “not_processed = true”, "src=module.v:87")
  3. TMapping.
tolg from_json:
  1. A LOT. I have to rewrite the whole entire function.

***************************************/

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
  // TODO: Rewrite Inou_json::from_json
  Pass::error("inou.json.tolg is currently under development; please do not use it right now");

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

int Inou_Tojson::backtrace_sink_pin(const Node_pin &spin, IPair ipair) {
  int retval = 0;
  if (ipair.second == 0) {
    for (auto &edge : spin.inp_edges()) {
      ipair.second = edge.get_bits();
    }
    if (ipair.second == 0) {
      Pass::error("sink pin \"{}\" has zero bits??\n", spin.debug_name());
    }
  }
#ifdef FROMLG_TOJSON_BACKTRACE_PRINT
  fmt::print("backtrace_sink_pin {}:{}\n", ipair.first, ipair.second);
#endif
  uint32_t num_edges = 0;
  for (auto &edge : spin.inp_edges()) {
    ++num_edges;
    if (backtrace_edge(edge, ipair) < 0) { return -1; }
  }
  if (num_edges > 1) {
    Pass::warn("backtrace_sink_pin found more than 1 ({}) edge for pin {}",
                num_edges, spin.debug_name());
  }
  return retval;
}

int Inou_Tojson::backtrace_edge(const XEdge &edge, IPair ipair) {
  Node_pin driver = edge.driver, spin = edge.sink;
  Node dnode = driver.get_node();
  Node_Type ntype = dnode.get_type();
  if (dnode.is_type(Pick_Op)) {
    Node_pin apin = dnode.get_sink_pin("A");
    Node_pin offpin = dnode.get_sink_pin("OFFSET");
    Node_pin::Compact_class npincc = offpin.get_compact_class();
    auto iter = pick_cache.find(npincc);
    I(iter != pick_cache.end()); // offpin should have been added to pick_cache
#ifdef FROMLG_TOJSON_BACKTRACE_PRINT
    fmt::print(" --Pick_Op {}:{}\n", ipair.first + iter->second, ipair.second);
#endif
    if (backtrace_sink_pin(apin, {ipair.first + iter->second, ipair.second}) < 0) { return -1; }
  } else if (dnode.is_type(Join_Op)) {
    // I'm going to assume that you iterate the Join_Op sink pins in order...
    uint32_t curwidth = 0, done_bits = 0;
    for (auto &inpin : dnode.inp_connected_pins()) {
      if (curwidth >= (ipair.first + ipair.second)) { break; }
      uint32_t bwidth = 0;
      int check_mult_edges = 0;
      for (auto &jedge : inpin.inp_edges()) {
        bwidth = jedge.get_bits();
        ++check_mult_edges;
      }
      if (bwidth == 0) {
        Pass::warn("connected in pin to Join_Op has bwidth of zero?");
        continue;
      }
      if (check_mult_edges > 1) {
        Pass::warn("backtrace_sink_pin found more than 1 ({}) edge for pin {}:{}",
                    check_mult_edges, dnode.debug_name(), spin.debug_name());
      }
      uint32_t eidx = curwidth + bwidth - 1;
      uint32_t diff_f, diff_s = ipair.second - done_bits;
      if (curwidth < ipair.first && eidx >= ipair.first) {
        diff_f = ipair.first - curwidth;
      } else {
        diff_f = 0;
      }
      if ((bwidth + curwidth) < (ipair.first + ipair.second)) {
        diff_s = bwidth + curwidth - ipair.first;
      }
#ifdef FROMLG_TOJSON_BACKTRACE_PRINT
      fmt::print("curwidth = {}, bwidth = {}\n", curwidth, bwidth);
      fmt::print(" --Join_Op {}:{}\n", diff_f, diff_s);
#endif
      if (backtrace_sink_pin(inpin, {diff_f, diff_s}) < 0) { return -1; }
      curwidth += bwidth;
      done_bits += diff_s;
    }
  } else if (dnode.is_type(Const_Op)) {
    size_t const_val;
    Lconst lcnst = dnode.get_type_const();
    I(lcnst.is_i());
    const_val = lcnst.to_i() >> ipair.first;
    for (uint32_t idx = 0; idx < ipair.second; ++idx) {
      if ((const_val & 0x1) == 0x1) {
        writer.String("1");
      } else {
        writer.String("0");
      }
      const_val = const_val >> 1;
    }
  } else {
    Node_pin::Compact_class npincc = driver.get_compact_class();
    auto iter = indices.find(npincc);
    if (iter == indices.end()) {
      fmt::print("ERROR: driver pin \"{}\" wasn't added to map??\n", driver.debug_name());
      return -1;
    }
    uint32_t startidx = iter->second.first + ipair.first;
    uint32_t endidx = startidx + ipair.second;
    for (uint32_t idx = startidx; idx < endidx; ++idx) {
      writer.Uint64(idx);
    }
  }
  return 0;
}

int Inou_Tojson::get_ports(LGraph *lg) {
  int retval = 0;
  writer.Key("ports");
  writer.StartObject();
  // === Input ports to lgraph:
  lg->each_graph_input([&](const Node_pin &out_pin) {
    if (!out_pin.has_name()) {
      Pass::error("input pin \"{}\" for module {} has no name???\n",
                  out_pin.debug_name(), lg->get_name());
    }
    Node_pin::Compact_class npincc = out_pin.get_compact_class();
    auto iter = indices.find(npincc);
    I(iter != indices.end()); // out_pin should have been added to the map
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
  lg->each_graph_output([&](const Node_pin &outp) {
    Node_pin out_pin = outp.get_sink_from_output();
    I(out_pin.is_sink());
    if (!outp.has_name()) {
      Pass::error("output pin \"{}\" for module {} has no name???\n",
                  outp.debug_name(), lg->get_name());
    }
    writer.Key(std::string(outp.get_name()).c_str());
    writer.StartObject();
    writer.Key("direction");
    writer.String("output");
    writer.Key("bits");
    retval = backtrace_sink_pin_wrapper(out_pin);
    if (retval < 0) { return false; }
    writer.EndObject();
    return true;
  });
  writer.EndObject();
  return retval;
}

int Inou_Tojson::write_cell_commutative(LGraph *lg, Node::Compact_class ncc, std::vector<XEdge::Compact> &edges) {
  ssize_t num_edges = edges.size();
  I(num_edges > 1);
  int retval = 0;
  Node node = Node(lg, ncc);
  auto edge_itor = edges.begin();
  XEdge test_edge = XEdge(lg, *edge_itor);
  uint32_t bsize = test_edge.get_bits();
  I(bsize > 0);
  std::string node_name;
  if (node.has_name()) {
    node_name = node.get_name();
  } else {
    node_name = node.debug_name();
  }
  // There will always be (num_edges-1) 2-input gates
  ssize_t num_gates = num_edges - 1, neg_num_1st_gates = 1, num_1st_conn = 2;
  while (num_1st_conn < num_edges) {
    neg_num_1st_gates *= 2;
    num_1st_conn *= 2;
  }
  ssize_t num_1st_gates = num_edges - neg_num_1st_gates; // determine # of tier 0 gates
  de_opt_vec.push_back({node_name, {}});
  std::vector<IPair> &ipair_vec = de_opt_vec.rbegin()->second;
  ssize_t tier_num = 0, tier = 0, prev_tier_count = 0, prev_tier_num = 0;
  const char keys[2][2] = {"A", "B"};
  ssize_t used_prev = 0;
  for (ssize_t gate_idx = 0; gate_idx < num_gates; ) {
    // gate_idx gets incremented after the main portion of the body...
    std::string nname_app = node_name + "$" + std::to_string(gate_idx+2);
    writer.Key(nname_app.c_str());
    writer.StartObject();
    writer.Key("hide_name");
    writer.Uint64(1);
    writer.Key("type");
    writer.String("TODO");
    writer.Key("parameters");
    writer.StartObject();
    // FIXME
    writer.EndObject();
    writer.Key("attributes");
    writer.StartObject();
    // FIXME
    writer.EndObject();
    writer.Key("port_directions");
    writer.StartObject();
    writer.Key("A");
    writer.String("input");
    writer.Key("B");
    writer.String("input");
    writer.Key("Y");
    writer.String("output");
    writer.EndObject(); // port_directions
    writer.Key("connections");
    writer.StartObject();
    for (ssize_t idx = 0; idx < 2; ++idx) {
      writer.Key(keys[idx]);
      writer.StartArray();
      if (used_prev == prev_tier_count) {
        XEdge edge = XEdge(lg, *edge_itor++);
        backtrace_edge(edge, {0, bsize});
      } else {
        I((used_prev + prev_tier_num) < ipair_vec.size());
        uint32_t start_idx = ipair_vec[used_prev + prev_tier_num].first;
        for (uint32_t idx = start_idx; idx < (start_idx + bsize); ++idx) {
          writer.Uint64(idx);
        }
        ++used_prev;
      }
      writer.EndArray();
    }
    bool last_gate = ((gate_idx + 1) == num_gates);
    if (last_gate) {
      bool check_dup = false;
      for (auto &pin : node.out_connected_pins()) {
        I(check_dup == false);
        check_dup = true;
        writer.Key("Y");
        Node_pin::Compact_class npincc = pin.get_compact_class();
        auto iter = indices.find(npincc);
        I(iter != indices.end()); // pin should have been added to the map
        IPair &ipair = iter->second;
        writer.StartArray();
        for (uint32_t idx = ipair.first; idx < (ipair.first + ipair.second); ++idx) {
          writer.Uint64(idx);
        }
        writer.EndArray();
      }
    } else {
      uint32_t start_idx = next_idx;
      ipair_vec.push_back({next_idx, bsize});
      next_idx += bsize;
      writer.Key("Y");
      writer.StartArray();
      for (uint32_t idx = start_idx; idx < (start_idx + bsize); ++idx) {
        writer.Uint64(idx);
      }
      writer.EndArray();
    }
    writer.EndObject(); // connections
    writer.EndObject(); // node_name_idx
    
    if (last_gate) { break; }
    // determine tier and tier_num:
    ++tier_num;
    ++gate_idx;
    ssize_t gates_left = num_gates - gate_idx;
    if ((tier == 0 && tier_num == num_1st_gates) || (tier > 0 && (tier_num*2) > gates_left)) {
      prev_tier_num = gate_idx - tier_num;
      prev_tier_count = tier_num;
      tier_num = 0;
      used_prev = 0;
      ++tier;
    }
  }
  return retval;
}

int Inou_Tojson::write_cells(LGraph *lg, const Cells &cells) {
  int retval = 0;
  writer.Key("cells");
  writer.StartObject();
  for (Node::Compact_class ncc : cells) {
    Node node = Node(lg, ncc);
    //Node_Type ntype = node.get_type();
    uint32_t hide_name = 1;
    std::string node_name;
    if (node.has_name()) {
      node_name = node.get_name();
      hide_name = 0;
    } else {
      node_name = node.debug_name();
    }
#ifdef FROMLG_TOJSON_BACKTRACE_PRINT
    fmt::print("{}\n", node_name);
#endif
    // First, check if we have a "commutative" node:
    ssize_t num_inp_pins = 0, num_edges;
    bool is_commutative = false;
    std::vector<XEdge::Compact> edges;
    for (auto &pin : node.inp_connected_pins()) {
      (void) pin;
      ++num_inp_pins;
    }
    for (auto &pin : node.inp_connected_pins()) {
      ssize_t num_inp_edges = 0;
      for (auto &oedge : pin.inp_edges()) {
        ++num_inp_edges;
        if (num_inp_pins == 1) { edges.push_back(oedge.get_compact()); }
      }
      I(num_inp_edges > 0);
      if (num_inp_edges > 1) {
        if (num_inp_pins > 1) {
          Pass::error("Internal error: Node {} has multiple input Node_pins, but Node_pin "
                      "{} has more than 1 input edge??\n", node_name, pin.get_pin_name());
        }
        is_commutative = true;
        num_edges = num_inp_edges;
      }
    }
    I(num_inp_pins);
    if (is_commutative == true) {
      // FIXME: Use return value.
      write_cell_commutative(lg, node.get_compact_class(), edges);
      continue;
    }

    // "non-commutative" node:
    writer.Key(node_name.c_str());
    writer.StartObject();
    writer.Key("hide_name");
    writer.Uint64(hide_name);
    writer.Key("type");
    writer.String("TODO");
    writer.Key("parameters");
    writer.StartObject();
    // FIXME
    writer.EndObject();
    writer.Key("attributes");
    writer.StartObject();
    // FIXME
    writer.EndObject();
    writer.Key("port_directions");
    writer.StartObject();
    for (auto &pin : node.inp_connected_pins()) {
      writer.Key(std::string(pin.get_pin_name()).c_str());
      writer.String("input");
    }
    for (auto &pin : node.out_connected_pins()) {
      writer.Key(std::string(pin.get_pin_name()).c_str());
      writer.String("output");
    }
    writer.EndObject(); // port_directions
    writer.Key("connections");
    writer.StartObject();
    for (auto &pin : node.inp_connected_pins()) {
      writer.Key(std::string(pin.get_pin_name()).c_str());
      if (backtrace_sink_pin_wrapper(pin) < 0) { return -1; }
    }
    for (auto &pin : node.out_connected_pins()) {
      writer.Key(std::string(pin.get_pin_name()).c_str());
      Node_pin::Compact_class npincc = pin.get_compact_class();
      auto iter = indices.find(npincc);
      I(iter != indices.end()); // pin should have been added to the map
      IPair &ipair = iter->second;
      writer.StartArray();
      for (uint32_t idx = ipair.first; idx < (ipair.first + ipair.second); ++idx) {
        writer.Uint64(idx);
      }
      writer.EndArray();
    }
    writer.EndObject(); // connections
    writer.EndObject(); // node_name
  }
  writer.EndObject(); // cells
  return retval;
}

int Inou_Tojson::write_netnames(LGraph *lg) {
  int retval = 0;
  writer.Key("netnames");
  writer.StartObject();
  for (auto &npcc_ipair : indices) {
    Node_pin dpin {lg, npcc_ipair.first};
    int hide_name = 1;
    std::string dname;
    if (dpin.has_name()) {
      hide_name = 0;
      dname = dpin.get_name();
    } else {
      dname = "___";
      dname += dpin.debug_name();
    }
    writer.Key(dname.c_str());
    writer.StartObject();
    writer.Key("hide_name");
    writer.Uint64(hide_name);
    writer.Key("bits");
    writer.StartArray();
    IPair &ipair = npcc_ipair.second;
    for (uint32_t idx = ipair.first; idx < (ipair.first + ipair.second); ++idx) {
      writer.Uint64(idx);
    }
    writer.EndArray();  // bits
    writer.Key("attributes");
    writer.StartObject();
    writer.EndObject(); // attributes
    writer.EndObject(); // dname
  }
  
  // if we broke down commutative nodes, print out the nets here:
  for (auto &de_opt_pair: de_opt_vec) {
    ssize_t snode_idx = 2;
    for (auto ipair: de_opt_pair.second) {
      std::string dname = de_opt_pair.first + "$" + std::to_string(snode_idx++) + "_Y";
      writer.Key(dname.c_str());
      writer.StartObject();
      writer.Key("hide_name");
      writer.Uint64(1);
      writer.Key("bits");
      writer.StartArray();
      for (uint32_t idx = ipair.first; idx < (ipair.first + ipair.second); ++idx) {
        writer.Uint64(idx);
      }
      writer.EndArray();  // bits
      writer.Key("attributes");
      writer.StartObject();
      writer.EndObject(); // attributes
      writer.EndObject(); // dname
    }
  }
  writer.EndObject(); // netnames
  return retval;
}

int Inou_Tojson::dump_graph(Lg_type_id lgid) {
  LGraph *lg = LGraph::open(toplg->get_path(), lgid);
  if (lg == nullptr) {
    Pass::error("Couldn't open LGraph for idx = {}\n", lgid);
  }
  reset_indices();
  // Step 1: Assign net indices to all "real driver pins":
  int retval = 0;
  lg->each_graph_input([&](const Node_pin &out_pin) {
    Node_pin::Compact_class npincc = out_pin.get_compact_class();
    uint32_t bsize = out_pin.get_bits();
    if (bsize == 0) {
      Pass::error("Pin {} has bit width of zero??\n", out_pin.debug_name());
    }
    indices[npincc] = {next_idx, bsize};
    next_idx += bsize;
    return true;
  });
  if (retval < 0) { return retval; }
  Cells cells;
  for (const auto &node: lg->forward())  {
    // Skip "fake" cell "Pick_Op": (however, cache the Pick_Op selection bits):
    if (node.is_type(Pick_Op)) {
      Node_pin offpin = node.get_sink_pin("OFFSET");
      Node_pin::Compact_class npincc = offpin.get_compact_class();
      for (auto &oedge : offpin.inp_edges()) {
        Node cnode = oedge.driver.get_node();
        Lconst lcnst = cnode.get_type_const();
        I(lcnst.is_i());
        pick_cache[npincc] = lcnst.to_i();
      }
      continue;
    }
    // Skip "fake" cell "Join_Op":
    if (node.is_type(Join_Op)) { continue; }
    // Skip constants; they will be handled differently:
    if (node.is_type(Const_Op)) { continue; }
    for (auto &out_pin : node.out_connected_pins()) {
      uint32_t bsize = out_pin.get_bits();
      if (bsize == 0) { continue; }
      Node_pin::Compact_class npincc = out_pin.get_compact_class();
      indices[npincc] = {next_idx, bsize};
      next_idx += bsize;
    }
    cells.push_back(node.get_compact_class());
    if (false) {
      fmt::print("Node type {} not supported by inou.json.fromlg\n", node.get_type().get_name());
      return -1;
    }
  }
  writer.Key(std::string(lg->get_name()).c_str());
  writer.StartObject();
  writer.Key("attributes");
  //writer.String("TODO");
  writer.StartObject();
  writer.EndObject();
  if (get_ports(lg) < 0) {
    return -1;
  }
  if (write_cells(lg, cells) < 0) {
    return -1;
  }
  if (write_netnames(lg) < 0) {
    return -1;
  }
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
    if (itj.dump_graph(lgid) < 0) {
      fmt::print("inou.json.fromlg encountered an error, writing json aborted\n");
      return;
    }
  }
  writer.EndObject(); // "modules"
  
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
