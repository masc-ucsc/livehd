// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <print>
#include <vector>

#include "absl/strings/str_cat.h"
#include "battr.hpp"
#include "bundle_key.hpp"
#include "hlop/dlop.hpp"
#include "kind.hpp"

class Bundle : std::enable_shared_from_this<Bundle> {
public:
  // One Entry per leaf (1b/D): the value plus the always-present,
  // single-writer pass facts as TYPED fields — never string-keyed attrs.
  // The per-pass attribute responsibilities (see upass/README.md) are the
  // authoritative registry of these fields (one writer each); extend the
  // struct and that table together. Default Dlop is Type::Invalid =
  // "unset/unknown" for every Dlop-typed fact.
  struct Entry {
    Entry() = default;
    Entry(bool i, const Dlop& t) : trivial(t), immutable(i) {}

    Dlop trivial;             // the comptime value (constprop; shared must-agree slice)
    Dlop decl_max, decl_min;  // declared envelope (type-bake pre-step; persistent)
    Dlop bw_max, bw_min;      // derived range (bitwidth; per-write)

    upass::Kind kind      = upass::Kind::unknown;  // typecheck / type-bake
    upass::Mode mode      = upass::Mode::unknown;  // per-field mut/const (type-bake; rides entry copies)
    bool        immutable = false;
    bool        comptime  = false;
  };

  static inline Dlop invalid_lconst{};  // default Dlop is Type::Invalid

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
    Dlop             scalar;
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
  // Storage is TWO sorted maps sharing Canonical_less:
  //   key_map  — data leaves only, keyed by canonical dotted path
  //              ("name", "0", "a.b", "a.0", …). No attribute keys.
  //   attr_map — attribute leaves, keyed by BARE attr name (no "__")
  //              with the attr name as the LAST segment: a whole-bundle
  //              attr is "bits"; a per-field attr is "a.b.bits". Sticky
  //              attrs carry their canonical leading "_" ("_foo");
  //              "debug" normalizes to "_debug" on the way in (canon_attr),
  //              so the sticky subset is one contiguous run per level.
  // Membership in attr_map IS what makes a key an attribute — the old
  // per-key "__" prefix sniff (is_attribute / is_root_attribute /
  // get_attribute and the triple-underscore guards) is gone.
  // Lookups by exact key are O(log N); sub-tree scans use
  // lower_bound("prefix.") + walk.
  using Key_map_type = std::map<std::string, Entry, Canonical_less>;

  // Binding-level typed facts (1b/D): mode is a property of the NAME
  // (mut/const/reg), type_name the nominal identity for `is` (empty =
  // anonymous). Per-leaf facts live on Entry.
  upass::Mode mode_ = upass::Mode::unknown;
  std::string type_name_;
  // The kind of the VALUE this bundle holds, stamped by producer ops
  // (typecheck). Distinct from Entry.kind, which is per-leaf (the "0" entry's
  // kind describes FIELD 0 on a multi-entry bundle — constprop copies field
  // entries wholesale, so entry kinds travel with fields). Needed for the
  // shapes the entry can't express: a 0/1-entry tuple (`mut d=(); d=d++(i,)`)
  // is scalar-SHAPED yet kind `tuple`.
  upass::Kind value_kind_ = upass::Kind::unknown;

  // correct mark as mutable to do not allow tup updates, just to mark the
  // tuple as incorrect (not allow to mark correct once it is incorrect)
  mutable bool         immutable;
  mutable bool         correct;
  mutable Key_map_type key_map;
  mutable Key_map_type attr_map;

  // 1b/E — inline root Entry: while a bundle is a bare scalar, its single
  // "0" leaf lives here and key_map stays EMPTY (zero map nodes for the
  // dominant scalar case). Invariant: has_root_ ⇒ key_map.empty(). Any
  // other data insert — or any whole-map view/iteration — spills the root
  // into key_map["0"] first (ensure_view_materialized; the map is mutable,
  // so const query paths may spill too). Whole-bundle attrs are unaffected
  // (they live in attr_map under bare names either way).
  mutable Entry root_;
  mutable bool  has_root_ = false;

