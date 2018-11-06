
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <set>
#include <string>

#include "elab_scanner.hpp"

// Control
#define TOK_KEY_IF        0x40
#define TOK_KEY_ELSE      0x41
#define TOK_KEY_FOR       0x42
#define TOK_KEY_WHILE     0x43
#define TOK_KEY_ELIF      0x44
#define TOK_KEY_RETURN    0x45
#define TOK_KEY_UNIQUE    0x46
#define TOK_KEY_WHEN      0x47

// type
#define TOK_KEY_AS        0x4a
#define TOK_KEY_IS        0x4b

// Debug
#define TOK_KEY_C         0x50
#define TOK_KEY_I         0x51
#define TOK_KEY_N         0x52
#define TOK_KEY_YIELD     0x53
#define TOK_KEY_WAITFOR   0x54


// logic ops
#define TOK_KEY_AND       0x5a
#define TOK_KEY_OR        0x5b
#define TOK_KEY_NOT       0x5c

// Range ops
#define TOK_KEY_INTERSECT 0x60
#define TOK_KEY_UNION     0x61
#define TOK_KEY_UNTIL     0x62
#define TOK_KEY_IN        0x63
#define TOK_KEY_BY        0x64


class Pyrope_scanner : public Elab_scanner {
  std::map<std::string, uint8_t> pyrope_keyword;
public:
  Pyrope_scanner() {
    pyrope_keyword["if"]        = TOK_KEY_IF;
    pyrope_keyword["else"]      = TOK_KEY_ELSE;
    pyrope_keyword["for"]       = TOK_KEY_FOR;
    pyrope_keyword["while"]     = TOK_KEY_WHILE; // FUTURE
    pyrope_keyword["elif"]      = TOK_KEY_ELIF;
    pyrope_keyword["return"]    = TOK_KEY_RETURN;
    pyrope_keyword["unique"]    = TOK_KEY_UNIQUE;
    pyrope_keyword["when"]      = TOK_KEY_WHEN;

    pyrope_keyword["as"]        = TOK_KEY_AS;
    pyrope_keyword["is"]        = TOK_KEY_IS;

    pyrope_keyword["and"]       = TOK_KEY_AND;
    pyrope_keyword["or"]        = TOK_KEY_OR;
    pyrope_keyword["not"]       = TOK_KEY_NOT;

    pyrope_keyword["C"]         = TOK_KEY_C;
    pyrope_keyword["I"]         = TOK_KEY_I;
    pyrope_keyword["N"]         = TOK_KEY_N;
    pyrope_keyword["yield"]     = TOK_KEY_YIELD;
    pyrope_keyword["waitfor"]   = TOK_KEY_WAITFOR;

    pyrope_keyword["intersect"] = TOK_KEY_INTERSECT;
    pyrope_keyword["union"]     = TOK_KEY_UNION;
    pyrope_keyword["until"]     = TOK_KEY_UNTIL;
    pyrope_keyword["in"]        = TOK_KEY_IN;
    pyrope_keyword["by"]        = TOK_KEY_BY;
  }

  void elaborate() {

    patch_pass(pyrope_keyword);

    int total = 0;
    while(!scan_is_end()) {

      //dump_token();
      total++;

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
