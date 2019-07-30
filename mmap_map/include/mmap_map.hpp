//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Code based on robin-hood hash map. Changed to use only flat_map (not
// node_map), small changes in hashing function (zero hash use as end), and
// significant changes to support mmap storage, strings with mmap at index...

//                 ______  _____                 ______                _________
//  ______________ ___  /_ ___(_)_______         ___  /_ ______ ______ ______  /
//  __  ___/_  __ \__  __ \__  / __  __ \        __  __ \_  __ \_  __ \_  __  /
//  _  /    / /_/ /_  /_/ /_  /  _  / / /        _  / / // /_/ // /_/ // /_/ /
//  /_/     \____/ /_.___/ /_/   /_/ /_/ ________/_/ /_/ \____/ \____/ \__,_/
//                                      _/_____/
//
// robin_hood::unordered_map for C++14
// version 3.2.7
// https://github.com/martinus/robin-hood-hashing
//
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
// SPDX-License-Identifier: MIT
// Copyright (c) 2018-2019 Martin Ankerl <http://martin.ankerl.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <queue>
#include <algorithm>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <iostream>

//#define mmap_map_LOG_ENABLED
#ifdef mmap_map_LOG_ENABLED
#    define mmap_map_LOG(x) std::cout << __FUNCTION__ << "@" << __LINE__ << ": " << x << std::endl
#else
#    define mmap_map_LOG(x)
#endif

// mark unused members with this macro
#define mmap_map_UNUSED(identifier)

// bitness
#ifdef FORCE_ROBIN_32
#define mmap_map_BITNESS 32
#else
#if SIZE_MAX == UINT32_MAX
#    define mmap_map_BITNESS 32
#elif SIZE_MAX == UINT64_MAX
#    define mmap_map_BITNESS 64
#else
#    error Unsupported bitness
#endif
#endif

// endianess
#ifdef _WIN32
#    define mmap_map_LITTLE_ENDIAN 1
#    define mmap_map_BIG_ENDIAN 0
#else
#    if __GNUC__ >= 4
#        define mmap_map_LITTLE_ENDIAN (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#        define mmap_map_BIG_ENDIAN (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#    else
#        error cannot determine endianness
#    endif
#endif

// inline
#ifdef _WIN32
#    define mmap_map_NOINLINE __declspec(noinline)
#else
#    if __GNUC__ >= 4
#        define mmap_map_NOINLINE __attribute__((noinline))
#    else
#        define mmap_map_NOINLINE
#    endif
#endif

// count leading/trailing bits
#ifdef _WIN32
#    if mmap_map_BITNESS == 32
#        define mmap_map_BITSCANFORWARD _BitScanForward
#    else
#        define mmap_map_BITSCANFORWARD _BitScanForward64
#    endif
#    include <intrin.h>
#    pragma intrinsic(mmap_map_BITSCANFORWARD)
#    define mmap_map_COUNT_TRAILING_ZEROES(x)                                          \
        [](size_t mask) -> int {                                                         \
            unsigned long index;                                                         \
            return mmap_map_BITSCANFORWARD(&index, mask) ? index : mmap_map_BITNESS; \
        }(x)
#else
#    if __GNUC__ >= 4
#        if mmap_map_BITNESS == 32
#            define mmap_map_CTZ(x) __builtin_ctzl(x)
#            define mmap_map_CLZ(x) __builtin_clzl(x)
#        else
#            define mmap_map_CTZ(x) __builtin_ctzll(x)
#            define mmap_map_CLZ(x) __builtin_clzll(x)
#        endif
#        define mmap_map_COUNT_LEADING_ZEROES(x) (x ? mmap_map_CLZ(x) : mmap_map_BITNESS)
#        define mmap_map_COUNT_TRAILING_ZEROES(x) (x ? mmap_map_CTZ(x) : mmap_map_BITNESS)
#    else
#        error clz not supported
#    endif
#endif

// umul
namespace mmap_map {
namespace detail {
#if defined(__SIZEOF_INT128__)
#    define mmap_map_UMULH(a, b) \
        static_cast<uint64_t>(     \
            (static_cast<unsigned __int128>(a) * static_cast<unsigned __int128>(b)) >> 64u)

#    define mmap_map_HAS_UMUL128 1
inline uint64_t umul128(uint64_t a, uint64_t b, uint64_t* high) {
    auto result = static_cast<unsigned __int128>(a) * static_cast<unsigned __int128>(b);
    *high = static_cast<uint64_t>(result >> 64);
    return static_cast<uint64_t>(result);
}
#elif (defined(_WIN32) && mmap_map_BITNESS == 64)
#    define mmap_map_HAS_UMUL128 1
#    include <intrin.h> // for __umulh
#    pragma intrinsic(__umulh)
#    pragma intrinsic(_umul128)
#    define mmap_map_UMULH(a, b) __umulh(a, b)
inline uint64_t umul128(uint64_t a, uint64_t b, uint64_t* high) {
    return _umul128(a, b, high);
}
#endif
} // namespace detail
} // namespace mmap_map

// likely/unlikely
#if __GNUC__ >= 4
#    define mmap_map_LIKELY(condition) __builtin_expect(static_cast<bool>(condition), 1)
#    define mmap_map_UNLIKELY(condition) __builtin_expect(static_cast<bool>(condition), 0)
#else
#    define mmap_map_LIKELY(condition) condition
#    define mmap_map_UNLIKELY(condition) condition
#endif
namespace mmap_map {

namespace detail {

	// make sure this is not inlined as it is slow and dramatically enlarges code, thus making other
	// inlinings more difficult. Throws are also generally the slow path.
	template <typename E, typename... Args>
		static mmap_map_NOINLINE void doThrow(Args&&... args) {
			throw E(std::forward<Args>(args)...);
		}

	template <typename E, typename T, typename... Args>
		static T* assertNotNull(T* t, Args&&... args) {
			if (mmap_map_UNLIKELY(nullptr == t)) {
				doThrow<E>(std::forward<Args>(args)...);
			}
			return t;
		}

	template <typename T>
		inline T unaligned_load(void const* ptr) {
			// using memcpy so we don't get into unaligned load problems.
			// compiler should optimize this very well anyways.
			T t;
			std::memcpy(&t, ptr, sizeof(T));
			return t;
		}

	// All empty maps initial mInfo point to this infobyte. That way lookup in an empty map
	// always returns false, and this is a very hot byte.
	//
	// we have to use data >1byte (at least 2 bytes), because initially we set mShift to 63 (has to be
	// <63), so initial index will be 0 or 1.

} // namespace detail

struct is_transparent_tag {};

// A custom pair implementation is used in the map because std::pair is not is_trivially_copyable,
// which means it would  not be allowed to be used in std::memcpy. This struct is copyable, which is
// also tested.
template <typename First, typename Second>
struct pair {
	using first_type = First;
	using second_type = Second;

	// pair constructors are explicit so we don't accidentally call this ctor when we don't have to.
	explicit pair(std::pair<First, Second> const& o)
		: first{o.first}
	, second{o.second} {}

	// pair constructors are explicit so we don't accidentally call this ctor when we don't have to.
	explicit pair(std::pair<First, Second>&& o)
		: first{std::move(o.first)}
	, second{std::move(o.second)} {}

