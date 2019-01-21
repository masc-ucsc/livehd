//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>

#include "absl/container/flat_hash_map.h"

#include "elab_scanner.hpp"

// control
constexpr uint8_t Pyrope_id_if   = 128;
constexpr uint8_t Pyrope_id_else = 129;
constexpr uint8_t Pyrope_id_for  = 130;
constexpr uint8_t Pyrope_id_while= 131;
constexpr uint8_t Pyrope_id_elif = 132;
constexpr uint8_t Pyrope_id_return = 133;
constexpr uint8_t Pyrope_id_unique = 134;
constexpr uint8_t Pyrope_id_when = 135;
// type
constexpr uint8_t Pyrope_id_as = 136;
constexpr uint8_t Pyrope_id_is = 137;
// Debug
constexpr uint8_t Pyrope_id_I = 138;
constexpr uint8_t Pyrope_id_N = 139;
constexpr uint8_t Pyrope_id_yield = 140;
constexpr uint8_t Pyrope_id_waitfor = 141;
// logic op
constexpr uint8_t Pyrope_id_and = 142;
constexpr uint8_t Pyrope_id_or = 143;
constexpr uint8_t Pyrope_id_not = 144;
// Range ops
constexpr uint8_t Pyrope_id_intersect = 145;
constexpr uint8_t Pyrope_id_union = 145;
constexpr uint8_t Pyrope_id_until = 146;
constexpr uint8_t Pyrope_id_in = 147;
constexpr uint8_t Pyrope_id_by = 148;

class Pyrope_scanner : public Elab_scanner {
  absl::flat_hash_map<std::string, uint8_t> pyrope_keyword;
public:
  Pyrope_scanner() {
    pyrope_keyword["if"]        = Pyrope_id_if;
    pyrope_keyword["else"]      = Pyrope_id_else;
    pyrope_keyword["for"]       = Pyrope_id_for;
    pyrope_keyword["while"]     = Pyrope_id_while; // FUTURE
    pyrope_keyword["elif"]      = Pyrope_id_elif;
    pyrope_keyword["return"]    = Pyrope_id_return;
    pyrope_keyword["unique"]    = Pyrope_id_unique;
    pyrope_keyword["when"]      = Pyrope_id_when;

    pyrope_keyword["as"]        = Pyrope_id_as;
    pyrope_keyword["is"]        = Pyrope_id_is;

    pyrope_keyword["and"]       = Pyrope_id_and;
    pyrope_keyword["or"]        = Pyrope_id_or;
    pyrope_keyword["not"]       = Pyrope_id_not;

    pyrope_keyword["I"]         = Pyrope_id_I;
    pyrope_keyword["N"]         = Pyrope_id_N;
    pyrope_keyword["yield"]     = Pyrope_id_yield;
    pyrope_keyword["waitfor"]   = Pyrope_id_waitfor;

    pyrope_keyword["intersect"] = Pyrope_id_intersect;
    pyrope_keyword["union"]     = Pyrope_id_union;
    pyrope_keyword["until"]     = Pyrope_id_until;
    pyrope_keyword["in"]        = Pyrope_id_in;
    pyrope_keyword["by"]        = Pyrope_id_by;
  }

  void elaborate() {

    patch_pass(pyrope_keyword);

    int total = 0;
    while(!scan_is_end()) {

      //dump_token();
      total++;

      if (scan_is_token(Token_id_label)) {
        std::string txt;
        scan_append(txt);
        fmt::print("LABEL [{}]\n",txt);
      }
      if (scan_is_token(Pyrope_id_if)) {
        std::string txt;
        scan_append(txt);
        fmt::print("IF [{}]\n",txt);
      }

      scan_next();
    }
    fmt::print("total tokens = {}\n",total);
  }
};

int main(int argc, char **argv) {

  if(argc != 2) {
    fprintf(stderr, "Usage:\n\t%s file\n", argv[0]);
    exit(-3);
  }

  int fd = open(argv[1], O_RDONLY);
  if(fd < 0) {
    fprintf(stderr, "error, could not open %s\n", argv[1]);
    exit(-3);
  }

  struct stat sb;
  fstat(fd, &sb);

  char *memblock = (char *)mmap(NULL, sb.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
  if(memblock == MAP_FAILED) {
    fprintf(stderr, "error, mmap failed\n");
    exit(-3);
  }

  Pyrope_scanner scanner;

  scanner.parse(argv[1], memblock, sb.st_size);
}
