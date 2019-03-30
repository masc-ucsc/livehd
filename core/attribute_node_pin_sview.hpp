
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


enum class Node_pin_mode {
  Driver,
  Sink,
  Both
};

template<const char *Name, Node_pin_mode Mode, bool Unique>
class Attribute_node_pin_sview_type {

  using Attr_data = Attr_sview_raw<uint32_t, Unique>;

  inline static std::vector<Attr_data *> table;

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

public:
  static void set(const Node_pin &pin, std::string_view wname) {
    GI(Mode == Node_pin_mode::Driver, pin.is_driver());
    GI(Mode == Node_pin_mode::Sink  , pin.is_sink());

    auto *lg   = pin.get_lgraph();
    size_t pos = lg->get_lgid().value;

    if (is_invalid(pos)) {
      table.resize(pos+1);
      I(table[pos] == nullptr);
      auto *lg = pin.get_lgraph();
      table[pos] = new Attr_data(lg->get_path(), get_filename(lg->get_lgid()));
    }

    I(!table[pos]->has(pin.get_compact())); // Do not double insert (why???) waste or bug with Name alias!!

    table[pos]->set(pin.get_compact(), wname);
  };

  static std::string_view get(const Node_pin &pin) {
    GI(Mode == Node_pin_mode::Driver, pin.is_driver());
    GI(Mode == Node_pin_mode::Sink  , pin.is_sink());

    auto *lg   = pin.get_lgraph();
    size_t pos = lg->get_lgid().value;
    if (is_invalid(pos))
      return "";

    return table[pos]->get(pin.get_compact());
  };

  static bool has(const Node_pin &pin) {
    GI(Mode == Node_pin_mode::Driver, pin.is_driver());
    GI(Mode == Node_pin_mode::Sink  , pin.is_sink());

    auto *lg   = pin.get_lgraph();
    size_t pos = lg->get_lgid().value;
    if (is_invalid(pos))
      return false;

    return table[pos]->has(pin.get_compact());
  };

  static Node_pin find(LGraph *g, std::string_view name) {

    size_t pos = g->get_lgid().value;
    if (is_invalid(pos))
      return Node_pin();

    auto raw = table[pos]->find(name);
    if (raw==0) {
      return Node_pin();
    }

    auto pin = Node_pin(g, 0, Node_pin::Compact(raw));

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
    size_t pos = lg->get_lgid().value;
    if (is_invalid(pos))
      return;

    table[pos]->clear();
    delete table[pos];
    table[pos] = nullptr;
  };
};

