
#include "attribute.hpp"
#include "lgraph.hpp"

// Example
//
// using wirename = Attribute_node_pin_sview<true,true,"wirename">;
//
// Wirename::set(pin,"foo")
// Wirename::get(pin)
// Wirename::has(pin)
// ...

template<const char *Name, bool Unique>
class Attribute_node_sview_type {

  using Attr_data = Attr_sview_raw<uint32_t, Unique>;

  inline static std::vector<Attr_data *> table;
  inline static LGraph    *last_lg   = nullptr;
  inline static Attr_data *last_attr = nullptr;

  static std::string get_filename(Lg_type_id lgid) {
    std::string long_name = absl::StrCat("lgraph_", std::to_string(lgid), "_node_sview_", Name, Unique?"_unique":"_dup");

    return long_name;
  };

  static bool is_invalid(size_t pos) {
    return (table.size() <= pos) || table[pos] == nullptr;
  };

public:
  static std::string_view set(const Node &node, std::string_view wname) {

    auto *lg   = node.get_lgraph();
    if (unlikely(lg!=last_lg)) {
      size_t pos = lg->get_lgid().value;
      if (is_invalid(pos)) {
        table.resize(pos+1);
        I(table[pos] == 0);
        table[pos] = new Attr_data(lg->get_path(), get_filename(lg->get_lgid()));
      }
      last_lg   = lg;
      last_attr = table[pos];
    }

    I(!last_attr->has(node.get_compact())); // Do not double insert (why???) waste or bug with Name alias!!

    return last_attr->set(node.get_compact(), wname);
  };

  static std::string_view get(const Node &node) {

    auto *lg   = node.get_lgraph();
    if (unlikely(lg!=last_lg)) {
      size_t pos = lg->get_lgid().value;
      if (is_invalid(pos))
        return "";
      last_lg   = lg;
      last_attr = table[pos];
    }

    return last_attr->get(node.get_compact());
  };

  static bool has(const Node &node) {

    auto *lg   = node.get_lgraph();
    if (unlikely(lg!=last_lg)) {
      size_t pos = lg->get_lgid().value;
      if (is_invalid(pos))
        return false;
      last_lg   = lg;
      last_attr = table[pos];
    }

    return last_attr->has(node.get_compact());
  };

  static Node find(LGraph *lg, std::string_view name) {

    if (unlikely(lg!=last_lg)) {
      size_t pos = lg->get_lgid().value;
      if (is_invalid(pos))
        return Node();
      last_attr = table[pos];
      last_lg   = lg;
    }

    auto raw = last_attr->find(name);
    if (raw==0) {
      return Node();
    }

    return Node(lg, 0, Node::Compact(raw));
  };

  static void sync() {
    last_lg   = nullptr;
    last_attr = nullptr;

    for(auto *ent:table) {
      if (ent == nullptr)
        continue;
      ent->sync();
    }
  };

  static void sync(LGraph *lg) {
    last_lg   = nullptr;
    last_attr = nullptr;

    size_t pos = lg->get_lgid().value;
    if (is_invalid(pos))
      return;

    table[pos]->sync();
  };

  static void clear(LGraph *lg) {
    last_lg   = nullptr;
    last_attr = nullptr;

    size_t pos = lg->get_lgid().value;
    if (is_invalid(pos))
      return;

    table[pos]->clear();
    delete table[pos];
    table[pos] = nullptr;
  };
};

