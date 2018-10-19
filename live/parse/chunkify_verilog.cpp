
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <set>
#include <string>

#include "chunkify_verilog.hpp"

Chunkify_verilog::Chunkify_verilog(const std::string &_path)
  :path(_path) {

}


int Chunkify_verilog::open_write_file(const std::string &filename) {
  int fd = open(filename.c_str(), O_WRONLY|O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
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
  fmt::print("1.done {}\n",sz);

  int fd = open_write_file(filename);
  if (fd<0)
    return;

  fmt::print("2.done {}\n",fd);

  int sz2 = write(fd,text,sz);
  if (sz2 != sz) {
    scan_error(fmt::format("could not write contents to file err:{} vs {}",sz2, sz));
    return;
  }

  close(fd);
}

void Chunkify_verilog::elaborate() {

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

#if 0
          if (last_input)
            fmt::print("{}  inp {}\n",module,label);
          else
            fmt::print("{}  out {}\n",module,label);
#endif
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
        fmt::print("{}  {}\n",module,in_module_token.back().pos);
        write_file(path + "/parse/chunk_" + format_name + ":" + module, not_in_module_text, in_module_text);
        module.clear();
        in_module_text.clear();
        in_module_token.clear();
      }
    }

    scan_next();
  }


}

