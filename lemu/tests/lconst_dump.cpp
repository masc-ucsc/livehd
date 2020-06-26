//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "fmt/format.h"

#include "lconst.hpp"
#include "lbench.hpp"
#include "mmap_map.hpp"

int main(int argc, char **argv) {

  if(argc != 2) {
    fprintf(stderr, "Usage:\n\t%s const_dump_file\n", argv[0]);
    exit(-3);
  }

  mmap_lib::map<uint32_t, Lconst::Container> map(".",argv[1]);

  for (const auto &it:map) {
    const auto &key = map.get_key(it);
    const auto  val = Lconst(map.get(it));
    fmt::print("{}:{}\n", key, val.to_pyrope());
  }

  return 0;
}
