//  This file is distributed under the BSD 3-Clause License. See LICENSE for
//  details.

#include "bundle.hpp"

#include <set>
#include <unordered_map>
#include <unordered_set>

#include "likely.hpp"
#include "lnast.hpp"
#include "str_tools.hpp"

using namespace std::literals;

// Canonical ordering for stored keys, per dotted segment:
//   named ("name…")         — alphabetical by segment ('_'-named sticky
//                             attrs sort before letter-named ones, so the
//                             sticky run is contiguous in attr_map)
//   unnamed ("0", "1", …)   — by integer value of the segment
//
// segment_category keeps the legacy "__" attribute category (0) so the
// comparator's documented total order is stable for arbitrary strings
// (the ordering unit tests pin it), but post-1b/B no stored key in either
// map ever carries a "__" prefix.
static std::string_view first_segment_view(std::string_view k) {
  auto dot = k.find('.');
  return dot == std::string_view::npos ? k : k.substr(0, dot);
}

static int segment_category(std::string_view seg) {
  if (seg.size() >= 2 && seg[0] == '_' && seg[1] == '_') {
    return 0; // legacy attribute spelling — never stored post-1b/B
  }
  if (!seg.empty() && std::isdigit(static_cast<unsigned char>(seg.front()))) {
    return 2; // unnamed (decimal index)
  }
  return 1; // named
}

// bundle_flat_storage §3 — recursive segment-aware comparator. Walks
// dotted segments left-to-right; at each level uses the category order
// above (attrs < named alpha < unnamed numeric) as a true per-level
// recursive compare suitable for use as a std::map ordering.
bool Bundle::Canonical_less::operator()(std::string_view a,
                                        std::string_view b) const {
  while (true) {
    auto a_dot = a.find('.');
    auto b_dot = b.find('.');
    auto a_seg = a.substr(0, a_dot);
    auto b_seg = b.substr(0, b_dot);
    int ca = segment_category(a_seg);
    int cb = segment_category(b_seg);
    if (ca != cb) {
      return ca < cb;
    }
    if (ca == 2) {
      int ai = str_tools::to_i(a_seg);
      int bi = str_tools::to_i(b_seg);
      if (ai != bi) {
        return ai < bi;
      }
    } else if (a_seg != b_seg) {
      return a_seg < b_seg;
    }
    // Segments equal so far. Recurse / terminate.
    if (a_dot == std::string_view::npos && b_dot == std::string_view::npos) {
      return false; // exactly equal keys
    }
    if (a_dot == std::string_view::npos) {
      return true; // a is a strict prefix → sorts first
    }
    if (b_dot == std::string_view::npos) {
      return false;
    }
    a.remove_prefix(a_dot + 1);
    b.remove_prefix(b_dot + 1);
  }
}

// Both maps are std::map ordered by Canonical_less, so iteration is
// already in canonical order — the views just project entries into the
// Entries_view::Item shape. entries() lists attribute leaves first
// (bare names), then data leaves.
Bundle::Entries_view Bundle::entries() const {
  ensure_view_materialized(); // 1b/E
  Entries_view v;
  v.items_.reserve(attr_map.size() + key_map.size());
  for (const auto &kv : attr_map) {
    v.items_.emplace_back(std::string_view(kv.first), &kv.second);
  }
  for (const auto &kv : key_map) {
    v.items_.emplace_back(std::string_view(kv.first), &kv.second);
  }
  return v;
}

Bundle::Top_levels_view Bundle::top_levels() const {
  ensure_view_materialized(); // 1b/E
  // Walk data entries in canonical order; group contiguous runs by their
  // first segment. Each run becomes one Top_level_entry.
  Top_levels_view tv;
  std::string_view cur_seg;
  for (const auto &[k, e] : key_map) {
    auto seg = first_segment_view(k);
    if (tv.items_.empty() || seg != cur_seg) {
      cur_seg = seg;
      Top_level_entry tl;
      if (segment_category(seg) == 2) {
        tl.pos = str_tools::to_i(seg);
      } else {
        tl.name = seg;
      }
      tv.items_.emplace_back(std::move(tl));
    }
    auto &tl = tv.items_.back();
    ++tl.leaf_count;
    if (k.size() > seg.size()) {
      tl.has_leafs = true;
    }
    if (tl.leaf_count == 1) {
      tl.scalar = e.trivial; // candidate; invalidated below if more leaves follow
    }
  }
  // Collapse rule: scalar valid only when leaf_count == 1.
  for (auto &tl : tv.items_) {
    if (tl.leaf_count != 1) {
      tl.scalar = invalid_lconst;
    }
  }
  return tv;
}

