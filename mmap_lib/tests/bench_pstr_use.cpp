#include "fmt/format.h"
#include "iassert.hpp"
#include <vector>
#include "lrand.hpp"
#include "lbench.hpp"
#include "flat_hash_map.hpp"
#include <type_traits>
#include "mmap_str.hpp"
#include "mmap_map.hpp"


void mmap_pstr(std::string_view name) {
  
mmap_lib::str foo("yohelloooooo12345678");
  //foo.print_size(); // expect 8
  //foo.print_e(); // 2345678
  //foo.print_PoS(); 
  //std::cout << foo.is_i() << std::endl; 
}


int main(int argc, char **argv) {

  mmap_pstr("bench_map_use_mmap.data");
  return 0;
}
