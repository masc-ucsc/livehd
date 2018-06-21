//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef NODESRCFILE_H
#define NODESRCFILE_H

#include <assert.h>

#include "dense.hpp"
#include <map>
#include <string>

#include "lgraphbase.hpp"
#include <string>

class File_Loc {
protected:
  uint8_t  length;
  uint8_t  start_hchunk;
  uint16_t start_lchunk;

public:
  File_Loc(uint32_t loc_start, uint32_t loc_length)
      : length((uint8_t)loc_length)
      , start_hchunk((uint8_t)loc_start >> 16)
      , start_lchunk((uint16_t)loc_start) {
  }

  uint32_t get_start() const {
    return (((uint32_t)start_hchunk) << 16) | start_lchunk;
  }
  uint32_t get_length() const {
    return length;
  }
};

class LGraph_Node_Src_Loc : virtual public LGraph_Base {
private:
  Char_Array<File_Loc> src_files;
  Dense<int>           node_src_loc;

public:
  LGraph_Node_Src_Loc() = delete;
  explicit LGraph_Node_Src_Loc(const std::string &path, const std::string &name) noexcept;
  virtual ~LGraph_Node_Src_Loc(){};

  virtual void clear();
  virtual void reload();
  virtual void sync();
  virtual void emplace_back();

  void        node_loc_set(Index_ID nid, const char *file_name, uint32_t offset, uint32_t length);
  const char *node_file_name_get(Index_ID nid) const;
  File_Loc    node_file_loc_get(Index_ID nid) const;
};

#endif