bool Bundle::has_top_named(std::string_view name) const {
  if (has_root_) {
    return false; // sole leaf is the unnamed "0"
  }
  // All keys sharing a first segment form one contiguous run; lower_bound
  // lands at the run's start (a strict prefix sorts before its extensions).
  auto it = key_map.lower_bound(name);
  return it != key_map.end() && first_segment_view(it->first) == name;
}

bool Bundle::has_top_unnamed(int pos) const {
  if (has_root_) {
    return pos == 0; // 1b/E: the inline root is slot 0
  }
  auto key = std::to_string(pos);
  auto it  = key_map.lower_bound(std::string_view{key});
  if (it == key_map.end()) {
    return false;
  }
  auto seg = first_segment_view(it->first);
  return segment_category(seg) == 2 && str_tools::to_i(seg) == pos;
}

size_t Bundle::named_top_count() const {
  if (has_root_) {
    return 0;
  }
  // The map is sorted with contiguous first-segment runs — count run
  // boundaries instead of dedup'ing through a std::set.
  size_t           n = 0;
  std::string_view cur;
  for (const auto &[k, _] : key_map) {
    auto seg = first_segment_view(k);
    if (segment_category(seg) != 1) {
      continue;
    }
    if (n == 0 || seg != cur) {
      cur = seg;
      ++n;
    }
  }
  return n;
}

size_t Bundle::unnamed_top_count() const {
  if (has_root_) {
    return 1; // 1b/E: the inline root is slot 0
  }
  size_t           n = 0;
  std::string_view cur;
  for (const auto &[k, _] : key_map) {
    auto seg = first_segment_view(k);
    if (segment_category(seg) != 2) {
      continue;
    }
    if (n == 0 || seg != cur) {
      cur = seg;
      ++n;
    }
  }
  return n;
}

// normalize_key / match / match_first_partial / match_either_partial and
// the get_*_level* helpers live in lnast/bundle_key.hpp (1b/A) — the hpp
// statics delegate.

void Bundle::del_int(std::string_view key) {
  // Data leaves only (1b/B): attribute leaves live in attr_map and are
  // never deleted by a data write ("set deletes what existed — not
  // attributes").
  if (key.empty()) {
    key_map.clear();
    has_root_ = false;
    return;
  }
  if (has_root_) { // 1b/E: the only data leaf is the inline root ("0")
    if (key == "0") {
      has_root_ = false;
    }
    return;
  }

  // Bounded run erase (1b/C): every key matching `key` fully or as a
  // dot-boundary prefix sits in one contiguous run starting at
  // lower_bound(key).
  auto it = key_map.lower_bound(key);
  while (it != key_map.end() && match_first_partial(key, it->first) != 0) {
    it = key_map.erase(it);
  }
}

const Bundle::Entry &Bundle::get_entry(std::string_view key) const {
  static Entry invalid(true, invalid_lconst);

  if (has_root_) { // 1b/E: the only data leaf is the inline root ("0")
    return key == "0" ? root_ : invalid;
  }
  const auto it = key_map.find(key);
  if (it != key_map.end()) {
    return it->second;
  }

  return invalid;
}

const Dlop &Bundle::lone_trivial() const {
  if (has_root_) {
    return root_.trivial;
  }
  I(key_map.size() <= 1); // only scalars, so trivial can not be defined twice
  if (key_map.empty()) {
    return invalid_lconst;
  }
  return key_map.begin()->second.trivial;
}

bool Bundle::has_trivial(std::string_view key) const {
  if (has_root_) {
    return key == "0";
  }
  return key_map.find(key) != key_map.end();
}

