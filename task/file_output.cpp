//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>

#include "fmt/format.h"

#include "file_output.hpp"
#include "iassert.hpp"

File_output::File_output(std::string_view fname) :filename(fname), sz(0), aborted(false) {

}

File_output::~File_output() {
  if (aborted)
    return;

  //---------------------------------- OPEN

  int fd = ::open(filename.c_str(), O_RDWR | O_CREAT, 0644);
  I(fd >= 0); // throw std::runtime_error(fmt::format("could not create destination {} file (permissions?)", filename));

  size_t map_size = sz;
  map_size>>=12;
  ++map_size;
  map_size<<=12;
  auto ok = ftruncate(fd, map_size);
  I(ok==0);

  void *base = ::mmap(0, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);  // no superpages
  if (base==MAP_FAILED) {
    fmt::print("mmap errno:{} for filename{}\n", strerror(errno), filename);
    I(false);
  }

  //---------------------------------- SAVE

  char *ptr = static_cast<char *>(base);
  for(const auto &e:sequence) {
    memcpy(ptr, e.data(), e.size());
    ptr+=e.size();
    I(static_cast<size_t>(ptr-static_cast<char *>(base))<=sz);
  }

  //---------------------------------- CLOSE
  ::munmap(base, map_size);
  if (map_size != sz) {
    auto ok2 = ftruncate(fd, sz);
    (void)ok2;
  }
  ::close(fd);
}
