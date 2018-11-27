//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fstream>
#include <iostream>

#include "tech_library.hpp"

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/prettywriter.h"

std::unordered_map<std::string, Tech_library *> Tech_library::instances;

bool Tech_library::include(const std::string &name) const {
  return cname2id.find(name) != cname2id.end();
}

uint16_t Tech_library::get_cell_id(const std::string &name) const {
  assert(include(name));
  return cname2id.at(name);
}

uint16_t Tech_library::create_cell_id(const std::string &name) {
  assert(!include(name));
  assert(cell_types.size() < std::numeric_limits<uint16_t>::max()); // increase to uin32t if needed

  // FIXME: Make sure that the cells are added in alphabetical order (otherwise, loading/unloading
  // can have different names and trigger a corrupt lgraph)

  uint16_t newid = cell_types.size();
  cell_types.push_back(Tech_cell(name, newid));
  cname2id.insert(std::make_pair(name, newid));

  clean = false;
  return cname2id[name];
}

Tech_cell *Tech_library::get_cell(uint16_t cell_id) {
  assert(cell_types.size() > cell_id);
  return &(cell_types.at(cell_id));
}

const Tech_cell *Tech_library::get_const_cell(uint16_t cell_id) const {
  assert(cell_types.size() > cell_id);
  return &(cell_types.at(cell_id));
}

#if 0
void Tech_library::load() {

  std::string full_path = (lgdb + "/" + lib_file);
  std::ifstream tech_lib_f(full_path.c_str());

  if(!tech_lib_f.good()) {
    //console->error("ERROR: could not open internal techlib file {}\n", full_path);
    console->info("Internal techlib file not loaded\n");
    return;
  }
  tech_lib_f.close();

  YAML::Node yfile;
  try {
    yfile = YAML::LoadFile(full_path);
  } catch(YAML::ParserException& e) {
    std::cerr << e.what() << std::endl;
    console->error("ERROR: invalid syntax in the internal techlib file {}\n", full_path);
  }

  std::vector<std::string> inputs;
  std::vector<std::string> outputs;
  Tech_cell* tcell;
  for(const auto &node:yfile) {
    assert(node.Type() == YAML::NodeType::Map);

    for(YAML::const_iterator it2=node.begin();it2!=node.end();++it2) {
      std::string key = it2->first.as<std::string>();

      if(key == "cell") {
        // FIXME: Put cell names in a sorted array first. Then populate calling create_cell_id. The
        // reason is that no matter the order of the names, the cell id should be the same (or
        // problems in mapping would happen)

        tcell = get_cell(create_cell_id(it2->second.as<std::string>()));
      } else if(key == "inps") {
        for(const auto &inp : it2->second) {
          inputs.push_back(inp.as<std::string>());
        }
      } else if(key == "outs") {
        for(const auto &oup : it2->second) {
          outputs.push_back(oup.as<std::string>());
        }
      } else {
        console->error("Error parsing internal tech library, unrecognized option {}\n", key);
      }
    }
    for(const auto& inp : inputs) {
      tcell->set_direction(tcell->add_pin(inp), Tech_cell::Direction::input);
    }
    for(const auto& oup : outputs) {
      tcell->set_direction(tcell->add_pin(oup), Tech_cell::Direction::output);
    }
    tcell = nullptr;
    inputs.clear();
    outputs.clear();
  }

  tech_lib_f.close();
  clean = true;
}
#endif