bool Bundle::has_bundle(std::string_view key) const {
  if (key.empty()) {
    return false;
  }
  if (has_root_) { // 1b/E: only the "0" leaf exists (exact hit counts, as before)
    if (key == "0") {
      return true;
    }
  }

  // Bounded probes (1b/C): the run for `key` starts at lower_bound(key).
  {
    auto it = key_map.lower_bound(key);
    if (it != key_map.end() && match_first_partial(key, it->first) != 0) {
      return true;
    }
  }
  // A field that carries only attributes (no data leaf yet) still "has" a
  // bundle — matches the pre-split behavior where attr leaves shared the
  // data keyspace. Exact-key hits are whole-bundle attrs, not fields.
  {
    auto it = attr_map.lower_bound(key);
    if (it != attr_map.end() && it->first == key) {
      ++it; // exact match = whole-bundle attr, not a field
    }
    if (it != attr_map.end()) {
      auto e_pos = match_first_partial(key, it->first);
      if (e_pos != 0 && e_pos < it->first.size()) {
        return true;
      }
    }
  }

  return false;
}

std::shared_ptr<Bundle> Bundle::get_bundle(std::string_view key) const {
  if (key.empty()) {
    return std::make_shared<Bundle>(*this);
  }
  ensure_view_materialized(); // 1b/E: sub-bundle extraction walks the map

  std::shared_ptr<Bundle> tup;
  auto lazy_tup = [&]() -> Bundle & {
    if (!tup) {
      tup = std::make_shared<Bundle>();
    }
    return *tup;
  };

  // Bounded run walks (1b/C): matching keys are contiguous from lower_bound.
  for (auto it = key_map.lower_bound(key); it != key_map.end(); ++it) {
    const auto &e     = *it;
    auto        e_pos = match_first_partial(key, e.first);
    if (e_pos == 0) {
      break;
    }
    GI(e_pos < e.first.size(), e.first[e_pos] != '.'); // . not included

    if (e_pos >= e.first.size()) {
      lazy_tup().key_map.insert_or_assign(std::string("0"), e.second);
    } else {
      auto key2 = e.first.substr(e_pos);
      I(!key2.empty());
      lazy_tup().key_map.insert_or_assign(std::string(key2), e.second);
    }
  }

  // Attribute leaves under the selected path travel with the sub-bundle:
  // "key.attr" becomes the sub's whole-bundle attr "attr"; "key.f.attr"
  // becomes its per-field "f.attr". An exact-key attr match is a
  // whole-bundle attr of THIS bundle, not of the field — skipped.
  for (auto it = attr_map.lower_bound(key); it != attr_map.end(); ++it) {
    const auto &e     = *it;
    auto        e_pos = match_first_partial(key, e.first);
    if (e_pos == 0) {
      break;
    }
    if (e_pos >= e.first.size()) {
      continue;
    }
    lazy_tup().attr_map.insert_or_assign(std::string(e.first.substr(e_pos)), e.second);
  }

  if (tup && !is_correct()) {
    tup->set_issue();
  }

  return tup;
}

std::shared_ptr<Bundle>
Bundle::get_bundle(const std::shared_ptr<Bundle const> &tup) const {
  // create a trivial or sub-bundle with the selected fields
  std::shared_ptr<Bundle> ret_tup;

  tup->ensure_view_materialized(); // 1b/E
  int pos = 0;
  for (const auto &e : tup->key_map) {
    auto field = e.second.trivial.to_field();

    if (!has_trivial(field)) {
      Lnast::info("bundle can not be indexed with key:{} with value {}", e.first, field);
      return nullptr;
    }
    auto entry = get_entry(field);
    if (!ret_tup) {
      ret_tup = std::make_shared<Bundle>();
    }
    ret_tup->key_map.insert_or_assign(std::to_string(pos), entry);
    ++pos;
  }

  if (!tup->is_correct()) {
    ret_tup->set_issue();
  }

  return ret_tup;
}

