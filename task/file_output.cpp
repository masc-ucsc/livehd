//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "file_output.hpp"

File_output::File_output(mmap_lib::str fname) :filename(fname), sz(0), aborted(false) {

}

File_output::~File_output() {
  if (aborted)
    return;

  //---------------------------------- OPEN
  auto fname_str = filename.to_s();

  int fd = ::open(fname_str.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    mmap_lib::mmap_gc::try_collect_fd();  // maybe they are all used by LG?
    fd = open(fname_str.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  }
  assert(fd >= 0); // throw std::runtime_error(fmt::format("could not create destination {} file (permissions?)", filename));

  void *base = ::mmap(0, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);  // no superpages
  if (base==nullptr) {
    mmap_lib::mmap_gc::try_collect_mmap();
    base = ::mmap(0, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);  // no superpages
    assert(base != nullptr); // throw std::runtime_error(fmt::format("could not create mmap for {} file", filename));
  }

  //---------------------------------- SAVE

  char *ptr = static_cast<char *>(base);
  for(const auto &e:sequence) {
    auto delta = e.fill_txt_direct(ptr);
    ptr+=delta;
    assert(static_cast<size_t>(ptr-static_cast<char *>(base))<=sz);
  }

  //---------------------------------- CLOSE
  ::munmap(base, sz);
  ::close(fd);

}
