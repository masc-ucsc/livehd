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

Chunkify_verilog::Chunkify_verilog(const mmap_lib::str &_path) : path(_path) { library = Graph_library::instance(path); }

int Chunkify_verilog::open_write_file(const mmap_lib::str &filename) const {
  int         fd = open(filename.to_s().c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
  if (fd < 0) {
    throw scan_error(*this, "could not open {} for output", filename);
  }

  return fd;
}

bool Chunkify_verilog::is_same_file(const mmap_lib::str &module_name, std::string_view text1, std::string_view text2) const {
  if (elab_path.empty())
    return false;

  const std::string elab_filename = elab_chunk_dir.to_s() + "/" + module_name.to_s() + ".v";
  int               fd            = open(elab_filename.c_str(), O_RDONLY);
  if (fd < 0)
    return false;

  struct stat sb;
  fstat(fd, &sb);

  if (static_cast<size_t>(sb.st_size) != (text1.size() + text2.size())) {
    close(fd);
    return false;
  }

  char *memblock2 = (char *)mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);  // Read only mmap
  if (memblock2 == MAP_FAILED) {
    Inou_liveparse::error(fmt::format("mmap failed?? for {}", module_name));
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

void Chunkify_verilog::write_file(const mmap_lib::str &filename, std::string_view text1, std::string_view text2) const {
  int fd = open_write_file(filename);
  if (fd < 0)
    return;

  size_t sz = write(fd, text1.data(), text1.size());
  if (sz != text1.size()) {
    throw scan_error(*this, "could not write contents to file err:{} vs {}", sz, text1.size());
  }
  sz = write(fd, text2.data(), text2.size());
  if (sz != text2.size()) {
    throw scan_error(*this, "could not write contents to file err:{} vs {}", sz, text2.size());
  }

  close(fd);
}

void Chunkify_verilog::write_file(const mmap_lib::str &filename, std::string_view text) const {
  auto fd = open_write_file(filename);
  if (fd < 0)
    return;

  size_t sz2 = write(fd, text.data(), text.size());
  if (sz2 != text.size()) {
    throw scan_error(*this, "could not write contents to file err:{} vs {}", sz2, text.size());
  }

  close(fd);
}

void Chunkify_verilog::add_io(Sub_node *sub, bool input, const mmap_lib::str &io_name, Port_ID pos) {
  I(sub);

  auto dir = input ? Sub_node::Direction::Input : Sub_node::Direction::Output;

  if (sub->has_pin(io_name)) {
    sub->map_graph_pos(io_name, dir, pos);
  } else {
    sub->add_pin(io_name, dir, pos);
  }
}

void Chunkify_verilog::elaborate() {
  auto parse_path = path.to_s() + "/parse/";  // Keep trailing /
  if (access(parse_path.c_str(), F_OK) != 0) {
    auto  spath = path.to_s();
    int     err = mkdir(spath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (err < 0 && errno != EEXIST) {
      throw scan_error(*this, "could not create {} directory", path);
    }

    err = mkdir(parse_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (err < 0 && errno != EEXIST) {
      throw scan_error(*this, "could not create {}/{} directory", path, "parse");
    }
  }

  std::string format_name;

  if (is_parse_inline()) {
    format_name = "inline";
  } else {
    format_name = get_filename().to_s();

    for (char &c : format_name) {
      if (c == '/')
        c = '.';
    }
  }

  std::string bench_name;
  bench_name.append("inou.LIVEPARSE_");
  bench_name.append(path.to_s());
  bench_name.append("_");
  bench_name.append(std::to_string(get_token_pos()));
  bench_name.append("_");
  bench_name.append(format_name);
  Lbench bench(bench_name);

  mmap_lib::str source(parse_path + "file_" + format_name);

  write_file(source, get_memblock());

  chunk_dir = mmap_lib::str(parse_path + "chunk_" + format_name);
  Eprp_utils::clean_dir(chunk_dir);

  elab_chunk_dir = mmap_lib::str();
  if (!elab_path.empty()) {
    elab_chunk_dir = mmap_lib::str::concat(elab_path, "/parse/chunk_", format_name);
  }

  bool in_module   = false;
  bool last_input  = false;
  bool last_output = false;

  mmap_lib::str module_name;
  Port_ID     module_io_pos = 1;

  // This has to be cut&pasted to each file
  std::string not_in_module_text;

  std::string in_module_text;

  not_in_module_text.reserve(get_memblock().size());

  in_module_text.reserve(get_memblock().size());

  Sub_node *sub = nullptr;

  int inside_task_function = 0;

  while (!scan_is_end()) {
    bool endmodule_found = false;
    if (scan_is_token(Token_id_alnum)) {
      auto txt = scan_text();
      // fmt::print("T:{}\n",txt);
      if (txt == "module") {
        if (in_module) {
          throw scan_error(*this, "unexpected nested modules");
        }
        format_append(in_module_text);
        scan_next();
        module_name = mmap_lib::str::concat(module_name, scan_text());
        module_io_pos = 1;
        in_module     = true;

        sub = &library->reset_sub(module_name, mmap_lib::str(source));
        I(sub);

      } else if (txt == "input") {
        last_input  = true;
        last_output = false;
      } else if (txt == "output") {
        last_input  = false;
        last_output = true;
      } else if (txt == "endmodule") {
        if (in_module) {
          in_module            = false;
          endmodule_found      = true;
          inside_task_function = 0;
        } else {
          throw scan_error(*this, "found endmodule without corresponding module");
        }
      } else if (txt == "task" || txt == "function") {
        inside_task_function++;
      } else if (txt == "endtask" || txt == "endfunction") {
        inside_task_function--;
      }

    } else if (scan_is_token(Token_id_comma) || scan_is_token(Token_id_semicolon) || scan_is_token(Token_id_cp)
               || scan_is_token(Token_id_comment)) {  // Before Token_id_comma
      if (last_input || last_output) {
        if (in_module && scan_is_prev_token(Token_id_alnum) && inside_task_function == 0) {

          add_io(sub, last_input, scan_prev_text(), module_io_pos);

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
      format_append(not_in_module_text);
    } else {
      format_append(in_module_text);
      if (endmodule_found) {
        bool same = is_same_file(module_name, not_in_module_text, in_module_text);
        if (!same) {
          auto outfile = mmap_lib::str(chunk_dir.to_s() + "/" + module_name.to_s() + ".v");
          write_file(outfile, not_in_module_text, in_module_text);
        }
        module_name = mmap_lib::str();
        in_module_text.clear();
      }
    }

    scan_next();
  }
}
