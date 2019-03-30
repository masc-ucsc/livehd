
#include "attribute.hpp"
#include "lgraph.hpp"

template<const char *Name, typename Data>
class Attribute_node_data_type {

  using Attr_data = Attr_data_raw<uint32_t, Data>;

  inline static std::vector<Attr_data *> table;

  static std::string get_filename(Lg_type_id lgid) {
    return absl::StrCat(std::to_string(lgid), "_node_data_", Name);
  };

  static bool is_invalid(size_t pos) {
    return (table.size() <= pos) || table[pos] == nullptr;
  };

public:
  static void set(const Node &node, Data data) {

    auto *lg   = node.get_lgraph();
    size_t pos = lg->get_lgid().value;

    if (is_invalid(pos)) {
      table.resize(pos+1);
      I(table[pos] == nullptr);
      table[pos] = new Attr_data(lg->get_path(), get_filename(lg->get_lgid()));
    }

    I(!table[pos]->has(node.get_compact())); // Do not double insert (why???) waste or bug with Name alias!!

    table[pos]->set(node.get_compact(), data);
  };

  static const Data &get(const Node &node) {

    auto *lg   = node.get_lgraph();
    size_t pos = lg->get_lgid().value;
    if (is_invalid(pos)) {
      if constexpr (std::is_arithmetic<Data>::value) {
        static constexpr Data data=0;
        return data;
      }else{
        static constexpr Data data;
        return data;
      }
    }

    return table[pos]->get(node.get_compact());
  };

  static Data &at(const Node &node) {

    auto *lg   = node.get_lgraph();
    size_t pos = lg->get_lgid().value;
    if (is_invalid(pos)) {
      if constexpr (std::is_arithmetic<Data>::value) {
        static Data data=0;
        return data;
      }else{
        static Data data;
        return data;
      }
    }

    return table[pos]->at(node.get_compact());
  };

  static bool has(const Node &node) {

    auto *lg   = node.get_lgraph();
    size_t pos = lg->get_lgid().value;
    if (is_invalid(pos))
      return false;

    return table[pos]->has(node.get_compact());
  };

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
    size_t pos = lg->get_lgid().value;
    if (is_invalid(pos))
      return;

    table[pos]->clear();
    delete table[pos];
    table[pos] = nullptr;
  };
};

