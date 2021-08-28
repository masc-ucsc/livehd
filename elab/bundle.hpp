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
  using Key_map_type = std::vector<std::pair<mmap_lib::str, Entry>>;

  const mmap_lib::str name;

  // correct mark as mutable to do not allow tup updates, just to mark the
  // tuple as incorrect (not allow to mark correct once it is incorrect)
  mutable bool         immutable;
  mutable bool         correct;
  mutable Key_map_type key_map;

  void sort_key_map();

  static std::tuple<bool, size_t, size_t> match_int_advance(const mmap_lib::str &a, const mmap_lib::str &b, size_t a_pos, size_t b_pos);
  static std::tuple<bool, bool  , size_t> match_int        (const mmap_lib::str &a, const mmap_lib::str &b);
  static mmap_lib::str                    append_field     (const mmap_lib::str &a, const mmap_lib::str &b);

  static std::tuple<mmap_lib::str, mmap_lib::str> learn_fix_int(const mmap_lib::str &a, const mmap_lib::str &b);

  static bool   match               (const mmap_lib::str &a, const mmap_lib::str &b);
  static size_t match_first_partial (const mmap_lib::str &a, const mmap_lib::str &b);
  static bool   match_either_partial(const mmap_lib::str &a, const mmap_lib::str &b);

  void          add_int(const mmap_lib::str &key, const std::shared_ptr<Bundle const> tup);
  void          del_int(const mmap_lib::str &key);

public:
  Bundle(const mmap_lib::str &_name) : name(_name), immutable(false), correct(true) {}

  mmap_lib::str get_name  () const { return name;     }
  bool          is_correct() const { return correct;  }
  void          set_issue () const { correct = false; }

  bool is_immutable () const { return immutable; }
  void set_immutable()       { immutable = true; }

  bool has_trivial(const mmap_lib::str &key) const;
  bool has_trivial()                         const { return has_trivial("0"_str); }

  mmap_lib::str learn_fix(const mmap_lib::str &key);

  Entry  get_entry(const mmap_lib::str &key) const;
  Lconst get_trivial(const mmap_lib::str &key) const {
    return get_entry(key).trivial;
  }
  Lconst get_trivial()                         const;

  bool                    has_bundle(const mmap_lib::str                 &key) const;
  std::shared_ptr<Bundle> get_bundle(const mmap_lib::str                 &key) const;
  std::shared_ptr<Bundle> get_bundle(const std::shared_ptr<Bundle const> &tup) const;

  // set a trivial/sub tuple. If already existed anything, it is deleted (not attributes)

  void set(const mmap_lib::str &key, const std::shared_ptr<Bundle const>& tup);
  void set(const mmap_lib::str &key, const Entry &&entry);
  void set(const mmap_lib::str &key, const Entry &entry) {
    set(key, Entry(entry));
  }

  void set(const mmap_lib::str &key, const Lconst &trivial) {
    set(key, Entry(false,trivial));
  }
  void let(const mmap_lib::str &key, const Lconst &trivial) {
    I(!immutable); // FIXME: use llog library
    set(key, Entry(true,trivial));
  }
  void mut(const mmap_lib::str &key, const Lconst &trivial) {
    I(!immutable); // FIXME: use llog library
    set(key, Entry(false,trivial));
  }
  void var(const mmap_lib::str &key, const Lconst &trivial) {
    I(!immutable); // FIXME: use llog library
    set(key, Entry(false,trivial));
  }

  void set(const Lconst &trivial) { // clear everything that is not 0.__attr. set 0
    return set("0"_str, trivial);
  }

  bool concat(const std::shared_ptr<Bundle const>& tup2);
  bool concat(const Lconst &trivial);

  std::shared_ptr<Bundle> create_assign(const std::shared_ptr<Bundle const>& tup) const;
  std::shared_ptr<Bundle> create_assign(const Lconst &lconst)                     const;

  const Key_map_type &get_map     () const { return key_map; }
  const Key_map_type &get_sort_map() const;
  Lconst              flatten     () const;

  mmap_lib::str get_scalar_name() const;  // empty if not scalar

  bool is_empty           () const { return key_map.empty(); }
  bool is_scalar          () const;
  bool is_trivial_scalar  () const;
  bool has_just_attributes() const;

  bool is_ordered         (const mmap_lib::str key) const;

  void dump() const;

  static mmap_lib::str get_last_level         (const mmap_lib::str &key);
  static mmap_lib::str get_all_but_last_level (const mmap_lib::str &key);
  static mmap_lib::str get_all_but_first_level(const mmap_lib::str &key);
  static mmap_lib::str get_first_level        (const mmap_lib::str &key);
  static mmap_lib::str get_first_level_name   (const mmap_lib::str &key);
  static int           get_first_level_pos    (const mmap_lib::str &key);
  static mmap_lib::str get_canonical_name     (const mmap_lib::str &key);
  static int           get_last_level_pos     (const mmap_lib::str &key) { return get_first_level_pos(get_last_level(key)); }

  static std::pair<int, mmap_lib::str> convert_key_to_io(const mmap_lib::str &key);

  static bool is_single_level(const mmap_lib::str &key) { return key.find('.') == std::string::npos; }

  static bool is_root_attribute(const mmap_lib::str &key) {
    if (key.substr(0, 2) == "__" && key[3] != '_')
      return true;
    if (key.substr(0, 4) == "0.__" && key[5] != '_')
      return true;

    return false;
  }

  static bool is_attribute(const mmap_lib::str &key) {
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

  static mmap_lib::str get_attribute(const mmap_lib::str &key) {
    auto last = get_last_level(key);
    if (last.size() > 2 && last[0] == '_' && last[1] == '_' && last[2] != '_')
      return last;
    return mmap_lib::str();
  }

};
