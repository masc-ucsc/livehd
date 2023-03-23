// See LICENSE.txt for details

#pragma once

#include <cassert>
#include <vector>
#include <array>

#include "iassert.hpp"

#include "graph_core_base.hpp"

class __attribute__((packed)) Graph_core_node {  // AKA pin or node entry
public:
  Graph_core_node() { clear(); }

  void clear() {
    entry_type = Entry_type::Node;
  }

  [[nodiscard]] uint8_t get_type() const { return type; }
  void set_type(uint8_t t) {
    I(entry_type == Entry_type::Node);
    type = t;
  }

  bool add_edge(uint32_t self_id, uint32_t other_id) {
    I(self_id!=other_id);
    if (n_sedges<sedge.size()) {
      int64_t s = other_id - self_id;
      bool fits = s>std::numeric_limits<int16_t>::min() && s<std::numeric_limits<int16_t>::max();
      if (fits) {
        for(auto &ent:sedge) {
          if (ent!=0)
            continue;
          ent = static_cast<int16_t>(s);
          ++n_sedges;
          return true;
        }
        I(false);
        return false;
      }
    }
    if (overflow_link|set_link)
      return false;
    ledge_or_overflow_or_set = other_id;
    return true;
  }

  bool del_edge(uint32_t self_id, uint32_t other_id) {
    I(self_id!=other_id);
    for(auto &ent:sedge) {
      if ((ent+self_id) != other_id)
        continue;

      --n_sedges;
      ent = 0;
      return true;
    }
    if (overflow_link|set_link)
      return false;
    if (ledge_or_overflow_or_set == other_id) {
      ledge_or_overflow_or_set = 0;
      return true;
    }

    return false;
  }

  void switch_to_overflow(uint32_t ov) {
    I(!overflow_link);
    I(!set_link);
    overflow_link = true;
    ledge_or_overflow_or_set = ov;
  }

  [[nodiscard]] size_t get_num_local_edges() const {
    auto total = n_sedges
      + ((set_link|overflow_link) ? 0 : (ledge_or_overflow_or_set?1:0));

    return total;
  }

  [[nodiscard]] bool has_edges() const { return (ledge_or_overflow_or_set|n_sedges)!=0; }

  void dump(uint32_t self_id) const;

  class iterator {
  public:
    using value_type      = uint32_t;
    using pointer         = const int16_t*;

    iterator(uint32_t sid, uint32_t x, pointer p, pointer e) : self_id(sid), xtra(x), ptr(p), end(e) {}

    [[nodiscard]] value_type operator*() const {
      if (xtra)
        return xtra;
      return self_id + *ptr;
    }

    iterator& operator++() {
      if (xtra) {
        xtra = 0;
        return *this;
      }
      do {
        ++ptr;
      } while (ptr != end && *ptr == 0);
      return *this;
    }

    [[nodiscard]] const iterator operator++(int) {
      iterator tmp(*this);
      operator++();
      return tmp;
    }

    [[nodiscard]] bool operator==(const iterator& rhs) const { return xtra == rhs.xtra && ptr == rhs.ptr; }
    [[nodiscard]] bool operator!=(const iterator& rhs) const { return !(*this == rhs); }

    [[nodiscard]] bool in_xtra() const { return xtra!=0; }
    [[nodiscard]] pointer get_ptr() { return ptr; }

  private:
    value_type self_id;
    value_type xtra;
    pointer ptr;
    pointer end;
  };

  iterator begin(uint32_t self_id) {
    uint32_t xtra_node = (set_link|overflow_link)? 0: ledge_or_overflow_or_set;

    iterator it(self_id, xtra_node, sedge.data(), sedge.data() + sedge.size());
    if (xtra_node == 0 && *it == 0) {
      ++it;
    }
    return it;
  }

  iterator end() { return {0, 0, sedge.data() + sedge.size(), sedge.data() + sedge.size()}; }

  [[nodiscard]] iterator begin(uint32_t self_id) const {
    uint32_t xtra_node = (set_link|overflow_link)? 0: ledge_or_overflow_or_set;

    iterator it(self_id, xtra_node, sedge.data(), sedge.data() + sedge.size());
    if (xtra_node == 0 && *it == 0) {
      ++it;
    }
    return it;
  }

  [[nodiscard]] iterator end() const { return {0,0, sedge.data() + sedge.size(), sedge.data() + sedge.size()}; }

  [[nodiscard]] iterator cbegin(uint32_t self_id) const { return begin(self_id); }

  [[nodiscard]] iterator cend() const { return end(); }

  void erase(iterator it) {
    if (it == end()) {
      return;
    }
    if (it.in_xtra()) {
      I(!set_link && !overflow_link);
      ledge_or_overflow_or_set = 0;
    }else{
      I(n_sedges);
      --n_sedges;

      auto pos = sedge.data()-it.get_ptr();
      I(pos>=0 && pos<sedge.size() && sedge[pos]!=0);
      sedge[sedge.data()-it.get_ptr()] = 0;
    }
  }
private:
  // Node (16 bytes)
  // Byte 0:1
  Entry_type  entry_type : 2;    // Free, Node, Pin, Overflow
  uint8_t  set_link      : 1;    // set link, ledge otherwise  not set (overflow_link should be false)
  uint8_t  overflow_link : 1;    // When set, ledge points to overflow
  uint8_t  n_sedges      : 2;
  uint16_t type:10;              // type in node
  // SEDGE: 2:7
  std::array<int16_t,3>  sedge;  // used if set_link?
  // next_pin 8:11
  uint32_t next_pin_ptr;         // next pointer (pin)
  // void *: Byte 12:15
  uint32_t ledge_or_overflow_or_set;  // ledge is overflow if overflow set
};

