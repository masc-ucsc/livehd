#ifndef VERILOGPARSER_HPP
#define VERILOGPARSER_HPP

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>
#include <map>
#include <list>
#include <array>

#include "elab_scanner.hpp"
#include "VerilogModule.hpp"
#include "VerilogType.hpp"
#include "VerilogKeyword.hpp"
#include "VerilogEnums.hpp"
#include "VerilogVariable.hpp"
#include "VerilogParserState.hpp"

class VerilogParser : public Elab_scanner {
  public:
    VerilogParser();
    ~VerilogParser();

    void printModules();
    void elaborate();

  private:
    VerilogType    type;
    VerilogKeyword keyword;
    VerilogModule* module;
    VerilogParserState state;
    std::list<VerilogModule*> moduleList;

    void alnumHandler(std::string token);
    void keywordHandler(std::string token);
    void variableHandler(std::string token);
    void variableDeclarationHandler(VerilogType_e type_);
    void variableArrayDeclarationHandler(std::list<Indices*> &indexList);
    void portHandler();
    void portDeclarationHandler(VerilogType_e type_);
}; //class VerilogParser : public Elab_scanner

#endif //VERILOGPARSER_HPP
