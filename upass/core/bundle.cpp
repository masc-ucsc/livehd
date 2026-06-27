//  This file is distributed under the BSD 3-Clause License. See LICENSE for
//  details.

#include "bundle.hpp"

#include <string>
#include <unordered_map>

#include "likely.hpp"
#include "lnast.hpp"
#include "str_tools.hpp"

using namespace std::literals;
using bundle_path::is_named;
using bundle_path::is_unnamed;
using bundle_path::make_unnamed;
using bundle_path::unnamed_pos;

// ─────────────────────────────────────────────────────────────────────────────
// Attr-map ordering (data is nested+indexed now; attrs stay string-keyed).
// Per dotted segment: attrs ('_'…) < named (alphabetical) < unnamed (numeric).
// ─────────────────────────────────────────────────────────────────────────────
static std::string_view first_segment_view(std::string_view k) {
  auto dot = k.find('.');
  return dot == std::string_view::npos ? k : k.substr(0, dot);
}

static int segment_category(std::string_view seg) {
  if (seg.size() >= 2 && seg[0] == '_' && seg[1] == '_') {
    return 0;
  }
  if (!seg.empty() && std::isdigit(static_cast<unsigned char>(seg.front()))) {
    return 2;  // unnamed (decimal index)
  }
  return 1;  // named
}

