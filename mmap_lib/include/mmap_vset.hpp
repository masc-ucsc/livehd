// Visitor set to LiveHD
// CSE220 Project

#pragma once

#include <string_view>

#include "mmap_map.hpp"

namespace mmap_lib {

// vset will have a Key to identify each BitMap
// data will be the BitMap (various numbers of different bits)

template <typename Key, typename T>
class vset {
public:
  using VisitorSet = typename mmap_lib::map<Key, T>;
  VisitorSet   visitor_set;
  T            max_inserted = 0;
  const size_t bucket_len   = sizeof(T) * 8;

  using iterator       = typename VisitorSet::iterator;
  using const_iterator = typename VisitorSet::const_iterator;

  // What does explicit do?
  explicit vset(std::string_view _set_name) : visitor_set(std::string(_set_name) + "_vs") {}
  explicit vset(std::string_view _path, std::string_view _set_name) : visitor_set(_path, std::string(_set_name) + "_vs") {}

  // Clears the whole data structures
  void clear() { visitor_set.clear(); }

  //====
  void   wht() { std::cout << sizeof(T) << std::endl; }
  size_t bucket_size() { return bucket_len; }

  size_t num_buckets(size_t ln) { return (ln / (sizeof(T) * 8)); }
  //====

  /* All the buckst_set() functions all returns const_iterator
   * These functions set the ENTIRE bitmap at the key inputted
   * If I want to use these, I need to change bits outside of the set()
   */
  const_iterator bucket_set(Key &&key, T &&bitmap) { return visitor_set.set(key, bitmap); }
  const_iterator bucket_set(const Key &key, T &&bitmap) { return visitor_set.set(key, bitmap); }
  const_iterator bucket_set(Key &&key, const T &bitmap) { return visitor_set.set(key, bitmap); }
  const_iterator bucket_set(const Key &key, const T &bitmap) { return visitor_set.set(key, bitmap); }

  // returns true/false depending on if the key exists
  [[nodiscard]] bool has_key(const Key &key) const { return visitor_set.has(key); }

  [[nodiscard]] Key get_key(const iterator &it) const { return visitor_set.get_key(it); }
  [[nodiscard]] Key get_key(const const_iterator &it) const { return visitor_set.get_key(it); }

  /* All the bucket_get() functions, returns whatever the whole 64 bit number
   * Why the need for a template here?
   * Need to access single bits of the bitmap outside of the functions
   */

  template <typename T_ = T, typename = std::enable_if_t<!is_array_serializable<T_>::value>>
  [[nodiscard]] const T &bucket_get_val(const Key &key) const {
    return visitor_set.get(key);
  }

  template <typename T_ = T, typename = std::enable_if_t<is_array_serializable<T_>::value>>
  [[nodiscard]] T bucket_get_val(const Key &key) const {
    return visitor_set.get(key);
  }

  template <typename T_ = T, typename = std::enable_if_t<!is_array_serializable<T_>::value>>
  [[nodiscard]] const T &bucket_get_val(const iterator &it) const {
    return visitor_set.get(it);
  }

  template <typename T_ = T, typename = std::enable_if_t<!is_array_serializable<T_>::value>>
  [[nodiscard]] const T &bucket_get_val(const const_iterator &it) const {
    return visitor_set.get(it);
  }

  template <typename T_ = T, typename = std::enable_if_t<is_array_serializable<T_>::value>>
  [[nodiscard]] T bucket_get_val(const iterator &it) const {
    return visitor_set.get(it);
  }

  template <typename T_ = T, typename = std::enable_if_t<is_array_serializable<T_>::value>>
  [[nodiscard]] T bucket_get_val(const const_iterator &it) const {
    return visitor_set.get(it);
  }

  [[nodiscard]] iterator       bucket_find(const Key &key) { return visitor_set.find(key); }
  [[nodiscard]] const_iterator bucket_find(const Key &key) const { return visitor_set.find(key); }

  // Functions used for iterating, begin() and end()
  [[nodiscard]] iterator       bucket_begin() { return visitor_set.begin(); }
  [[nodiscard]] const_iterator bucket_begin() const { return visitor_set.cbegin(); }
  [[nodiscard]] const_iterator bucket_cbegin() const { return visitor_set.cbegin(); }

  [[nodiscard]] iterator       bucket_end() { return visitor_set.end(); }
  [[nodiscard]] const_iterator bucket_end() const { return visitor_set.cend(); }
  [[nodiscard]] const_iterator bucket_cend() const { return visitor_set.cend(); }