	constexpr pair(const First& firstArg, const Second& secondArg)
		: first{firstArg}
	, second{secondArg} {}

	constexpr pair(First&& firstArg, Second&& secondArg)
		: first{std::move(firstArg)}
	, second{std::move(secondArg)} {}

	template <typename FirstArg, typename SecondArg>
		constexpr pair(FirstArg&& firstArg, SecondArg&& secondArg)
		: first{std::forward<FirstArg>(firstArg)}
	, second{std::forward<SecondArg>(secondArg)} {}

	template <typename... Args1, typename... Args2>
		pair(std::piecewise_construct_t /*unused*/, std::tuple<Args1...> firstArgs,
				std::tuple<Args2...> secondArgs)
		: pair{firstArgs, secondArgs, std::index_sequence_for<Args1...>{},
			std::index_sequence_for<Args2...>{}} {}

	// constructor called from the std::piecewise_construct_t ctor
	template <typename... Args1, size_t... Indexes1, typename... Args2, size_t... Indexes2>
		inline pair(std::tuple<Args1...>& tuple1, std::tuple<Args2...>& tuple2,
				std::index_sequence<Indexes1...> /*unused*/,
				std::index_sequence<Indexes2...> /*unused*/)
		: first{std::forward<Args1>(std::get<Indexes1>(tuple1))...}
	, second{std::forward<Args2>(std::get<Indexes2>(tuple2))...} {
		// make visual studio compiler happy about warning about unused tuple1 & tuple2.
		// Visual studio's pair implementation disables warning 4100.
		(void)tuple1;
		(void)tuple2;
	}

	first_type& getFirst() {
		return first;
	}
	first_type const& getFirst() const {
		return first;
	}
	second_type& getSecond() {
		return second;
	}
	second_type const& getSecond() const {
		return second;
	}

	void swap(pair<First, Second>& o) {
		using std::swap;
		swap(first, o.first);
		swap(second, o.second);
	}

