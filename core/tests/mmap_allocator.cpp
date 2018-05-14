
#include "mmap_allocator.hpp"


class char_alloc : public mmap_allocator<char> {
  public:
    char_alloc(const std::string & filename) : mmap_allocator<char>(filename) { }
    size_t file_size() { return mmap_allocator<char>::file_size; }
    uint64_t * mmap_base() { return mmap_allocator<char>::mmap_base; }
    size_t mmap_size() {  return mmap_allocator<char>::mmap_size; }
};

int main(int argc, char** argv) {

  uint32_t sz = 100;
  char_alloc alloc("mmaloc_test");
  char* foo = alloc.allocate(sz);
  auto* base = (char*) alloc.mmap_base();
  //assert(alloc.capacity() == 100);
  //assert(alloc.file_size() == 0);
  auto foo_p = 	reinterpret_cast<uint64_t>(foo) >> (MMAPA_ALIGN_BITS) &  MMAPA_ALIGN_MASK;
  auto base_p = reinterpret_cast<uint64_t> (base) >> (MMAPA_ALIGN_BITS) & MMAPA_ALIGN_MASK;
  assert(foo_p == base_p || base_p + 1 == foo_p);


  return 0;
}
