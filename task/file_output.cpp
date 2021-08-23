//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <cerrno>

#include "file_output.hpp"
#include "iassert.hpp"

File_output::File_output(mmap_lib::str fname) :filename(fname), sz(0), aborted(false) {

}

File_output::~File_output() {
  if (aborted)
    return;

  //---------------------------------- OPEN
  auto fname_str = filename.to_s();

  int fd = ::open(fname_str.c_str(), O_RDWR | O_CREAT, 0644);
  if (fd < 0) {
    mmap_lib::mmap_gc::try_collect_fd();  // maybe they are all used by LG?
    fd = open(fname_str.c_str(), O_RDWR | O_CREAT, 0644);
  }
  assert(fd >= 0); // throw std::runtime_error(fmt::format("could not create destination {} file (permissions?)", filename));

  size_t map_size = sz;
  map_size>>=12;
  ++map_size;
  map_size<<=12;
  auto ok = ftruncate(fd, map_size);
  I(ok==0);

  void *base = ::mmap(0, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);  // no superpages
  if (base==MAP_FAILED) {
    fmt::print("mmap errno:{} for filename{}\n", strerror(errno), fname_str);

    mmap_lib::mmap_gc::try_collect_mmap();
    base = ::mmap(0, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);  // no superpages
    assert(base != MAP_FAILED); // throw std::runtime_error(fmt::format("could not create mmap for {} file", filename));
  }

  //---------------------------------- SAVE

  char *ptr = static_cast<char *>(base);
  for(const auto &e:sequence) {
    auto delta = e.fill_txt_direct(ptr);
    ptr+=delta;
    assert(static_cast<size_t>(ptr-static_cast<char *>(base))<=sz);
  }

  //---------------------------------- CLOSE
  ::munmap(base, map_size);
  if (map_size != sz) {
    auto ok2 = ftruncate(fd, sz);
    (void)ok2;
  }
  ::close(fd);
}
