// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <map>
#include <memory>
#include <print>
#include <vector>

#include "absl/strings/str_cat.h"
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

  // Canonical key ordering used by Bundle storage. Per dotted segment:
  // attrs (`__foo`) < named (alphabetical) < unnamed (numeric, not lex).
  // Walks segment-by-segment; shorter prefix sorts before its extensions
  // at any given level. is_transparent enables string_view lookups in maps.
  struct Canonical_less {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const;
  };

  // Per top-level summary yielded by Bundle::top_levels(). Exactly one
  // entry per distinct top-level segment in the bundle's non-attribute
  // data:
  //   - `name`       = top-level segment when named (pos == -1)
  //   - `pos`        = top-level unnamed index when unnamed (>= 0)
  //   - `leaf_count` = number of leaves under this top-level
  //   - `has_leafs`  = true iff any leaf has a dotted suffix under it
  //                    (i.e., top-level is a sub-bundle, not a bare scalar)
  //   - `scalar`     = collapsed scalar value when leaf_count == 1
  //                    (matches pyrope's `f.foo=44 ⇒ f == 44` rule);
  //                    invalid otherwise.
  struct Top_level_entry {
    std::string_view name;
    int              pos        = -1;
    size_t           leaf_count = 0;
    bool             has_leafs  = false;
    Const            scalar;
  };

  // Sorted-iteration views over Bundle data. Materialized on each call
  // (Phase 1: adapters over the underlying key_map storage). Cheap value
  // semantics; callers typically range-for or read by index.
  class Entries_view {
  public:
    using Item           = std::pair<std::string_view, const Entry*>;
    using const_iterator = std::vector<Item>::const_iterator;
    const_iterator    begin() const { return items_.begin(); }
    const_iterator    end() const { return items_.end(); }
    bool              empty() const { return items_.empty(); }
    size_t            size() const { return items_.size(); }
    std::vector<Item> items_;
  };

  class Top_levels_view {
  public:
    using const_iterator = std::vector<Top_level_entry>::const_iterator;
    const_iterator               begin() const { return items_.begin(); }
    const_iterator               end() const { return items_.end(); }
    bool                         empty() const { return items_.empty(); }
    size_t                       size() const { return items_.size(); }
    std::vector<Top_level_entry> items_;
  };

protected:
  // bundle_flat_storage Phase 3 — primary storage is a single sorted
  // map keyed by canonical dotted path. Canonical_less yields canonical
  // iteration order: per dotted level, attrs first, then named
  // alphabetical, then unnamed numeric. Lookups by exact key are
  // O(log N); sub-tree scans use lower_bound("prefix.") + walk.
  using Key_map_type = std::map<std::string, Entry, Canonical_less>;

  const std::string name;

  // correct mark as mutable to do not allow tup updates, just to mark the
  // tuple as incorrect (not allow to mark correct once it is incorrect)
  mutable bool         immutable;
  mutable bool         correct;
  mutable Key_map_type key_map;

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

  // Direct attribute access — Step B of upass redesign. Wraps the legacy
  // "__attr" key-prefix convention so passes can read/write attrs without
  // hand-rolling the prefix dance. The bare attribute name (e.g. "bits",
  // "type", "pending_import") routes to "__<name>" in the underlying
  // key_map. Sub-fields can use "field.attr_name" form (e.g.
  // set_attr("foo", "bits", …) → "foo.__bits").
  bool has_attr(std::string_view attr_name) const { return has_trivial(absl::StrCat("__", attr_name)); }
  bool has_attr(std::string_view field, std::string_view attr_name) const {
    return has_trivial(absl::StrCat(field, ".__", attr_name));
  }
  const Const& get_attr(std::string_view attr_name) const { return get_trivial(absl::StrCat("__", attr_name)); }
  const Const& get_attr(std::string_view field, std::string_view attr_name) const {
    return get_trivial(absl::StrCat(field, ".__", attr_name));
  }
  void set_attr(std::string_view attr_name, const Const& v) { set(absl::StrCat("__", attr_name), v); }
  void set_attr(std::string_view field, std::string_view attr_name, const Const& v) {
    set(absl::StrCat(field, ".__", attr_name), v);
  }

  bool concat(const std::shared_ptr<Bundle const>& tup2);
  bool concat(const Const& trivial);

  std::string_view get_scalar_name() const;  // empty if not scalar

  bool is_empty() const { return key_map.empty(); }
  bool is_scalar() const;
  bool is_trivial_scalar() const;
  bool has_just_attributes() const;

  // bundle_flat_storage query API. All counts and presence checks
  // compute on demand from key_map; results match the canonical
  // semantics (attrs counted once; named/unnamed counted by distinct
  // top-level segment).
  size_t unnamed_top_count() const;
  size_t named_top_count() const;
  size_t attrs_top_count() const;
  bool   has_named_top() const { return named_top_count() > 0; }
  bool   has_unnamed_top() const { return unnamed_top_count() > 0; }

  // bundle_flat_storage Phase 1 — sorted iteration views over the bundle.
  // Each call materializes a small vector in canonical order.
  //
  //   entries()          — all leaves (attrs + data), sorted
  //   non_attr_entries() — data leaves only, sorted
  //   get_attrs()        — attribute leaves only, sorted
  //   top_levels()       — one Top_level_entry per distinct top-level
  //                        segment of non-attr data, in canonical order
  Entries_view    entries() const;
  Entries_view    non_attr_entries() const;
  Entries_view    get_attrs() const;
  Top_levels_view top_levels() const;

  // Top-level membership queries. has_top_named checks whether any
  // non-attribute leaf has the given top-level NAME. has_top_unnamed
  // checks for an unnamed top-level at the given decimal position.
  bool has_top_named(std::string_view name) const;
  bool has_top_unnamed(int pos) const;

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
