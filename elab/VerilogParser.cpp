#include <VerilogParser.hpp>

VerilogParser::VerilogParser() {

}

VerilogParser::~VerilogParser() {
  for(auto modulesList : this->moduleList) {
    delete modulesList;
  }
}

void VerilogParser::printModules() {
  bool displayTypeHeader;
  int  counter = 1;

  for(auto modulesList : this->moduleList) {
    printf("%s\n", std::string(50, '=').c_str());
    printf("= MODULE : %s %*s\n", modulesList->name.c_str(), (int)(50 - 12 - modulesList->name.length()), "=");
    printf("%s\n", std::string(50, '=').c_str());
    printf("%*s=================\n", 2, " ");
    printf("%*s= VARIABLE LIST =\n", 2, " ");
    printf("%*s=================\n", 2, " ");
    for(auto typeList : this->type.string_map) {
      displayTypeHeader = true;
      counter = 1;
      for(auto variableList : modulesList->variables) {
        if(variableList.second->type == typeList.second) {
          if(displayTypeHeader) {
            printf("%*s%s\n", 4, " ", std::string(typeList.second.length() + 4, '=').c_str());
            printf("%*s= %s =\n", 4, " ", typeList.second.c_str());
            printf("%*s%s\n", 4, " ", std::string(typeList.second.length() + 4, '=').c_str());
            displayTypeHeader = false;
          }
          printf("%*sname - %s\n", 6, " ", variableList.second->name.c_str());
          printf("%*stype%*s: %s\n", 8, " ", 10, " ", variableList.second->type.c_str());
          if(variableList.second->parity.length()) {
            printf("%*sparity%*s: %s\n", 8, " ", 8, " ", variableList.second->parity.c_str());
          }
          if(variableList.second->value.length()) {
            printf("%*svalue%*s: %s\n", 8, " ", 9, " ", variableList.second->value.c_str());
          }
          for(auto vectorList : variableList.second->vectorList) {
            printf("%*svector[%d]\n", 8, " ", counter++);
            printf("%*sleft index  : %s\n", 10, " ", vectorList->left.c_str());
            printf("%*sright index : %s\n", 10, " ", vectorList->right.c_str());
          }
          counter = 1;
          for(auto arrayList : variableList.second->arrayList) {
            printf("%*sarray[%d]\n", 8, " ", counter++);
            printf("%*sleft index  : %s\n", 10, " ", arrayList->left.c_str());
            printf("%*sright index : %s\n", 10, " ", arrayList->right.c_str());
          }
          printf("\n");
        }
      }
    }
  }
}

void VerilogParser::elaborate() {
  std::string token;

  while(!this->scan_is_end()) {
    switch(this->get_token_id()) {
      case TOK_ALNUM: //alphanumeric
        this->scan_append(token);
        this->alnumHandler(token);
        break;

//       case TOK_OP: //open paren
//         if(this->state.in(VerilogParserState_e::IN_PORT_DECLARATION)) {
//           this->portHandler();
//           this->state.toggle(VerilogParserState_e::IN_PORT_DECLARATION);
//         }
//         break;
        
      default:
        break;
    }

    token.erase();
    this->scan_next();
  } //while(!scan_is_end())
}

void VerilogParser::alnumHandler(std::string token) {
  if(this->keyword.isValidKeyword(token)) {
    this->keywordHandler(token);
  }
  else if(this->type.isValidType(token)) {
    this->variableHandler(token);
  }
}

void VerilogParser::keywordHandler(std::string token) {
  std::string tmpStr;

  switch(this->keyword.toEnum(token)) {
    case VerilogKeyword_e::MODULE:
      if(!this->state.in(VerilogParserState_e::IN_MODULE)) {
        this->scan_next();
        this->scan_append(tmpStr);
        this->module = new VerilogModule();
        this->module->setName(tmpStr);
        this->state.toggle(VerilogParserState_e::IN_MODULE);
//         this->state.toggle(VerilogParserState_e::IN_PORT_DECLARATION);
      }
      break;

    case VerilogKeyword_e::ENDMODULE:
      this->moduleList.push_back(this->module);
      this->state.toggle(VerilogParserState_e::IN_MODULE);
      break;

    default:
      break;
  }
}

void VerilogParser::variableHandler(std::string token) {
  switch(this->type.toEnum(token)) {
    case VerilogType_e::INPUT:
      this->variableDeclarationHandler(VerilogType_e::INPUT);
      break;

    case VerilogType_e::OUTPUT:
      this->variableDeclarationHandler(VerilogType_e::OUTPUT);
      break;

    case VerilogType_e::INOUT:
      this->variableDeclarationHandler(VerilogType_e::INOUT);
      break;

    case VerilogType_e::REG:
      this->variableDeclarationHandler(VerilogType_e::REG);
      break;

    case VerilogType_e::TRI:
      this->variableDeclarationHandler(VerilogType_e::TRI);
      break;

    case VerilogType_e::WIRE:
      this->variableDeclarationHandler(VerilogType_e::WIRE);
      break;

    default:
      break;
  }
}

