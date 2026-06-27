// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
#include "battr.hpp"
#include "bundle_key.hpp"
#include "bundle_path.hpp"
#include "hlop/dlop.hpp"
#include "kind.hpp"

// Bundle (indexed nested tree). A binding's value as a tree of fields instead of
// a flat dotted-string map:
//   - the dominant case is a bare SCALAR (one value) — stored inline (scalar_),
//     no field vector, no nodes;
//   - a nested bundle keeps a small `fields_` vector (counts are tiny — <~20
//     total, 1-10 per level, 1-3 deep), each field NAMED (a positive LNAST
//     name-id reused as the field id) or UNNAMED (a positional index), holding a
//     leaf value or a sub-Bundle. Linear scan beats a map/binary-search here.
// Data keys are int32 segment-id PATHS (bundle_path::Seg): a caller splits a
// dotted LNAST name once via bundle_path::of_name and navigates by ids — no
// dotted string touches data storage. The dotted-string data methods remain as
// thin adapters (split via bundle_path) during the caller migration.
//
// Attributes are RARE residuals (battr) and stay string-keyed in attr_map (their
// Canonical_less cost is negligible at attr volumes, and the sticky / enum-
// identity / inheritance / relocation semantics stay exact). Only the data path
// — the hot one — is indexed.
class Bundle : std::enable_shared_from_this<Bundle> {
public:
  using Seg  = bundle_path::Seg;
  using Path = std::span<const Seg>;

  // One Entry per leaf: the value plus the single-writer pass facts as TYPED
  // fields (never string-keyed). Default Dlop is Type::Invalid = "unset".
  struct Entry {
    Entry() = default;
    Entry(bool i, const Dlop& t) : trivial(t), immutable(i) {}

    Dlop trivial;             // the comptime value (constprop; shared must-agree slice)
    Dlop decl_max, decl_min;  // declared envelope (type-bake pre-step; persistent)
    Dlop bw_max, bw_min;      // derived range (bitwidth; per-write)

    upass::Kind kind      = upass::Kind::unknown;  // typecheck / type-bake
    upass::Mode mode      = upass::Mode::unknown;  // per-field mut/const (type-bake)
    bool        immutable = false;
    bool        comptime  = false;
  };

  static inline Dlop invalid_lconst{};  // default Dlop is Type::Invalid

  // A field of a nested bundle: a leaf value (sub == nullptr) or a sub-Bundle.
  struct Field {
    Seg                     seg{0};  // > 0 named (LNAST name-id); < 0 unnamed pos = -(p+1)
    Entry                   leaf;    // valid when sub == nullptr
    std::shared_ptr<Bundle> sub;     // non-null => sub-bundle field
  };

  // Canonical key ordering for the ATTR map. Per dotted segment: attrs (`__`/`_`)
  // < named (alphabetical) < unnamed (numeric). is_transparent enables
  // string_view lookups.
  struct Canonical_less {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const;
  };
  using Attr_map = std::map<std::string, Entry, Canonical_less>;

  // Per top-level summary yielded by top_levels(). Exactly one per distinct
  // top-level field of the bundle's data.
  struct Top_level_entry {
    std::string_view name;             // top-level segment when named (pos == -1)
    int              pos        = -1;  // top-level unnamed index when unnamed (>= 0)
    size_t           leaf_count = 0;
    bool             has_leafs  = false;  // top-level is a sub-bundle, not a bare scalar
    Dlop             scalar;            // collapsed value when leaf_count == 1
  };

  // Materialized sorted-iteration views. Data keys are dotted strings
  // reconstructed at the edge (OWNED by the view); attr items view attr_map.
  class Entries_view {
  public:
    // Entry BY VALUE (these views are materialized): a leaf's `e.trivial` and a
    // `set(k, e)` round-trip read like the old map-backed iteration.
    using Item           = std::pair<std::string, Entry>;
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
  // ── data storage ─────────────────────────────────────────────────────────
  // Dominant case: a bare scalar (the unnamed pos-0 leaf) lives inline, fields_
  // empty. Any other data write spills scalar_ into fields_[0] (seg == -1) first.
  Entry scalar_;
  bool  has_scalar_ = false;  // has_scalar_ ⇒ fields_.empty()

