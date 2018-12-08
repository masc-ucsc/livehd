#ifndef VERILOGKEYWORD_HPP
#define VERILOGKEYWORD_HPP

#include <map>
#include <string>

#include "VerilogEnums.hpp"

class VerilogKeyword {
  public:
    VerilogKeyword();
    ~VerilogKeyword();

    bool             isValidKeyword(std::string str);
    bool             isParityKeyword(std::string str);

    std::string      toStr(VerilogKeyword_e num) const;
    std::string      toStr(int num) const;
    char*            toCStr(VerilogKeyword_e num) const;
    char*            toCStr(int num) const;
    VerilogKeyword_e toEnum(std::string name) const;
    VerilogKeyword_e toEnum(int num) const;
    int              toInt(std::string name) const;
    int              toInt(VerilogKeyword_e num) const;

    std::map<VerilogKeyword_e, std::string> string_map;
    std::map<std::string, VerilogKeyword_e> enum_map;
};

#endif //VERILOGKEYWORD_HPP