  /* These functions erase the WHOLE bitmap,
   * Erases element at pos, returns iterator to next element
   */
  iterator bucket_erase(const_iterator pos) { return visitor_set.erase(pos); }
  iterator bucket_erase(iterator pos) { return visitor_set.erase(pos); }

  // erases the key if it's there, if not, returns 0
  size_t bucket_erase_key(const Key &key) {
    auto it = visitor_set.find(key);
    if (it == visitor_set.end()) {
      return 0;
    }
    erase(it);
    return 1;
  }

  // Makes space inside the map to accommodate for whatever size s is
  void reserve(size_t s) { visitor_set.reserve(s); }

  [[nodiscard]] size_t size() const { return visitor_set.size(); }
  [[nodiscard]] bool   empty() const { return visitor_set.empty(); }
  [[nodiscard]] size_t capacity() const { return visitor_set.capacity(); }

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  /* Can add functions here to set a bit specifically in the bitmap at key
   * call them bit_set() x 4, all return const_iterator too
   *
   * Can also add functions here to un-set bits instead of deleting entire bitmap
   * Kind of like another variation of set but set it to 0 instead of 1
   *
   * ex) func -->
   * const_iterator bit_set(Key &&key, T &&bitmap, someTypeDependsOnBitMap bit_pos) {
   *   const auto t = visitor_set.get(key)
   *   [change bitmap at index bit_pos]
   *   return visitor_set.set(key, bitmap)
   * }
   */

  /* All the insert() functions are void
   * The funcs intake a number you wish to insert into the set
   *
   * All the erase() functions are void too
   * The funcs intake a number you wish to remove from the set
   */
  [[nodiscard]] void insert(T &&ele) {
    if (ele > max_inserted) {
      max_inserted = ele;
    }

    // find correct index the pos is at
    const auto p    = ele / (sizeof(T) * 8);  // p will be the key that points to the correct bitmap
    const auto i    = ele % (sizeof(T) * 8);  // i will be the bit we are interested in in the bitmap
    T          hold = 0;                      // will hold the bitmap at index p if there is one
    if (visitor_set.has((Key)p)) {
      hold = visitor_set.get((Key)p);
    }                                  // is there a bitmap at key p
    hold = hold | (1 << i);            // modify the bit at pos
    visitor_set.set((Key)p, (T)hold);  // put it back in the bitmap
  }

  [[nodiscard]] void insert(const T &&ele) {
    if (ele > max_inserted) {
      max_inserted = ele;
    }

    // find correct index the pos is at
    const auto p    = ele / (sizeof(T) * 8);  // p will be the key for the bitmap
    const auto i    = ele % (sizeof(T) * 8);  // i will be the bit we are interested in
    T          hold = 0;                      // will hold the bitmap at index p if there is one
    if (visitor_set.has((Key)p)) {
      hold = visitor_set.get((Key)p);
    }                                  // is there a bitmap at key p
    hold = hold | (1 << i);            // modify the bit at pos
    visitor_set.set((Key)p, (T)hold);  // put it back in the bitmap
  }

  [[nodiscard]] void erase(T &&ele) {
    // find correct index the pos is at
    Key     p    = ele / (sizeof(T) * 8);  // p will be the key that points to the correct bitmap
    uint8_t i    = ele % (sizeof(T) * 8);  // i will be the bit we are interested in in the bitmap
    T       hold = 0;                      // will hold the bitmap at index p if there is one

    if (visitor_set.has((Key)p)) {
      hold = visitor_set.get((Key)p);
      hold = hold & ~(1 << i);           // modify the bit at i
      visitor_set.set((Key)p, (T)hold);  // put it back in the bitmap
      p--;
    }

    // 1) detect if max needs to be updated
    if (ele == max_inserted) {
      // std::cout << "Time to update max_inserted, ele = " << ele << "\n";
      // std::cout << "Initial hold = " << hold << "\n";

      --max_inserted;
      T mask = 1 << (i - 1);  // used to detect if bit is 1 or 0
      // std::cout << "Initial mask = " << mask << "\n";

      // mask will do this:
      // 100000000...
      // 010000000...
      // 001000000...
      // 000100000...
      // 000010000...
      // to check for bits starting from MSB

      // std::cout << "Loop Start\n\n";
      for (; mask >= 0; mask = mask / 2) {
        // std::cout << "mask is: " << mask << "\n";
        if (mask == 0) {       // hit the end of the bitmap
          if (p == 0) {        // if this is the last index of map, no more elements left
            max_inserted = 0;  // max insert is 0
            // std::cout << "reset?\n";
            break;  // ready to remove
          }
          hold = visitor_set.get((Key)(p--));  // get next bitmap
          // std::cout << "reload hold: " << hold << "\n";
          mask = 1 << ((sizeof(T) * 8) - 1);  // reset mask
          // std::cout << "reload mask: " << mask << "\n";
          max_inserted = max_inserted - 1;
        }
        if (hold & mask) {  // if number detected by mask is there
          // std::cout << "exit here?\n";
          break;  // ready to remove
        } else {  // if mask number is not there, add to decrement
          max_inserted = max_inserted - 1;
        }
      }
    }
  }

