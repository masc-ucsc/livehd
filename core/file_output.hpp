//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

class File_output {
  std::string filename;

  size_t sz;
  bool   aborted;

  std::vector<std::string> sequence;

  // Newline tallies for source-map egress: a consumer recording
  // generated positions reads `append_line()` while emitting. Prepends land
  // before every append in the final file, so a recorded append-relative line
  // becomes absolute by adding `prepend_lines()` once emission is complete.
  size_t append_newlines_  = 0;
  size_t prepend_newlines_ = 0;

  static size_t count_nl(std::string_view s) { return static_cast<size_t>(std::count(s.begin(), s.end(), '\n')); }

  // True when `filename` already holds exactly the bytes queued in `sequence`,
  // so the destructor can skip the write and leave the file's mtime alone. See
  // the comment on the definition: this is what makes a regenerated tree
  // incremental for any mtime-keyed build system downstream.
  [[nodiscard]] bool same_on_disk() const;

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

  // Mark/detach: a generator that must REWRITE a region it has already emitted
  // (inou.cgen.sim's dead-temporary sweep runs over one finished method body)
  // takes a mark before the region, detaches the region once it is complete,
  // rewrites the text, and appends the result back. A mark is only valid while
  // nothing is prepend()ed -- a prepend shifts every index.
  [[nodiscard]] size_t mark() const noexcept { return sequence.size(); }
  std::string          detach_from(size_t m) {
    std::string out;
    if (m >= sequence.size()) {
      return out;
    }
    for (size_t i = m; i < sequence.size(); ++i) {
      out += sequence[i];
    }
    sz -= out.size();
    append_newlines_ -= count_nl(out);
    sequence.erase(sequence.begin() + static_cast<std::ptrdiff_t>(m), sequence.end());
    return out;
  }

  void abort() { aborted = true; }  // abort/cancel (the destructor will do nothing)
};
