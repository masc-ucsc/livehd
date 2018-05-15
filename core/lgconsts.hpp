#ifndef LGCONSTS_H
#define LGCONSTS_H

#include <assert.h>

#include "dense.hpp"
#include <map>
#include <string>

#include "lgraphbase.hpp"
#include <string>

typedef Char_Array_ID Const_ID;

class LGraph_Consts : virtual public LGraph_Base {
private:
  Char_Array<Const_ID> consts;

public:
  LGraph_Consts() = delete;
  explicit LGraph_Consts(const std::string & path, const std::string & name) noexcept;
  virtual ~LGraph_Consts(){};

  virtual void clear();
  virtual void reload();
  virtual void sync();

  Const_ID    get_constant_id(const char *constant);
  const char *get_constant(Const_ID const_id) const;
};

#endif
