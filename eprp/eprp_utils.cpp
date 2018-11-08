
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <cassert>
#include <iostream>

#include "eprp_utils.hpp"

std::vector<std::string> Eprp_utils::parse_files(const std::string &files, const std::string &module) {
  char seps[] = ",";
  char *token;

  std::vector<std::string> raw_file_list;

  char *files_char = (char *)alloca(files.size());
  strcpy(files_char,files.c_str());
  token = std::strtok(files_char, seps);
  while( token != NULL ) {

    if (token[0] != 0) { // Empty sequences are valid and ignored. E.g: foo,bar,,,,potato
      raw_file_list.push_back(token);
    }

    token = std::strtok( NULL, seps );
  }

  return raw_file_list;
}

std::string Eprp_utils::get_exe_path() {
  char exePath[PATH_MAX] = {0,};
  int len = readlink("/proc/self/exe", exePath, PATH_MAX);
  assert(len > 0 && len < PATH_MAX);
  for(int p=len-1;p>=0;p--) {
    if (exePath[p] == '/') {
      len = p;
      break;
    }
  }

  std::string path(exePath,0,len);
  return path;
}

bool Eprp_utils::setup_directory(const std::string &dir) {

  if (dir == ".")
    return true;

  struct stat sb;

  if (stat(dir.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode))
    return true;

  int e = mkdir(dir.c_str(),0755);

  return e>=0;
}

bool Eprp_utils::ends_with(const std::string &s, const std::string &suffix) {
  return s.rfind(suffix) == (s.size()-suffix.size());
}

