
#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "absl/strings/substitute.h"
#include "chunkify_verilog.hpp"
#include "eprp_utils.hpp"

#include "inou_liveparse.hpp"

void setup_inou_liveparse() {
  Inou_liveparse p;
  p.setup();
}

void Inou_liveparse::setup() {
  Eprp_method m1("inou.liveparse", "liveparse and chunkify verilog/pyrope files", &Inou_liveparse::tolg);

  m1.add_label_optional("elab_path", "elaborate (setup) lgdb path");

  register_inou(m1);
}

Inou_liveparse::Inou_liveparse()
    : Pass("liveparse") {
}

void Inou_liveparse::tolg(Eprp_var &var) {
  auto files     = var.get("files");
  auto path      = var.get("path");
  auto elab_path = var.get("elab_path");
  std::vector<Token> tlist;

  if(files.empty()) {
    error("inou.liveparse: no files provided");
    return;
  }

  std::vector<std::string> list_files = absl::StrSplit(files, ',');
  for(const auto &f : list_files) {
    int fd = open(f.c_str(), O_RDONLY);
    if(fd < 0) {
      error("could not open {}", f);
      return;
    }

    struct stat sb;
    fstat(fd, &sb);

    char *memblock = (char *)mmap(NULL, sb.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
    if(memblock == MAP_FAILED) {
      error("mmap failed??");
      close(fd);
      return;
    }

    if(absl::EndsWith(f, ".v") || absl::EndsWith(f, ".sv")) {
      Chunkify_verilog chunker(path, elab_path);
      chunker.parse(f, memblock, tlist, sb.st_size);
    } else if(absl::EndsWith(f, ".prp")) {
      error(fmt::format("inou.liveparse pyrope chunkify NOT implemented for {}", f));
      close(fd);
      munmap(memblock, sb.st_size);
      return;
    } else {
      error(fmt::format("inou.liveparse chunkify unrecognized file format {}", f));
      close(fd);
      munmap(memblock, sb.st_size);
      return;
    }

    close(fd);
    munmap(memblock, sb.st_size);
  }
}