bool Bundle::Canonical_less::operator()(std::string_view a, std::string_view b) const {
  while (true) {
    auto a_dot = a.find('.');
    auto b_dot = b.find('.');
    auto a_seg = a.substr(0, a_dot);
    auto b_seg = b.substr(0, b_dot);
    int  ca    = segment_category(a_seg);
    int  cb    = segment_category(b_seg);
    if (ca != cb) {
      return ca < cb;
    }
    if (ca == 2) {
      int ai = str_tools::to_i(a_seg);
      int bi = str_tools::to_i(b_seg);
      if (ai != bi) {
        return ai < bi;
      }
    } else if (!(a_seg == b_seg)) {
      return (a_seg < b_seg);
    }
    if (a_dot == std::string_view::npos && b_dot == std::string_view::npos) {
      return false;
    }
    if (a_dot == std::string_view::npos) {
      return true;
    }
    if (b_dot == std::string_view::npos) {
      return false;
    }
    a.remove_prefix(a_dot + 1);
    b.remove_prefix(b_dot + 1);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// seg helpers
// ─────────────────────────────────────────────────────────────────────────────
std::string Bundle::seg_text(Seg s) {
  if (is_unnamed(s)) {
    return std::to_string(unnamed_pos(s));
  }
  return std::string(Lnast::active_name_pool()->resolve(s));
}

static std::string path_to_string(Bundle::Path path) {
  std::string out;
  for (size_t i = 0; i < path.size(); ++i) {
    if (i != 0) {
      out.push_back('.');
    }
    out += Bundle::seg_text(path[i]);
  }
  return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// navigation primitives
// ─────────────────────────────────────────────────────────────────────────────
const Bundle::Field* Bundle::find_field(Seg s) const {
  for (const auto& f : fields_) {
    if (f.seg == s) {
      return &f;
    }
  }
  return nullptr;
}
Bundle::Field* Bundle::find_field(Seg s) {
  for (auto& f : fields_) {
    if (f.seg == s) {
      return &f;
    }
  }
  return nullptr;
}

void Bundle::spill_scalar() {
  if (!has_scalar_) {
    return;
  }
  has_scalar_ = false;
  fields_.push_back(Field{make_unnamed(0), scalar_, nullptr});
  scalar_ = Entry();
}

std::shared_ptr<Bundle> Bundle::clone() const {
  auto b          = std::make_shared<Bundle>();
  b->scalar_      = scalar_;
  b->has_scalar_  = has_scalar_;
  b->fields_.reserve(fields_.size());
  for (const auto& f : fields_) {
    b->fields_.push_back(Field{f.seg, f.leaf, f.sub ? f.sub->clone() : nullptr});
  }
  b->attr_map      = attr_map;
  b->mode_         = mode_;
  b->type_name_id_ = type_name_id_;
  b->value_kind_   = value_kind_;
  b->correct       = correct;
  b->immutable     = immutable;
  return b;
}

const Bundle* Bundle::descend(Path path, Seg* last) const {
  if (path.empty()) {
    return nullptr;
  }
  const Bundle* cur = this;
  for (size_t i = 0; i + 1 < path.size(); ++i) {
    if (cur->has_scalar_) {
      return nullptr;  // a bare scalar has no sub-fields
    }
    const Field* f = cur->find_field(path[i]);
    if (f == nullptr || !f->sub) {
      return nullptr;  // missing, or a leaf we cannot descend into
    }
    cur = f->sub.get();
  }
  *last = path.back();
  return cur;
}

Bundle* Bundle::descend_or_create(Path path, Seg* last) {
  Bundle* cur = this;
  for (size_t i = 0; i + 1 < path.size(); ++i) {
    cur->spill_scalar();
    Field* f = cur->find_field(path[i]);
    if (f == nullptr) {
      cur->fields_.push_back(Field{path[i], Entry(), std::make_shared<Bundle>()});
      f = &cur->fields_.back();
    } else if (!f->sub) {
      // promote a scalar leaf to a sub-bundle holding the old value at pos-0
      auto sub          = std::make_shared<Bundle>();
      sub->scalar_      = f->leaf;
      sub->has_scalar_  = true;
      f->sub            = std::move(sub);
      f->leaf           = Entry();
    }
    cur = f->sub.get();
  }
  *last = path.back();
  return cur;
}

// ─────────────────────────────────────────────────────────────────────────────
// reads
// ─────────────────────────────────────────────────────────────────────────────
bool Bundle::has_trivial(Path path) const {
  if (path.empty()) {
    return false;
  }
  if (has_scalar_) {
    return path.size() == 1 && path[0] == make_unnamed(0);
  }
  Seg           last  = 0;
  const Bundle* owner = descend(path, &last);
  if (owner == nullptr) {
    return false;
  }
  const Field* f = owner->find_field(last);
  return f != nullptr && !f->sub;  // a leaf exists (sub-bundle is not a trivial)
}

const Bundle::Entry& Bundle::get_entry(Path path) const {
  static Entry invalid(true, invalid_lconst);
  if (path.empty()) {
    return invalid;
  }
  if (has_scalar_) {
    return (path.size() == 1 && path[0] == make_unnamed(0)) ? scalar_ : invalid;
  }
  Seg           last  = 0;
  const Bundle* owner = descend(path, &last);
  if (owner == nullptr) {
    return invalid;
  }
  const Field* f = owner->find_field(last);
  if (f == nullptr || f->sub) {
    return invalid;
  }
  return f->leaf;
}

const Dlop& Bundle::lone_trivial() const {
  if (has_scalar_) {
    return scalar_.trivial;
  }
  if (fields_.empty()) {
    return invalid_lconst;
  }
  I(fields_.size() == 1);
  const Field* f = &fields_.front();
  while (f->sub) {  // a 1-element tuple-of-tuple flattens to its lone leaf
    I(f->sub->fields_.size() == 1 || f->sub->has_scalar_);
    if (f->sub->has_scalar_) {
      return f->sub->scalar_.trivial;
    }
    f = &f->sub->fields_.front();
  }
  return f->leaf.trivial;
}

bool Bundle::has_bundle(Path path) const {
  if (path.empty()) {
    return false;
  }
  if (has_scalar_) {
    return path.size() == 1 && path[0] == make_unnamed(0);
  }
  Seg           last  = 0;
  const Bundle* owner = descend(path, &last);
  if (owner != nullptr && owner->find_field(last) != nullptr) {
    return true;
  }
  // A field that carries only attributes (no data leaf) still "has" a bundle.
  const std::string dotted = path_to_string(path);
  auto              it     = attr_map.lower_bound(dotted);
  if (it != attr_map.end() && it->first == dotted) {
    ++it;  // exact = whole-bundle attr, not a field
  }
  if (it != attr_map.end()) {
    const auto e_pos = bundle_key::match_first_partial(dotted, it->first);
    if (e_pos != 0 && e_pos < it->first.size()) {
      return true;
    }
  }
  return false;
}

std::shared_ptr<Bundle> Bundle::get_bundle(Path path) const {
  if (path.empty()) {
    return clone();
  }
  std::shared_ptr<Bundle> result;
  if (has_scalar_) {
    if (path.size() == 1 && path[0] == make_unnamed(0)) {
      result             = std::make_shared<Bundle>();
      result->scalar_    = scalar_;
      result->has_scalar_ = true;
    }
  } else {
    Seg           last  = 0;
    const Bundle* owner = descend(path, &last);
    if (owner != nullptr) {
      const Field* f = owner->find_field(last);
      if (f != nullptr) {
        if (f->sub) {
          result = f->sub->clone();
        } else {
          result             = std::make_shared<Bundle>();
          result->scalar_    = f->leaf;
          result->has_scalar_ = true;
        }
      }
    }
  }

  // Attribute leaves under the selected path travel with the sub-bundle:
  // "path.attr" → the sub's "attr"; an exact-key match is a whole-bundle attr
  // of THIS bundle, not the field, so it is skipped.
  const std::string dotted = path_to_string(path);
  for (auto it = attr_map.lower_bound(dotted); it != attr_map.end(); ++it) {
    const auto e_pos = bundle_key::match_first_partial(dotted, it->first);
    if (e_pos == 0) {
      break;
    }
    if (e_pos >= it->first.size()) {
      continue;  // exact = whole-bundle attr of this bundle
    }
    if (!result) {
      result = std::make_shared<Bundle>();
    }
    result->attr_map.insert_or_assign(std::string(std::string_view(it->first).substr(e_pos)), it->second);
  }

  if (result && !is_correct()) {
    result->set_issue();
  }
  return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// writes
// ─────────────────────────────────────────────────────────────────────────────
void Bundle::set(Path path, Entry entry) {
  I(!path.empty());
  // Bare-scalar fast path: a single unnamed-pos-0 write into a leaf-less bundle.
  if (path.size() == 1 && path[0] == make_unnamed(0) && fields_.empty()) {
    scalar_     = std::move(entry);
    has_scalar_ = true;
    return;
  }
  spill_scalar();
  Seg     last  = 0;
  Bundle* owner = descend_or_create(path, &last);
  owner->spill_scalar();
  Field* f = owner->find_field(last);
  if (f != nullptr) {
    f->leaf = std::move(entry);
    f->sub  = nullptr;  // set replaces any existing subtree
  } else {
    owner->fields_.push_back(Field{last, std::move(entry), nullptr});
  }
}

void Bundle::set(Path path, const std::shared_ptr<Bundle const>& tup) {
  I(!path.empty());
  if (tup == nullptr) {
    set(path, value_entry(path, false, invalid_lconst));
    return;
  }
  correct = correct && tup->correct;

  const std::string dotted = path_to_string(path);

  // A BARE scalar (the inline scalar, or a lone UNNAMED pos-0 leaf) collapses to
  // a leaf at `path` — the implicit "0" is dropped. A single NAMED field (e.g.
  // `(zero=0)`) is NOT a bare scalar: it must stay a sub-bundle so the field name
  // survives (else `a ++ b` over `lane=(zero=0)` would lose `zero`).
  const bool bare_scalar
      = tup->has_scalar_
        || (tup->fields_.size() == 1 && tup->fields_.front().seg == make_unnamed(0) && !tup->fields_.front().sub);

  if (bare_scalar) {
    Entry e;
    if (tup->has_scalar_) {
      e = tup->scalar_;
    } else if (!tup->fields_.empty()) {
      e = tup->fields_.front().leaf;
    }
    set(path, std::move(e));
  } else {
    // Non-scalar tup → the field at `path` becomes a deep copy of tup.
    spill_scalar();
    Seg     last  = 0;
    Bundle* owner = descend_or_create(path, &last);
    owner->spill_scalar();
    if (Field* f = owner->find_field(last); f != nullptr) {
      f->leaf = Entry();
      f->sub  = tup->clone();
    } else {
      owner->fields_.push_back(Field{last, Entry(), tup->clone()});
    }
  }

  // Attribute leaves ride under the destination path.
  for (const auto& [k, e] : tup->attr_map) {
    attr_map.insert_or_assign(absl::StrCat(dotted, ".", k), e);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// attrs
// ─────────────────────────────────────────────────────────────────────────────
const Bundle::Entry& Bundle::attr_entry(std::string_view attr_key) const {
  const auto it = attr_map.find(attr_key);
  if (it == attr_map.end()) {
    static Entry invalid(true, invalid_lconst);
    return invalid;
  }
  return it->second;
}

// ─────────────────────────────────────────────────────────────────────────────
// concat (join) — field-list merge (03-bundle.md)
// ─────────────────────────────────────────────────────────────────────────────
bool Bundle::concat(const std::shared_ptr<Bundle const>& tup, std::string* conflict) {
  spill_scalar();

  // Effective top-level fields of tup (a bare scalar reads as one unnamed-0 leaf).
  std::vector<Field> rhs_fields;
  if (tup->has_scalar_) {
    rhs_fields.push_back(Field{make_unnamed(0), tup->scalar_, nullptr});
  } else {
    rhs_fields = tup->fields_;  // shallow copy of the field list (subs shared until cloned below)
  }

  // Next free unnamed slot on the LHS.
  int next_unnamed = 0;
  for (const auto& f : fields_) {
    if (is_unnamed(f.seg)) {
      next_unnamed = std::max(next_unnamed, unnamed_pos(f.seg) + 1);
    }
  }

  // old first-segment text → landed first-segment text (for attr relocation).
  std::unordered_map<std::string, std::string> seg_landed;

  for (const auto& g : rhs_fields) {
    if (is_named(g.seg)) {
      const bool g_scalar  = !g.sub;
      if (g_scalar && g.leaf.trivial.is_nil()) {
        continue;  // a nil scalar from the RHS drops itself
      }
      Field* lf = find_field(g.seg);
      if (lf == nullptr) {
        fields_.push_back(Field{g.seg, g.leaf, g.sub ? g.sub->clone() : nullptr});
        seg_landed.emplace(seg_text(g.seg), seg_text(g.seg));
        continue;
      }
      const bool lhs_scalar = !lf->sub;
      if (lhs_scalar && lf->leaf.trivial.is_nil()) {
        lf->leaf = g.leaf;
        lf->sub  = g.sub ? g.sub->clone() : nullptr;
        seg_landed.emplace(seg_text(g.seg), seg_text(g.seg));
        continue;
      }
      if (lhs_scalar && g_scalar) {
        const auto& lv = lf->leaf.trivial;
        const auto& rv = g.leaf.trivial;
        if (!lv.is_invalid() && !rv.is_invalid() && lv.is_known_eq(rv)) {
          lf->leaf = g.leaf;  // provably equal — rhs wins
          seg_landed.emplace(seg_text(g.seg), seg_text(g.seg));
          continue;
        }
        if (conflict != nullptr) {
          *conflict = seg_text(g.seg);
        }
        return false;
      }
      if (!lhs_scalar && !g_scalar) {
        std::string sub_conflict;
        if (!lf->sub->concat(g.sub, &sub_conflict)) {
          if (conflict != nullptr) {
            *conflict = absl::StrCat(seg_text(g.seg), ".", sub_conflict);
          }
          return false;
        }
        seg_landed.emplace(seg_text(g.seg), seg_text(g.seg));
        continue;
      }
      // scalar vs sub-tuple — no merge is defined
      if (conflict != nullptr) {
        *conflict = seg_text(g.seg);
      }
      return false;
    }

    // unnamed: renumber at the next free slot
    const Seg new_seg = make_unnamed(next_unnamed);
    fields_.push_back(Field{new_seg, g.leaf, g.sub ? g.sub->clone() : nullptr});
    seg_landed.emplace(seg_text(g.seg), seg_text(new_seg));
    ++next_unnamed;
  }

  // Attribute merge — rhs wins. Whole-bundle attrs copy as-is; per-field attrs
  // follow their field's landing spot.
  for (const auto& [k, e] : tup->attr_map) {
    if (is_single_level(k)) {
      attr_map.insert_or_assign(k, e);
      continue;
    }
    const auto first = first_segment_view(k);
    const auto rest  = std::string_view(k).substr(first.size());  // incl. leading '.'
    const auto it    = seg_landed.find(std::string(first));
    if (it == seg_landed.end()) {
      attr_map.insert_or_assign(k, e);
    } else {
      attr_map.insert_or_assign(absl::StrCat(it->second, rest), e);
    }
  }

  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// shape predicates / counts
// ─────────────────────────────────────────────────────────────────────────────
bool Bundle::is_trivial_scalar() const {
  if (!has_scalar_) {
    for (const auto& f : fields_) {
      if (f.seg != make_unnamed(0) || f.sub) {
        return false;
      }
    }
  }
  for (const auto& e : attr_map) {
    if (!is_single_level(e.first)) {
      return false;
    }
  }
  return true;
}

size_t Bundle::named_top_count() const {
  if (has_scalar_) {
    return 0;
  }
  size_t n = 0;
  for (const auto& f : fields_) {
    if (is_named(f.seg)) {
      ++n;
    }
  }
  return n;
}

size_t Bundle::unnamed_top_count() const {
  if (has_scalar_) {
    return 1;
  }
  size_t n = 0;
  for (const auto& f : fields_) {
    if (is_unnamed(f.seg)) {
      ++n;
    }
  }
  return n;
}

bool Bundle::has_top_named(std::string_view name) const {
  if (has_scalar_) {
    return false;
  }
  const int32_t id = Lnast::active_name_pool()->intern(name);
  return find_field(id) != nullptr;
}

bool Bundle::has_top_unnamed(int pos) const {
  if (has_scalar_) {
    return pos == 0;
  }
  return find_field(make_unnamed(pos)) != nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// iteration views
// ─────────────────────────────────────────────────────────────────────────────
// Fields in CANONICAL iteration order: named (alphabetical by resolved name)
// before unnamed (numeric by position). Storage stays insertion-order (fast
// linear scan); only iteration is sorted, so two equal tuples built in different
// field orders flatten identically (tuple equality zips positionally).
static std::vector<const Bundle::Field*> sorted_fields(const std::vector<Bundle::Field>& fields) {
  std::vector<std::pair<std::string, const Bundle::Field*>> keyed;
  keyed.reserve(fields.size());
  for (const auto& f : fields) {
    keyed.emplace_back(bundle_path::is_named(f.seg) ? Bundle::seg_text(f.seg) : std::string(), &f);
  }
  std::sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) {
    const bool an = bundle_path::is_named(a.second->seg);
    const bool bn = bundle_path::is_named(b.second->seg);
    if (an != bn) {
      return an;  // named before unnamed
    }
    if (an) {
      return a.first < b.first;  // alphabetical by resolved name
    }
    return bundle_path::unnamed_pos(a.second->seg) < bundle_path::unnamed_pos(b.second->seg);  // numeric
  });
  std::vector<const Bundle::Field*> out;
  out.reserve(keyed.size());
  for (const auto& [k, f] : keyed) {
    out.push_back(f);
  }
  return out;
}

void Bundle::flatten_into(const std::string& prefix, std::vector<Entries_view::Item>& out) const {
  if (has_scalar_) {
    out.emplace_back(prefix.empty() ? std::string("0") : prefix, scalar_);
    return;
  }
  for (const Field* f : sorted_fields(fields_)) {
    std::string key = prefix.empty() ? seg_text(f->seg) : absl::StrCat(prefix, ".", seg_text(f->seg));
    if (f->sub) {
      f->sub->flatten_into(key, out);
    } else {
      out.emplace_back(std::move(key), f->leaf);
    }
  }
}

Bundle::Entries_view Bundle::non_attr_entries() const {
  Entries_view v;
  flatten_into("", v.items_);
  return v;
}

Bundle::Entries_view Bundle::entries() const {
  Entries_view v;
  for (const auto& kv : attr_map) {
    v.items_.emplace_back(kv.first, kv.second);
  }
  flatten_into("", v.items_);
  return v;
}

Bundle::Entries_view Bundle::sticky_attributes() const {
  Entries_view v;
  for (const auto& kv : attr_map) {
    if (battr::is_sticky(get_last_level(kv.first))) {
      v.items_.emplace_back(kv.first, kv.second);
    }
  }
  return v;
}

Bundle::Top_levels_view Bundle::top_levels() const {
  Top_levels_view tv;
  if (has_scalar_) {
    Top_level_entry tl;
    tl.pos        = 0;
    tl.leaf_count = 1;
    tl.scalar     = scalar_.trivial;
    tv.items_.push_back(std::move(tl));
    return tv;
  }
  // The name_ holder strings must outlive the view (Top_level_entry.name is a
  // string_view); they are pool-resolved (stable) for named, and parked here
  // for unnamed decimals.
  for (const Field* f : sorted_fields(fields_)) {
    Top_level_entry tl;
    if (is_named(f->seg)) {
      tl.name = Lnast::active_name_pool()->resolve(f->seg);  // stable pool string
    } else {
      tl.pos = unnamed_pos(f->seg);
    }
    if (f->sub) {
      tl.has_leafs = true;
      // leaf_count = number of data leaves under this top-level
      std::vector<Entries_view::Item> leaves;
      f->sub->flatten_into("", leaves);
      tl.leaf_count = leaves.size();
      if (tl.leaf_count == 1) {
        tl.scalar = leaves.front().second.trivial;
      }
    } else {
      tl.leaf_count = 1;
      tl.scalar     = f->leaf.trivial;
    }
    tv.items_.push_back(std::move(tl));
  }
  return tv;
}

// ─────────────────────────────────────────────────────────────────────────────
// dump
// ─────────────────────────────────────────────────────────────────────────────
void Bundle::dump(std::string_view name) const {
  std::print("bundle_name: {}{}\n", name, correct ? "" : " ISSUES");
  for (const auto& it : attr_map) {
    std::print("  attr:{} trivial:{}\n", it.first, it.second.trivial);
  }
  std::vector<Entries_view::Item> leaves;
  flatten_into("", leaves);
  for (const auto& [k, e] : leaves) {
    std::print("  key:{} trivial:{} {}\n", k, e.trivial, e.immutable ? "let" : "mut");
  }
}