void Bundle::set(std::string_view key,
                 const std::shared_ptr<Bundle const> &tup) {
  I(!key.empty());

  if (tup == nullptr) {
    set(key, invalid_lconst);
    return;
  }

  correct = correct && tup->correct;

  I(get_last_level(key).substr(0, 2) != "__"); // attr writes go through set_attr

  bool tup_scalar = tup->is_scalar();

  tup->ensure_view_materialized(); // 1b/E
  for (const auto &e : tup->key_map) {
    std::string key2;
    // Remove 0. from tup if tup is scalar
    if (tup_scalar && e.first.front() == '0' &&
        (e.first.size() == 1 || e.first[1] == '.')) {
      if (e.first.size() == 1) {
        key2 = ""; // remove "0"
      } else {
        key2 = e.first.substr(2); // remove "0."
      }
    } else {
      key2 = e.first;
    }

    if (key2.empty()) {
      set(key, e.second);
    } else {
      auto key3 = absl::StrCat(key, ".", key2);
      set(key3, e.second);
    }
  }

  // Attribute leaves ride along under the destination path: the source's
  // whole-bundle attr "x" becomes "key.x"; its per-field "f.x" becomes
  // "key.f.x".
  for (const auto &e : tup->attr_map) {
    attr_map.insert_or_assign(absl::StrCat(key, ".", e.first), e.second);
  }
}

void Bundle::set(std::string_view key_in, const Entry &&entry) {
  I(!key_in.empty());

  // normalize_key asserts the canonical form and otherwise returns the
  // key as-is; producers no longer emit `:N:` annotations.
  auto normalized_key = normalize_key(key_in);
  std::string_view key{normalized_key};
  I(!key.empty());
  I(get_last_level(key).substr(0, 2) != "__"); // attr writes go through set_attr

  // 1b/E — bare-scalar fast path: the "0" leaf of a leaf-less bundle lives
  // in the inline root Entry; no map node, no promotion scan.
  if (key == "0" && key_map.empty()) {
    root_     = entry;
    has_root_ = true;
    return;
  }
  ensure_view_materialized(); // any other data write spills the root first

  del_int(key);

  // Promotion pass: any scalar data entry whose key is a strict prefix of
  // `key` (at a dot boundary) becomes `prefix.0` so the new deeper insert
  // can coexist. The same rename applies to attribute leaves scoped under
  // the promoted path ("f.attr" → "f.0.attr"; whole-bundle attrs are
  // single-level and never promoted). Collect targets first, then rename
  // via extract/reinsert — std::map keys are immutable in place.
  std::vector<std::pair<std::string, std::string>> renames; // {old_key, new_key}
  for (const auto &e : key_map) {
    std::string_view fpart{e.first};
    if (fpart.size() >= key.size()) {
      continue;
    }
    if (key[fpart.size()] == '.' && fpart == key.substr(0, fpart.size())) {
      renames.emplace_back(e.first, absl::StrCat(fpart, ".0"));
    }
  }
  bool fixed_overwritten = false;
  for (auto &[old_key, new_key] : renames) {
    auto node = key_map.extract(old_key);
    I(!node.empty());
    node.key() = new_key;
    // If the promoted key collides with the incoming key, store the new
    // entry and skip the final emplace below.
    if (new_key == key) {
      node.mapped() = entry;
      fixed_overwritten = true;
    }
    key_map.insert(std::move(node));
  }

  std::vector<std::pair<std::string, std::string>> attr_renames;
  for (const auto &e : attr_map) {
    auto fpart = get_all_but_last_level(e.first);
    if (fpart.empty() || fpart.size() >= key.size()) {
      continue;
    }
    if (key[fpart.size()] == '.' && fpart == key.substr(0, fpart.size())) {
      // Rename only when the DATA entry at this prefix was actually promoted
      // above. An attr scoped to a bundle-valued field ("b.fmode" while
      // "b.c"/"b.d" data keys exist) coexists with deeper inserts — renaming
      // it to "b.0.fmode" would orphan the field fact.
      const bool promoted = std::any_of(renames.begin(), renames.end(), [&](const auto &r) { return r.first == fpart; });
      if (!promoted) {
        continue;
      }
      attr_renames.emplace_back(e.first, absl::StrCat(fpart, ".0.", get_last_level(e.first)));
    }
  }
  for (auto &[old_key, new_key] : attr_renames) {
    auto node = attr_map.extract(old_key);
    I(!node.empty());
    node.key() = new_key;
    attr_map.insert(std::move(node));
  }

  if (fixed_overwritten) {
    return;
  }

  I(!has_trivial(key));
  key_map.insert_or_assign(std::string(key), entry);

#ifdef DEBUG_SLOW
  for (const auto &e : key_map) {
    auto lower = get_all_but_last_level(e.first);
    while (!lower.empty()) {
      if (has_trivial(lower)) {
        dump();
        std::print("OOPPPS (bundle is corrupted). Time to debug!!! {} {}\n",
                   e.first, lower);
        exit(-3);
        return;
      }
      lower = get_all_but_last_level(lower);
    }
  }
#endif
}

