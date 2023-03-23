// See LICENSE.txt for details

#pragma once

#include <cassert>
#include <vector>
#include <array>

#include "iassert.hpp"

#include "graph_core_base.hpp"

class __attribute__((packed)) Graph_core_pin {  // AKA pin or node entry
public:
  Graph_core_pin() { clear(); }

  void clear() {
    bzero(this, sizeof(Graph_core_pin));  // set zero everything
    entry_type = Entry_type::Pin;
  }

  [[nodiscard]] uint16_t get_pid() const { return pid; }
  void set_pid(uint16_t p) {
    I(entry_type == Entry_type::Pin);
    pid = p;
  }

  bool add_edge(uint32_t self_id, uint32_t other_id) {
    I(self_id!=other_id);
    if (sedge_0 == 0) {
      int64_t s = other_id - self_id;
      bool fits = s>std::numeric_limits<int16_t>::min() && s<std::numeric_limits<int16_t>::max();
      if (fits) {
        sedge_0 = static_cast<int16_t>(s);
        return true;
      }
    }
    if (overflow_link|set_link)
      return false;
    ledge_or_overflow_or_set = other_id;
    return true;
  }

  bool del_edge(uint32_t self_id, uint32_t other_id) {
    I(self_id!=other_id);
    if ((sedge_0+self_id) == other_id) {
      sedge_0 = 0;
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
    auto total = (sedge_0?1:0)
      + ((set_link|overflow_link) ? 0 : (ledge_or_overflow_or_set?1:0));

    return total;
  }

  [[nodiscard]] bool has_edges() const { return (ledge_or_overflow_or_set|sedge_0)!=0; }

  void dump(uint32_t self_id) const;

  class iterator {
  public:
    using value_type      = uint32_t;
    iterator(uint32_t x1, uint32_t x2) : data1(x1), data2(x2) {}

    [[nodiscard]] value_type operator*() const {
      if (data1)
        return data1;
      return data2;
    }

    iterator& operator++() {
      if (data1) {
        data1 = 0;
      }else if (data2) {
        data2 = 0;
      }
      return *this;
    }

    [[nodiscard]] const iterator operator++(int) {
      iterator tmp(*this);
      operator++();
      return tmp;
    }

    [[nodiscard]] bool operator==(const iterator& rhs) const { return data1 == rhs.data1 && data2 == rhs.data2; }
    [[nodiscard]] bool operator!=(const iterator& rhs) const { return !(*this == rhs); }

    [[nodiscard]] bool in_data1() const { return data1!=0; }
    [[nodiscard]] bool in_data2() const { return data2!=0; }

  private:
    value_type data1;
    value_type data2;
  };

  iterator begin(uint32_t self_id) {
    uint32_t data1 = (sedge_0? (sedge_0+self_id):0);
    uint32_t data2 = (set_link|overflow_link)? 0: ledge_or_overflow_or_set;

    return {data1, data2};
  }

  iterator end() { return {0, 0}; }

  [[nodiscard]] iterator begin(uint32_t self_id) const {
    uint32_t data1 = (sedge_0? (sedge_0+self_id):0);
    uint32_t data2 = (set_link|overflow_link)? 0: ledge_or_overflow_or_set;

    return {data1, data2};
  }

  [[nodiscard]] iterator end() const { return {0,0}; }
  [[nodiscard]] iterator cbegin(uint32_t self_id) const { return begin(self_id); }
  [[nodiscard]] iterator cend() const { return end(); }

  void erase(iterator it) {
    if (it == end()) {
      return;
    }
    if (it.in_data1()) {
      sedge_0 = 0;
    }else{
      I(it.in_data2());
      I(!set_link && !overflow_link);
      ledge_or_overflow_or_set = 0;
    }
  }

  uint32_t get_node_id() const { return node_id; }
private:
  // Pin  (16 bytes)
  // Byte 0:1
  Entry_type  entry_type : 2;    // Free, Node, Pin, Overflow
  uint8_t  set_link      : 1;    // set link, ledge otherwise  not set (overflow_link should be false)
  uint8_t  overflow_link : 1;    // When set, ledge points to overflow
  uint32_t pid:12;               // pid in node
  // SEDGE: 2:3
  int16_t  sedge_0;              // used if set_link?
  // node_pin 4:7
  uint32_t node_id;              // points to master node
  // next_pin 8:11
  uint32_t next_id;               // next pointer (pin)
  // void *: Byte 12:15
  uint32_t ledge_or_overflow_or_set;  // ledge is overflow if overflow set
};

