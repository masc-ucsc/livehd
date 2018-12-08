#include "VerilogParserState.hpp"

VerilogParserState::VerilogParserState() {
  this->string_map.insert(std::pair<VerilogParserState_e, std::string>(VerilogParserState_e::IN_MODULE,           "in module"));
  this->string_map.insert(std::pair<VerilogParserState_e, std::string>(VerilogParserState_e::IN_FUNCTION,         "in function"));
  this->string_map.insert(std::pair<VerilogParserState_e, std::string>(VerilogParserState_e::IN_TASK,             "in task"));
  this->string_map.insert(std::pair<VerilogParserState_e, std::string>(VerilogParserState_e::IN_BLOCK,            "in block"));
  this->string_map.insert(std::pair<VerilogParserState_e, std::string>(VerilogParserState_e::IN_PORT_DECLARATION, "in port declaration"));
  this->string_map.insert(std::pair<VerilogParserState_e, std::string>(VerilogParserState_e::ERROR,               "error"));

  this->enum_map.insert(std::pair<std::string, VerilogParserState_e>("in module",           VerilogParserState_e::IN_MODULE));
  this->enum_map.insert(std::pair<std::string, VerilogParserState_e>("in function",         VerilogParserState_e::IN_FUNCTION));
  this->enum_map.insert(std::pair<std::string, VerilogParserState_e>("in task",             VerilogParserState_e::IN_TASK));
  this->enum_map.insert(std::pair<std::string, VerilogParserState_e>("in block",            VerilogParserState_e::IN_BLOCK));
  this->enum_map.insert(std::pair<std::string, VerilogParserState_e>("in port declaration", VerilogParserState_e::IN_PORT_DECLARATION));
  this->enum_map.insert(std::pair<std::string, VerilogParserState_e>("error",               VerilogParserState_e::ERROR));

  this->stateList.reset();
}

VerilogParserState::~VerilogParserState() {

}

void VerilogParserState::toggle(VerilogParserState_e state) {
  this->stateList.flip(static_cast<int>(state));
}

bool VerilogParserState::in(VerilogParserState_e state) {
  return this->stateList.test(static_cast<int>(state));
}