bool Bundle::concat(const std::shared_ptr<Bundle const> &tup, std::string *conflict) {
  // Pyrope tuple concat (`a ++ b`, 03-bundle.md) in canonical-key form:
  //   - attrs: rhs wins on collision; per-field attrs follow their field's
  //     relocation (same name / renumbered position / recursive merge).
  //   - named: per-name merge — a `nil` scalar from either side yields to
  //     the other; tuple+tuple merges recursively under the same rules;
  //     equal comptime leaf values dedup (rhs entry wins). Any other leaf
  //     overlap is a compile error: return false with *conflict = the
  //     colliding canonical path (callers discard `this` on failure, so a
  //     partial merge is fine).
  //   - unnamed: appended at the next free position (densely renumbered).

  ensure_view_materialized(); // 1b/E: concat grows the bundle
  tup->ensure_view_materialized();

  auto seg = [](std::string_view k) -> std::string_view {
    return first_segment_view(k);
  };
  auto cat = [&](std::string_view k) -> int {
    return segment_category(seg(k));
  };

  // Snapshot LHS shape: names already present and the next free decimal slot.
  std::unordered_set<std::string> lhs_named;
  int next_unnamed = 0;
  for (const auto &e : key_map) {
    auto c = cat(e.first);
    if (c == 1) {
      lhs_named.emplace(std::string(seg(e.first)));
    } else if (c == 2) {
      int n = str_tools::to_i(seg(e.first));
      if (n + 1 > next_unnamed) {
        next_unnamed = n + 1;
      }
    }
  }

  // Group RHS data entries by first segment, preserving first-seen order.
  struct Rhs_group {
    std::string first_seg; // canonical first segment
    int category;          // 1 named / 2 unnamed
    std::vector<std::pair<std::string, Entry>>
        entries; // (suffix incl. leading ".", entry)
  };
  std::vector<Rhs_group> groups;
  std::unordered_map<std::string, size_t> group_idx;
  for (const auto &it : tup->key_map) {
    auto seg_sv = seg(it.first);
    std::string seg_s(seg_sv);
    std::string suffix;
    if (it.first.size() > seg_sv.size()) {
      I(it.first[seg_sv.size()] == '.');
      suffix = it.first.substr(seg_sv.size()); // keep leading '.'
    }
    auto [iter, inserted] = group_idx.try_emplace(seg_s, groups.size());
    if (inserted) {
      groups.push_back({seg_s, segment_category(seg_sv), {}});
    }
    groups[iter->second].entries.emplace_back(std::move(suffix), it.second);
  }

  // Where each RHS top segment landed: same name, "name.K" sub-slot, or a
  // renumbered decimal. Used to relocate the RHS's per-field attrs.
  std::unordered_map<std::string, std::string> seg_landed;

  // Emit an RHS group under a chosen first-segment.
  auto emit_group =
      [&](const std::vector<std::pair<std::string, Entry>> &entries,
          std::string_view new_seg) {
        for (const auto &[suffix, ent] : entries) {
          std::string new_key(new_seg);
          new_key.append(suffix);
          key_map.insert_or_assign(std::move(new_key), ent);
        }
      };

  // Erase every LHS data entry whose first segment equals `s` — one
  // contiguous run starting at lower_bound(s) (1b/C).
  auto erase_top = [&](std::string_view s) {
    auto it = key_map.lower_bound(s);
    while (it != key_map.end() && seg(it->first) == s) {
      it = key_map.erase(it);
    }
  };

  // Top segments merged via tuple+tuple recursion: their RHS per-field attrs
  // were consumed by the recursive concat, so the post-loop attr pass must
  // skip them (a relocated inner slot would otherwise resurrect the old path).
  std::unordered_set<std::string> recursed_segs;

  for (auto &g : groups) {
    if (g.category == 1) { // named
      bool rhs_is_nil_scalar = g.entries.size() == 1 &&
                               g.entries[0].first.empty() &&
                               g.entries[0].second.trivial.is_nil();
      if (rhs_is_nil_scalar) {
        continue; // RHS nil drops itself.
      }
      if (!lhs_named.count(g.first_seg)) {
        emit_group(g.entries, g.first_seg);
        lhs_named.emplace(g.first_seg);
        seg_landed.emplace(g.first_seg, g.first_seg);
        continue;
      }
      // Collision — merge per the leaf rules (03-bundle.md): a nil scalar
      // yields, tuple+tuple recurses, equal comptime leaves dedup, and any
      // other final-field overlap is an error (never accumulate both values).
      const auto lhs_scalar_it = key_map.find(g.first_seg);
      const bool lhs_is_scalar = lhs_scalar_it != key_map.end();
      if (lhs_is_scalar && lhs_scalar_it->second.trivial.is_nil()) {
        erase_top(g.first_seg);
        emit_group(g.entries, g.first_seg);
        seg_landed.emplace(g.first_seg, g.first_seg);
        continue;
      }
      const bool rhs_is_scalar = g.entries.size() == 1 && g.entries[0].first.empty();
      if (lhs_is_scalar && rhs_is_scalar) {
        const auto &lv = lhs_scalar_it->second.trivial;
        const auto &rv = g.entries[0].second.trivial;
        if (!lv.is_invalid() && !rv.is_invalid() && lv.is_known_eq(rv)) {
          erase_top(g.first_seg); // provably the same value — rhs entry wins
          emit_group(g.entries, g.first_seg);
          seg_landed.emplace(g.first_seg, g.first_seg);
          continue;
        }
        if (conflict != nullptr) {
          *conflict = g.first_seg;
        }
        return false;
      }
      if (!lhs_is_scalar && !rhs_is_scalar) {
        // Both sides are sub-tuples: recursive merge under the same rules.
        // Lift each side's entries (and per-field attrs) under this segment
        // into a temp Bundle, concat, and reinstall the merged result.
        Bundle lhs_sub;
        for (auto it = key_map.lower_bound(g.first_seg);
             it != key_map.end() && seg(it->first) == g.first_seg; ++it) {
          lhs_sub.key_map.emplace(it->first.substr(g.first_seg.size() + 1),
                                  it->second);
        }
        for (auto it = attr_map.lower_bound(g.first_seg);
             it != attr_map.end() && seg(it->first) == g.first_seg; ++it) {
          if (it->first.size() > g.first_seg.size()) {
            lhs_sub.attr_map.emplace(it->first.substr(g.first_seg.size() + 1),
                                     it->second);
          }
        }
        auto rhs_sub = std::make_shared<Bundle>();
        for (const auto &[suffix, ent] : g.entries) {
          rhs_sub->key_map.emplace(suffix.substr(1), ent); // drop leading '.'
        }
        for (auto it = tup->attr_map.lower_bound(g.first_seg);
             it != tup->attr_map.end() && seg(it->first) == g.first_seg; ++it) {
          if (it->first.size() > g.first_seg.size()) {
            rhs_sub->attr_map.emplace(it->first.substr(g.first_seg.size() + 1),
                                      it->second);
          }
        }
        std::string sub_conflict;
        if (!lhs_sub.concat(rhs_sub, &sub_conflict)) {
          if (conflict != nullptr) {
            *conflict = absl::StrCat(g.first_seg, ".", sub_conflict);
          }
          return false;
        }
        erase_top(g.first_seg);
        for (auto it = attr_map.lower_bound(g.first_seg);
             it != attr_map.end() && seg(it->first) == g.first_seg;) {
          if (it->first.size() > g.first_seg.size()) {
            it = attr_map.erase(it); // replaced by the merged set below
          } else {
            ++it; // a whole-bundle attr that happens to share the name
          }
        }
        for (const auto &e : lhs_sub.key_map) {
          key_map.insert_or_assign(absl::StrCat(g.first_seg, ".", e.first),
                                   e.second);
        }
        for (const auto &e : lhs_sub.attr_map) {
          attr_map.insert_or_assign(absl::StrCat(g.first_seg, ".", e.first),
                                    e.second);
        }
        recursed_segs.insert(g.first_seg);
        seg_landed.emplace(g.first_seg, g.first_seg);
        continue;
      }
      // A non-nil scalar against a sub-tuple — no merge is defined.
      if (conflict != nullptr) {
        *conflict = g.first_seg;
      }
      return false;
    }

    // category == 2 (unnamed): re-number at the next free slot.
    auto new_seg = std::to_string(next_unnamed);
    emit_group(g.entries, new_seg);
    seg_landed.emplace(g.first_seg, new_seg);
    ++next_unnamed;
  }

  // Attribute merge — rhs wins. Whole-bundle attrs (single-level) copy as
  // they are; per-field attrs follow their field's landing spot. An attr
  // for a field with no RHS data leaf (attr-only field) copies under its
  // original path.
  for (const auto &e : tup->attr_map) {
    if (is_single_level(e.first)) {
      attr_map.insert_or_assign(e.first, e.second);
      continue;
    }
    auto first = seg(e.first);
    if (recursed_segs.count(std::string(first))) {
      continue; // already merged by the recursive tuple+tuple concat
    }
    auto rest = std::string_view(e.first).substr(first.size()); // incl. leading '.'
    auto it = seg_landed.find(std::string(first));
    if (it == seg_landed.end()) {
      attr_map.insert_or_assign(e.first, e.second);
    } else {
      attr_map.insert_or_assign(absl::StrCat(it->second, rest), e.second);
    }
  }

  return true;
}

