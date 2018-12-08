#ifndef VERILOGMODULE_HPP
#define VERILOGMODULE_HPP

#include <stdio.h>

#include <array>
#include <list>
#include <map>
#include <string>

#include "VerilogVariable.hpp"
#include "VerilogEnums.hpp"
#include "VerilogType.hpp"

class VerilogModule {
  public:
    VerilogModule();
    ~VerilogModule();

    void setName(std::string name_);
    void addVariable(std::string name_, VerilogVariable* var_);

    std::string name;
    std::map<std::string, VerilogVariable*> variables;
};

#endif //VERILOGMODULE_HPP
