//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

class File_output {
  std::string filename;

  size_t sz;
  bool   aborted;

  std::vector<std::string> sequence;

  // Newline tallies for source-map egress ([[1f]]-G): a consumer recording
  // generated positions reads `append_line()` while emitting. Prepends land
  // before every append in the final file, so a recorded append-relative line
  // becomes absolute by adding `prepend_lines()` once emission is complete.
  size_t append_newlines_  = 0;
  size_t prepend_newlines_ = 0;

  static size_t count_nl(std::string_view s) { return static_cast<size_t>(std::count(s.begin(), s.end(), '\n')); }

public:
  File_output(std::string_view fname);
  ~File_output();

  void prepend(std::string s1) {
    prepend_newlines_ += count_nl(s1);
    sequence.insert(sequence.begin(), s1);
    sz += s1.size();
  }
  void append(std::string_view s1) {
    append_newlines_ += count_nl(s1);
    sequence.emplace_back(s1);
    sz += s1.size();
  }
  void append(std::string_view s1, std::string_view s2) {
    append(s1);
    append(s2);
  }
  void append(std::string_view s1, std::string_view s2, std::string_view s3) {
    append(s1);
    append(s2);
    append(s3);
  }
  void append(std::string_view s1, std::string_view s2, std::string_view s3, std::string_view s4) {
    append(s1);
    append(s2);
    append(s3);
    append(s4);
  }
  void append(std::string_view s1, std::string_view s2, std::string_view s3, std::string_view s4, std::string_view s5) {
    append(s1);
    append(s2);
    append(s3);
    append(s4);
    append(s5);
  }
  void append(std::string_view s1, std::string_view s2, std::string_view s3, std::string_view s4, std::string_view s5,
              std::string_view s6) {
    append(s1);
    append(s2);
    append(s3);
    append(s4);
    append(s5);
    append(s6);
  }

  // 0-based line index the NEXT appended character lands on, relative to the
  // appended content only (add prepend_lines() for the final absolute line).
  [[nodiscard]] size_t append_line() const noexcept { return append_newlines_; }
  [[nodiscard]] size_t prepend_lines() const noexcept { return prepend_newlines_; }

  void abort() { aborted = true; }  // abort/cancel (the destructor will do nothing)
};
