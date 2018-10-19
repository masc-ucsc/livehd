
#include <unistd.h>

#include "chunkify_verilog.hpp"
#include "main_api.hpp"
#include "eprp_utils.hpp"

class Live_parse_api {
protected:
  static bool ends_with(const std::string &s, const std::string &suffix) {
    return s.rfind(suffix) == (s.size()-suffix.size());
  }

  static void tolg(Eprp_var &var) {
    const std::string files   = var.get("files");
    const std::string path    = var.get("path","lgdb");

    if (files.empty()) {
      Main_api::error(fmt::format("live.parse: no files provided"));
      return;
    }

    for(const auto &f:Eprp_utils::parse_files(files,"live.parse")) {
      int fd = open(f.c_str(), O_RDONLY);
      if(fd < 0) {
        Main_api::error(fmt::format("could not open {}", f.c_str()));
        return;
      }

      struct stat sb;
      fstat(fd, &sb);

      char *memblock = (char *)mmap(NULL, sb.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
      if(memblock == MAP_FAILED) {
        Main_api::error(fmt::format("mmap failed??"));
        close(fd);
        return;
      }

      if (ends_with(f,".v") || ends_with(f,".sv")) {
        Chunkify_verilog chunker(path);
        chunker.parse(f, memblock, sb.st_size);
      }else if (ends_with(f,".prp")) {
        Main_api::error(fmt::format("pyrope chunkify NOT implemented for {}", f));
        close(fd);
        munmap(memblock, sb.st_size);
        return;
      }else{
        Main_api::error(fmt::format("chunkify unrecognized file format {}", f));
        close(fd);
        munmap(memblock, sb.st_size);
        return;
      }

      close(fd);
      munmap(memblock, sb.st_size);
    }
  }

public:
  static void setup(Eprp &eprp) {
    Eprp_method m1("live.parse", "parse and chunkify verilog/pyrope files", &Live_parse_api::tolg);
    m1.add_label_optional("path","lgraph path");
    m1.add_label_required("files","json input file[s] to create lgraph[s]");

    eprp.register_method(m1);
  }

};

