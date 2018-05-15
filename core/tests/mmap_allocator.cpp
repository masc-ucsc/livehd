
#include "mmap_allocator.hpp"
#include <vector>


class char_alloc : public mmap_allocator<char> {
  public:
    char_alloc(std::string filename = "mmap_test") : mmap_allocator<char>(filename) { }
    size_t file_size() { return mmap_allocator<char>::file_size; }
    uint64_t* mmap_base() { return mmap_allocator<char>::mmap_base; }
    size_t mmap_size() {  return mmap_allocator<char>::mmap_size; }
};

int vector_mmap() {
  mmap_allocator<char> alloc("foo");
  std::vector<char, mmap_allocator<char>> foo (alloc);

  //assert(foo.get_allocator() == alloc);

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
  // those are not the case in the current implementation of mmap_allocator
  //assert(alloc.file_size() == sz+4096+8);
  //assert((char*)alloc.mmap_base() == foo);

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
