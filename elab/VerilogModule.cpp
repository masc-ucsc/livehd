#include "VerilogModule.hpp"

VerilogModule::VerilogModule() {

}

VerilogModule::~VerilogModule() {
  for(auto var : this->variables) {
    delete var.second;
  }
}

void VerilogModule::setName(std::string name_) {
  this->name = name_;
}

void VerilogModule::addVariable(std::string name_, VerilogVariable* var_) {
  this->variables.insert(std::pair<std::string, VerilogVariable*>(name_, var_));
}

