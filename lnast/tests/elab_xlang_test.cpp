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
constexpr uint8_t Xlanguage_id_if     = 128;
constexpr uint8_t Xlanguage_id_else   = 129;
constexpr uint8_t Xlanguage_id_for    = 130;
constexpr uint8_t Xlanguage_id_while  = 131;
constexpr uint8_t Xlanguage_id_elif   = 132;
constexpr uint8_t Xlanguage_id_return = 133;
constexpr uint8_t Xlanguage_id_unique = 134;
constexpr uint8_t Xlanguage_id_when   = 135;
// type
constexpr uint8_t Xlanguage_id_as = 136;
constexpr uint8_t Xlanguage_id_is = 137;
// Debug
constexpr uint8_t Xlanguage_id_I       = 138;
constexpr uint8_t Xlanguage_id_N       = 139;
constexpr uint8_t Xlanguage_id_yield   = 140;
constexpr uint8_t Xlanguage_id_waitfor = 141;
// logic op
constexpr uint8_t Xlanguage_id_and = 142;
constexpr uint8_t Xlanguage_id_or  = 143;
constexpr uint8_t Xlanguage_id_not = 144;
// Range ops
constexpr uint8_t Xlanguage_id_intersect = 145;
constexpr uint8_t Xlanguage_id_union     = 145;
constexpr uint8_t Xlanguage_id_until     = 146;
constexpr uint8_t Xlanguage_id_in        = 147;
constexpr uint8_t Xlanguage_id_by        = 148;

class Xlanguage_scanner : public Elab_scanner {
  absl::flat_hash_map<std::string, uint8_t> xlang_keyword;

public:
  // pub vars for testing
  int total;
  int n_ifs;

  Xlanguage_scanner() {
    xlang_keyword["if"]     = Xlanguage_id_if;
    xlang_keyword["else"]   = Xlanguage_id_else;
    xlang_keyword["for"]    = Xlanguage_id_for;
    xlang_keyword["while"]  = Xlanguage_id_while;
    xlang_keyword["elif"]   = Xlanguage_id_elif;
    xlang_keyword["return"] = Xlanguage_id_return;
    xlang_keyword["unique"] = Xlanguage_id_unique;
    xlang_keyword["when"]   = Xlanguage_id_when;

    xlang_keyword["as"] = Xlanguage_id_as;
    xlang_keyword["is"] = Xlanguage_id_is;

    xlang_keyword["and"] = Xlanguage_id_and;
    xlang_keyword["or"]  = Xlanguage_id_or;
    xlang_keyword["not"] = Xlanguage_id_not;

    xlang_keyword["I"]       = Xlanguage_id_I;
    xlang_keyword["N"]       = Xlanguage_id_N;
    xlang_keyword["yield"]   = Xlanguage_id_yield;
    xlang_keyword["waitfor"] = Xlanguage_id_waitfor;

    xlang_keyword["intersect"] = Xlanguage_id_intersect;
    xlang_keyword["union"]     = Xlanguage_id_union;
    xlang_keyword["until"]     = Xlanguage_id_until;
    xlang_keyword["in"]        = Xlanguage_id_in;
    xlang_keyword["by"]        = Xlanguage_id_by;

    total = 0;
    n_ifs = 0;
  }

  void elaborate() {
    patch_pass(xlang_keyword);

    total = 0;
    n_ifs = 0;
    while (!scan_is_end()) {
      // dump_token();
      total++;

      if (scan_is_token(Xlanguage_id_if)) {
        n_ifs++;
        fmt::print("IF [{}]\n", scan_text());
      }

      scan_next();
    }
    fmt::print("total tokens = {}\n", total);
  }
};

int main(int argc, char **argv) {
  Xlanguage_scanner scanner;
  if (argc == 2) {
    scanner.parse_file(argv[1]);
  } else {
    const char *txt
        = "a = 3\n"
          "if a==3 {\n"
          "  I(3)\n"
          "}\n"
          "call(a)";

    scanner.parse_inline(txt);

    I(scanner.n_ifs == 1);
    I(scanner.total == 17);
  }
}
