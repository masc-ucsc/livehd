//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <atomic>
#include <mutex>

#include "absl/container/flat_hash_map.h"

#include "lgraph.hpp"
#include "mmap_bimap.hpp"
#include "mmap_map.hpp"
#include "mmap_str.hpp"

template <const char *Name, typename Base, typename Attr_data>
class Attribute {
  inline static std::mutex  lgs_mutex;

  inline static absl::flat_hash_map<std::pair<mmap_lib::str, mmap_lib::str>, Attr_data *> lg2attr;

  inline static thread_local const Lgraph *last_lg   = nullptr;
  inline static thread_local Attr_data *   last_attr = nullptr;

  static std::string_view get_base() {
    if constexpr (std::is_same<Base, Node>::value) {
      return "_node_";
    } else if constexpr (std::is_same<Base, Node_pin>::value) {
      return "_npin_";
    }
    return "bogus";
  }

  static std::string get_filename(Lg_type_id lgid) { return absl::StrCat("lg_", std::to_string(lgid), get_base(), Name); };

  static void setup_table(const Lgraph *lg) {
    last_lg = lg;

    const auto key = std::pair(lg->get_unique_name(), mmap_lib::str(Name));

    std::lock_guard<std::mutex> guard(lgs_mutex);

    auto it = lg2attr.find(key);
    if (likely(it != lg2attr.end())) {
      last_attr = it->second;
    } else {
      last_attr    = new Attr_data(lg->get_path().to_s(), get_filename(lg->get_lgid()));
      lg2attr[key] = last_attr;
    }
  };

  static_assert(std::is_same<Base, Node_pin>::value || std::is_same<Base, Node>::value, "Base should be Node or Node_pin");

public:
  static Attr_data *ref(const Base &obj) {
    if (unlikely(obj.get_top_lgraph() != last_lg))
      setup_table(obj.get_top_lgraph());
    return last_attr;
  }
  static Attr_data *ref(const Lgraph *lg) {
    if (unlikely(lg != last_lg))
      setup_table(lg);
    return last_attr;
  }

  static void clear(const Lgraph *lg) {
    setup_table(lg);

    I(last_lg == lg);  // setup table forces this

    {
      std::lock_guard<std::mutex> guard(lgs_mutex);

      const auto key = std::pair(lg->get_unique_name(), mmap_lib::str(Name));
      I(lg2attr[key] == last_attr);
      lg2attr.erase(key);
    }

    last_attr->clear();
    delete last_attr;  // Delete does not clear

    last_lg   = nullptr;
    last_attr = nullptr;
  }

  static void sync(const Lgraph *lg) {
    if (last_lg == lg) {
      last_lg   = nullptr;
      last_attr = nullptr;
    }
    return;  // FIXME: Why does this create a deadlock/livelock with OPT only??? (not needed beyond asan, but it should not happen)

    const auto key = std::pair(lg->get_unique_name(), mmap_lib::str(Name));

    std::lock_guard<std::mutex> guard(lgs_mutex);

    auto it = lg2attr.find(key);
    if (it == lg2attr.end())
      return;
    delete it->second;
    lg2attr.erase(it);
  }
};
