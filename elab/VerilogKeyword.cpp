#include "VerilogKeyword.hpp"

VerilogKeyword::VerilogKeyword() {
  this->string_map.insert(std::pair<VerilogKeyword_e, std::string>(VerilogKeyword_e::MODULE,    "module"));
  this->string_map.insert(std::pair<VerilogKeyword_e, std::string>(VerilogKeyword_e::ENDMODULE, "endmodule"));
  this->string_map.insert(std::pair<VerilogKeyword_e, std::string>(VerilogKeyword_e::SIGNED,    "signed"));
  this->string_map.insert(std::pair<VerilogKeyword_e, std::string>(VerilogKeyword_e::UNSIGNED,  "unsigned"));
  this->string_map.insert(std::pair<VerilogKeyword_e, std::string>(VerilogKeyword_e::ERROR,     "error"));

  this->enum_map.insert(std::pair<std::string, VerilogKeyword_e>("module",    VerilogKeyword_e::MODULE));
  this->enum_map.insert(std::pair<std::string, VerilogKeyword_e>("endmodule", VerilogKeyword_e::ENDMODULE));
  this->enum_map.insert(std::pair<std::string, VerilogKeyword_e>("signed",    VerilogKeyword_e::SIGNED));
  this->enum_map.insert(std::pair<std::string, VerilogKeyword_e>("unsigned",  VerilogKeyword_e::UNSIGNED));
  this->enum_map.insert(std::pair<std::string, VerilogKeyword_e>("error",     VerilogKeyword_e::ERROR));
}

VerilogKeyword::~VerilogKeyword() {

}

bool VerilogKeyword::isValidKeyword(std::string str) {
  auto it = this->enum_map.find(str);
  if(it != this->enum_map.end()) {
    return true;
  }
  return false;
}

bool VerilogKeyword::isParityKeyword(std::string str) {
  return str == this->string_map.at(VerilogKeyword_e::SIGNED) || str == this->string_map.at(VerilogKeyword_e::UNSIGNED);
}

std::string VerilogKeyword::toStr(VerilogKeyword_e num) const {
  auto it = this->string_map.find(num);
  if(it != this->string_map.end()) {
    return it->second;
  }
  return "error";
}

std::string VerilogKeyword::toStr(int num) const {
  return this->toStr((VerilogKeyword_e)num);
}

char* VerilogKeyword::toCStr(VerilogKeyword_e num) const {
  auto it = this->string_map.find(num);
  if(it != this->string_map.end()) {
    return (char*)it->second.c_str();
  }
  return (char*)"error";
}

char* VerilogKeyword::toCStr(int num) const {
  return this->toCStr((VerilogKeyword_e)num);
}

VerilogKeyword_e VerilogKeyword::toEnum(std::string name) const {
  auto it = this->enum_map.find(name);
  if(it != this->enum_map.end()) {
    return it->second;
  }
  return VerilogKeyword_e::ERROR;
}

VerilogKeyword_e VerilogKeyword::toEnum(int num) const {
  auto it = this->string_map.find((VerilogKeyword_e)num);
  if(it != this->string_map.end()) {
    return it->first;
  }
  return VerilogKeyword_e::ERROR;
}

int VerilogKeyword::toInt(std::string name) const {
  auto it = this->enum_map.find(name);
  if(it != this->enum_map.end()) {
    return (int)it->second;
  }
  return (int)VerilogKeyword_e::ERROR;
}

int VerilogKeyword::toInt(VerilogKeyword_e num) const {
  auto it = this->string_map.find(num);
  if(it != this->string_map.end()) {
    return (int)it->first;
  }
  return (int)VerilogKeyword_e::ERROR;
}
