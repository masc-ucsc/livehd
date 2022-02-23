// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <vector>

#include "lconst.hpp"

class Bundle : std::enable_shared_from_this<Bundle> {
public:
  struct Entry {
    Entry(const Entry &ent) : immutable(ent.immutable) , trivial(ent.trivial) { }
    Entry() : immutable(false), trivial(Lconst::invalid()) { }
    Entry(bool i, const Lconst &t) : immutable(i), trivial(t) {}
    Entry& operator=(const Entry &ent) {
      immutable = ent.immutable;
      trivial   = ent.trivial;
      return *this;
    }
    bool   immutable;
    Lconst trivial;
  };

protected:
  using Key_map_type = std::vector<std::pair<std::string, Entry>>;

  const std::string name;

  // correct mark as mutable to do not allow tup updates, just to mark the
  // tuple as incorrect (not allow to mark correct once it is incorrect)
  mutable bool         immutable;
  mutable bool         correct;
  mutable Key_map_type key_map;

  void sort_key_map();

  static std::tuple<bool, size_t, size_t> match_int_advance(std::string_view a, std::string_view b, size_t a_pos, size_t b_pos);
  static std::tuple<bool, bool  , size_t> match_int        (std::string_view a, std::string_view b);
  static std::string                      append_field     (std::string_view a, std::string_view b);

  static std::tuple<std::string, std::string> learn_fix_int(std::string_view a, std::string_view b);

  static bool   match               (std::string_view a, std::string_view b);
  static size_t match_first_partial (std::string_view a, std::string_view b);
  static bool   match_either_partial(std::string_view a, std::string_view b);

  void          add_int(std::string_view key, const std::shared_ptr<Bundle const> tup);
  void          del_int(std::string_view key);

public:
  Bundle(std::string_view _name) : name(_name), immutable(false), correct(true) {}

  std::string_view get_name  () const { return name;     }
  bool          is_correct() const { return correct;  }
  void          set_issue () const { correct = false; }

  bool is_immutable () const { return immutable; }
  void set_immutable()       { immutable = true; }

  bool has_trivial(std::string_view key) const;
  bool has_trivial()                         const { return has_trivial("0"); }

  std::string learn_fix(std::string_view key);

  Entry  get_entry(std::string_view key) const;
  Lconst get_trivial(std::string_view key) const {
    return get_entry(key).trivial;
  }
  Lconst get_trivial()                         const;

  bool                    has_bundle(std::string_view key) const;
  std::shared_ptr<Bundle> get_bundle(std::string_view key) const;
  std::shared_ptr<Bundle> get_bundle(const std::shared_ptr<Bundle const> &tup) const;

  // set a trivial/sub tuple. If already existed anything, it is deleted (not attributes)

  void set(std::string_view key, const std::shared_ptr<Bundle const>& tup);
  void set(std::string_view key, const Entry &&entry);
  void set(std::string_view key, const Entry &entry) {
    set(key, Entry(entry));
  }

  void set(std::string_view key, const Lconst &trivial) {
    set(key, Entry(false,trivial));
  }
  void let(std::string_view key, const Lconst &trivial) {
    I(!immutable); // FIXME: use llog library
    set(key, Entry(true,trivial));
  }
  void mut(std::string_view key, const Lconst &trivial) {
    I(!immutable); // FIXME: use llog library
    set(key, Entry(false,trivial));
  }
  void var(std::string_view key, const Lconst &trivial) {
    I(!immutable); // FIXME: use llog library
    set(key, Entry(false,trivial));
  }

  void set(const Lconst &trivial) { // clear everything that is not 0.__attr. set 0
    return set("0", trivial);
  }

  bool concat(const std::shared_ptr<Bundle const>& tup2);
  bool concat(const Lconst &trivial);

  std::shared_ptr<Bundle> create_assign(const std::shared_ptr<Bundle const>& tup) const;
  std::shared_ptr<Bundle> create_assign(const Lconst &lconst)                     const;

  const Key_map_type &get_map     () const { return key_map; }
  const Key_map_type &get_sort_map() const;
  Lconst              flatten     () const;

  std::string_view get_scalar_name() const;  // empty if not scalar

  bool is_empty           () const { return key_map.empty(); }
  bool is_scalar          () const;
  bool is_trivial_scalar  () const;
  bool has_just_attributes() const;

  bool is_ordered         (std::string_view key) const;

  void dump() const;

  static std::string_view get_last_level         (std::string_view key);
  static std::string_view get_all_but_last_level (std::string_view key);
  static std::string_view get_all_but_first_level(std::string_view key);
  static std::string_view get_first_level        (std::string_view key);
  static std::string_view get_first_level_name   (std::string_view key);
  static std::string_view get_canonical_name     (std::string_view key);
  static int              get_first_level_pos    (std::string_view key);
  static int              get_last_level_pos     (std::string_view key) { return get_first_level_pos(get_last_level(key)); }

  static std::pair<int, std::string_view> convert_key_to_io(std::string_view key);

  static bool is_single_level(std::string_view key) { return key.find('.') == std::string::npos; }

  static bool is_root_attribute(std::string_view key) {
    if (key.substr(0, 2) == "__" && key[3] != '_')
      return true;
    if (key.substr(0, 4) == "0.__" && key[5] != '_')
      return true;

    return false;
  }

  static bool is_attribute(std::string_view key) {
    if (is_root_attribute(key))
      return true;

    auto it = key.find(".__");
    if (it != std::string::npos) {
      if (key[it + 3] != '_')
        return true;
    }

    auto it2 = key.find(":__");
    if (it2 != std::string::npos) {
      if (key[it2 + 3] != '_')
        return true;
    }

    return false;
  }

  static std::string_view get_attribute(std::string_view key) {
    auto last = get_last_level(key);
    if (last.size() > 2 && last[0] == '_' && last[1] == '_' && last[2] != '_')
      return last;
    return "";
  }

};