	First first;
	Second second;
};

// A thin wrapper around std::hash, performing a single multiplication to (hopefully) get nicely
// randomized upper bits, which are used by the mmap_map.
template <typename T>
struct hash : public std::hash<T> {
	size_t operator()(T const& obj) const {
		return std::hash<T>::operator()(obj);
	}
};

// specialization used for uint64_t and int64_t. Uses 128bit multiplication
template <>
struct hash<uint64_t> {
	size_t operator()(uint64_t const& obj) const {
#if defined(mmap_map_HAS_UMUL128)
		// 167079903232 masksum, 120428523 ops best: 0xde5fb9d2630458e9
		static constexpr uint64_t k = UINT64_C(0xde5fb9d2630458e9);
		uint64_t h;
		uint64_t l = detail::umul128(obj, k, &h);
		return h + l;
#elif mmap_map_BITNESS == 32
		static constexpr uint32_t k = UINT32_C(0x9a0ecda7);
		uint64_t const r = obj * k;
		uint32_t h = static_cast<uint32_t>(r >> 32);
		uint32_t l = static_cast<uint32_t>(r);
		return h + l;
#else
		// murmurhash 3 finalizer
		uint64_t h = obj;
		h ^= h >> 33;
		h *= 0xff51afd7ed558ccd;
		h ^= h >> 33;
		h *= 0xc4ceb9fe1a85ec53;
		h ^= h >> 33;
		return static_cast<size_t>(h);
#endif
	}
};

template <>
struct hash<int64_t> {
	size_t operator()(int64_t const& obj) const {
		return hash<uint64_t>{}(static_cast<uint64_t>(obj));
	}
};

template <>
struct hash<uint32_t> {
	size_t operator()(uint32_t const& h) const {
//#if mmap_map_BITNESS == 32
		return static_cast<size_t>((UINT64_C(0xca4bcaa75ec3f625) * (uint64_t)h) >> 32);
//#else
		//return hash<uint64_t>{}(static_cast<uint64_t>(h));
//#endif
	}
};

template <>
struct hash<int32_t> {
	size_t operator()(int32_t const& obj) const {
		return hash<uint32_t>{}(static_cast<uint32_t>(obj));
	}
};

// Hash an arbitrary amount of bytes. This is basically Murmur2 hash without caring about big
// endianness. TODO add a fallback for very large strings?
inline size_t hash_bytes(void const* ptr, size_t const len) {
	static constexpr uint64_t m = UINT64_C(0xc6a4a7935bd1e995);
	static constexpr uint64_t seed = UINT64_C(0xe17a1465);
	static constexpr unsigned int r = 47;

	auto const data64 = reinterpret_cast<uint64_t const*>(ptr);
	uint64_t h = seed ^ (len * m);

	size_t const n_blocks = len / 8;
	for (size_t i = 0; i < n_blocks; ++i) {
		uint64_t k = detail::unaligned_load<uint64_t>(data64 + i);

		k *= m;
		k ^= k >> r;
		k *= m;

		h ^= k;
		h *= m;
	}

	auto const data8 = reinterpret_cast<uint8_t const*>(data64 + n_blocks);
	switch (len & 7u) {
		case 7:
			h ^= static_cast<uint64_t>(data8[6]) << 48u;
			// fallthrough
		case 6:
			h ^= static_cast<uint64_t>(data8[5]) << 40u;
			// fallthrough
		case 5:
			h ^= static_cast<uint64_t>(data8[4]) << 32u;
			// fallthrough
		case 4:
			h ^= static_cast<uint64_t>(data8[3]) << 24u;
			// fallthrough
		case 3:
			h ^= static_cast<uint64_t>(data8[2]) << 16u;
			// fallthrough
		case 2:
			h ^= static_cast<uint64_t>(data8[1]) << 8u;
			// fallthrough
		case 1:
			h ^= static_cast<uint64_t>(data8[0]);
			h *= m;
	};

	h ^= h >> r;
	h *= m;
	h ^= h >> r;

	return static_cast<size_t>(h);
}

template <>
struct hash<std::string> {
	size_t operator()(std::string const& str) const {
		return hash_bytes(str.data(), str.size());
	}
};

namespace detail {

// A highly optimized hashmap implementation, using the Robin Hood algorithm.
//
// In most cases, this map should be usable as a drop-in replacement for std::mmap_map, but be
// about 2x faster in most cases and require much less allocations.
//
// This implementation uses the following memory layout:
//
// [Node, Node, ... Node | info, info, ... infoSentinel ]
//
// * Node: either a DataNode that directly has the std::pair<key, val> as member,
//   or a DataNode with a pointer to std::pair<key,val>. Which DataNode representation to use
//   depends on how fast the swap() operation is. Heuristically, this is automatically choosen based
//   on sizeof(). there are always 2^n Nodes.
//
// * info: Each Node in the map has a corresponding info byte, so there are 2^n info bytes.
//   Each byte is initialized to 0, meaning the corresponding Node is empty. Set to 1 means the
//   corresponding node contains data. Set to 2 means the corresponding Node is filled, but it
//   actually belongs to the previous position and was pushed out because that place is already
//   taken.
//
// * infoSentinel: Sentinel byte set to 1, so that iterator's ++ can stop at end() without the need
// for a idx
//   variable.
//
// According to STL, order of templates has effect on throughput. That's why I've moved the boolean
// to the front.
// https://www.reddit.com/r/cpp/comments/ahp6iu/compile_time_binary_size_reductions_and_cs_future/eeguck4/
template <size_t MaxLoadFactor100, typename Key, typename T, typename Hash>
class map
: public Hash {
public:
	using key_type    = Key;
	using mapped_type = typename std::conditional<std::is_same<T  , std::string_view>::value, uint32_t, T  >::type;
	using value_type  = mmap_map::pair<typename std::conditional<std::is_same<Key, std::string_view>::value, uint32_t, Key>::type
                                    ,typename std::conditional<std::is_same<T  , std::string_view>::value, uint32_t, T  >::type>;
	using size_type   = size_t;
	using hasher      = Hash;
	using Self        = map<MaxLoadFactor100, key_type, T, hasher>;

private:
	static_assert(!std::is_same<Key, std::string>::value     ,"mmap_map uses string_view as key (not slow std::string)\n");
	//static_assert(!std::is_same<Key, std::string_view>::value,"mmap_map does not deal with strings or string_views (pointers). Use char_array\n");
	static_assert(!std::is_same<T, std::string>::value       ,"mmap_map uses string_view as value (not slow std::string)\n");
	//static_assert(!std::is_same<T, std::string_view>::value  ,"mmap_map does not deal with strings or string_views (pointers). Use char_array\n");

	static_assert(!(std::is_same<Key, std::string_view>::value && std::is_same<T, std::string_view>::value)
        ,"mmap_map can not have std::string_view for key and value simultaneously\n");
	static_assert(MaxLoadFactor100 > 10 && MaxLoadFactor100 < 100, "MaxLoadFactor100 needs to be >10 && < 100");

  static constexpr bool    using_key_sview      = std::is_same<Key, std::string_view>::value;
  static constexpr bool    using_val_sview      = std::is_same<T, std::string_view>::value;
  static constexpr bool    using_sview          = using_key_sview || using_val_sview;
	static constexpr size_t  InitialNumElements   = 1024;
	static constexpr int     InitialInfoNumBits   = 5;
	static constexpr uint8_t InitialInfoInc       = 1 << InitialInfoNumBits;
	static constexpr uint8_t InitialInfoHashShift = sizeof(size_t) * 8 - InitialInfoNumBits;

	// type needs to be wider than uint8_t.
	using InfoType = int32_t;

	// DataNode ////////////////////////////////////////////////////////

	// Primary template for the data node. We have special implementations for small and big
	// objects. For large objects it is assumed that swap() is fairly slow, so we allocate these on
	// the heap so swap merely swaps a pointer.

	// Small: just allocate on the stack.
	template <typename M>
	class DataNode {
		public:
			template <typename... Args>
				explicit DataNode(M& mmap_map_UNUSED(map) /*unused*/, Args&&... args)
				: mData(std::forward<Args>(args)...) {}

			DataNode(M& mmap_map_UNUSED(map) /*unused*/, DataNode<M>&& n)
				: mData(std::move(n.mData)) {}

			// doesn't do anything
			void destroy(M& mmap_map_UNUSED(map) /*unused*/) {}
			void destroyDoNotDeallocate() {}

			value_type const* operator->() const {
				return &mData;
			}
			value_type* operator->() {
				return &mData;
			}

			const value_type& operator*() const {
				return mData;
			}

			value_type& operator*() {
				return mData;
			}

			typename value_type::first_type& getFirst() {
				return mData.first;
			}

			typename value_type::first_type const& getFirst() const {
				return mData.first;
			}

			typename value_type::second_type& getSecond() {
				return mData.second;
			}

			typename value_type::second_type const& getSecond() const {
				return mData.second;
			}

			void swap(DataNode<M>& o) {
				mData.swap(o.mData);
			}

		private:
			value_type mData;
	};

	using Node = DataNode<Self>;

	// Iter ////////////////////////////////////////////////////////////

	struct fast_forward_tag {};

	// generic iterator for both const_iterator and iterator.
	template <bool IsConst>
		class Iter {
			private:
				using NodePtr = typename std::conditional<IsConst, Node const*, Node*>::type;

			public:
				using difference_type = std::ptrdiff_t;
				using value_type = typename Self::value_type;
				using reference = typename std::conditional<IsConst, value_type const&, value_type&>::type;
				using pointer = typename std::conditional<IsConst, value_type const*, value_type*>::type;
				using iterator_category = std::forward_iterator_tag;

				// default constructed iterator can be compared to itself, but WON'T return true when
				// compared to end().
				Iter()
					: mKeyVals(nullptr)
						, mInfo(nullptr) {}

				// both const_iterator and iterator can be constructed from a non-const iterator
				Iter(Iter<false> const& other)
					: mKeyVals(other.mKeyVals)
						, mInfo(other.mInfo) {}

				Iter(NodePtr valPtr, uint8_t const* infoPtr)
					: mKeyVals(valPtr)
						, mInfo(infoPtr) {}

				Iter(NodePtr valPtr, uint8_t const* infoPtr,
						fast_forward_tag mmap_map_UNUSED(tag) /*unused*/)
					: mKeyVals(valPtr)
						, mInfo(infoPtr) {
							fastForward();
						}

				// prefix increment. Undefined behavior if we are at end()!
				Iter& operator++() {
					mInfo++;
					mKeyVals++;
					fastForward();
					return *this;
				}

				reference operator*() const {
					return **mKeyVals;
				}

				pointer operator->() const {
					return &**mKeyVals;
				}

				template <bool O>
					bool operator==(Iter<O> const& o) const {
						return mKeyVals == o.mKeyVals;
					}

				template <bool O>
					bool operator!=(Iter<O> const& o) const {
						return mKeyVals != o.mKeyVals;
					}

			private:
				// fast forward to the next non-free info byte
        void fastForward() {
#ifdef FAST_MISS_ALIGNED_CPU
          int inc;
					do {
						auto const n = detail::unaligned_load<uint64_t>(mInfo);
#if mmap_map_LITTLE_ENDIAN
						inc = mmap_map_COUNT_TRAILING_ZEROES(n) / 8;
#else
						inc = mmap_map_COUNT_LEADING_ZEROES(n) / 8;
#endif
						mInfo += inc;
						mKeyVals += inc;
					} while (mmap_map_UNLIKELY(inc == sizeof(uint64_t)));
#else
          while(true) {
						if (*mInfo)
              return;
						mInfo++;
						mKeyVals++;
					}
#endif
				}

				friend class map<MaxLoadFactor100, key_type, T, hasher>;
				NodePtr mKeyVals;
				uint8_t const* mInfo;
		};

	////////////////////////////////////////////////////////////////////

	size_t calcNumBytesInfo(size_t numElements) const {
		const size_t s = sizeof(uint8_t) * (numElements + 1);
		assert(s / sizeof(uint8_t) == numElements + 1);
		// make sure it's a bit larger, so we can load 64bit numbers
		return s + sizeof(uint64_t);
	}
	size_t calcNumBytesNode(size_t numElements) const {
		const size_t s = sizeof(Node) * numElements;
		assert(s / sizeof(Node) == numElements);
		return s;
	}
	size_t calcNumBytesTotal(size_t numElements) const {
		const size_t si = calcNumBytesInfo(numElements);
		const size_t sn = calcNumBytesNode(numElements);
		const size_t s = si + sn;
		assert(!(s <= si || s <= sn));
		return s;
	}

	void garbage_collect() const {
		if (mmap_map_UNLIKELY(!loaded))
			return;

		loaded = false;
		bool is_empty = empty();

		if(mmap_base) {
			munmap(mmap_base, mmap_size);
			mmap_base = 0;
			mmap_size = 0;
		}

		if(mmap_fd >= 0) {
			close(mmap_fd);
			mmap_fd = -1;
		}
    if constexpr (using_sview) {
      if (mmap_txt_fd >= 0) {
        close(mmap_txt_fd);
        mmap_txt_fd = -1;
      }
    }

		if (is_empty) {
			unlink(mmap_name.c_str());
      if constexpr (using_sview) {
        unlink((mmap_name+"txt").c_str());
      }
		}
	}

	size_t calc_mmap_size(size_t nelems) const {
		size_t total = (3+2)*sizeof(uint64_t); // m* fields + 2 for alignment
		total += calcNumBytesTotal(nelems);

		return total;
	}

	void grow_txt_mmap(size_t size) {
    assert(mmap_txt_size<size);
    mmap_txt_base = reinterpret_cast<uint64_t *>(mremap(mmap_txt_base, mmap_txt_size, size, MREMAP_MAYMOVE));
    if (mmap_txt_base == MAP_FAILED) {
      std::cerr << "ERROR: remmap could not allocate" << mmap_name << "txt with " << size << "bytes\n";
      exit(-1);
    }
    if (mmap_txt_fd>=0) {
      int ret = ftruncate(mmap_txt_fd, size);
      if(ret<0) {
        std::cerr << "ERROR: ftruncate could not allocate " << mmap_name << " to " << size << "\n";
        mmap_base = 0;
        exit(-1);
      }
    }

    mmap_txt_size = size;
  }

	std::tuple<uint64_t *, size_t> create_mmap(int fd, size_t size) const {
    void *base;

    if (fd < 0) {
      base = mmap(0, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, fd, 0); // superpages
    }else{
      struct stat s;
      int status = fstat(fd, &s);
      if (status < 0) {
        std::cerr << "mmap_map::reload ERROR Could not check file status " << mmap_name << std::endl;
        exit(-3);
      }
      if (s.st_size <= size) {
        int ret = ftruncate(fd, size);
        if (ret<0) {
          std::cerr << "mmap_map::reload ERROR ftruncate could not resize  " << mmap_name << " to " << size << "\n";
          exit(-1);
        }
      } else {
        size = s.st_size;
      }

      base = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); // no superpages
    }

    if(base == MAP_FAILED) {
			std::cerr << "mmap_map::reload ERROR mmap could not adjust\n";
			exit(-1);
		}

		return std::make_tuple(reinterpret_cast<uint64_t *>(base),size);
	}

