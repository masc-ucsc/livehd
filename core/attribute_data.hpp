#include "attribute.hpp"
#include "lgraph.hpp"

template<const char *Name, typename Base, typename Key, typename Data, Node_pin_mode Mode=Node_pin_mode::Both>
class Attribute_data {

  using Attr_data = Attr_data_raw<Key, Data>;

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
    size_t pos = lg->get_lgid().value;
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
  static void set(const Base &obj, Data data) {

    if (unlikely(obj.get_top_lgraph()!=last_lg))
      setup_table(obj.get_top_lgraph());

    if constexpr (std::is_same<Key, Node_pin::Compact>::value) {
      GI(Mode == Node_pin_mode::Driver, obj.is_driver());
      GI(Mode == Node_pin_mode::Sink  , obj.is_sink());
      I(!last_attr->has(obj.get_compact(Mode)));
      last_attr->set(obj.get_compact(Mode), data);
    }else if constexpr (std::is_same<Key, Node_pin::Compact_class>::value) {
      GI(Mode == Node_pin_mode::Driver, obj.is_driver());
      GI(Mode == Node_pin_mode::Sink  , obj.is_sink());
      I(!last_attr->has(obj.get_compact_class(Mode)));
      last_attr->set(obj.get_compact_class(Mode), data);
    }else if constexpr (std::is_same<Key, Node::Compact>::value) {
      I(!last_attr->has(obj.get_compact()));
      last_attr->set(obj.get_compact(), data);
    }else if constexpr (std::is_same<Key, Node::Compact_class>::value) {
      I(!last_attr->has(obj.get_compact_class()));
      last_attr->set(obj.get_compact_class(), data);
    }
  };

  static const Data &get(const Base &obj) {
    if (unlikely(obj.get_top_lgraph()!=last_lg))
      setup_table(obj.get_top_lgraph());

    if constexpr (std::is_same<Key, Node_pin::Compact>::value) {
      GI(Mode == Node_pin_mode::Driver, obj.is_driver());
      GI(Mode == Node_pin_mode::Sink  , obj.is_sink());
      return last_attr->get(obj.get_compact(Mode));
    }else if constexpr (std::is_same<Key, Node_pin::Compact_class>::value) {
      GI(Mode == Node_pin_mode::Driver, obj.is_driver());
      GI(Mode == Node_pin_mode::Sink  , obj.is_sink());
      return last_attr->get(obj.get_compact_class(Mode));
    }else if constexpr (std::is_same<Key, Node::Compact>::value) {
      return last_attr->get(obj.get_compact());
    }else if constexpr (std::is_same<Key, Node::Compact_class>::value) {
      return last_attr->get(obj.get_compact_class());
    }
  }

  static Data &at(const Base &obj) {
    if (unlikely(obj.get_top_lgraph()!=last_lg))
      setup_table(obj.get_top_lgraph());

    if constexpr (std::is_same<Key, Node_pin::Compact>::value) {
      GI(Mode == Node_pin_mode::Driver, obj.is_driver());
      GI(Mode == Node_pin_mode::Sink  , obj.is_sink());
      return last_attr->at(obj.get_compact(Mode));
    }else if constexpr (std::is_same<Key, Node_pin::Compact_class>::value) {
      GI(Mode == Node_pin_mode::Driver, obj.is_driver());
      GI(Mode == Node_pin_mode::Sink  , obj.is_sink());
      return last_attr->at(obj.get_compact_class(Mode));
    }else if constexpr (std::is_same<Key, Node::Compact>::value) {
      return last_attr->at(obj.get_compact());
    }else if constexpr (std::is_same<Key, Node::Compact_class>::value) {
      return last_attr->at(obj.get_compact_class());
    }
  }

  static bool has(const Base &obj) {
    if (unlikely(obj.get_top_lgraph()!=last_lg))
      setup_table(obj.get_top_lgraph());

    if constexpr (std::is_same<Key, Node_pin::Compact>::value) {
      GI(Mode == Node_pin_mode::Driver, obj.is_driver());
      GI(Mode == Node_pin_mode::Sink  , obj.is_sink());
      return last_attr->has(obj.get_compact(Mode));
    }else if constexpr (std::is_same<Key, Node_pin::Compact_class>::value) {
      GI(Mode == Node_pin_mode::Driver, obj.is_driver());
      GI(Mode == Node_pin_mode::Sink  , obj.is_sink());
      return last_attr->has(obj.get_compact_class(Mode));
    }else if constexpr (std::is_same<Key, Node::Compact>::value) {
      return last_attr->has(obj.get_compact());
    }else if constexpr (std::is_same<Key, Node::Compact_class>::value) {
      return last_attr->has(obj.get_compact_class());
    }
  }

  static void sync() {
    for(auto *ent:table) {
      if (ent == nullptr)
        continue;
      ent->sync();
    }
  };

  static void sync(LGraph *lg) {
    size_t pos = lg->get_lgid().value;
    if (is_invalid(pos))
      return;

    table[pos]->sync();
  };

  static void clear(LGraph *lg) {
    setup_table(lg);

    size_t pos = lg->get_lgid().value;
    table[pos]->clear();
    delete table[pos];
    table[pos] = nullptr;

    last_lg   = nullptr;
    last_attr = nullptr;
  };
};
