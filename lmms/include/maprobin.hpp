//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <sys/mman.h>

#include "fmt/format.h"
#include "robin_hood.hpp"

class Mapped_area {
  std::size_t _size = 0;
  void*       _area = nullptr;

public:
  explicit Mapped_area(std::size_t size);
  ~Mapped_area();

  std::size_t size() const noexcept { return _size; }
  char*       data() const noexcept { return static_cast<char*>(_area); }
};

Mapped_area::Mapped_area(std::size_t size_)
  : _size(size_) {
    fmt::print("Mapped_area {}\n", size_);
    _area = ::mmap(nullptr,
        size(),
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0);
    if (_area == MAP_FAILED) {
      throw std::bad_alloc();
    }
    // mprotect() the tail so that we can detect running off of it
    auto tail = data() + size() - ::getpagesize();
    auto rc   = ::mprotect(tail, ::getpagesize(), PROT_NONE);
    if (rc) {
      ::munmap(_area, size());
      throw std::system_error(std::error_code(errno, std::system_category()),
          "mprotect() failed");
    }
  }

Mapped_area::~Mapped_area() {
  if (_area) {
    fmt::print("~Mapped_area area:{} size:{}\n", _area, _size);
    // Give that memory back...
    ::munmap(_area, _size);
  }
}

template <std::size_t Alignment = sizeof(std::max_align_t)>
class bumping_memory_resource {
  std::atomic<char*> _ptr;
  std::size_t        _ptr_size;
  std::size_t        _ptr_adjusted_size;

public:
  template <typename Area, typename = decltype(std::declval<Area&>().data())>
    explicit bumping_memory_resource(Area& a)
    : _ptr(a.data()) {}

  constexpr static auto alignment = Alignment;

  void *allocate(std::size_t size) noexcept {
    fmt::print("allocate {}\n", size);
    auto under         = size % Alignment;
    auto adjust_size   = Alignment - under;
    auto adjusted_size = size + adjust_size;
    auto ret           = _ptr.fetch_add(adjusted_size);

    _ptr_size          = size;
    _ptr_adjusted_size = adjusted_size;

    return ret;
  }

  void deallocate(void*) noexcept {
    fmt::print("deallocate size:{} adjusted_size:{}\n", _ptr_size, _ptr_adjusted_size);
  }
};

template <typename T, typename Resource = bumping_memory_resource<>>
class bumping_allocator {
  Resource* _res;

public:
  using value_type = T;

  static_assert(std::alignment_of<T>::value <= Resource::alignment, "Given type cannot be allocator with the given resource");

  explicit bumping_allocator(Resource& res)
    : _res(&res) {}

  bumping_allocator(const bumping_allocator&) = default;
  template <typename U>
    bumping_allocator(const bumping_allocator<U>& other)
    : bumping_allocator(other.resource()) {
  }

  Resource& resource() const { return *_res; }

  T *allocate(std::size_t n) {
    fmt::print("bumpling_allocator::allocate size:{}\n", n);
    return static_cast<T*>(_res->allocate(sizeof(T) * n));
  }
  void deallocate(T* ptr, std::size_t sz) {
    fmt::print("bumpling_allocator::deallocate size:{}\n", sz);
    _res->deallocate(ptr);
  }

  friend bool operator==(const bumping_allocator& lhs, const bumping_allocator& rhs) {
    return lhs._res == rhs._res;
  }

  friend bool operator!=(const bumping_allocator& lhs, const bumping_allocator& rhs) {
    return lhs._res != rhs._res;
  }
};