  void try_reclaim_mmaps() const {
    while(!gc_queue.empty()) {
      auto func = gc_queue.front();
      gc_queue.pop();
      func();
    }
  }

  void setup_mmap(size_t n_entries) const {

    if (mmap_name.empty()) {
      mmap_fd     = -1;
      if constexpr (using_sview) {
        mmap_txt_fd = -1;
      }
      if (n_entries == 0)
        n_entries = InitialNumElements;
    }else{
      if (mmap_fd<0) {
        mmap_fd = open(mmap_name.c_str(), O_RDWR | O_CREAT, 0644);
        if (mmap_fd<0) {
          try_reclaim_mmaps();

          mmap_fd = open(mmap_name.c_str(), O_RDWR | O_CREAT, 0644);
          if (mmap_fd<0) {
            std::cerr << "mmap_map::reload ERROR failed to setup " << mmap_name << std::endl;
            exit(-4);
          }
        }
      }

      if (n_entries==0) { // reload
        int sz = read(mmap_fd,&n_entries,8);
        if (sz!=8) {
          n_entries = InitialNumElements;
        }else{
          n_entries++; // We read mMask at base 0
          assert(n_entries>=InitialNumElements);
        }
      }

      if constexpr (using_sview) {
        if (mmap_txt_fd<0) {
          mmap_txt_fd = open((mmap_name + "txt").c_str(), O_RDWR | O_CREAT, 0644);
          if (mmap_txt_fd < 0) {
            try_reclaim_mmaps();

            mmap_txt_fd = open((mmap_name + "txt").c_str(), O_RDWR | O_CREAT, 0644);
            if (mmap_txt_fd < 0) {
              std::cerr << "mmap_map::reload ERROR failed to setup " << mmap_name << "txt\n";
              exit(-4);
            }
          }
        }
      }
    }

    std::tie(mmap_base    , mmap_size    ) = create_mmap(mmap_fd    , calc_mmap_size(n_entries));
    if constexpr (using_sview) {
      if (mmap_txt_size==0) {
        std::tie(mmap_txt_base, mmap_txt_size) = create_mmap(mmap_txt_fd, 8192);
      }
    }

		mMask                  = &mmap_base[0];
    mNumElements           = &mmap_base[1];
		mMaxNumElementsAllowed = &mmap_base[2];
		mInfoInc               = reinterpret_cast<InfoType *>(&mmap_base[3]);
		mInfoHashShift         = reinterpret_cast<InfoType *>(&mmap_base[4]);

		mInfo = reinterpret_cast<uint8_t*>(&mmap_base[5]);
		if (*mMask == n_entries - 1) {
			assert(*mMaxNumElementsAllowed<*mMask);
			assert(calc_mmap_size(*mMask+1)==mmap_size);
			assert(mInfo[*mMask+1] == 1); // Sentinel
			mKeyVals = reinterpret_cast<Node*>(&mmap_base[5+(*mMask+9)/sizeof(uint64_t)]);
    }else{
			assert(*mMaxNumElementsAllowed <= n_entries); // less due to load factor
			assert(*mNumElements==0);
      // Setup intial mmap values
			*mMaxNumElementsAllowed = calcMaxNumElementsAllowed(n_entries);
			*mMask = n_entries - 1;
			mInfo[n_entries] = 1; // Sentinel
      *mInfoInc       = InitialInfoInc;
      *mInfoHashShift = InitialInfoHashShift;
			mKeyVals = reinterpret_cast<Node*>(&mmap_base[5+(*mMask+9)/sizeof(uint64_t)]); // 9 to be 8 byte aligned
		}

	}

	void reload() const {
		assert(!loaded);

		assert(mmap_fd <0);

		setup_mmap(0);

		gc_queue.push(std::bind(&map<MaxLoadFactor100, Key, T, Hash>::garbage_collect, this));

		loaded = true;
	}

	// highly performance relevant code.
	// Lower bits are used for indexing into the array (2^n size)
	// The upper 1-5 bits need to be a reasonable good hash, to save comparisons.
	void keyToIdx(const Key &key, int& idx, InfoType& info) const {
		static constexpr size_t bad_hash_prevention =
			std::is_same<::mmap_map::hash<key_type>, hasher>::value
			? 1
			: (mmap_map_BITNESS == 64 ? UINT64_C(0xb3727c1f779b8d8b) : UINT32_C(0xda4afe47));

		if (mmap_map_UNLIKELY(!loaded)) {
			reload();
		}

		idx = Hash::operator()(key) * bad_hash_prevention;
		info = static_cast<InfoType>(*mInfoInc + static_cast<InfoType>(idx >> *mInfoHashShift));
		idx &= *mMask;
	}