void Tech_library::load_json() {
  std::string full_path = (lgdb + "/" + lib_file);
  FILE *      pFile     = fopen(full_path.c_str(), "rb");
  if(!pFile) {
    console->info("Internal techlib file not loaded\n");
    return;
  }
  char                      buffer[65536];
  rapidjson::FileReadStream is(pFile, buffer, sizeof(buffer));
  rapidjson::Document       document;
  document.ParseStream<0, rapidjson::UTF8<>, rapidjson::FileReadStream>(is);
  if(document.HasParseError()) {
    console->error("TechLibrary reading error \"{}\", offset: {}\n", rapidjson::GetParseError_En(document.GetParseError()),
                   static_cast<unsigned>(document.GetErrorOffset()));
    exit(-1);

  } else {
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    Tech_cell *              tcell;
    assert(document.HasMember("cells"));
    const rapidjson::Value &nodeArray = document["cells"];

    assert(nodeArray.IsArray());
    for(const auto &nodes : nodeArray.GetArray()) {
      assert(nodes.IsObject());

      assert(nodes.HasMember("cell"));
      tcell = get_cell(create_cell_id(nodes["cell"].GetString()));
      if(nodes.HasMember("inps")) {
        assert(nodes["inps"].IsArray());
        for(const auto &inp : nodes["inps"].GetArray()) {
          inputs.emplace_back(inp.GetString());
        }
      }
      if(nodes.HasMember("outs")) {
        assert(nodes["outs"].IsArray());
        for(const auto &oup : nodes["outs"].GetArray()) {
          outputs.emplace_back(oup.GetString());
        }
      }

      for(const auto &inp : inputs) {
        tcell->add_pin(inp, Tech_cell::Direction::input);
      }
      for(const auto &oup : outputs) {
        tcell->add_pin(oup, Tech_cell::Direction::output);
      }

      inputs.clear();
      outputs.clear();
    }
  }
  fclose(pFile);
  clean = true;
}

#if 0
void Tech_library::to_yaml() const {
  YAML::Emitter out;
  out << YAML::BeginSeq;

  for(auto& cell : cell_types) {
    out << YAML::BeginMap;
    out << YAML::Key << "cell";
    out << YAML::Value << cell.get_name();

    out << YAML::Key << "inps";
    {
      out << YAML::BeginSeq;

      for(auto& inp : cell.get_inputs()) {
        out << YAML::Value << cell.get_name(inp);
      }
      out << YAML::EndSeq;
    }

    out << YAML::Key << "outs";
    {
      out << YAML::BeginSeq;

      for(auto& oup : cell.get_outputs()) {
        out << YAML::Value << cell.get_name(oup);
      }
      out << YAML::EndSeq;
    }
    out << YAML::EndMap;
  }
  out << YAML::EndSeq;

  std::string full_path = lgdb + "/" + lib_file;
  std::ofstream fs(full_path);
  if(!fs.is_open()) {
    std::cerr << "ERROR: could not open internal library file [" << full_path << "] for writing\n";
    exit(-1);
  }
  fs << out.c_str() << std::endl;
  fs.close();
}
#endif

void Tech_library::to_json() const {
  rapidjson::StringBuffer                          s;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(s);
  {
    writer.StartObject();
    writer.Key("cells");
    writer.StartArray();
    for(auto &cell : cell_types) {
      writer.StartObject();
      writer.Key("cell");
      writer.String(cell.get_name().c_str());

      writer.Key("size");
      writer.StartArray();
      writer.Double(cell.get_cell_size().first);
      writer.Double(cell.get_cell_size().second);
      writer.EndArray();

      writer.Key("inps");
      writer.StartArray();
      for(auto &inp : cell.get_inputs()) {
        writer.String(cell.get_name(inp).c_str());
      }
      writer.EndArray();

      writer.Key("outs");
      writer.StartArray();
      for(auto &oup : cell.get_outputs()) {
        writer.String(cell.get_name(oup).c_str());
      }
      writer.EndArray();

      writer.EndObject();
    }
    writer.EndArray();

    writer.EndObject();
  }

  std::string   full_path = lgdb + "/" + lib_file;
  std::ofstream fs(full_path);
  if(!fs.is_open()) {
    std::cerr << "ERROR: could not open internal library file [" << full_path << "] for writing\n";
    exit(-1);
  }
  fs << s.GetString() << std::endl;
  fs.close();
}