  [[nodiscard]] void erase(const T &&ele) {
    // find correct index the pos is at
    Key     p    = ele / (sizeof(T) * 8);  // p will be the key that points to the correct bitmap
    uint8_t i    = ele % (sizeof(T) * 8);  // i will be the bit we are interested in in the bitmap
    T       hold = 0;                      // will hold the bitmap at index p if there is one

    if (visitor_set.has((Key)p)) {
      hold = visitor_set.get((Key)p);
      hold = hold & ~(1 << i);           // modify the bit at i
      visitor_set.set((Key)p, (T)hold);  // put it back in the bitmap
      p--;
    }

    // 1) detect if max needs to be updated
    if (ele == max_inserted) {
      // std::cout << "Time to update max_inserted, ele = " << ele << "\n";
      // std::cout << "Initial hold = " << hold << "\n";

      --max_inserted;
      T mask = 1 << (i - 1);  // used to detect if bit is 1 or 0
      // std::cout << "Initial mask = " << mask << "\n";

      // mask will do this:
      // 100000000...
      // 010000000...
      // 001000000...
      // 000100000...
      // 000010000...
      // to check for bits starting from MSB

      // std::cout << "Loop Start\n\n";
      for (; mask >= 0; mask = mask / 2) {
        // std::cout << "mask is: " << mask << "\n";
        if (mask == 0) {       // hit the end of the bitmap
          if (p == 0) {        // if this is the last index of map, no more elements left
            max_inserted = 0;  // max insert is 0
            // std::cout << "reset?\n";
            break;  // ready to remove
          }
          hold = visitor_set.get((Key)(p--));  // get next bitmap
          // std::cout << "reload hold: " << hold << "\n";
          mask = 1 << ((sizeof(T) * 8) - 1);  // reset mask
          // std::cout << "reload mask: " << mask << "\n";
          max_inserted = max_inserted - 1;
        }
        if (hold & mask) {  // if number detected by mask is there
          // std::cout << "exit here?\n";
          break;  // ready to remove
        } else {  // if mask number is not there, add to decrement
          max_inserted = max_inserted - 1;
        }
      }
    }
  }

  /* Can add functions here to get a single bit of the bitmap
   * size_t find(const T &&ele) const {
   *   const auto t = visitor_set.get(key);
   *   [find the correct bit somehow]
   *   return true if 1, false otherwise
   * }
   */

  [[nodiscard]] bool find(T &&ele) {
    // finding the correct bitmap to get
    const auto p = ele / (sizeof(T) * 8);
    const auto i = ele % (sizeof(T) * 8);

    // if this map is there and the bit we want is set high, return true
    if (visitor_set.has((Key)p) && (visitor_set.get((Key)p) >> i & 1)) {
      return true;
    } else {
      return false;  // otherwise it's not there
    }
  }

  [[nodiscard]] bool find(const T &&ele) {
    const auto p = ele / (sizeof(T) * 8);
    const auto i = ele % (sizeof(T) * 8);

    if (visitor_set.has((Key)p) && (visitor_set.get((Key)p) >> i & 1)) {
      return true;
    } else {
      return false;
    }
  }

  // Functions used for iterating, begin() and end()
  [[nodiscard]] bool is_start(T &&ele) {
    if (ele == 0) {
      return true;
    }
    return false;
  }

  [[nodiscard]] bool is_start(const T &&ele) {
    if (ele == 0) {
      return true;
    }
    return false;
  }

  [[nodiscard]] bool is_end(T &&ele) {
    if (ele == (T)max_inserted) {
      return true;
    }
    return false;
  }

  [[nodiscard]] bool is_end(const T &&ele) {
    if (ele == (T)max_inserted) {
      return true;
    }
    return false;
  }

  [[nodiscard]] uint64_t get_max() { return max_inserted; }
};

}  // namespace mmap_lib
