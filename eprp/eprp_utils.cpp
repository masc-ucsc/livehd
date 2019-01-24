
#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <cstring>
#include <iostream>

#include "eprp_utils.hpp"
#include "thread_pool.hpp"

std::string Eprp_utils::get_exe_path() {
  char exePath[PATH_MAX] = {
      0,
  };
  int len = readlink("/proc/self/exe", exePath, PATH_MAX);
  assert(len > 0 && len < PATH_MAX);
  for (int p = len - 1; p >= 0; p--) {
    if (exePath[p] == '/') {
      len = p;
      break;
    }
  }

  std::string path(exePath, 0, len);
  return path;
}

static int rm_file(const char *pathname, const struct stat *sbuf, int type, struct FTW *ftwb) {
  remove(pathname);
  return 0;
}

static void clean_dir_thread(char *path) {
  nftw(path, rm_file, 10, FTW_DEPTH | FTW_MOUNT | FTW_PHYS);
  free(path);
}

void Eprp_utils::clean_dir(std::string_view dir) {
  if (dir == "") return;

  const std::string path(dir.data(), dir.size());  // null terminated

  DIR *dirp = opendir(path.c_str());
  if (dirp == 0) {
    mkdir(path.c_str(), 0755);
    return;
  }

  // Rename, and allow a slow thread to delete it
  char  dtemp[] = "deleting_dir.XXXXXX";
  char *dtemp2  = strdup(mkdtemp(dtemp));
  rename(path.c_str(), dtemp2);

  mkdir(path.c_str(), 0755);  // Create clean directory again

  static Thread_pool tp(2);  // Keep pool running for frequent calls

  tp.add(clean_dir_thread, dtemp2);
}
