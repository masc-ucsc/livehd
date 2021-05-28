//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "mmap_str.hpp"

template <int map_id>
mmap_lib::map<std::string_view, bool> mmap_lib::str<map_id>::m0;

template <int map_id>
mmap_lib::map<std::string_view, bool> mmap_lib::str<map_id>::m1("lgdb", "strMap1");

template <int map_id>
mmap_lib::map<std::string_view, bool> mmap_lib::str<map_id>::m2("lgdb", "strMap2");

template <int map_id>
mmap_lib::map<std::string_view, bool> mmap_lib::str<map_id>::m3("lgdb", "strMap3");

template class mmap_lib::str<0>;
template class mmap_lib::str<1>;
template class mmap_lib::str<2>;
template class mmap_lib::str<3>;