  void ensure_view_materialized() const {
    if (has_root_) {
      key_map.emplace(std::string("0"), root_);
      has_root_ = false;
    }
  }

  // Canonical attribute-name spelling: stickiness is encoded in the name
  // (leading '_'); the builtin alias "debug" stores as "_debug".
  static std::string_view canon_attr(std::string_view attr_name) {
    return attr_name == "debug" ? std::string_view{"_debug"} : attr_name;
  }

  const Entry& attr_entry(std::string_view attr_key) const {
    const auto it = attr_map.find(attr_key);
    if (it == attr_map.end()) {
      static Entry invalid(true, invalid_lconst);
      return invalid;
    }
    return it->second;
  }

  // Normalize the textual key on the way into the bundle. The canonical
  // storage convention is:
  //   - named first-segment kept as-is: "name"
  //   - unnamed first-segment is the bare decimal index: "0", "1", …
  //   - attribute first-segment: "__attr"
  // Producers have been migrated to the canonical form; in NDEBUG-off
  // builds normalize_key asserts that no legacy `:N:` keys are passed in.
  // The function itself is the identity on canonical keys.
  static std::string normalize_key(std::string_view key) { return bundle_key::normalize_key(key); }

  static bool   match(std::string_view a, std::string_view b) { return bundle_key::match(a, b); }
  static size_t match_first_partial(std::string_view a, std::string_view b) { return bundle_key::match_first_partial(a, b); }

  void del_int(std::string_view key);  // data leaves only; attrs are never deleted by data writes

public:
  // The name member is gone (its last cross-pass reader died with
  // runner_type_query_fn); the runner passes names to the hooks for
  // diagnostics. The ctor still ACCEPTS a name and discards it so the ~30
  // construction sites stay readable about WHAT they build.
  Bundle() : immutable(false), correct(true) {}
  explicit Bundle(std::string_view /*name — discarded*/) : Bundle() {}

  bool is_correct() const { return correct; }
  void set_issue() const { correct = false; }

  // Binding-level typed facts (1b/D).
  upass::Mode      get_mode() const { return mode_; }
  void             set_mode(upass::Mode m) { mode_ = m; }
  std::string_view get_type_name() const { return type_name_; }
  void             set_type_name(std::string_view tn) { type_name_ = std::string(tn); }
  upass::Kind      get_value_kind() const { return value_kind_; }
  void             set_value_kind(upass::Kind k) { value_kind_ = k; }

  // The hot "this operand's comptime value, or unknown" read (1b/D+E).
  // Defined only for scalar bundles (≤1 data leaf); nullopt when the
  // value is absent or unknown. Reads the inline root Entry — never spills.
  std::optional<Dlop> scalar() const {
    if (has_root_) {
      if (root_.trivial.is_invalid()) {
        return std::nullopt;
      }
      return root_.trivial;
    }
    if (key_map.size() != 1 || key_map.begin()->second.trivial.is_invalid()) {
      return std::nullopt;
    }
    return key_map.begin()->second.trivial;
  }

  // Dlop-operand factory for the runner's operand resolution (one wrapper
  // per const operand per node). One allocation, zero map nodes (inline root).
  static std::shared_ptr<Bundle> make_const(const Dlop& v, upass::Kind k) {
    auto b        = std::make_shared<Bundle>("");
    b->root_      = Entry(false, v);
    b->root_.kind = k;
    b->has_root_  = true;
    return b;
  }

  // For whole-bundle scalar reads prefer scalar(); the keyed
  // forms are for FIELD reads. NOTE: has_trivial is EXISTENCE-only — an
  // entry with an INVALID trivial (a runtime-unknown marker) still counts;
  // first-write gates must test get_trivial(key).is_invalid() instead.
  bool has_trivial(std::string_view key) const;

