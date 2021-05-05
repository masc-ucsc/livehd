//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "mmap_str.hpp"

//#define HACK1

#ifdef HACK1
mmap_lib::map<std::string_view, uint32_t> mmap_lib::str::string_map2("lgdb","file1");
#else
mmap_lib::map<std::string_view, uint32_t> mmap_lib::str::string_map2;
#endif

#if 0
std::array<mmap_lib::map<std::string_view, uint32_t>,4> mmap_lib::str::string_map2;

string_map2[0] is no disk saved (same as now)
string_map2[1] "lgdb/mmap_str1" // LNAST
string_map2[2] "lgdb/mmap_str2" // LGRaph
string_map2[3] "lgdb/mmap_str3" // Other

operator==() {
  string_map[0] != string_map[0]
  string_map[0] != string_map[1] {
    get_sv() != get_sv();
  }

#endif