  std::vector<Field> fields_;  // nested fields; named (+id) and unnamed (−pos) mixed, insertion order

  // ── rare residual attributes (battr) — string-keyed ───────────────────────
  // Whole-bundle attr keys on the bare name ("bits"); per-field on "a.b.bits".
  // Sticky attrs carry the canonical leading "_". Membership IS attribute-ness.
  mutable Attr_map attr_map;

  // Binding-level typed facts. mode = property of the NAME (mut/const/reg);
  // type_name = nominal identity for `is` (0 = anonymous). value_kind = kind of
  // the VALUE this bundle holds (distinct from per-leaf Entry.kind).
  upass::Mode mode_         = upass::Mode::unknown;
  int32_t     type_name_id_ = 0;  // interned type name (LNAST pool); 0 = anonymous
  upass::Kind value_kind_   = upass::Kind::unknown;

  mutable bool immutable;
  mutable bool correct;

  // ── navigation primitives (linear scan; counts are tiny) ──────────────────
  [[nodiscard]] const Field* find_field(Seg s) const;
  [[nodiscard]] Field*       find_field(Seg s);
  void                       spill_scalar();  // scalar_ → fields_[0] (seg -1) when has_scalar_
  [[nodiscard]] std::shared_ptr<Bundle> clone() const;  // deep copy (subs cloned recursively)

public:
  static std::string seg_text(Seg s);  // a single seg back to its source text ("a" / "0")

protected:

  // Descend `path[0..n-1]` into sub-bundles; create when asked. Returns the
  // innermost Bundle and writes the last seg to *last; nullptr when a non-create
  // walk hits a missing/leaf node (or path empty).
  [[nodiscard]] const Bundle* descend(Path path, Seg* last) const;
  [[nodiscard]] Bundle*       descend_or_create(Path path, Seg* last);

  // Flatten this bundle's data leaves into (dotted-key, Entry*) pairs in
  // insertion order — the materialized view backing the data adapters.
  void flatten_into(const std::string& prefix, std::vector<Entries_view::Item>& out) const;

  // Canonical attribute-name spelling: stickiness encoded in the name (leading
  // '_'); the builtin alias "debug" stores as "_debug".
  static std::string_view canon_attr(std::string_view attr_name) {
    return attr_name == "debug" ? std::string_view{"_debug"} : attr_name;
  }
  const Entry& attr_entry(std::string_view attr_key) const;

public:
  // The ctor accepts (and discards) a name so the ~30 construction sites stay
  // readable about WHAT they build.
  Bundle() : immutable(false), correct(true) {}
  explicit Bundle(std::string_view /*name — discarded*/) : Bundle() {}

  bool is_correct() const { return correct; }
  void set_issue() const { correct = false; }

  // Binding-level typed facts.
  upass::Mode      get_mode() const { return mode_; }
  void             set_mode(upass::Mode m) { mode_ = m; }
  std::string_view get_type_name() const { return Lnast::active_name_pool()->resolve(type_name_id_); }
  void             set_type_name(std::string_view tn) { type_name_id_ = Lnast::active_name_pool()->intern(tn); }
  upass::Kind      get_value_kind() const { return value_kind_; }
  void             set_value_kind(upass::Kind k) { value_kind_ = k; }

  // The hot "this operand's comptime value, or unknown" read. Defined only for
  // scalar bundles (≤1 data leaf); nullopt when absent/unknown. Reads the inline
  // scalar — never spills.
  std::optional<Dlop> scalar() const {
    if (has_scalar_) {
      return scalar_.trivial.is_invalid() ? std::nullopt : std::optional<Dlop>(scalar_.trivial);
    }
    if (fields_.size() != 1 || fields_.front().sub || fields_.front().leaf.trivial.is_invalid()) {
      return std::nullopt;
    }
    return fields_.front().leaf.trivial;
  }