  const Entry& get_entry(std::string_view key) const;
  const Dlop&  get_trivial(std::string_view key) const { return get_entry(key).trivial; }
  // The LONE non-attr entry's value regardless of key depth (a 1-element
  // tuple-of-tuple flattens); invalid when the bundle isn't single-entry.
  // (The explicit spelling — a no-arg get_trivial() was ambiguous with the
  // "0"-keyed form, which means FIELD ZERO on a multi-shaped bundle.)
  const Dlop&  lone_trivial() const;

  bool                    has_bundle(std::string_view key) const;
  std::shared_ptr<Bundle> get_bundle(std::string_view key) const;

  // set a trivial/sub tuple. If already existed anything, it is deleted (not attributes)

  void set(std::string_view key, const std::shared_ptr<Bundle const>& tup);
  void set(std::string_view key, const Entry&& entry);
  void set(std::string_view key, const Entry& entry) { set(key, Entry(entry)); }

  // A VALUE write preserves the entry's typed fact fields (kind,
  // declared envelope, derived range, comptime): only the value slice and the
  // mutability flag change. "Metadata that must not change across assignments
  // remains declaration-persistent."
  Entry value_entry(std::string_view key, bool immutable_flag, const Dlop& trivial) const {
    Entry e     = get_entry(key);  // copies the facts; invalid sentinel when missing
    e.trivial   = trivial;
    e.immutable = immutable_flag;
    // The DERIVED range describes the previous value — a new value
    // invalidates it (bitwidth re-stamps the fresh derivation right after;
    // a binding must never hold a range that does not contain its value).
    // Declared facts (kind / decl envelope / comptime / mode) persist.
    e.bw_max    = invalid_lconst;
    e.bw_min    = invalid_lconst;
    return e;
  }

  void set(std::string_view key, const Dlop& trivial) { set(key, value_entry(key, false, trivial)); }
  void var(std::string_view key, const Dlop& trivial) {
    I(!immutable);  // FIXME: use llog library
    set(key, value_entry(key, false, trivial));
  }

  void set(const Dlop& trivial) {  // clear everything that is not 0.__attr. set 0
    return set("0", trivial);
  }

  // Direct attribute access — routes to the dedicated attr_map.
  // Bare attribute names throughout (no "__"); a whole-bundle attr keys on
  // the name alone, a per-field attr on "field.attr_name". "debug" is
  // normalized to its canonical sticky spelling "_debug" on both reads and
  // writes (canon_attr).
  //
  // DEPRECATION NOTE (1b/F): these are for the RESIDUAL attrs only
  // (battr.hpp). The promoted pass facts — kind, declared max/min,
  // bw_max/bw_min, comptime, mode, typename — are typed Entry/Bundle
  // FIELDS; never store them through this string-keyed API.
  bool has_attr(std::string_view attr_name) const { return attr_map.find(canon_attr(attr_name)) != attr_map.end(); }
  bool has_attr(std::string_view field, std::string_view attr_name) const {
    return attr_map.find(absl::StrCat(field, ".", canon_attr(attr_name))) != attr_map.end();
  }
  const Dlop& get_attr(std::string_view attr_name) const { return attr_entry(canon_attr(attr_name)).trivial; }
  const Dlop& get_attr(std::string_view field, std::string_view attr_name) const {
    return attr_entry(absl::StrCat(field, ".", canon_attr(attr_name))).trivial;
  }
  void set_attr(std::string_view attr_name, const Dlop& v) {
    attr_map.insert_or_assign(std::string(canon_attr(attr_name)), Entry(false, v));
  }
  void set_attr(std::string_view field, std::string_view attr_name, const Dlop& v) {
    attr_map.insert_or_assign(absl::StrCat(field, ".", canon_attr(attr_name)), Entry(false, v));
  }
  void clear_attr(std::string_view attr_name) { attr_map.erase(std::string(canon_attr(attr_name))); }
  void clear_attr(std::string_view field, std::string_view attr_name) {
    attr_map.erase(absl::StrCat(field, ".", canon_attr(attr_name)));
  }

