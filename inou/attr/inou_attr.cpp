//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fstream>
#include <string>

#include "inou_attr.hpp"

static Pass_plugin sample("inou_attr", Inou_attr::setup);

void Inou_attr::setup() {
  Eprp_method m1("inou.attr.load", "load attributes to LG from json file", &Inou_attr::set_color_to_lg);
  Eprp_method m2("inou.attr.save", "save attributes from LG to json file", &Inou_attr::get_color_from_lg);

  m1.add_label_required("files", "input file like color.json");
  m2.add_label_required("files", "output file like color_out.json");

  register_inou("attr", m1);
  register_inou("attr", m2);
}

Inou_attr::Inou_attr(const Eprp_var &var) : Pass("inou.attr", var) {
}


void Inou_attr::set_color_to_lg(Eprp_var &var){
  Inou_attr p(var);

  TRACE_EVENT("inou", "ATTR_set_color");
  
  auto filename = p.get_files(var);
  for (const auto &lg : var.lgs) {
    //fmt::print("\n\n---> current module: {}\n\n", lg->get_name());
    p.read_json(filename, lg);
    p.color_lg(lg);
  }
}

void Inou_attr::read_json(const std::string &filename, Lgraph *lg) {
  //fmt::print("{}", filename);
  node2color.clear();
  const auto top_name = lg->get_name();
  //fmt::print("{}", top_name);
  FILE *pFile = fopen(filename.c_str(), "rb");
  if (pFile == 0) {
    Pass::error("Could not open file {}", filename);
    return;
  }
  char                      buffer[65536];
  rapidjson::FileReadStream is(pFile, buffer, sizeof(buffer));
  rapidjson::Document       document;
  document.ParseStream<0, rapidjson::UTF8<>, rapidjson::FileReadStream>(is);
 
  if (document.HasParseError()) {
    Pass::error("input file: relaod {} Error(offset {}): {}", filename,
                static_cast<unsigned>(document.GetErrorOffset()),
                rapidjson::GetParseError_En(document.GetParseError()) );
    return;
  }
  I(document.IsArray());
  for (rapidjson::SizeType i = 0; i<document.Size(); i++) {//because doc is array//this loop iterates over all class_entries
    const rapidjson::Value &class_entries = document[i];
    I(class_entries.HasMember("class"));
    const auto &class_val = class_entries["class"].GetString();
    std::string class_val_str = std::string(class_val);
    if(class_val_str=="livehd.lgraph.color"){
      I(class_entries.HasMember("modules"));
      //iterate over modules to get each module and its nodes
      const rapidjson::Value& modules = class_entries["modules"];
      I(modules.IsArray());
      for (rapidjson::SizeType j = 0; j<modules.Size(); j++) {
        //iterate through modules
        const rapidjson::Value &module_entry = modules[j];
        I(module_entry.HasMember("name"));
        I(module_entry.HasMember("node_colors"));

        const auto &module_name=module_entry["name"].GetString();
        std::string module_name_str = std::string(module_name);
        if (top_name==module_name_str) {
          //fmt::print("Found module {} present in Json, in LG.\n", module_name_str);
          const rapidjson::Value &nodes = module_entry["node_colors"];
          I(nodes.IsObject());
          for (rapidjson::Value::ConstMemberIterator itr = nodes.MemberBegin() ; itr != nodes.MemberEnd(); itr++) {
            //iterate through nodes
            const auto &nodeName = itr->name.GetString(); //make object value
            std::string nodeName_str = std::string(nodeName);
            const auto &colorVal = itr->value.GetDouble();
            node2color.insert({nodeName_str, colorVal});
          }
        } else { continue; }

      }
    } else {
      continue;
    }
  }
  
}

void Inou_attr::color_lg (Lgraph *lg) {
  //auto lg_name = lg->get_name();
  //fmt::print("{}",lg_name);

  //since map is formed with nodes and color, set_color to this lg using this map.
  for ( auto node : lg->fast(true)) {
    const auto &iname = node.default_instance_name();
    if(node2color.find(iname)!=node2color.end()){
      node.set_color(node2color[iname]);
      fmt::print("Set color {} to instance {} at nid {}.\n", node2color[iname], iname, node.get_nid());
    }
  }

}

void Inou_attr::get_color_from_lg(Eprp_var &var){
  Inou_attr p(var);

  TRACE_EVENT("inou", "ATTR_get_color");


  rapidjson::StringBuffer s;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(s);
  writer.StartArray();
  writer.StartObject();
  writer.Key("class");
  writer.String("livehd.lgraph.color");
  writer.Key("modules");
  writer.StartArray();

  for (const auto &lg : var.lgs) {
    writer.StartObject();
    writer.Key("name");
    auto top_name = lg->get_name();
    std::string top_name_str = {top_name.begin(), top_name.end()};
    writer.String(top_name_str.c_str());
    writer.Key("node_colors");
    
    writer.StartObject();
    for ( auto node : lg->fast(true)) {
      if (node.has_color()) {
        const auto &color_val = node.get_color();
        const auto &iname = node.default_instance_name();
        std::string iname_str = { iname.begin(), iname.end()};
        //dump this to the file with filename.
        writer.Key(iname_str.c_str());
        writer.Double(color_val);
      }
    }
    writer.EndObject();

    writer.EndObject();
  }
  writer.EndArray();//all modules captured.
  writer.EndObject();//closing dictionary with color class
  writer.EndArray();//final file closing

  auto filename = p.get_files(var);
  std::ofstream fs;
  fs.open(filename, std::ios::out | std::ios::trunc);
  if (!fs.is_open()) {
    Pass::error("Could not open file {}.\n", filename);
    return;
  }
  fs << s.GetString() << std::endl;
  fs.close();

}

