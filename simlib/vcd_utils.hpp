// This file is distributed under the LICENSE.vcd-writer License. See LICENSE for details.

#include <cstdarg>
#include <ctime>
#include <cstring>
#include <cassert>
#include <sstream>
#include <iostream>

#include <vector>
#include <string>

namespace vcd {
namespace utils {
// -----------------------------
std::string format(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  std::vector<char> v(1024);
  while (true) {
    va_list args2;
    va_copy(args2, args);
    int res = vsnprintf(v.data(), v.size(), fmt, args2);
    if ((res >= 0) && (res < static_cast<int>(v.size()))) {
      va_end(args);
      va_end(args2);
      return std::string(v.data());
    }

    size_t size;
    if (res < 0)
      size = v.size() * 2;
    else
      size = static_cast<size_t>(res) + 1;

    v.clear();
    v.resize(size);
    va_end(args2);
  }
}

// -----------------------------
std::string now() {
  time_t rawtime;
  time(&rawtime);

  struct tm *timeinfo = localtime(&rawtime);

  char buffer[128];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);

  return { buffer };
}

// -----------------------------
void replace_new_lines(std::string &str, const std::string &sub) {

  bool nl = false;
  int k = 0;

  for (auto j = 0u; j < str.size(); ++j) {
    if (str[j] == '\n' || str[j] == '\r') {
      if (!nl) // new_lines may be 2-chars length
      { for (auto c : sub) str[k++] = c; }
      nl = !nl;
    } else {
      nl       = false;
      str[k++] = str[j];
    }
  }
}

// -----------------------------
}  // namespace utils
}  // namespace vcd