	// forwards the index by one, wrapping around at the end
	inline int next_idx(int idx) const {
		idx = (idx + 1) & *mMask;
    return idx;
	}
	inline InfoType next_info(InfoType info) const {
		return static_cast<InfoType>(info + *mInfoInc);
	}

	void nextWhileLess(InfoType* info, int* idx) const {
		// unrolling this by hand did not bring any speedups.
		while (*info < mInfo[*idx]) {
			*idx  = next_idx(*idx);
			*info = next_info(*info);
		}
	}

	// Shift everything up by one element. Tries to move stuff around.
	// True if some shifting has occured (entry under idx is a constructed object)
	// Fals if no shift has occured (entry under idx is unconstructed memory)
	void shiftUp(int idx, int const insertion_idx) {
#if 0
		if (mmap_map_LIKELY(idx>insertion_idx)) {
			memmove(&mKeyVals[insertion_idx+1],&mKeyVals[insertion_idx],(idx-insertion_idx)*sizeof(Node));
			for(auto i=idx;i>insertion_idx;--i) {
				mInfo[i] = static_cast<uint8_t>(mInfo[i-1] + *mInfoInc);
				if (mmap_map_UNLIKELY(mInfo[i] + *mInfoInc > 0xFF)) {
					*mMaxNumElementsAllowed = 0;
				}
			}

			return;
		}
#endif
		while (idx != insertion_idx) {
			unsigned int prev_idx = (idx - 1) & *mMask;
#if 0
			::memcpy(&mKeyVals[idx],&mKeyVals[prev_idx],sizeof(Node));
#else
			if (mInfo[idx]) {
				mKeyVals[idx] = std::move(mKeyVals[prev_idx]);
			} else {
				::new (static_cast<void*>(mKeyVals + idx)) Node(std::move(mKeyVals[prev_idx]));
			}
#endif
			mInfo[idx] = static_cast<uint8_t>(mInfo[prev_idx] + *mInfoInc);
			if (mmap_map_UNLIKELY(mInfo[idx] + *mInfoInc > 0xFF)) {
				*mMaxNumElementsAllowed = 0;
			}
			idx = prev_idx;
		}
	}

	void shiftDown(int idx) {
		// until we find one that is either empty or has zero offset.
		// TODO we don't need to move everything, just the last one for the same bucket.
		mKeyVals[idx].destroy(*this);

		// until we find one that is either empty or has zero offset.
		auto nextIdx = next_idx(idx);

		while (mInfo[nextIdx] >= 2 * *mInfoInc) {
			mInfo[idx] = static_cast<uint8_t>(mInfo[nextIdx] - *mInfoInc);
			mKeyVals[idx] = std::move(mKeyVals[nextIdx]);
			idx = nextIdx;
			nextIdx = next_idx(idx);
		}

		mInfo[idx] = 0;
		// don't destroy, we've moved it
		// mKeyVals[idx].destroy(*this);
		mKeyVals[idx].~Node();
	}

	inline bool equals(const Key k1, const Key k2) const {
		return k1 == k2;
	}

	inline bool equals(std::string_view k1, const uint32_t key_pos) const {
    assert(using_sview);
		auto txt = get_sview(key_pos);
		return k1 == txt;
	}

	// copy of find(), except that it returns iterator instead of const_iterator.
	template <typename Other>
		int findIdx(Other const& key) const {
			int idx;
			InfoType info;
			keyToIdx(key, idx, info);

			do {
				// unrolling this twice gives a bit of a speedup. More unrolling did not help.
				if (info == mInfo[idx] && equals(key, mKeyVals[idx].getFirst())) {
					return idx;
				}
				idx  = next_idx(idx);
				info = next_info(info);
				if (info == mInfo[idx] && equals(key, mKeyVals[idx].getFirst())) {
					return idx;
				}
				idx  = next_idx(idx);
				info = next_info(info);
			} while (info <= mInfo[idx]);

			// nothing found!
			return -1; //*mMask == 0 ? 0 : *mMask + 1;
		}

	// inserts a keyval that is guaranteed to be new, e.g. when the hashmap is resized.
	// @return index where the element was created
	size_t insert_move(Node&& keyval) {
		// we don't retry, fail if overflowing
		// don't need to check max num elements
    if (*mMaxNumElementsAllowed == 0) {
      bool ok = try_increase_info();
      assert(ok);
    }

		int idx;
		InfoType info;
		if constexpr (using_key_sview) {
			keyToIdx(get_sview(keyval.getFirst()), idx, info);
		}else{
			keyToIdx(keyval.getFirst(), idx, info);
		}

		// skip forward. Use <= because we are certain that the element is not there.
		while (info <= mInfo[idx]) {
			idx = next_idx(idx);
      info = next_info(info);
		}

		// key not found, so we are now exactly where we want to insert it.
		auto const insertion_idx = idx;
		auto const insertion_info = static_cast<uint8_t>(info);
		if (mmap_map_UNLIKELY(insertion_info + *mInfoInc > 0xFF)) {
			*mMaxNumElementsAllowed = 0;
		}

		// find an empty spot
		while (0 != mInfo[idx]) {
      idx  = next_idx(idx);
      info = next_info(info);
#ifndef NDEBUG
			conflicts++;
#endif
		}

		auto& l = mKeyVals[insertion_idx];
		if (idx == insertion_idx) {
			::new (static_cast<void*>(&l)) Node(std::move(keyval));
		} else {
			shiftUp(idx, insertion_idx);
			l = std::move(keyval);
		}

		// put at empty spot
		mInfo[insertion_idx] = insertion_info;
#ifndef NDEBUG
		static int conta=0;
		if (((++conta)&0xFFFF)==0 && *mNumElements>100) {
			if (conflict_factor()>0.05) {
				std::cerr << "potential bad hash for mmap_name:" << mmap_name << ", try to debug it\n";
			}
		}
#endif

		++(*mNumElements);
		return insertion_idx;
	}

  void setup_pointers() {
		static size_t static_mNumElements           = 0;
		static size_t static_mMask                  = 0;
		static size_t static_mMaxNumElementsAllowed = 0;
		static InfoType static_InitialInfoInc       = InitialInfoInc;
		static InfoType static_InitialInfoHashShift = InitialInfoHashShift;

		assert(static_mMask==0);

		mNumElements           = &static_mNumElements;
		mMask                  = &static_mMask;
		mMaxNumElementsAllowed = &static_mMaxNumElementsAllowed;
		mInfoInc               = &static_InitialInfoInc;
		mInfoHashShift         = &static_InitialInfoHashShift;
  }
public:
	using iterator = Iter<false>;
	using const_iterator = Iter<true>;

	explicit map(std::string_view _map_name)
		: Hash{Hash{}}
	  , mmap_name{_map_name} {

    setup_pointers();
	}

	explicit map(std::string_view _path, std::string_view _map_name)
		: Hash{Hash{}}
	  , mmap_name{std::string(_path) + std::string("/") + std::string(_map_name)} {

    struct stat sb;
    if (_path != ".") {
      std::string path(_path);
      if (stat(path.c_str(), &sb) != 0 || !S_ISDIR(sb.st_mode)) {
        int e = mkdir(path.c_str(), 0755);
        assert(e>=0);
      }
    }

    setup_pointers();
	}

