
#include <iostream>
#include <fstream>
#include "yaml-cpp/yaml.h"
#include <random>


void to_yaml() {
  std::mt19937 rnd;
  rnd.seed(0); // Repeatable random

  std::uniform_int_distribution<uint32_t> rnd_16(0,16);

  YAML::Emitter out;
  out << YAML::BeginSeq;
  for(int i=0;i<3;i++) {
    out << YAML::Value << i;
    {
      out << YAML::BeginMap;
      out << YAML::Key << "inputs";
      {
        out << YAML::BeginSeq;
        for(int j=0;j<2+rnd_16(rnd);j++) {
          out << YAML::Key << rnd_16(rnd);
          {
            out << YAML::BeginMap;

            out << YAML::Key << "bits";
            out << YAML::Value << rnd_16(rnd);
            out << YAML::Key << "src_nid";
            out << YAML::Value << rnd_16(rnd);
            out << YAML::Key << "src_eid";
            out << YAML::Value << rnd_16(rnd);

            out << YAML::EndMap;
          }
        }

        out << YAML::EndSeq;
      }
      out << YAML::Key << "delay";
      out << YAML::Value << i+0.2;
      out << YAML::Key << "op";
      out << YAML::Value << "add";
      out << YAML::EndMap;
    }
  }
  out << YAML::EndSeq;

  std::cout << out.c_str() << std::endl;
}

void from_yaml_delay(uint64_t nid, const YAML::Node node) {
  float delay = node.as<double>();

  std::cout << "nid:" << nid << " delay:" << delay << std::endl;
}

void from_yaml_op(uint64_t nid, const YAML::Node node) {
  std::string op = node.as<std::string>();

  std::cout << "nid:" << nid << " op:" << op << std::endl;
}

void from_yaml_inputs(uint64_t nid, const YAML::Node nlist) {
  uint64_t last_eid = 0;

  for(const auto &node:nlist) {

    if(node.Type() == YAML::NodeType::Scalar) {
      last_eid = std::stoi(node.as<std::string>());
    }else if(node.Type() == YAML::NodeType::Map) {
      uint16_t bits    = 1;
      uint64_t src_nid = 0;
      uint16_t src_eid = 0;

      src_nid = node["src_nid"].as<int>(); // Required
      if (node["bits"])
        bits = node["bits"].as<int>(); // Optional
      if (node["src_eid"])
        src_eid = node["src_eid"].as<int>(); // Optional

      std::cout << "dst=(" << nid << "," << last_eid << ") src=(" << src_nid << "," << src_eid << ")\n";
    }else{
      std::cerr << "Unexpected entry at line " << node.Mark().line << " col " << node.Mark().column << std::endl;
      exit(-1);
    }
  }
}

void from_yaml(std::string filename) {

  YAML::Node yfile = YAML::LoadFile(filename);

  uint64_t last_nid = 0;

  for(const auto &node:yfile) {

    if(node.Type() == YAML::NodeType::Scalar) {
      last_nid = std::stoi(node.as<std::string>());
    }else if(node.Type() == YAML::NodeType::Map) {

      std::cout << "nid: " << last_nid << std::endl;

      for(YAML::const_iterator it2=node.begin();it2!=node.end();++it2) {
        std::string key = it2->first.as<std::string>();
        if (key == "inputs") {
          from_yaml_inputs(last_nid, it2->second);
        }else if (key == "delay") {
          from_yaml_delay(last_nid, it2->second);
        }else if (key == "op") {
          from_yaml_op(last_nid, it2->second);
        }else{
          std::cerr << "Unknown [" << key << "] field for nid: " << last_nid;
          std::cerr << " at line " << node.Mark().line << " col " << node.Mark().column << std::endl;
          exit(-1);
        }
      }
    }else{
      switch (node.Type()) {
        case YAML::NodeType::Null: 
          std::cerr << "Null\n";
          break;
        case YAML::NodeType::Scalar: // ...
          std::cerr << "scalar \n";
          break;
        case YAML::NodeType::Sequence: // ...
          std::cerr << "sequence\n";
          break;
        case YAML::NodeType::Map: // ...
          std::cerr << "map\n";
          break;
        case YAML::NodeType::Undefined: // ...
          std::cerr << "undefined\n";
          break;
      }
      std::cerr << "Unexpected entry at line " << node.Mark().line << " col " << node.Mark().column << std::endl;
      exit(-1);
    }
  }
}

int main() {

  to_yaml();
  from_yaml("test.yaml");

  return 0;
}
