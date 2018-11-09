
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

#include "lgraph.hpp"
#include "graph_library.hpp"
#include "chunkify_verilog.hpp"

Chunkify_verilog::Chunkify_verilog(const std::string &_path)
  :path(_path) {

  library  = Graph_library::instance(path);

}


int Chunkify_verilog::open_write_file(const std::string &filename) {
  int fd = open(filename.c_str(), O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
  if (fd<0) {
    scan_error(fmt::format("could not open {} for output", filename));
    return -1;
  }

  return fd;
}

void Chunkify_verilog::write_file(const std::string &filename, const std::string &text1, const std::string &text2) {
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

void Chunkify_verilog::write_file(const std::string &filename, const char *text, int sz) {

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

void Chunkify_verilog::add_io(bool input, const std::string &mod_name, const std::string &io_name, Port_ID original_pos) {

  LGraph *lg = library->try_find_lgraph(mod_name);
  if (lg==0) {
    fmt::print("chunkify_verilog::add_io create module {}\n",mod_name);
    lg = LGraph::create(path, mod_name);
  }

  fmt::print("add_io {}:{} pos:{}\n",mod_name, io_name, original_pos);

  if (input)
    lg->add_graph_input(io_name.c_str(), 0, 0, original_pos);
  else
    lg->add_graph_output(io_name.c_str(), 0, 0, original_pos);

  library->unregister_lgraph(mod_name, lg->lg_id(), lg);
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

  write_file(path + "/parse/file_" + format_name, buffer, buffer_sz);

  bool in_module=false;
  bool last_input = false;
  bool last_output = false;

  std::string module;
  Port_ID module_io_pos = 0;

  // This has to be cut&pasted to each file
  std::string not_in_module_text;
  Token_list  not_in_module_token;

  std::string in_module_text;
  Token_list  in_module_token;

  not_in_module_text.reserve(buffer_sz);
  not_in_module_token.reserve(token_list.size());

  in_module_text.reserve(buffer_sz);
  in_module_token.reserve(token_list.size());


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
        module_io_pos = 0;
        in_module = true;
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

          add_io(last_input, module, label, module_io_pos);

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
        // fmt::print("{}  {}\n",module,in_module_token.back().pos);
        write_file(path + "/parse/chunk_" + format_name + ":" + module, not_in_module_text, in_module_text);
        module.clear();
        in_module_text.clear();
        in_module_token.clear();
      }
    }

    scan_next();
  }

}