	explicit map()
		: Hash{Hash{}} {

    setup_pointers();
	}

	map(map&& o) = delete;
	map& operator=(map&& o) = delete;
	map(const map& o) = delete;
	map& operator=(map const& o) = delete;
	void swap(map& o) = delete;

	// Clears all data, without resizing.
	void clear() {
		if (mmap_map_UNLIKELY(!loaded)) {
			unlink(mmap_name.c_str());
			if constexpr (using_sview) {
				clear_sview();
			}
			return;
		}
		if (empty()) {
			garbage_collect();
			return;
		}

		static_assert(std::is_trivially_destructible<Node>::value,"Objects in map should be simple without pointer (simple destruction)");

		// clear everything except the sentinel
		// std::memset(mInfo, 0, sizeof(uint8_t) * (mMask + 1));
		uint8_t const z = 0;
		std::fill(mInfo, mInfo + (sizeof(uint8_t) * (*mMask + 1)), z);

		*mInfoInc = InitialInfoInc;
		*mInfoHashShift = InitialInfoHashShift;

    *mNumElements = 0;
		// Do not clear mmap_name or loaded
	}

	// Destroys the map and all it's contents.
	virtual ~map() {
		destroy();
	}

	iterator set(key_type&& key, T &&val) {
		return doCreate(std::move(key), std::move(val));
	}
	iterator set(const key_type& key, T &&val) {
		return doCreate(key, std::move(val));
	}
	iterator set(const key_type& key, const T &val) {
		return doCreate(key, val);
	}
	iterator set(key_type&& key, const T &val) {
		return doCreate(std::move(key), val);
	}

#if 0
	template <typename Iter>
		void insert(Iter first, Iter last) {
			for (; first != last; ++first) {
				// value_type ctor needed because this might be called with std::pair's
				insert(value_type(*first));
			}
		}

	template <typename... Args>
		std::pair<iterator, bool> emplace(Args&&... args) {
			Node n{*this, std::forward<Args>(args)...};
			auto r = doInsert(std::move(n));
			if (!r.second) {
				// insertion not possible: destroy node
				n.destroy(*this);
			}
			return r;
		}

	std::pair<iterator, bool> insert(const value_type& keyval) {
		return doInsert(keyval);
	}

	std::pair<iterator, bool> insert(value_type&& keyval) {
		return doInsert(std::move(keyval));
	}
#endif

	// Returns 1 if key is found, 0 otherwise.
	[[nodiscard]] bool has(const key_type& key) const {
		return findIdx(key)>=0;
	}

	std::string_view get_sview(uint32_t key_pos) const {
		if (mmap_map_UNLIKELY(!loaded)) {
			reload();
		}
    assert(using_sview);
		assert(key_pos<mmap_txt_base[0]);
		std::string_view sview(reinterpret_cast<char *>(&mmap_txt_base[key_pos+1]),mmap_txt_base[key_pos]);
		return sview;
	}

	// Returns a reference to the value found for key.
	[[nodiscard]] T get(key_type const& key) const {
		auto idx = findIdx(key);
		assert(idx>=0);

		if constexpr (using_val_sview) {
      return get_sview(mKeyVals[idx].getSecond());
    }else{
      return mKeyVals[idx].getSecond();
    }
	}

	[[nodiscard]] const T &get(const value_type& it) const {
		if constexpr (using_val_sview) {
      return get_sview(it.second);
    }else{
      return it.second;
    }
	}

	[[nodiscard]] const T &get(const_iterator it) const {
		if constexpr (using_val_sview) {
      return get_sview(it->second);
    }else{
      return it->second;
    }
	}

	[[nodiscard]] Key get_key(const_iterator it) const {
		if constexpr (using_key_sview) {
      return get_sview(it->first);
    }else{
      return it->first;
    }
	}
	[[nodiscard]] Key get_key(const value_type& it) const {
		if constexpr (using_key_sview) {
      return get_sview(it.first);
    }else{
      return it.first;
    }
	}

	[[nodiscard]] T *ref(key_type const& key) {
		auto idx = findIdx(key);
		assert(idx>=0);

		if constexpr (using_val_sview) {
      assert(false); // Do not get a reference to a std::string_view
      return &get_sview(mKeyVals[idx].getSecond());
    }else{
      return &mKeyVals[idx].getSecond();
    }
	}

	[[nodiscard]] T *ref(const value_type& it) {
		if constexpr (using_val_sview) {
      assert(false); // Do not get a reference to a std::string_view
      return &get_sview(it.second);
    }else{
      return &it.second;
    }
	}

	[[nodiscard]] T *ref(iterator it) {
		if constexpr (using_val_sview) {
      assert(false); // Do not get a reference to a std::string_view
      return &get_sview(it->second);
    }else{
      return &it->second;
    }
	}

	[[nodiscard]] const_iterator find(const key_type& key) const {
		const auto idx = findIdx(key);
    if (idx<0)
      return end();
		return const_iterator{mKeyVals + idx, mInfo + idx};
	}

	template <typename OtherKey>
		[[nodiscard]] const_iterator find(const OtherKey& key, is_transparent_tag /*unused*/) const {
			const auto idx = findIdx(key);
      if (idx<0)
        return cend();
			return const_iterator{mKeyVals + idx, mInfo + idx};
		}

	[[nodiscard]] iterator find(const key_type& key) {
		const auto idx = findIdx(key);
    if (idx<0)
      return end();
		return iterator{mKeyVals + idx, mInfo + idx};
	}

	template <typename OtherKey>
		[[nodiscard]] iterator find(const OtherKey& key, is_transparent_tag /*unused*/) {
			const auto idx = findIdx(key);
      if (idx<0)
        return end();
			return iterator{mKeyVals + idx, mInfo + idx};
		}

	[[nodiscard]] iterator begin() {
		if (mmap_map_UNLIKELY(!loaded)) {
			reload();
		}

		if (empty()) {
			return end();
		}
		return iterator(mKeyVals, mInfo, fast_forward_tag{});
	}
	[[nodiscard]] const_iterator begin() const {
		return cbegin();
	}
	[[nodiscard]] const_iterator cbegin() const {
		if (mmap_map_UNLIKELY(!loaded)) {
			reload();
		}

		if (empty()) {
			return cend();
		}
		return const_iterator(mKeyVals, mInfo, fast_forward_tag{});
	}

	[[nodiscard]] iterator end() {
		// no need to supply valid info pointer: end() must not be dereferenced, and only node
		// pointer is compared.
		return iterator{reinterpret_cast<Node*>(&mKeyVals[*mMask+1]), nullptr};
	}
	[[nodiscard]] const_iterator end() const {
		return cend();
	}
	[[nodiscard]] const_iterator cend() const {
		return const_iterator{reinterpret_cast<Node*>(&mKeyVals[*mMask+1]), nullptr};
	}

	iterator erase(const_iterator &pos) {
		// its safe to perform const cast here
		return erase(iterator{const_cast<Node*>(pos.mKeyVals), const_cast<uint8_t*>(pos.mInfo)});
	}

	// Erases element at pos, returns iterator to the next element.
	iterator erase(iterator pos) {
		// we assume that pos always points to a valid entry, and not end().
		auto const idx = static_cast<size_t>(pos.mKeyVals - mKeyVals);

		shiftDown(idx);
		--(*mNumElements);

		if (*pos.mInfo) {
			// we've backward shifted, return this again
			return pos;
		}

		// no backward shift, return next element
		return ++pos;
	}

