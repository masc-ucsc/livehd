/*
 * Copyright © 2010 Aleksey Kunitskiy <alexey.kv@gmail.com>
 * 
 * This file is part of AlignedAllocator
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _ALIGNEDALLOCATOR_H_
#define _ALIGNEDALLOCATOR_H_

#include <map>
#include <stdexcept>
#include <new>

namespace AAlloc {

using std::size_t;
using std::ptrdiff_t;

template <typename T, const unsigned align> class AlignedAllocator;

// void
template <const unsigned align> class AlignedAllocator<void, align> {
  public:
    typedef void*       pointer;
    typedef const void* const_pointer;
    // reference to void members are impossible.
    typedef void value_type;
    template <class U> struct rebind { typedef AlignedAllocator<U, align>
                                       other; };
};

template <typename T, const unsigned align = 16>
class AlignedAllocator {

  typedef std::map<std::ptrdiff_t, std::ptrdiff_t>::iterator ptrmap_it;

  static  std::map<std::ptrdiff_t, std::ptrdiff_t> _ptrmap;

#ifdef DEBUG
  static  unsigned _reference_count;
#endif // DEBUG

  void erase(ptrmap_it it);

  public:
    typedef size_t    size_type;
    typedef ptrdiff_t difference_type;
    typedef T*        pointer;
    typedef const T*  const_pointer;
    typedef T&        reference;
    typedef const T&  const_reference;
    typedef T         value_type;
    template <class U> struct rebind { typedef AlignedAllocator<U, align>
                                   other; };

    AlignedAllocator();
    AlignedAllocator(const AlignedAllocator&);
    ~AlignedAllocator();

    pointer address(reference x) const { return &x; };
    const_pointer address(const_reference x) const { return &x; };

    pointer allocate(size_type);
    void deallocate(pointer p, size_type n);
    size_type max_size() const throw() {
      return size_type(-1) / sizeof(value_type);
    };

    void construct(pointer p, const_reference val);

    void destroy(pointer p) { p->~T(); };

};

template <typename T, const unsigned align>
std::map<ptrdiff_t, ptrdiff_t> AlignedAllocator<T, align>::_ptrmap;

#ifdef DEBUG
template <typename T, const unsigned align>
unsigned AlignedAllocator<T, align>::_reference_count = 0;
#endif // DEBUG

template <typename T, const unsigned align>
AlignedAllocator<T,align>::AlignedAllocator()
{
#ifdef DEBUG
  if( (align & (align - 1)) != 0)
    throw std::runtime_error("AlignedAllocator : AlignedAllocator() template parameter align is not power of two");
#ifdef OPENMP
  #pragma omp critical(ptrmap_modify)
#endif // OPENMP
  ++_reference_count;
#endif // DEBUG
}

template <typename T, const unsigned align>
AlignedAllocator<T,align>::AlignedAllocator(const AlignedAllocator&)
{
#ifdef DEBUG
#ifdef OPENMP
  #pragma omp critical(ptrmap_modify)
#endif // OPENMP
  ++_reference_count;
#endif // DEBUG
}

template <typename T, const unsigned align>
typename AlignedAllocator<T,align>::pointer AlignedAllocator<T,align>::allocate(
                 size_type n)
{
  if (n > this->max_size())
    throw std::bad_alloc();

  ptrdiff_t op = reinterpret_cast<ptrdiff_t>(
      ::operator new(n
        * sizeof (T)
        + static_cast<size_t>(align)
        - static_cast<size_t>(1))
      );
  ptrdiff_t ap = (op + static_cast<ptrdiff_t>(align) - static_cast<ptrdiff_t>(1))
    & ~(static_cast<ptrdiff_t>(align) - static_cast<ptrdiff_t>(1));

#ifdef OPENMP
  #pragma omp critical(ptrmap_modify)
#endif // OPENMP
  _ptrmap.insert( std::pair<ptrdiff_t, ptrdiff_t>(ap, op) );
  return reinterpret_cast<pointer>(ap); 
}

template <typename T, const unsigned align>
void AlignedAllocator<T, align>::erase(ptrmap_it it)
{
  pointer real_p = reinterpret_cast<pointer>(it->second);
  ::operator delete(real_p);
  _ptrmap.erase(it);
}

template <typename T, const unsigned align>
void AlignedAllocator<T, align>::construct(pointer p, const_reference val)
{
#ifdef OPENMP
  #pragma omp critical(ptrmap_modify)
#endif // OPENMP
  new(static_cast<void*>(p)) T(val); // placement new
}
template <typename T, const unsigned align>
void AlignedAllocator<T, align>::deallocate(pointer p, size_type n)
{
  // p is not permitted to be a null pointer. 

  assert(n); // Just to remove warning of unused argument
  ptrdiff_t _p = reinterpret_cast<ptrdiff_t>(p);
#ifdef OPENMP
  #pragma omp critical(ptrmap_modify)
  {
#endif // OPENMP
    ptrmap_it p_it = _ptrmap.find(_p);
    if (p_it != _ptrmap.end())
    {
      erase(p_it);
    }
#ifdef DEBUG
      else
        throw std::runtime_error("AlignedAllocator::deallocate : _ptrmap doesn't have mapped pointer");
#endif // DEBUG
#ifdef OPENMP
  }
#endif // OPENMP
}

template <typename T, const unsigned align>
AlignedAllocator<T,align>::~AlignedAllocator()
{
#ifdef DEBUG
#ifdef OPENMP
  #pragma omp critical (ptrmap_modify)
  {
#endif // OPENMP
    if ( _reference_count > 0 ) {
      --_reference_count;
    }
    else {
      if (!_ptrmap.empty())
        throw std::runtime_error("AlignedAllocator::destructor : there was still allocated memory in the pool");
    }
#ifdef OPENMP
  }
#endif // OPENMP
#endif // DEBUG
}

} // namespace AAlloc

#endif
//  vim:set ts=2 sw=2 tw=78 et:
