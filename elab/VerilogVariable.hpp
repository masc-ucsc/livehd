#ifndef VERILOGVARIABLE_HPP
#define VERILOGVARIABLE_HPP

#include <string>
#include <list>

#include "VerilogEnums.hpp"

typedef struct {
  std::string left;
  std::string right;
} Indices;

class VerilogVariable {
  public:
    VerilogVariable();
    ~VerilogVariable();

    void copyFrom(VerilogVariable* var_);

    std::string name;
    std::string type;
    std::string parity;
    std::string value;
    std::list<Indices*> vectorList;
    std::list<Indices*> arrayList;
};

#endif //VERILOGVERIABLE_HPP
