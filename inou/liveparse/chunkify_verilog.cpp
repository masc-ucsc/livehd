//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "chunkify_verilog.hpp"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <set>
#include <string>

#include "eprp_utils.hpp"
#include "graph_library.hpp"
#include "inou_liveparse.hpp"
#include "lbench.hpp"
#include "lgraph.hpp"

Chunkify_verilog::Chunkify_verilog(std::string_view _path) : path(_path) { library = Graph_library::instance(path); }

int Chunkify_verilog::open_write_file(std::string_view filename) const {
  std::string sfilename(filename);
  int         fd = open(sfilename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
  if (fd < 0) {
    scan_error(fmt::format("could not open {} for output", sfilename));
    return -1;
  }

  return fd;
}

bool Chunkify_verilog::is_same_file(std::string_view module, std::string_view text1, std::string_view text2) const {
  if (elab_path.empty()) return false;

  const std::string elab_filename = elab_chunk_dir + "/" + std::string(module) + ".v";
  int               fd            = open(elab_filename.c_str(), O_RDONLY);
  if (fd < 0) return false;

  struct stat sb;
  fstat(fd, &sb);

  if (static_cast<size_t>(sb.st_size) != (text1.size() + text2.size())) {
    close(fd);
    return false;
  }

  char *memblock2 = (char *)mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);  // Read only mmap
  if (memblock2 == MAP_FAILED) {
    Inou_liveparse::error(fmt::format("mmap failed?? for {}", module));
    close(fd);
    return false;
  }

  int n = memcmp(memblock2, text1.data(), text1.size());
  if (n) {
    close(fd);
    munmap(memblock2, sb.st_size);
  }
  n = memcmp(&memblock2[text1.size()], text2.data(), text2.size());
  close(fd);
  munmap(memblock2, sb.st_size);

  return n == 0;  // same file if n==0
}

void Chunkify_verilog::write_file(std::string_view filename, std::string_view text1, std::string_view text2) const {
  int fd = open_write_file(filename);
  if (fd < 0) return;

  size_t sz = write(fd, text1.data(), text1.size());
  if (sz != text1.size()) {
    scan_error(fmt::format("could not write contents to file err:{} vs {}", sz, text1.size()));
    return;
  }
  sz = write(fd, text2.data(), text2.size());
  if (sz != text2.size()) {
    scan_error(fmt::format("could not write contents to file err:{} vs {}", sz, text2.size()));
    return;
  }

  close(fd);
}

void Chunkify_verilog::write_file(std::string_view filename, std::string_view text) const {
  auto fd = open_write_file(filename);
  if (fd < 0) return;

  size_t sz2 = write(fd, text.data(), text.size());
  if (sz2 != text.size()) {
    scan_error(fmt::format("could not write contents to file err:{} vs {}", sz2, text.size()));
    return;
  }

  close(fd);
}

void Chunkify_verilog::add_io(Sub_node *sub, bool input, std::string_view io_name, Port_ID pos) {
  I(sub);

  auto dir = input ? Sub_node::Direction::Input : Sub_node::Direction::Output;

  I(!sub->has_pin(io_name));  // reset_sub clears all the pins
  sub->add_pin(io_name, dir, pos);
}

void Chunkify_verilog::elaborate() {

  std::string parse_path = absl::StrCat(path, "/parse/");  // Keep trailing /
  if (access(parse_path.c_str(), F_OK) != 0) {
    std::string spath(path);
    int         err = mkdir(spath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (err < 0 && errno != EEXIST) {
      scan_error(fmt::format("could not create {} directory", path));
      return;
    }

    err = mkdir(parse_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (err < 0 && errno != EEXIST) {
      scan_error(fmt::format("could not create {}/{} directory", path, "parse"));
      return;
    }
  }

  std::string format_name;

  if (is_parse_inline()) {
    format_name = "inline";
  } else {
    format_name = get_filename();

    for (size_t i = 0; i < format_name.size(); i++) {
      if (format_name[i] == '/') format_name[i] = '.';
    }
  }
  Lbench bench("live.parse " + format_name);

  auto source = absl::StrCat(parse_path, "file_", format_name);

  write_file(source, get_memblock());

  chunk_dir = absl::StrCat(parse_path, "chunk_", format_name);
  Eprp_utils::clean_dir(chunk_dir);

  elab_chunk_dir.clear();
  if (!elab_path.empty()) {
    elab_chunk_dir.append(elab_path);
    elab_chunk_dir.append("/parse/chunk_");
    elab_chunk_dir.append(format_name);
  }

  bool in_module   = false;
  bool last_input  = false;
  bool last_output = false;

  std::string module;
  Port_ID     module_io_pos = 1;

  // This has to be cut&pasted to each file
  std::string not_in_module_text;

  std::string in_module_text;

  not_in_module_text.reserve(get_memblock().size());

  in_module_text.reserve(get_memblock().size());

  Sub_node *sub = nullptr;

  while (!scan_is_end()) {
    bool endmodule_found = false;
    if (scan_is_token(Token_id_alnum)) {
      auto txt = scan_text();
      if (txt == "module") {
        if (in_module) {
          scan_error(fmt::format("unexpected nested modules"));
        }
        scan_format_append(in_module_text);
        scan_next();
        absl::StrAppend(&module, scan_text());
        module_io_pos = 1;
        in_module     = true;

        sub = &library->reset_sub(module, source);
        I(sub);

      } else if (txt == "input") {
        last_input = true;
      } else if (txt == "output") {
        last_output = true;
      } else if (txt == "endmodule") {
        if (in_module) {
          in_module       = false;
          endmodule_found = true;
        } else {
          scan_error(fmt::format("found endmodule without corresponding module"));
        }
      }
    } else if (scan_is_token(Token_id_comma) || scan_is_token(Token_id_semicolon) || scan_is_token(Token_id_cp) ||
               scan_is_token(Token_id_comment)) {  // Before Token_id_comma
      if (last_input || last_output) {
        if (in_module && scan_is_prev_token(Token_id_alnum)) {
#if 0
          // FIXME: bug in char_array prevents to use sview. Delete this once we move to LGraph 0.2 (mmap_map/mmap_bimap)
          std::string label{scan_prev_text()};
#else
          auto label = scan_prev_text();
#endif

          add_io(sub, last_input, label, module_io_pos);

          module_io_pos++;
        }
        if (!scan_is_token(Token_id_comma)) {
          // E.g: input a, b, d;
          last_input  = false;
          last_output = false;
        }
      }
      if (scan_is_token(Token_id_comment)) {
        if (in_module)
          in_module_text.append("\n");
        else
          not_in_module_text.append("\n");
        scan_next();
        continue;
      }
    }
    if (!in_module && !endmodule_found) {
      scan_format_append(not_in_module_text);
    } else {
      scan_format_append(in_module_text);
      if (endmodule_found) {
        bool same = is_same_file(module, not_in_module_text, in_module_text);
        if (!same) {
          write_file(chunk_dir + "/" + module + ".v", not_in_module_text, in_module_text);
        }
        module.clear();
        in_module_text.clear();
      }
    }

    scan_next();
  }
}
