//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "attribute.hpp"
#include "lgraph.hpp"

template<const char *Name, typename Base, typename Key, typename Data, Node_pin_mode Mode=Node_pin_mode::Both>
class Attribute_data {

HERE: Choose compact/compact_class if Hierarchy or no Huerarchy in template

  using Attr_data = Attr_data_raw<Key::Compact, Data>;
  using Attr_data = Attr_data_raw<Key::Compact_class, Data>;

  inline static std::vector<Attr_data *> table;
  inline static LGraph    *last_lg   = nullptr;
  inline static Attr_data *last_attr = nullptr;

  static std::string_view get_base() {
    if constexpr (std::is_same<Base, Node>::value) {
      return "lgraph_data_node";
    }else if constexpr (std::is_same<Base, Node_pin>::value) {
      return "lgraph_data_npin";
    }
    return "bogus";
  }

  static std::string get_filename(Lg_type_id lgid) {
    if constexpr (Mode == Node_pin_mode::Driver)
      return absl::StrCat(get_base(), std::to_string(lgid), Name, "_d");
    else if constexpr (Mode == Node_pin_mode::Sink)
      return absl::StrCat(get_base(), std::to_string(lgid), Name, "_s");
    else if constexpr (Mode == Node_pin_mode::Both)
      return absl::StrCat(get_base(), std::to_string(lgid), Name);
    I(false);
    return "lgraph_bogus";
  };

  static bool is_invalid(size_t pos) {
    return (table.size() <= pos) || table[pos] == nullptr;
  };

  static void setup_table(LGraph *lg) {
    last_lg   = lg;
    auto pos = lg->get_lgid().value;
    if (!is_invalid(pos)) {
      last_attr = table[pos];
      return;
    }

    if (pos>=table.size())
      table.resize(pos+1);
    I(table[pos] == 0);
    last_attr  = new Attr_data(lg->get_path(), get_filename(lg->get_lgid()));
    table[pos] = last_attr;
  };

  static_assert(std::is_same<Key, Node_pin::Compact>::value
      || std::is_same<Key, Node_pin::Compact_class>::value
      || std::is_same<Key, Node::Compact>::value
      || std::is_same<Key, Node::Compact_class>::value
      ,"Key should be Node_pin::Compact or Node_pin::Compact_class or Node::Compact or Node::Compact_class");

  static_assert(std::is_same<Base, Node_pin>::value
      || std::is_same<Base, Node>::value
      ,"Base should be Node or Node_pin");

  static_assert(
    (std::is_same<Base, Node_pin>::value == (std::is_same<Key, Node_pin::Compact>::value || std::is_same<Key, Node_pin::Compact_class>::value))
    || (std::is_same<Base, Node>::value == (std::is_same<Key, Node::Compact>::value || std::is_same<Key, Node::Compact_class>::value))
    ,"Base and Key should be from the same group. E.g: Node_pin and Node_pin::Compact");

public:
  static Attr_data *ref(const Base &obj) {
    if (unlikely(obj.get_top_lgraph()!=last_lg))
      setup_table(obj.get_top_lgraph());
    return last_attr;
  }
  static Attr_data *ref(const LGraph *lg) {
    if (unlikely(lg!=last_lg))
      setup_table(lg);
    return last_attr;
  }

  static void clear(LGraph *lg) {
    setup_table(lg);

    size_t pos = lg->get_lgid().value;
    table[pos]->clear();
    delete table[pos];
    table[pos] = nullptr;

    last_lg   = nullptr;
    last_attr = nullptr;
  }

};
