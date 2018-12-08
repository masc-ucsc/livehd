#include "VerilogType.hpp"

VerilogType::VerilogType() {
  this->string_map.insert(std::pair<VerilogType_e, std::string>(VerilogType_e::INPUT,     "input"));
  this->string_map.insert(std::pair<VerilogType_e, std::string>(VerilogType_e::OUTPUT,    "output"));
  this->string_map.insert(std::pair<VerilogType_e, std::string>(VerilogType_e::INOUT,     "inout"));
  this->string_map.insert(std::pair<VerilogType_e, std::string>(VerilogType_e::WIRE,      "wire"));
  this->string_map.insert(std::pair<VerilogType_e, std::string>(VerilogType_e::REG,       "reg"));
  this->string_map.insert(std::pair<VerilogType_e, std::string>(VerilogType_e::TRI,       "tri"));
  this->string_map.insert(std::pair<VerilogType_e, std::string>(VerilogType_e::PARAMETER, "parameter"));
  this->string_map.insert(std::pair<VerilogType_e, std::string>(VerilogType_e::DEFPARAM,  "defparam"));
  this->string_map.insert(std::pair<VerilogType_e, std::string>(VerilogType_e::ERROR,     "error"));

  this->enum_map.insert(std::pair<std::string, VerilogType_e>("input",     VerilogType_e::INPUT));
  this->enum_map.insert(std::pair<std::string, VerilogType_e>("output",    VerilogType_e::OUTPUT));
  this->enum_map.insert(std::pair<std::string, VerilogType_e>("inout",     VerilogType_e::INOUT));
  this->enum_map.insert(std::pair<std::string, VerilogType_e>("wire",      VerilogType_e::WIRE));
  this->enum_map.insert(std::pair<std::string, VerilogType_e>("reg",       VerilogType_e::REG));
  this->enum_map.insert(std::pair<std::string, VerilogType_e>("tri",       VerilogType_e::TRI));
  this->enum_map.insert(std::pair<std::string, VerilogType_e>("parameter", VerilogType_e::PARAMETER));
  this->enum_map.insert(std::pair<std::string, VerilogType_e>("defparam",  VerilogType_e::DEFPARAM));
  this->enum_map.insert(std::pair<std::string, VerilogType_e>("error",     VerilogType_e::ERROR));
}

VerilogType::~VerilogType() {

}

bool VerilogType::isValidType(std::string name) {
  auto it = this->enum_map.find(name);
  if(it != this->enum_map.end()) {
    return true;
  }
  return false;
}

std::string VerilogType::toStr(VerilogType_e num) const {
  auto it = this->string_map.find(num);
  if(it != this->string_map.end()) {
    return it->second;
  }
  return "error";
}

std::string VerilogType::toStr(int num) const {
  return this->toStr((VerilogType_e)num);
}

char* VerilogType::toCStr(VerilogType_e num) const {
  auto it = this->string_map.find(num);
  if(it != this->string_map.end()) {
    return (char*)it->second.c_str();
  }
  return (char*)"error";
}

char* VerilogType::toCStr(int num) const {
  return this->toCStr((VerilogType_e)num);
}

VerilogType_e VerilogType::toEnum(std::string name) const {
  auto it = this->enum_map.find(name);
  if(it != this->enum_map.end()) {
    return it->second;
  }
  return VerilogType_e::ERROR;
}

VerilogType_e VerilogType::toEnum(int num) const {
  auto it = this->string_map.find((VerilogType_e)num);
  if(it != this->string_map.end()) {
    return it->first;
  }
  return VerilogType_e::ERROR;
}

int VerilogType::toInt(std::string name) const {
  auto it = this->enum_map.find(name);
  if(it != this->enum_map.end()) {
    return (int)it->second;
  }
  return (int)VerilogType_e::ERROR;
}

int VerilogType::toInt(VerilogType_e num) const {
  auto it = this->string_map.find(num);
  if(it != this->string_map.end()) {
    return (int)it->first;
  }
  return (int)VerilogType_e::ERROR;
}

