//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fstream>
#include <iostream>

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/prettywriter.h"

#include "pass.hpp"

#include "tech_library.hpp"

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

void Tech_library::try_load_json() {
  std::string full_path = (lgdb + "/" + lib_file);

  FILE *pFile = fopen(full_path.c_str(), "rb");
  if(!pFile) {
    return;
  }

  char                      buffer[65536];
  rapidjson::FileReadStream is(pFile, buffer, sizeof(buffer));
  rapidjson::Document       document;
  document.ParseStream<0, rapidjson::UTF8<>, rapidjson::FileReadStream>(is);
  if(document.HasParseError()) {
    Pass::error(fmt::format("TechLibrary reading error \"{}\", offset: {}"
          , rapidjson::GetParseError_En(document.GetParseError())
          , static_cast<unsigned>(document.GetErrorOffset())));
    return;

  }

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
