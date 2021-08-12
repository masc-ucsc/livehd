//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "mmap_str.hpp"

class File_output {
  mmap_lib::str filename;

  size_t sz;
  bool   aborted;

  std::vector<mmap_lib::str> sequence;

public:
  File_output(mmap_lib::str fname);
  ~File_output();

  void append(mmap_lib::str s1) {
    sequence.emplace_back(s1);
    sz += s1.size();
  }
  void append(mmap_lib::str s1, mmap_lib::str s2) {
    sequence.emplace_back(s1);
    sequence.emplace_back(s2);
    sz += s1.size() + s2.size();
  }
  void append(mmap_lib::str s1, mmap_lib::str s2, mmap_lib::str s3) {
    sequence.emplace_back(s1);
    sequence.emplace_back(s2);
    sequence.emplace_back(s3);
    sz += s1.size() + s2.size() + s3.size();
  }
  void append(mmap_lib::str s1, mmap_lib::str s2, mmap_lib::str s3, mmap_lib::str s4) {
    sequence.emplace_back(s1);
    sequence.emplace_back(s2);
    sequence.emplace_back(s3);
    sequence.emplace_back(s4);
    sz += s1.size() + s2.size() + s3.size() + s4.size();
  }
  void append(mmap_lib::str s1, mmap_lib::str s2, mmap_lib::str s3, mmap_lib::str s4, mmap_lib::str s5) {
    sequence.emplace_back(s1);
    sequence.emplace_back(s2);
    sequence.emplace_back(s3);
    sequence.emplace_back(s4);
    sequence.emplace_back(s5);
    sz += s1.size() + s2.size() + s3.size() + s4.size() + s5.size();
  }
  void append(mmap_lib::str s1, mmap_lib::str s2, mmap_lib::str s3, mmap_lib::str s4, mmap_lib::str s5, mmap_lib::str s6) {
    sequence.emplace_back(s1);
    sequence.emplace_back(s2);
    sequence.emplace_back(s3);
    sequence.emplace_back(s4);
    sequence.emplace_back(s5);
    sequence.emplace_back(s6);
    sz += s1.size() + s2.size() + s3.size() + s4.size() + s5.size() + s6.size();
  }

  void abort() { aborted = true; } // abort/cancel (the destructor will do nothing)
};