	size_t erase(const key_type& key) {
		int idx;
		InfoType info;
		keyToIdx(key, idx, info);

		// check while info matches with the source idx
		do {
			if (info == mInfo[idx] && equals(key, mKeyVals[idx].getFirst())) {
				shiftDown(idx);
				--(*mNumElements);
				return 1;
			}
      idx  = next_idx(idx);
      info = next_info(info);
		} while (info <= mInfo[idx]);

		// nothing found to delete
		return 0;
	}

	void reserve(size_t count) {
		auto newSize = InitialNumElements > *mMask + 1 ? InitialNumElements : *mMask + 1;
		while (calcMaxNumElementsAllowed(newSize) < count && newSize != 0) {
			newSize *= 2;
		}
		assert(newSize != 0);

		rehash(newSize);
	}

	[[nodiscard]] size_type size() const {
		return *mNumElements;
	}

	[[nodiscard]] bool empty() const {
		return 0 == *mNumElements;
	}

	[[nodiscard]] size_t capacity() const {
		if (loaded)
			return *mMaxNumElementsAllowed;
		return calcMaxNumElementsAllowed(InitialNumElements);
	}

	[[nodiscard]] float max_load_factor() const {
		return MaxLoadFactor100 / 100.0f;
	}

	// Average number of elements per bucket. Since we allow only 1 per bucket
	[[nodiscard]] float load_factor() const {
		return static_cast<float>(size()) / (*mMask + 1);
	}

	[[nodiscard]] size_t txt_size() const {
		if constexpr (using_sview) {
			return mmap_txt_size;
		}else{
			return 0;
		}
	}

#ifndef NDEBUG
	float conflict_factor() const {
		return static_cast<float>(conflicts) / (*mNumElements + 1);
	}
#else
	float conflict_factor() const { return 0.0; }
#endif

private:
	void rehash(size_t numBuckets) {
		assert(mmap_map_UNLIKELY((numBuckets & (numBuckets - 1)) == 0)); // rehash only allowed for power of two

		if (mmap_map_UNLIKELY(!loaded))
			reload();

		const size_t oldMaxElements = *mMask + 1;
		if (oldMaxElements >= numBuckets)
			return; // done

		rename(mmap_name.c_str(), (mmap_name + "old").c_str());

		const size_t  old_mmap_size = mmap_size;
		uint64_t     *old_mmap_base = mmap_base;
		const int     old_mmap_fd   = mmap_fd;

		Node* const oldKeyVals        = mKeyVals;
		uint8_t const* const oldInfo  = mInfo;

    mmap_fd = -1;
		setup_mmap(numBuckets);

		assert(*mNumElements == 0);
		assert(*mMask == numBuckets - 1);
		assert(*mMaxNumElementsAllowed == calcMaxNumElementsAllowed(numBuckets));

		//std::cout << "resize sz:" << numBuckets << " mmap_name:" << mmap_name << "\n";

		for (size_t i = 0; i < oldMaxElements; ++i) {
			if (oldInfo[i] != 0) {
				insert_move(std::move(oldKeyVals[i]));
				// destroy the node but DON'T destroy the data.
				oldKeyVals[i].~Node();
			}
		}

		// don't destroy old data: put it into the pool instead
		munmap(old_mmap_base, old_mmap_size);
		close(old_mmap_fd);

		unlink((mmap_name + "old").c_str());
	}
	size_t mask() const {
		return *mMask;
	}

	uint32_t allocate_sview_id(std::string_view txt) {

		if (mmap_map_UNLIKELY(!loaded))
			reload();

    assert(using_sview);

    auto insert_point = mmap_txt_base[0]+1;
    if (mmap_txt_size <= (8*insert_point+4096))
      grow_txt_mmap(std::max(mmap_txt_size*2,8*insert_point+8192));
    assert(mmap_txt_size > (8*insert_point+4096));

    char *ptr = reinterpret_cast<char *>(&mmap_txt_base[insert_point+1]);
    size_t sz=0;
    for(const auto c:txt) {
      *ptr++ = c;
      sz++;
      assert(sz<4096); // IDs should not be so long
    }
    mmap_txt_base[insert_point] = sz;
    assert(sz == txt.size());
    *ptr=0;
    auto bytes = sz+1;
    // Extra space from bytes&0xF to xtra_space
    // 0 -> 0
    // 1 -> 15
    // 2 -> 14
    // 14 -> 2
    // 15 -> 1
    uint8_t xtra_space = bytes&0xF;
    xtra_space = (~xtra_space)&0xF;
    mmap_txt_base[0] += 1+(bytes+xtra_space+7)/8; // +7 to cheaply round up, +1 for the strlen

		return insert_point;
	}

	void clear_sview() const {
    assert(using_sview);
    if (mmap_txt_base == nullptr) {
      unlink((mmap_name+"txt").c_str());
      return;
    }
    mmap_txt_base[0] = 0;
	}

