
#include "attribute.hpp"
#include "lgraph.hpp"

template<const char *Name, Node_pin_mode Mode, typename Data>
class Attribute_node_pin_data_type {

  using Attr_data = Attr_data_raw<uint32_t, Data>;

  inline static std::vector<Attr_data *> table;
  inline static LGraph    *last_lg   = nullptr;
  inline static Attr_data *last_attr = nullptr;

  static std::string get_filename(Lg_type_id lgid) {
    if constexpr (Mode == Node_pin_mode::Driver)
      return absl::StrCat(std::to_string(lgid), "_node_pin_data", Name, "_driver");
    else if constexpr (Mode == Node_pin_mode::Sink)
      return absl::StrCat(std::to_string(lgid), "_node_pin_data", Name, "_sink");
    else if constexpr (Mode == Node_pin_mode::Both)
      return absl::StrCat(std::to_string(lgid), "_node_pin_data", Name, "_both");
    I(false);
    return "bogus";
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
  static void set(const Node_pin &pin, Data data) {
    GI(Mode == Node_pin_mode::Driver, pin.is_driver());
    GI(Mode == Node_pin_mode::Sink  , pin.is_sink());

    if (unlikely(pin.get_lgraph()!=last_lg))
      setup_table(pin.get_lgraph());

    I(!last_attr->has(pin.get_compact(Mode))); // Do not double insert (why???) waste or bug with Name alias!!

    return last_attr->set(pin.get_compact(Mode), data);
  };

  static const Data &get(const Node_pin &pin) {
    GI(Mode == Node_pin_mode::Driver, pin.is_driver());
    GI(Mode == Node_pin_mode::Sink  , pin.is_sink());

    if (unlikely(pin.get_lgraph()!=last_lg))
      setup_table(pin.get_lgraph());

    return last_attr->get(pin.get_compact(Mode));
  };

  static Data &at(const Node_pin &pin) {
    GI(Mode == Node_pin_mode::Driver, pin.is_driver());
    GI(Mode == Node_pin_mode::Sink  , pin.is_sink());

    if (unlikely(pin.get_lgraph()!=last_lg))
      setup_table(pin.get_lgraph());

    return last_attr->at(pin.get_compact(Mode));
  };

  static bool has(const Node_pin &pin) {
    GI(Mode == Node_pin_mode::Driver, pin.is_driver());
    GI(Mode == Node_pin_mode::Sink  , pin.is_sink());

    if (unlikely(pin.get_lgraph()!=last_lg))
      setup_table(pin.get_lgraph());

    return last_attr->has(pin.get_compact(Mode));
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

