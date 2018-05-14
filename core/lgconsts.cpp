#include "lgconsts.hpp"

LGraph_Consts::LGraph_Consts(std::string path, std::string name)
    : consts(path, name + "_constants") {
  locked = false;
}

void LGraph_Consts::clear() {
  consts.clear();
}

void LGraph_Consts::reload() {
  consts.reload();
}

void LGraph_Consts::sync() {
  consts.sync();
}

Const_ID LGraph_Consts::get_constant_id(const char *constant) {
  //create_id checks if ID exists
  return consts.create_id(constant);
}

const char *LGraph_Consts::get_constant(Const_ID const_id) const {
  return consts.get_char(const_id);
}
