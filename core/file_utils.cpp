
#include "file_utils.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <climits>
#include <cstring>
#include <iostream>

#include "iassert.hpp"

#ifdef __APPLE__
#include <libproc.h>
#endif

std::string file_utils::get_exe_path() {
  char exePath[PATH_MAX] = {
      0,
  };
#ifdef __APPLE__
  pid_t pid = getpid();
  int   ret = proc_pidpath(pid, exePath, PATH_MAX);
  I(ret > 0);
  int len = strlen(exePath);
#else
  int len = readlink("/proc/self/exe", exePath, PATH_MAX);
#endif
  I(len > 0 && len < PATH_MAX);
  for (int p = len - 1; p >= 0; p--) {
    if (exePath[p] == '/') {
      len = p;
      break;
    }
  }

  std::string str(exePath, len);

  return str;
}