void VerilogParser::variableDeclarationHandler(VerilogType_e type_) {
  VerilogVariable* newVariable;
  VerilogVariable* prevVariable;
  std::string      token;
  bool             initialVariable = true;
  bool             memoryAllocated = false;
  bool             vectorSet       = false;

  while(this->get_token_id() != TOK_SEMICOLON) {
    token.erase();
    this->scan_next();
    this->scan_append(token);

    //assuming any variable declared on the same line after the initial, 
    //(those separated by comma's), share the same attributes
    //need to keep track of the initial variable to copy data to subsequent ones
    if(initialVariable) {
      if(!memoryAllocated) {
        newVariable = new VerilogVariable;
        newVariable->type = this->type.toStr(type_);
        memoryAllocated = true;
      }

      if(this->keyword.isParityKeyword(token)) {    //check for <signed> or <unsigned> keywords
        newVariable->parity = token;
      }
      else if(this->type.isValidType(token)) {      //check for <reg>, <tri>, <wire> keywords
        newVariable->type = token;
      }
      else if(this->get_token_id() == TOK_OBR) {    //check for <[>, meaning a vector or array declaration
        if(!vectorSet) {      //TYPE [#:#][#:#]...[#:#]
          this->variableArrayDeclarationHandler(newVariable->vectorList);
        } else {              //VAR [#:#][#:#]...[#:#]
          this->variableArrayDeclarationHandler(newVariable->arrayList);
        }
      }
      else if(    this->get_token_id() == TOK_COMMA
               || this->get_token_id() == TOK_SEMICOLON) {  //commit variable to list
        this->module->addVariable(newVariable->name, newVariable);
        prevVariable = newVariable;
        initialVariable = false;
        memoryAllocated = false;
      }
      else {                                        //the variable name
        newVariable->name = token;
        vectorSet = true;
      }
    }
    else {
      if(!memoryAllocated) {
        newVariable = new VerilogVariable;
        newVariable->copyFrom(prevVariable);
        memoryAllocated = true;
      }

      if(this->get_token_id() == TOK_OBR) {         //can only declare arrays at this stage
        this->variableArrayDeclarationHandler(newVariable->arrayList);
      }
      else if(    this->get_token_id() == TOK_COMMA
               || this->get_token_id() == TOK_SEMICOLON) {  //commit variable to list
        this->module->addVariable(newVariable->name, newVariable);
        prevVariable = newVariable;
        memoryAllocated = false;
      }
      else {                                        //the variable name
        newVariable->name = token;
      }
    }
  }
}

void VerilogParser::variableArrayDeclarationHandler(std::list<Indices*> &indexList) {
  std::string token;
  Indices* newPair = new Indices;

  while(this->get_token_id() != TOK_COLON) {  //colon separates first index from second
    token.erase();
    this->scan_next();
    this->scan_append(token);
    newPair->left += token;
  }

  while(this->get_token_id() != TOK_CBR) {
    token.erase();
    this->scan_next();
    this->scan_append(token);
    newPair->right += token;
  }

  newPair->left.pop_back();     //delete :
  newPair->right.pop_back();    //delete ]
  indexList.push_back(newPair);
}

// void VerilogParser::portHandler() {
//   std::string token;
//
//   while(    this->get_token_id() != TOK_CP
//          || this->get_token_id() != TOK_SEMICOLON) {
//     token.erase();
//     this->scan_next();
//     this->scan_append(token);
//
//     switch(this->type.toEnum(token)) {
//       case VerilogType_e::INPUT:
//         this->portDeclarationHandler(VerilogType_e::INPUT);
//         break;
//
//       case VerilogType_e::OUTPUT:
//         this->portDeclarationHandler(VerilogType_e::OUTPUT);
//         break;
//
//       case VerilogType_e::INOUT:
//         this->portDeclarationHandler(VerilogType_e::INOUT);
//         break;
//
//       default:
//         break;
//     }
//   }
// }
//
// void VerilogParser::portDeclarationHandler(VerilogType_e type_) {
//   VerilogVariable* newVariable;
//   std::string      token;
//   bool             memoryAllocated = false;
//
//   while(this->get_token_id() != TOK_CP) {
//     token.erase();
//     this->scan_next();
//     this->scan_append(token);
//
//     if(!memoryAllocated) {
//       newVariable = new VerilogVariable;
//       newVariable->type = this->type.toStr(type_);
//       memoryAllocated = true;
//     }
//
//     if(this->keyword.isParityKeyword(token)) {    //check for <signed> or <unsigned> keywords
//       newVariable->parity = token;
//     }
//     else if(this->type.isValidType(token)) {      //check for <reg>, <tri>, <wire> keywords
//       newVariable->type = token;
//     }
//     else if(this->get_token_id() == TOK_OBR) {    //check for <[>, meaning a vector or array declaration
//       this->variableArrayDeclarationHandler(newVariable->vectorList);
//     }
//     else if(this->get_token_id() != TOK_CP) {
//       newVariable->name = token;
//       this->module->addVariable(newVariable->name, newVariable);
//       break;
//     }
//   }
// }
