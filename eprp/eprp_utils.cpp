
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <ftw.h>
#include <string.h>

#include <cstring>
#include <cassert>
#include <iostream>

#include "thread_pool.hpp"
#include "eprp_utils.hpp"

std::vector<std::string> Eprp_utils::parse_files(std::string_view files, std::string_view module) {
  char seps[] = ",";
  char *token;

  std::vector<std::string> raw_file_list;

  char *files_char = (char *)alloca(files.size()+1); // NOTE: alloca is OK, the vector<string> creates a duplicate
  strcpy(files_char,std::string(files).c_str());
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

bool Eprp_utils::ends_with(const std::string &s, const std::string &suffix) {
  return s.rfind(suffix) == (s.size()-suffix.size());
}

static int rm_file(const char *pathname, const struct stat *sbuf, int type, struct FTW *ftwb) {
  remove(pathname);
  return 0;
}

static void clean_dir_thread(char *path) {
  nftw(path, rm_file,10, FTW_DEPTH|FTW_MOUNT|FTW_PHYS);
  free(path);
}

void Eprp_utils::clean_dir(const std::string &dir) {

  const char *path = dir.c_str();

  DIR* dirp = opendir(path);
  if (dirp==0) {
    mkdir(path,0755);
    return;
  }

  // Rename, and allow a slow thread to delete it
  char dtemp[] = "deleting_dir.XXXXXX";
  char *dtemp2 = strdup(mkdtemp(dtemp));
  rename(path,dtemp2);

  mkdir(path,0755); // Create clean directory again

  static Thread_pool tp(2); // Keep pool running for frequent calls

  tp.add(clean_dir_thread,dtemp2);
}

