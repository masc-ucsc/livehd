//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>
#include <string_view>
#include <vector>

class File_output {
  std::string filename;

  size_t sz;
  bool   aborted;

  std::vector<std::string> sequence;

public:
  File_output(std::string_view fname);
  ~File_output();

  void prepend(std::string s1) {
    sequence.insert(sequence.begin(), s1);
    sz += s1.size();
  }
  void append(std::string_view s1) {
    sequence.emplace_back(s1);
    sz += s1.size();
  }
  void append(std::string_view s1, std::string_view s2) {
    sequence.emplace_back(s1);
    sequence.emplace_back(s2);
    sz += s1.size() + s2.size();
  }
  void append(std::string_view s1, std::string_view s2, std::string_view s3) {
    sequence.emplace_back(s1);
    sequence.emplace_back(s2);
    sequence.emplace_back(s3);
    sz += s1.size() + s2.size() + s3.size();
  }
  void append(std::string_view s1, std::string_view s2, std::string_view s3, std::string_view s4) {
    sequence.emplace_back(s1);
    sequence.emplace_back(s2);
    sequence.emplace_back(s3);
    sequence.emplace_back(s4);
    sz += s1.size() + s2.size() + s3.size() + s4.size();
  }
  void append(std::string_view s1, std::string_view s2, std::string_view s3, std::string_view s4, std::string_view s5) {
    sequence.emplace_back(s1);
    sequence.emplace_back(s2);
    sequence.emplace_back(s3);
    sequence.emplace_back(s4);
    sequence.emplace_back(s5);
    sz += s1.size() + s2.size() + s3.size() + s4.size() + s5.size();
  }
  void append(std::string_view s1, std::string_view s2, std::string_view s3, std::string_view s4, std::string_view s5,
              std::string_view s6) {
    sequence.emplace_back(s1);
    sequence.emplace_back(s2);
    sequence.emplace_back(s3);
    sequence.emplace_back(s4);
    sequence.emplace_back(s5);
    sequence.emplace_back(s6);
    sz += s1.size() + s2.size() + s3.size() + s4.size() + s5.size() + s6.size();
  }

  void abort() { aborted = true; }  // abort/cancel (the destructor will do nothing)
};
