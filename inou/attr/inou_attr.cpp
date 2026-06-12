//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_attr.hpp"

#include <format>
#include <fstream>
#include <iostream>
#include <string>

#include "hhds/graph.hpp"
#include "node_util.hpp"

using livehd::graph_util::color_of;
using livehd::graph_util::default_instance_name;
using livehd::graph_util::has_color;
using livehd::graph_util::set_color;

static Pass_plugin sample("inou_attr", Inou_attr::setup);

void Inou_attr::setup() {
  Eprp_method m1("inou.attr.load", "load attributes to LG from json file", &Inou_attr::set_color_to_lg);
  Eprp_method m2("inou.attr.save", "save attributes from LG to json file", &Inou_attr::get_color_from_lg);

  m1.add_label_required("files", "input file like color.json");
  m2.add_label_required("files", "output file like color_out.json");

  register_inou("attr", m1);
  register_inou("attr", m2);
}

Inou_attr::Inou_attr(const Eprp_var& var) : Pass("inou.attr", var) {}

void Inou_attr::set_color_to_lg(Eprp_var& var) {
  Inou_attr p(var);

  TRACE_EVENT("inou", "ATTR_set_color");

  auto filename = p.get_files(var);
  for (const auto& g : var.graphs) {
    p.read_json(filename, g.get());
    p.color_lg(g.get());
  }
}

void Inou_attr::read_json(const std::string& filename, hhds::Graph* lg) {
  node2color.clear();
  auto  gio      = lg->get_io();
  auto  top_name = gio ? std::string{gio->get_name()} : std::string{};
  FILE* pFile    = fopen(filename.c_str(), "rb");
  if (pFile == nullptr) {
    livehd::diag::err("inou.attr", "missing-file", "io").msg("Could not open file {}", filename).fatal();
    return;
  }
  char                      buffer[65536];
  rapidjson::FileReadStream is(pFile, buffer, sizeof(buffer));
  rapidjson::Document       document;
  document.ParseStream<0, rapidjson::UTF8<>, rapidjson::FileReadStream>(is);

  if (document.HasParseError()) {
    fclose(pFile);
    livehd::diag::err("inou.attr", "parse-failed", "syntax")
        .msg("input file: relaod {} Error(offset {}): {}",
             filename,
             static_cast<unsigned>(document.GetErrorOffset()),
             rapidjson::GetParseError_En(document.GetParseError()))
        .fatal();
    return;
  }
  I(document.IsArray());
  for (rapidjson::SizeType i = 0; i < document.Size(); i++) {
    const rapidjson::Value& class_entries = document[i];
    I(class_entries.HasMember("class"));
    const auto& class_val     = class_entries["class"].GetString();
    std::string class_val_str = std::string(class_val);
    if (class_val_str == "livehd.lgraph.color") {
      I(class_entries.HasMember("modules"));
      const rapidjson::Value& modules = class_entries["modules"];
      I(modules.IsArray());
      for (rapidjson::SizeType j = 0; j < modules.Size(); j++) {
        const rapidjson::Value& module_entry = modules[j];
        I(module_entry.HasMember("name"));
        I(module_entry.HasMember("node_colors"));

        const auto& module_name     = module_entry["name"].GetString();
        std::string module_name_str = std::string(module_name);
        if (top_name == module_name_str) {
          const rapidjson::Value& nodes = module_entry["node_colors"];
          I(nodes.IsObject());
          for (rapidjson::Value::ConstMemberIterator itr = nodes.MemberBegin(); itr != nodes.MemberEnd(); itr++) {
            const auto& nodeName     = itr->name.GetString();
            std::string nodeName_str = std::string(nodeName);
            const auto& colorVal     = itr->value.GetDouble();
            node2color.insert({nodeName_str, colorVal});
          }
        } else {
          continue;
        }
      }
    } else {
      continue;
    }
  }

  fclose(pFile);
}

void Inou_attr::color_lg(hhds::Graph* lg) {
  for (auto node : lg->fast_hier()) {
    const auto iname = default_instance_name(node);
    auto       it    = node2color.find(iname);
    if (it != node2color.end()) {
      set_color(node, static_cast<int32_t>(it->second));
      std::print("Set color {} to instance {} at nid {}.\n", it->second, iname, static_cast<uint64_t>(node.get_debug_nid()));
    }
  }
}

void Inou_attr::get_color_from_lg(Eprp_var& var) {
  Inou_attr p(var);

  TRACE_EVENT("inou", "ATTR_get_color");

  rapidjson::StringBuffer                          s;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(s);
  writer.StartArray();
  writer.StartObject();
  writer.Key("class");
  writer.String("livehd.lgraph.color");
  writer.Key("modules");
  writer.StartArray();

  for (const auto& g : var.graphs) {
    writer.StartObject();
    writer.Key("name");
    auto        gio          = g->get_io();
    std::string top_name_str = gio ? std::string{gio->get_name()} : std::string{};
    writer.String(top_name_str.c_str());
    writer.Key("node_colors");

    writer.StartObject();
    for (auto node : g->fast_hier()) {
      if (has_color(node)) {
        auto        color_val = color_of(node);
        const auto  iname     = default_instance_name(node);
        std::string iname_str{iname};
        writer.Key(iname_str.c_str());
        writer.Double(color_val);
      }
    }
    writer.EndObject();

    writer.EndObject();
  }
  writer.EndArray();
  writer.EndObject();
  writer.EndArray();

  auto          filename = p.get_files(var);
  std::ofstream fs;
  fs.open(filename, std::ios::out | std::ios::trunc);
  if (!fs.is_open()) {
    livehd::diag::err("inou.attr", "write-failed", "io").msg("Could not open file {}", filename).fatal();
    return;
  }
  fs << s.GetString() << std::endl;
  fs.close();
}