bool Bundle::concat(const Dlop &trivial) {
  ensure_view_materialized(); // 1b/E
  int next_pos = 0;
  for (const auto &e : key_map) {
    auto seg = first_segment_view(e.first);
    if (segment_category(seg) != 2) {
      dump();
      Lnast::info("can not concat trivial:{} to unordered bundle field {}", trivial, e.first);
      return false;
    }
    int n = str_tools::to_i(seg);
    if (n + 1 > next_pos) {
      next_pos = n + 1;
    }
  }

  key_map.insert_or_assign(std::to_string(next_pos), Entry(false, trivial));
  return true;
}

bool Bundle::is_trivial_scalar() const {
  // A trivial scalar holds at most the single "0" data leaf (the inline
  // root after 1b/E); whole-bundle attrs (single-level keys) don't break
  // trivial-ness, a per-field attr does.
  if (!has_root_) {
    for (const auto &e : key_map) {
      if (e.first != "0") {
        return false;
      }
    }
  }
  for (const auto &e : attr_map) {
    if (!is_single_level(e.first)) {
      return false;
    }
  }
  return true;
}

void Bundle::dump(std::string_view name) const {
  // The bundle no longer stores a name; the caller supplies one for
  // the printout (the table knows the binding's name).
  std::print("bundle_name: {}{}\n", name, correct ? "" : " ISSUES");

  for (const auto &it : attr_map) {
    std::print("  attr:{} trivial:{}\n", it.first, it.second.trivial);
  }
  if (has_root_) {
    std::print("  key:0 trivial:{} {} (inline root)\n", root_.trivial, root_.immutable ? "let" : "mut");
  }
  for (const auto &it : key_map) {
    std::print("  key:{} trivial:{} {}\n", it.first, it.second.trivial,
               it.second.immutable ? "let" : "mut");
  }
}
