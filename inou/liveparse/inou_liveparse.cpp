
#include <sys/mman.h>
#include <unistd.h>

#include "chunkify_verilog.hpp"
#include "eprp_utils.hpp"

#include "inou_liveparse.hpp"

void setup_inou_liveparse() {
  Inou_liveparse p;
  p.setup();
}

void Inou_liveparse::setup() {
  Eprp_method m1("inou.liveparse", "liveparse and chunkify verilog/pyrope files", &Inou_liveparse::tolg);

  m1.add_label_optional("elab_path","elaborate (setup) lgdb path");

  register_inou(m1);
}

Inou_liveparse::Inou_liveparse()
  :Pass("liveparse") {
}

void Inou_liveparse::tolg(Eprp_var &var) {
  const std::string files   = var.get("files");
  const std::string path    = var.get("path");
  const std::string elab_path = var.get("elab_path");

  if (files.empty()) {
    error(fmt::format("inou.liveparse: no files provided"));
    return;
  }

  for(const auto &f:Eprp_utils::parse_files(files,"inou.liveparse")) {
    int fd = open(f.c_str(), O_RDONLY);
    if(fd < 0) {
      error(fmt::format("could not open {}", f.c_str()));
      return;
    }

    struct stat sb;
    fstat(fd, &sb);

    char *memblock = (char *)mmap(NULL, sb.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
    if(memblock == MAP_FAILED) {
      error(fmt::format("mmap failed??"));
      close(fd);
      return;
    }

    if (Eprp_utils::ends_with(f,".v") || Eprp_utils::ends_with(f,".sv")) {
      Chunkify_verilog chunker(path, elab_path);
      chunker.parse(f, memblock, sb.st_size);
    }else if (Eprp_utils::ends_with(f,".prp")) {
      error(fmt::format("inou.liveparse pyrope chunkify NOT implemented for {}", f));
      close(fd);
      munmap(memblock, sb.st_size);
      return;
    }else{
      error(fmt::format("inou.liveparse chunkify unrecognized file format {}", f));
      close(fd);
      munmap(memblock, sb.st_size);
      return;
    }

    close(fd);
    munmap(memblock, sb.st_size);
  }
}

