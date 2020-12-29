//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_json.hpp"
#include "lgraph.hpp"

static absl::flat_hash_map<int, Node::Compact_class> json_remap;

void from_json(LGraph *g, rapidjson::Document &document) {

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
    int last_nid = nodes["nid"].GetUint64();

    Node last_node;

    const auto it = json_remap.find(last_nid);
    if (it == json_remap.end()) {
      last_node = g->create_node();
      json_remap.insert({last_nid, last_node.get_compact_class()});
    }else{
      last_node = Node(g, it->second);
    }

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
        std::string op_txt = nodes["op"].GetString();
        auto op            = Ntype::get_op(op_txt);

        if (op != Ntype_op::Invalid) {
          last_node.set_type(op);
        } else if (!op_txt.empty() && std::isdigit(op_txt[0])) {
          fmt::print("DEBUG:: const op : {} \n", op_txt);
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
        auto src_pid = output_edge["driver_pid"].GetString();
        int dst_nid = output_edge["sink_idx"].GetUint64();

        const auto dit = json_remap.find(dst_nid);
        Node dst_node;
        if (dit == json_remap.end()) {
          dst_node = g->create_node();
          json_remap.insert({dst_nid, dst_node.get_compact_class()});
        }else{
          dst_node = Node(g, dit->second);
        }

        auto dst_pid = output_edge["sink_pid"].GetString();
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
