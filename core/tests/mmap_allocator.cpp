//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "mmap_allocator.hpp"
#include <vector>


class char_alloc : public mmap_allocator<char> {
  public:
    char_alloc(const std::string & filename) : mmap_allocator<char>(filename) { }
    size_t file_size() { return mmap_allocator<char>::file_size; }
    uint64_t * mmap_base() { return mmap_allocator<char>::mmap_base; }
    size_t mmap_size() {  return mmap_allocator<char>::mmap_size; }
};

int vector_mmap() {
  mmap_allocator<char> alloc("foo");
  std::vector<char, mmap_allocator<char>> foo (alloc);

  uint32_t sz = 1024;

  foo.reserve(sz);
  assert(foo.capacity() == sz);
  assert(foo.size() == 0);

  foo.resize(sz);
  assert(foo.capacity() == sz);
  assert(foo.size() == sz);

  for(int i = 0; i < sz; i++) {
    char a = i%256 < '!' ? '!' : i%256 > '~' ? '~' : i%256;
    foo[i] = a;
  }

  alloc.save_size(sz);

  return 0;
}

int simply_mmap() {
  uint32_t sz = 1024;
  char_alloc alloc("mmaloc_test");
  char* foo = alloc.allocate(sz);

  assert(alloc.capacity() == sz);
  auto* base = (char*) alloc.mmap_base();
  auto foo_p = 	reinterpret_cast<uint64_t>(foo) >> (MMAPA_ALIGN_BITS) &  MMAPA_ALIGN_MASK;
  auto base_p = reinterpret_cast<uint64_t> (base) >> (MMAPA_ALIGN_BITS) & MMAPA_ALIGN_MASK;
  assert(foo_p == base_p || base_p + 1 == foo_p);

  for(int i = 0; i < sz; i++) {
    char a = i%256 < '!' ? '!' : i%256 > '~' ? '~' : i%256;
    foo[i] = a;
  }

  alloc.save_size(sz);
  return 0;
}

int main(int argc, char** argv) {
  vector_mmap();
  simply_mmap();
  return 0;
}
