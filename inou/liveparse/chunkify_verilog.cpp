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

#include "file_utils.hpp"
#include "graph_library.hpp"
#include "inou_liveparse.hpp"
#include "lbench.hpp"
#include "lgraph.hpp"
#include "perf_tracing.hpp"

Chunkify_verilog::Chunkify_verilog(std::string_view _path, bool _incremental_mode)
  :path(_path)
  ,incremental_mode(_incremental_mode) {

  library = Graph_library::instance(path);

  chunk_dir = absl::StrCat(path, "/liverparse");
  if (access(chunk_dir.c_str(), F_OK) != 0) {
    std::string spath(path);
    int         err = mkdir(spath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (err < 0 && errno != EEXIST) {
      throw scan_error(*this, "could not create {} directory", path);
    }

    err = mkdir(chunk_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (err < 0 && errno != EEXIST) {
      throw scan_error(*this, "could not create {}/{} directory", path, "liverparse");
    }
  }
}

int Chunkify_verilog::open_write_file(std::string_view fname) const {
  std::string f(fname);

  int fd = open(f.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
  if (fd < 0) {
    throw scan_error(*this, "could not open {} for output", fname);
  }

  return fd;
}

bool Chunkify_verilog::is_same_file(std::string_view module_name, std::string_view text1, std::string_view text2) const {
  I(incremental_mode);

  const std::string elab_filename = absl::StrCat(chunk_dir, "/", module_name, ".v");
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

void Chunkify_verilog::write_file(std::string_view fname, std::string_view text1, std::string_view text2) const {
  int fd = open_write_file(fname);
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

void Chunkify_verilog::write_file(std::string_view fname, std::string_view text) const {
  auto fd = open_write_file(fname);
  if (fd < 0)
    return;

  size_t sz2 = write(fd, text.data(), text.size());
  if (sz2 != text.size()) {
    throw scan_error(*this, "could not write contents to file err:{} vs {}", sz2, text.size());
  }

  close(fd);
}

void Chunkify_verilog::add_io(Sub_node *sub, bool input, std::string_view io_name, Port_ID pos) {
  I(sub);

  auto dir = input ? Sub_node::Direction::Input : Sub_node::Direction::Output;

  if (sub->has_pin(io_name)) {
    sub->map_graph_pos(io_name, dir, pos);
  } else {
    sub->add_pin(io_name, dir, pos);
  }
}

void Chunkify_verilog::elaborate() {
  std::string fname{get_filename()};
  // TRACE_EVENT("liveparse", perfetto::DynamicString{fname.c_str()});
  // note: tricks to make perfetto display different color on sub-modules
  TRACE_EVENT("liveparse", "chunfigy_verilog");
  // TRACE_EVENT("liveparse", nullptr, [&fname](perfetto::EventContext ctx) { 
  //     std::string converted_str{(char)('A' + (trace_module_cnt++ % 25))};
  //     ctx.event()->set_name(converted_str + fname.c_str()); 
  //     });

  bool in_module   = false;
  bool last_input  = false;
  bool last_output = false;

  std::string module_name;
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
        absl::StrAppend(&module_name, scan_text());
        module_io_pos = 1;
        in_module     = true;

        sub = library->create_sub(module_name, get_filename());
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
        bool same = incremental_mode && is_same_file(module_name, not_in_module_text, in_module_text);
        if (!same) {
          auto outfile = absl::StrCat(chunk_dir, "/", module_name, ".v");
          write_file(outfile, not_in_module_text, in_module_text);
          generated_files.emplace_back(outfile);
        }
        module_name = "";
        in_module_text.clear();
      }
    }

    scan_next();
  }
}