  // Pyrope `a ++ b` (03-bundle.md). A colliding named field merges only when
  // one side is `nil`, both leaves carry the same comptime value, or both
  // sides are sub-tuples (recursive merge). Any other leaf overlap returns
  // false with *conflict set to the colliding canonical path — the caller
  // diagnoses; `this` may be left partially merged, so discard it on failure.
  bool concat(const std::shared_ptr<Bundle const>& tup2, std::string* conflict = nullptr);

  bool is_empty() const { return !has_root_ && key_map.empty() && attr_map.empty(); }
  bool is_scalar() const { return has_root_ || key_map.size() <= 1; }
  bool is_trivial_scalar() const;

  // bundle_flat_storage query API. All counts and presence checks
  // compute on demand; named/unnamed counted by distinct top-level
  // segment of the data map.
  size_t unnamed_top_count() const;
  size_t named_top_count() const;
  bool   has_named_top() const { return named_top_count() > 0; }
  bool   has_unnamed_top() const { return unnamed_top_count() > 0; }

  // Sorted iteration views over the bundle (1b/C — zero-copy where the
  // storage already IS the view):
  //
  //   entries()          — all leaves (attrs first, then data); materialized
  //   non_attr_entries() — the data map itself (no copy, no per-call vector)
  //   get_attrs()        — the attr map itself (bare attr names)
  //   top_levels()       — one Top_level_entry per distinct top-level
  //                        segment of the data map, in canonical order
  Entries_view        entries() const;
  const Key_map_type& non_attr_entries() const {
    ensure_view_materialized();  // 1b/E: iteration spills the inline root
    return key_map;
  }
  const Key_map_type& get_attrs() const { return attr_map; }
  Top_levels_view     top_levels() const;

  // Sticky subset of the attrs: canonical leading-'_' names
  // (battr::is_sticky on the attr-name segment). The propagation rule is
  // "merge each src's sticky_attributes() into dst". Small materialized
  // view — sticky sets are tiny; '_' sorts apart from letter-named attrs,
  // so per level the subset is one contiguous run.
  Entries_view sticky_attributes() const {
    Entries_view v;
    for (const auto& kv : attr_map) {
      if (battr::is_sticky(get_last_level(kv.first))) {
        v.items_.emplace_back(std::string_view(kv.first), &kv.second);
      }
    }
    return v;
  }

  // Top-level membership queries. has_top_named checks whether any
  // non-attribute leaf has the given top-level NAME. has_top_unnamed
  // checks for an unnamed top-level at the given decimal position.
  bool has_top_named(std::string_view name) const;
  bool has_top_unnamed(int pos) const;

  void dump(std::string_view name = "?") const;

  // Dotted-key string helpers — implementations live in lnast/bundle_key.hpp
  // (shared with lnast_builder without a lnast→upass dependency); these
  // statics delegate to keep the public Bundle API unchanged (1b/A).
  static std::string_view get_last_level(std::string_view key) { return bundle_key::get_last_level(key); }
  static std::string_view get_all_but_last_level(std::string_view key) { return bundle_key::get_all_but_last_level(key); }
  static std::string_view get_all_but_first_level(std::string_view key) { return bundle_key::get_all_but_first_level(key); }
  static std::string_view get_first_level(std::string_view key) { return bundle_key::get_first_level(key); }
  static std::string_view get_first_level_name(std::string_view key) { return bundle_key::get_first_level_name(key); }

  static bool is_single_level(std::string_view key) { return bundle_key::is_single_level(key); }

  // is_root_attribute / is_attribute / get_attribute are GONE:
  // attribute-ness is membership in attr_map, not a "__" spelling.
};
