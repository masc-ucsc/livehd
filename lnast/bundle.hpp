// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <map>
#include <memory>
#include <print>
#include <vector>

#include "const.hpp"

class Bundle : std::enable_shared_from_this<Bundle> {
public:
  struct Entry {
    Entry(const Entry& ent) : immutable(ent.immutable), trivial(ent.trivial) {}
    Entry() : immutable(false), trivial() {}  // default Dlop is Type::Invalid
    Entry(bool i, const Const& t) : immutable(i), trivial(t) {}
    Entry& operator=(const Entry& ent) {
      immutable = ent.immutable;
      trivial   = ent.trivial;
      return *this;
    }
    bool  immutable;
    Const trivial;
  };

  static inline Const invalid_lconst{};  // default Dlop is Type::Invalid

protected:
  using Key_map_type = std::vector<std::pair<std::string, Entry>>;
  // bundle_sorted plan §3 storage split: three side-containers indexed by
  // first-segment category. Eagerly rebuilt from key_map at the end of every
  // mutation; queries read them directly for O(1)/O(log N) answers.
  using Named_map_type   = std::map<std::string, Entry>;
  using Unnamed_map_type = std::map<int, Named_map_type>;

  const std::string name;

  // correct mark as mutable to do not allow tup updates, just to mark the
  // tuple as incorrect (not allow to mark correct once it is incorrect)
  mutable bool         immutable;
  mutable bool         correct;
  mutable Key_map_type key_map;

  // Side-indices, derived from key_map. attrs_ and named_ are keyed by the
  // full key (alphabetical); unnamed_ is a nested map outer-keyed by the
  // first-segment integer so unnamed_.size() is exactly the number of
  // distinct unnamed top-level positions in O(1).
  mutable Named_map_type   attrs_;
  mutable Named_map_type   named_;
  mutable Unnamed_map_type unnamed_;

  void sort_key_map();
  // Rebuild attrs_/named_/unnamed_ from key_map. Cheap (O(N log N)) and run
  // at the end of every public mutation point.
  void rebuild_indices() const;

  // Normalize the textual key on the way into the bundle. The canonical
  // storage convention is:
  //   - named first-segment kept as-is: "name"
  //   - unnamed first-segment is the bare decimal index: "0", "1", …
  //   - attribute first-segment: "__attr"
  // Producers have been migrated to the canonical form; in NDEBUG-off
  // builds normalize_key asserts that no legacy `:N:` keys are passed in.
  // The function itself is the identity on canonical keys.
  static std::string normalize_key(std::string_view key);

  static bool   match(std::string_view a, std::string_view b);
  static size_t match_first_partial(std::string_view a, std::string_view b);
  static bool   match_either_partial(std::string_view a, std::string_view b);

  void add_int(std::string_view key, const std::shared_ptr<Bundle const> tup);
  void del_int(std::string_view key);

public:
  Bundle(std::string_view _name) : name(_name), immutable(false), correct(true) {}

  std::string_view get_name() const { return name; }
  bool             is_correct() const { return correct; }
  void             set_issue() const { correct = false; }

  bool is_immutable() const { return immutable; }
  void set_immutable() { immutable = true; }

  bool has_trivial(std::string_view key) const;
  bool has_trivial() const { return has_trivial("0"); }

  std::string learn_fix(std::string_view key);

  const Entry& get_entry(std::string_view key) const;
  const Const& get_trivial(std::string_view key) const { return get_entry(key).trivial; }
  const Const& get_trivial() const;

  bool                    has_bundle(std::string_view key) const;
  std::shared_ptr<Bundle> get_bundle(std::string_view key) const;
  std::shared_ptr<Bundle> get_bundle(const std::shared_ptr<Bundle const>& tup) const;

  // set a trivial/sub tuple. If already existed anything, it is deleted (not attributes)

  void set(std::string_view key, const std::shared_ptr<Bundle const>& tup);
  void set(std::string_view key, const Entry&& entry);
  void set(std::string_view key, const Entry& entry) { set(key, Entry(entry)); }

  void set(std::string_view key, const Const& trivial) { set(key, Entry(false, trivial)); }
  void let(std::string_view key, const Const& trivial) {
    I(!immutable);  // FIXME: use llog library
    set(key, Entry(true, trivial));
  }
  void mut(std::string_view key, const Const& trivial) {
    I(!immutable);  // FIXME: use llog library
    set(key, Entry(false, trivial));
  }
  void var(std::string_view key, const Const& trivial) {
    I(!immutable);  // FIXME: use llog library
    set(key, Entry(false, trivial));
  }

  void set(const Const& trivial) {  // clear everything that is not 0.__attr. set 0
    return set("0", trivial);
  }

  bool concat(const std::shared_ptr<Bundle const>& tup2);
  bool concat(const Const& trivial);

  const Key_map_type& get_map() const { return key_map; }
  const Key_map_type& get_sort_map() const;
  Const               flatten() const;

  std::string_view get_scalar_name() const;  // empty if not scalar

  bool is_empty() const { return key_map.empty(); }
  bool is_scalar() const;
  bool is_trivial_scalar() const;
  bool has_just_attributes() const;

  // bundle_sorted §3 query API. Side-indices are kept in sync with key_map;
  // these are the canonical O(1) shape checks the plan calls for.
  size_t unnamed_top_count() const { return unnamed_.size(); }
  size_t named_top_count() const { return named_.size(); }
  size_t attrs_top_count() const { return attrs_.size(); }
  bool   has_named_top() const { return !named_.empty(); }
  bool   has_unnamed_top() const { return !unnamed_.empty(); }

  bool is_ordered(std::string_view key) const;

  void dump() const;

  static std::string_view get_last_level(std::string_view key);
  static std::string_view get_all_but_last_level(std::string_view key);
  static std::string_view get_all_but_first_level(std::string_view key);
  static std::string_view get_first_level(std::string_view key);
  static std::string_view get_first_level_name(std::string_view key);
  static int              get_first_level_pos(std::string_view key);
  static int              get_last_level_pos(std::string_view key) { return get_first_level_pos(get_last_level(key)); }

  static bool is_single_level(std::string_view key) { return key.find('.') == std::string::npos; }

  static bool is_root_attribute(std::string_view key) {
    if (key.substr(0, 2) == "__" && key[3] != '_') {
      return true;
    }
    if (key.substr(0, 4) == "0.__" && key[5] != '_') {
      return true;
    }

    return false;
  }

  static bool is_attribute(std::string_view key) {
    if (is_root_attribute(key)) {
      return true;
    }

    auto it = key.find(".__");
    if (it != std::string::npos) {
      if (key[it + 3] != '_') {
        return true;
      }
    }

    auto it2 = key.find(":__");
    if (it2 != std::string::npos) {
      if (key[it2 + 3] != '_') {
        return true;
      }
    }

    return false;
  }

  static std::string_view get_attribute(std::string_view key) {
    auto last = get_last_level(key);
    if (last.size() > 2 && last[0] == '_' && last[1] == '_' && last[2] != '_') {
      return last;
    }
    return "";
  }
};
