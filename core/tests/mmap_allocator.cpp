
#include "mmap_allocator.hpp"


class char_alloc : public mmap_allocator<char> {
  public:
    char_alloc(std::string filename) : mmap_allocator<char>(filename) { }
    size_t file_size() { return mmap_allocator<char>::file_size; }
    uint64_t* mmap_base() { return mmap_allocator<char>::mmap_base; }
    size_t mmap_size() {  return mmap_allocator<char>::mmap_size; }
};

int main(int argc, char** argv) {

  uint32_t sz = 100;
  char_alloc alloc("mmaloc_test");
  char* foo = alloc.allocate(sz);

  assert(alloc.capacity() == 100);
  assert(alloc.file_size() == 0);
  assert((char*)alloc.mmap_base() == foo);


  return 0;
}
