//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "lgraph.hpp"
#include "mmap_bimap.hpp"
#include "mmap_map.hpp"

template <const char *Name, typename Base, typename Attr_data>
class Attribute {
  inline static std::vector<Attr_data *> table;
  inline static const LGraph *           last_lg   = nullptr;
  inline static Attr_data *              last_attr = nullptr;

  static std::string_view get_base() {
    if constexpr (std::is_same<Base, Node>::value) {
      return "lg_data_node";
    } else if constexpr (std::is_same<Base, Node_pin>::value) {
      return "lg_data_npin";
    }
    return "bogus";
  }

  static std::string get_filename(Lg_type_id lgid) { return absl::StrCat(get_base(), std::to_string(lgid), Name); };

  static bool is_invalid(size_t pos) { return (table.size() <= pos) || table[pos] == nullptr; };

  static void setup_table(const LGraph *lg) {
    last_lg  = lg;
    auto pos = lg->get_lgid().value;
    if (!is_invalid(pos)) {
      last_attr = table[pos];
      return;
    }

    if (pos >= table.size())
      table.resize(pos + 1);
    I(table[pos] == 0);
    last_attr  = new Attr_data(lg->get_path(), get_filename(lg->get_lgid()));
    table[pos] = last_attr;
  };

  static_assert(std::is_same<Base, Node_pin>::value || std::is_same<Base, Node>::value, "Base should be Node or Node_pin");

public:
  static Attr_data *ref(const Base &obj) {
    if (unlikely(obj.get_top_lgraph() != last_lg))
      setup_table(obj.get_top_lgraph());
    return last_attr;
  }
  static Attr_data *ref(const LGraph *lg) {
    if (unlikely(lg != last_lg))
      setup_table(lg);
    return last_attr;
  }

  static void clear(const LGraph *lg) {
    setup_table(lg);

    size_t pos = lg->get_lgid().value;
    table[pos]->clear();
    delete table[pos];
    table[pos] = nullptr;

    last_lg   = nullptr;
    last_attr = nullptr;
  }
};
