#include "attribute.hpp"
#include "lgraph.hpp"

template<const char *Name, typename Data>
class Attribute_node_data_type {

  using Attr_data = Attr_data_raw<Node::Compact, Data>;

  inline static std::vector<Attr_data *> table;
  inline static LGraph    *last_lg   = nullptr;
  inline static Attr_data *last_attr = nullptr;

  static std::string get_filename(Lg_type_id lgid) {
    return absl::StrCat(std::to_string(lgid), "_node_data_", Name);
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

public:
  static void set(const Node &node, Data data) {

    if (unlikely(node.get_top_lgraph()!=last_lg))
      setup_table(node.get_top_lgraph());

    I(!last_attr->has(node.get_compact())); // Do not double insert (why???) waste or bug with Name alias!!

    return last_attr->set(node.get_compact(), data);
  };

  static const Data &get(const Node &node) {

    if (unlikely(node.get_top_lgraph()!=last_lg))
      setup_table(node.get_top_lgraph());

    return last_attr->get(node.get_compact());
  };

  static Data &at(const Node &node) {

    if (unlikely(node.get_top_lgraph()!=last_lg))
      setup_table(node.get_top_lgraph());

    return last_attr->at(node.get_compact());
  };

  static bool has(const Node &node) {

    if (unlikely(node.get_top_lgraph()!=last_lg))
      setup_table(node.get_top_lgraph());

    return last_attr->has(node.get_compact());
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
    setup_table(lg);

    size_t pos = lg->get_lgid().value;
    table[pos]->clear();
    delete table[pos];
    table[pos] = nullptr;

    last_lg   = nullptr;
    last_attr = nullptr;
  };
};