  // Dlop-operand factory for the runner's operand resolution.
  static std::shared_ptr<Bundle> make_const(const Dlop& v, upass::Kind k) {
    auto b          = std::make_shared<Bundle>("");
    b->scalar_      = Entry(false, v);
    b->scalar_.kind = k;
    b->has_scalar_  = true;
    return b;
  }

  // ── indexed (seg-path) data API — the migration target ────────────────────
  [[nodiscard]] bool                    has_trivial(Path path) const;
  [[nodiscard]] const Entry&            get_entry(Path path) const;
  [[nodiscard]] bool                    has_bundle(Path path) const;
  [[nodiscard]] std::shared_ptr<Bundle> get_bundle(Path path) const;
  void                                  set(Path path, const std::shared_ptr<Bundle const>& tup);
  void                                  set(Path path, Entry entry);

  [[nodiscard]] const Dlop& get_trivial(Path path) const { return get_entry(path).trivial; }

  // The LONE non-attr entry's value regardless of depth (a 1-element
  // tuple-of-tuple flattens); invalid when not single-entry.
  [[nodiscard]] const Dlop& lone_trivial() const;

  // A VALUE write preserves the entry's typed facts (kind, declared envelope,
  // comptime, mode): only the value slice and mutability change.
  Entry value_entry(Path path, bool immutable_flag, const Dlop& trivial) const {
    Entry e     = get_entry(path);  // copies the facts; invalid sentinel when missing
    e.trivial   = trivial;
    e.immutable = immutable_flag;
    e.bw_max    = invalid_lconst;  // derived range describes the old value
    e.bw_min    = invalid_lconst;
    return e;
  }
  void set(Path path, const Dlop& trivial) { set(path, value_entry(path, false, trivial)); }
  void var(Path path, const Dlop& trivial) {
    I(!immutable);
    set(path, value_entry(path, false, trivial));
  }
  void set(const Dlop& trivial) {  // clear everything that is not pos-0; set pos-0
    const Seg s = bundle_path::make_unnamed(0);
    set(Path(&s, 1), value_entry(Path(&s, 1), false, trivial));
  }

  // ── attribute access (rare residual attrs; battr) ─────────────────────────
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

  // Pyrope `a ++ b`: merge tup into this. A colliding named field merges only
  // when one side is nil, both carry the same comptime value, or both are
  // sub-tuples (recursive). Other overlap → false with *conflict set; `this` may
  // be left partial, so discard on failure.
  bool concat(const std::shared_ptr<Bundle const>& tup, std::string* conflict = nullptr);

  bool is_empty() const { return !has_scalar_ && fields_.empty() && attr_map.empty(); }
  bool is_scalar() const { return has_scalar_ || fields_.size() <= 1; }
  bool is_trivial_scalar() const;

  // Counts by distinct top-level field.
  size_t unnamed_top_count() const;
  size_t named_top_count() const;
  bool   has_named_top() const { return named_top_count() > 0; }
  bool   has_unnamed_top() const { return unnamed_top_count() > 0; }

  // Sorted iteration views.
  Entries_view    entries() const;
  Entries_view    non_attr_entries() const;
  const Attr_map& get_attrs() const { return attr_map; }
  Top_levels_view top_levels() const;
  Entries_view    sticky_attributes() const;

  bool has_top_named(std::string_view name) const;
  bool has_top_unnamed(int pos) const;

  void dump(std::string_view name = "?") const;

  // Dotted-key string helpers (delegate to bundle_key; parse SOURCE notation).
  static std::string_view get_last_level(std::string_view key) { return bundle_key::get_last_level(key); }
  static std::string_view get_all_but_last_level(std::string_view key) { return bundle_key::get_all_but_last_level(key); }
  static std::string_view get_all_but_first_level(std::string_view key) { return bundle_key::get_all_but_first_level(key); }
  static std::string_view get_first_level(std::string_view key) { return bundle_key::get_first_level(key); }
  static std::string_view get_first_level_name(std::string_view key) { return bundle_key::get_first_level_name(key); }
  static bool             is_single_level(std::string_view key) { return bundle_key::is_single_level(key); }
};