	template <typename Arg, typename Data>
		iterator doCreate(Arg&& key, Data&& val) {
			while (true) {
				int idx;
				InfoType info;
				keyToIdx(key, idx, info);
				nextWhileLess(&info, &idx);

				// while we potentially have a match. Can't do a do-while here because when mInfo is 0
				// we don't want to skip forward
        bool found = false;
				while (info == mInfo[idx]) {
					if (equals(key, mKeyVals[idx].getFirst())) {
            found = true;
            break;
					}
          idx  = next_idx(idx);
          info = next_info(info);
				}

				auto const insertion_idx = idx;
				auto const insertion_info = info;
        if (!found) {
          // unlikely that this evaluates to true
          if (mmap_map_UNLIKELY(*mNumElements >= *mMaxNumElementsAllowed)) {
            increase_size();
            continue;
          }

          // key not found, so we are now exactly where we want to insert it.
          if (mmap_map_UNLIKELY(insertion_info + *mInfoInc > 0xFF)) {
            *mMaxNumElementsAllowed = 0;
          }

          // find an empty spot
          while (0 != mInfo[idx]) {
            idx  = next_idx(idx);
            info = next_info(info);
          }
        }

				auto& l = mKeyVals[insertion_idx];
				if (idx == insertion_idx) {
					// put at empty spot. This forwards all arguments into the node where the object is
					// constructed exactly where it is needed.
					if constexpr (using_key_sview) {
						uint32_t key_pos = allocate_sview_id(key);
						::new (static_cast<void*>(&l))
							Node(*this, std::piecewise_construct,
									std::forward_as_tuple(key_pos), std::forward_as_tuple(val));
          }else if constexpr (using_val_sview) {
						uint32_t val_pos = allocate_sview_id(val);
						::new (static_cast<void*>(&l))
							Node(*this, std::piecewise_construct,
									std::forward_as_tuple(std::forward<Arg>(key)), std::forward_as_tuple(val_pos));
					}else{
						::new (static_cast<void*>(&l))
							Node(*this, std::piecewise_construct,
									std::forward_as_tuple(std::forward<Arg>(key)), std::forward_as_tuple(val));
					}
				} else {
          assert(!found);
					shiftUp(idx, insertion_idx);
					if constexpr (using_key_sview) {
						uint32_t key_pos = allocate_sview_id(key);
						l = Node(*this, std::piecewise_construct,
								std::forward_as_tuple(key_pos), std::forward_as_tuple(val));
          }else if constexpr (using_val_sview) {
						uint32_t val_pos = allocate_sview_id(val);
						l = Node(*this, std::piecewise_construct,
								std::forward_as_tuple(std::forward<Arg>(key)), std::forward_as_tuple(val_pos));
					}else{
						l = Node(*this, std::piecewise_construct,
								std::forward_as_tuple(std::forward<Arg>(key)), std::forward_as_tuple(val));
					}
				}

        if (!found) {
          // mKeyVals[idx].getFirst() = std::move(key);
          mInfo[insertion_idx] = static_cast<uint8_t>(insertion_info);

          ++(*mNumElements);
        }

				return iterator(mKeyVals + insertion_idx, mInfo + insertion_idx);
        //return;
			}
		}

#if 0
	// This is exactly the same code as operator[], except for the return values
	template <typename Arg>
		std::pair<iterator, bool> doInsert(Arg&& keyval) {
			while (true) {
				int idx;
				InfoType info;
				if constexpr (using_key_sview) {
					keyToIdx(get_sview(keyval.getFirst()), idx, info);
				}else{
					keyToIdx(keyval.getFirst(), idx, info);
				}
				nextWhileLess(&info, &idx);

				// while we potentially have a match
				while (info == mInfo[idx]) {
					if (equals(keyval.getFirst(), mKeyVals[idx].getFirst())) {
						// key already exists, do NOT insert.
						// see http://en.cppreference.com/w/cpp/container/map/insert
						return std::make_pair<iterator, bool>(iterator(mKeyVals + idx, mInfo + idx),
								false);
					}
          idx  = next_idx(idx);
          info = next_info(info);
				}

				// unlikely that this evaluates to true
				if (mmap_map_UNLIKELY(*mNumElements >= *mMaxNumElementsAllowed)) {
					increase_size();
					continue;
				}

				// key not found, so we are now exactly where we want to insert it.
				auto const insertion_idx = idx;
				auto const insertion_info = info;
				if (mmap_map_UNLIKELY(insertion_info + *mInfoInc > 0xFF)) {
					*mMaxNumElementsAllowed = 0;
				}

				// find an empty spot
				while (0 != mInfo[idx]) {
          idx  = next_idx(idx);
          info = next_info(info);
#ifndef NDEBUG
					conflicts++;
#endif
				}

				auto& l = mKeyVals[insertion_idx];
				if (idx == insertion_idx) {
					::new (static_cast<void*>(&l)) Node(*this, std::forward<Arg>(keyval));
				} else {
					shiftUp(idx, insertion_idx);
					l = Node(*this, std::forward<Arg>(keyval));
				}

				// put at empty spot
				mInfo[insertion_idx] = static_cast<uint8_t>(insertion_info);
#ifndef NDEBUG
				static int conta=0;
				if (((++conta)&0xFFFF)==0 && *mNumElements>100) {
					if (conflict_factor()>0.01) {
						std::cerr << "potential bad hash for mmap_name:" << mmap_name << ", try to debug it\n";
					}
				}
#endif

				++(*mNumElements);
				return std::make_pair(iterator(mKeyVals + insertion_idx, mInfo + insertion_idx), true);
			}
		}
#endif

	size_t calcMaxNumElementsAllowed(size_t maxElements) const {
		static constexpr size_t overflowLimit = (std::numeric_limits<size_t>::max)() / 100;
		static constexpr double factor = MaxLoadFactor100 / 100.0;

		// make sure we can't get an overflow; use floatingpoint arithmetic if necessary.
		if (maxElements > overflowLimit) {
			return static_cast<size_t>(static_cast<double>(maxElements) * factor);
		} else {
			return (maxElements * MaxLoadFactor100) / 100;
		}
	}

	bool try_increase_info() {
		mmap_map_LOG("mInfoInc=" << *mInfoInc << ", numElements=" << mNumElements
				<< ", maxNumElementsAllowed="
				<< calcMaxNumElementsAllowed(mMask + 1));
		if (*mInfoInc <= 2) {
			// need to be > 2 so that shift works (otherwise undefined behavior!)
			return false;
		}
		// we got space left, try to make info smaller
		*mInfoInc = static_cast<uint8_t>(*mInfoInc >> 1);

		// remove one bit of the hash, leaving more space for the distance info.
		// This is extremely fast because we can operate on 8 bytes at once.
		++(*mInfoHashShift);
		auto const data = reinterpret_cast<uint64_t*>(mInfo);
		auto const numEntries = (*mMask + 1) / 8;

		for (size_t i = 0; i < numEntries; ++i) {
			data[i] = (data[i] >> 1) & UINT64_C(0x7f7f7f7f7f7f7f7f);
		}
		*mMaxNumElementsAllowed = calcMaxNumElementsAllowed(*mMask + 1);
		return true;
	}

	void increase_size() {
		// nothing allocated yet? just allocate InitialNumElements
		if (0 == *mMask) {
			reload();
			return;
		}

		auto const maxNumElementsAllowed = calcMaxNumElementsAllowed(*mMask + 1);
		if (*mNumElements < maxNumElementsAllowed && try_increase_info()) {
			return;
		}

		mmap_map_LOG("mNumElements=" << *mNumElements << ", maxNumElementsAllowed="
				<< maxNumElementsAllowed << ", load="
				<< (static_cast<double>(*mNumElements) * 100.0 /
					(static_cast<double>(*mMask) + 1)));
		// it seems we have a really bad hash function! don't try to resize again
		assert(*mNumElements * 2 >= calcMaxNumElementsAllowed(*mMask + 1));

		rehash((*mMask + 1) * 2);
	}

	void destroy() {
    if (!loaded)
      return;

		// Destroy is very infrequent in LGraph, must GC everybody
		while(!gc_queue.empty()) {
			auto func = gc_queue.front();
			gc_queue.pop();
			func();
		}

    assert(!loaded);
	}

	// members are sorted so no padding occurs
	mutable Node *mKeyVals = nullptr;
	mutable uint8_t *mInfo = nullptr;
	mutable size_t *mNumElements;                                                   // 8 byte 24
	mutable size_t *mMask;                                                          // 8 byte 32
	mutable size_t *mMaxNumElementsAllowed;                                         // 8 byte 40
	mutable InfoType *mInfoInc;                                                     // 4 byte 44
	mutable InfoType *mInfoHashShift;                                               // 4 byte 48
	mutable bool loaded = false;
	std::string       mmap_name;
	mutable int       mmap_fd       = -1;
	mutable size_t    mmap_size     = 0;
	mutable uint64_t *mmap_base     = 0;
	mutable int       mmap_txt_fd   = -1;
	mutable size_t    mmap_txt_size = 0;
	mutable uint64_t *mmap_txt_base = 0;
	inline static std::queue<std::function<void(void)>> gc_queue;
#ifndef NDEBUG
	size_t conflicts = 0;
#endif
};

} // namespace detail

template <typename Key, typename T, typename Hash = hash<Key>, size_t MaxLoadFactor100 = 80>
using map = detail::map<MaxLoadFactor100, Key, T, Hash>;


} // namespace mmap_map


