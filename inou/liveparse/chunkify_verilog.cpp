
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <set>
#include <string>

#include "lgbench.hpp"

#include "inou_liveparse.hpp"
#include "lgraph.hpp"
#include "graph_library.hpp"
#include "chunkify_verilog.hpp"

//#include "progress_bar.hpp"

Chunkify_verilog::Chunkify_verilog(const std::string &_path, const std::string &_elab_path)
  :path(_path)
  ,elab_path(_elab_path) {

  library  = Graph_library::instance(path);

  if (elab_path.empty())
    elab_library = 0;
  else
    elab_library  = Graph_library::instance(elab_path);
}

int Chunkify_verilog::open_write_file(const std::string &filename) const {
  int fd = open(filename.c_str(), O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
  if (fd<0) {
    scan_error(fmt::format("could not open {} for output", filename));
    return -1;
  }

  return fd;
}

bool Chunkify_verilog::is_same_file(const std::string &module, const std::string &text1, const std::string &text2) const {

  if (elab_path.empty())
    return false;

  const std::string elab_filename = elab_path + "/parse/chunk_" + module + ".v";

  int fd = open(elab_filename.c_str(), O_RDONLY);
  if (fd<0)
    return false;

  struct stat sb;
  fstat(fd, &sb);

  if (static_cast<size_t>(sb.st_size) != static_cast<size_t>(text1.size() + text2.size())) {
    close(fd);
    return false;
  }

  char *memblock = (char *)mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0); // Read only mmap
  if(memblock == MAP_FAILED) {
    Inou_liveparse::error(fmt::format("mmap failed?? for {}", module));
    close(fd);
    return false;
  }

  int n = memcmp(memblock, text1.data(), text1.size());
  if (n) {
    close(fd);
    munmap(memblock, sb.st_size);
  }
  n = memcmp(&memblock[text1.size()], text2.data(), text2.size());
  close(fd);
  munmap(memblock, sb.st_size);

  return n==0; // same file if n==0
}

void Chunkify_verilog::write_file(const std::string &filename, const std::string &text1, const std::string &text2) const {
  int fd = open_write_file(filename);
  if (fd<0)
    return;

  size_t sz = write(fd,text1.c_str(),text1.size());
  if (sz!= text1.size()) {
    scan_error(fmt::format("could not write contents to file err:{} vs {}",sz, text1.size()));
    return;
  }
  sz = write(fd,text2.c_str(),text2.size());
  if (sz != text2.size()) {
    scan_error(fmt::format("could not write contents to file err:{} vs {}",sz, text2.size()));
    return;
  }

  close(fd);
}

void Chunkify_verilog::write_file(const std::string &filename, const char *text, int sz) const {

  int fd = open_write_file(filename);
  if (fd<0)
    return;

  int sz2 = write(fd,text,sz);
  if (sz2 != sz) {
    scan_error(fmt::format("could not write contents to file err:{} vs {}",sz2, sz));
    return;
  }

  close(fd);
}

void Chunkify_verilog::add_io(LGraph *lg, bool input, const std::string &io_name, Port_ID original_pos) {

  assert(lg);

  if (input) {
    if (!lg->is_graph_input(io_name.c_str()))
      lg->add_graph_input(io_name.c_str(), 0, 0, 0, original_pos);
  }else{
    if (!lg->is_graph_output(io_name.c_str()))
      lg->add_graph_output(io_name.c_str(), 0, 0, 0, original_pos);
  }

}

void Chunkify_verilog::elaborate() {

  LGBench bench("live.parse");

  std::string format_name(buffer_name);
  for(size_t i=0;i<buffer_name.size();i++) {
    if (format_name[i] == '/')
      format_name[i] = '.';
  }

  int err = mkdir(path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  if (err<0 && errno != EEXIST) {
    scan_error(fmt::format("could not create {} directory", path));
    return;
  }

  std::string cadena = path + "/parse";
  err = mkdir(cadena.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  if (err<0 && errno != EEXIST) {
    scan_error(fmt::format("could not create {}/{} directory", path, "parse"));
    return;
  }

  //ProgressBar bar(buffer_sz,"liveparse");

  write_file(path + "/parse/file_" + format_name, buffer, buffer_sz);

  bool in_module=false;
  bool last_input = false;
  bool last_output = false;

  std::string module;
  Port_ID module_io_pos = 1;

  // This has to be cut&pasted to each file
  std::string not_in_module_text;
  Token_list  not_in_module_token;

  std::string in_module_text;
  Token_list  in_module_token;

  not_in_module_text.reserve(buffer_sz);
  not_in_module_token.reserve(token_list.size());

  in_module_text.reserve(buffer_sz);
  in_module_token.reserve(token_list.size());

  LGraph *lg=0;

  while(!scan_is_end()) {
    bool endmodule_found=false;
    if (scan_is_token(TOK_ALNUM)) {
      std::string token;
      scan_append(token);
      if (strcasecmp(token.c_str(),"module")==0) {
        if (in_module) {
          scan_error(fmt::format("unexpected nested modules"));
        }
        scan_format_append(in_module_text);
        scan_token_append(in_module_token);
        scan_next();
        scan_append(module);
        module_io_pos = 1; // pos 0 means I do not care
        in_module = true;

        lg = library->try_find_lgraph(module);
        if (lg==0) {
          //fmt::print("chunkify_verilog::add_io create module {}\n",mod_name);
          lg = LGraph::create(path, module);
        }

      }else if (strcasecmp(token.c_str(),"input")==0) {
        last_input = true;
      }else if (strcasecmp(token.c_str(),"output")==0) {
        last_output = true;
      }else if (strcasecmp(token.c_str(),"endmodule")==0) {
        if (in_module) {
          in_module = false;
          endmodule_found = true;
        }else{
          scan_error(fmt::format("found endmodule without corresponding module"));
        }
      }
    }else if (scan_is_token(TOK_COMMA) ||scan_is_token(TOK_SEMICOLON) || scan_is_token(TOK_CP)) {
      if (last_input || last_output) {
        if (scan_is_prev_token(TOK_ALNUM)) {
          std::string label;
          scan_prev_append(label);

          add_io(lg, last_input, label, module_io_pos);

          module_io_pos++;

        }
        last_input = false;
        last_output = false;
      }
    }else if (scan_is_token(TOK_COMMENT)) {
      // Drop comment, to avoid unneded recompilations
      scan_next();
      continue;
    }
    if (!in_module && !endmodule_found) {
      scan_token_append(not_in_module_token);
      scan_format_append(not_in_module_text);
    }else{
      scan_format_append(in_module_text);
      scan_token_append(in_module_token);
      if (endmodule_found) {
        //bar.Progressed(in_module_token.back().pos);

        //fmt::print("{}  {} {}\n",module,in_module_token.back().pos, buffer_sz);
        bool same = is_same_file(module, not_in_module_text, in_module_text);
        if (!same) {
          write_file(path + "/parse/chunk_" + module + ".v", not_in_module_text, in_module_text);
        }
        if (lg) {
          library->unregister_lgraph(module, lg->lg_id(), lg);
          lg->sync();
          lg = 0;
        }
        module.clear();
        in_module_text.clear();
        in_module_token.clear();
      }
    }

    scan_next();
  }
}

