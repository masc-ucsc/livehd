#ifndef LGCONSTS_H
#define LGCONSTS_H

#include <assert.h>

#include <string>
#include <map>
#include "dense.hpp"

#include "lgraphbase.hpp"
#include <string>

typedef Char_Array_ID Const_ID;

class LGraph_Consts : virtual public LGraph_Base {
private:
  Char_Array<Const_ID> consts;

public:
  LGraph_Consts(std::string path, std::string name);

  virtual void clear();
  virtual void reload();
  virtual void sync();

  Const_ID get_constant_id(const char *constant);
  const char * get_constant(Const_ID const_id) const;
};

#endif
