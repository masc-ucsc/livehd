
#include "attribute.hpp"
#include "lgraph.hpp"

template<const char *Name, Node_pin_mode Mode, bool Unique>
class Attribute_node_pin_sview_type {

  using Attr_data = Attr_sview_raw<uint32_t, Unique>;

  inline static std::vector<Attr_data *> table;
  inline static LGraph    *last_lg   = nullptr;
  inline static Attr_data *last_attr = nullptr;

  static std::string get_filename(Lg_type_id lgid) {
    if constexpr (Mode == Node_pin_mode::Driver)
      return absl::StrCat("lgraph_", std::to_string(lgid), "_node_pin_sview", Name, "_driver", Unique?"_unique":"_dup");
    else if constexpr (Mode == Node_pin_mode::Sink)
      return absl::StrCat("lgraph_", std::to_string(lgid), "_node_pin_sview", Name, "_sink", Unique?"_unique":"_dup");
    else if constexpr (Mode == Node_pin_mode::Both)
      return absl::StrCat("lgraph_", std::to_string(lgid), "_node_pin_sview", Name, "_both", Unique?"_unique":"_dup");
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

    table.resize(pos+1);
    I(table[pos] == 0);
    last_attr  = new Attr_data(lg->get_path(), get_filename(lg->get_lgid()));
    table[pos] = last_attr;
  };

public:
  static std::string_view set(const Node_pin &pin, std::string_view data) {
    GI(Mode == Node_pin_mode::Driver, pin.is_driver());
    GI(Mode == Node_pin_mode::Sink  , pin.is_sink());

    if (unlikely(pin.get_lgraph()!=last_lg))
      setup_table(pin.get_lgraph());

    I(!last_attr->has(pin.get_compact(Mode))); // Do not double insert (why???) waste or bug with Name alias!!

    return last_attr->set(pin.get_compact(Mode), data);
  };

  static std::string_view get(const Node_pin &pin) {
    GI(Mode == Node_pin_mode::Driver, pin.is_driver());
    GI(Mode == Node_pin_mode::Sink  , pin.is_sink());

    if (unlikely(pin.get_lgraph()!=last_lg))
      setup_table(pin.get_lgraph());

    return last_attr->get(pin.get_compact(Mode));
  };

  static bool has(const Node_pin &pin) {
    GI(Mode == Node_pin_mode::Driver, pin.is_driver());
    GI(Mode == Node_pin_mode::Sink  , pin.is_sink());

    if (unlikely(pin.get_lgraph()!=last_lg))
      setup_table(pin.get_lgraph());

    return last_attr->has(pin.get_compact(Mode));
  };

  static Node_pin find(LGraph *lg, std::string_view name) {

    I(Mode != Node_pin_mode::Both); // not supported mode for find

    if (unlikely(lg!=last_lg))
      setup_table(lg);

    auto raw = last_attr->find(name);
    if (raw == 0)
      return Node_pin();

    auto pin = Node_pin(lg, 0, Node_pin::Compact(raw,Mode == Node_pin_mode::Sink));

    GI(Mode == Node_pin_mode::Driver, pin.is_driver());
    GI(Mode == Node_pin_mode::Sink  , pin.is_sink());

    return pin;
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
    last_lg   = nullptr;
    last_attr = nullptr;

    if (unlikely(lg!=last_lg))
      setup_table(lg);

    size_t pos = lg->get_lgid().value;
    table[pos]->clear();
    delete table[pos];
    table[pos] = nullptr;
  };
};

