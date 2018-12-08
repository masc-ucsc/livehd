#ifndef VERILOGTYPE_HPP
#define VERILOGTYPE_HPP

#include <map>
#include <string>

#include "VerilogEnums.hpp"

class VerilogType {
  public:
    VerilogType();
    ~VerilogType();

    bool          isValidType(std::string str);

    std::string   toStr(VerilogType_e num) const;
    std::string   toStr(int num) const;
    char*         toCStr(VerilogType_e num) const;
    char*         toCStr(int num) const;
    VerilogType_e toEnum(std::string name) const;
    VerilogType_e toEnum(int num) const;
    int           toInt(std::string name) const;
    int           toInt(VerilogType_e num) const;

    std::map<VerilogType_e, std::string> string_map;
    std::map<std::string, VerilogType_e> enum_map;
};

#endif //VERILOGTYPE_HPP
