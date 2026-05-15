//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "bundle.hpp"

#include <algorithm>
#include <format>
#include <iostream>

#include "likely.hpp"
#include "lnast.hpp"
#include "str_tools.hpp"

using namespace std::literals;

// Canonical ordering for keys stored after normalize_key():
//   1. attributes ("__name…") — alphabetical by first segment
//   2. named ("name…")         — alphabetical by first segment
//   3. unnamed ("0", "1", …)   — by integer value of first segment
//
// Ties broken by full-key lexicographic compare (matters for nested paths
// like "foo.bar.__bits" vs "foo.baz").
static std::string_view first_segment_view(std::string_view k) {
  auto dot = k.find('.');
  return dot == std::string_view::npos ? k : k.substr(0, dot);
}

static int segment_category(std::string_view seg) {
  if (seg.size() >= 2 && seg[0] == '_' && seg[1] == '_') {
    return 0;  // attribute
  }
  if (!seg.empty() && std::isdigit(static_cast<unsigned char>(seg.front()))) {
    return 2;  // unnamed (decimal index)
  }
  return 1;  // named
}

static bool canonical_compare(const std::pair<std::string, Bundle::Entry>& lhs, const std::pair<std::string, Bundle::Entry>& rhs) {
  auto l_seg = first_segment_view(lhs.first);
  auto r_seg = first_segment_view(rhs.first);
  auto l_cat = segment_category(l_seg);
  auto r_cat = segment_category(r_seg);
  if (l_cat != r_cat) {
    return l_cat < r_cat;
  }
  if (l_cat == 2) {
    int li = str_tools::to_i(l_seg);
    int ri = str_tools::to_i(r_seg);
    if (li != ri) {
      return li < ri;
    }
  } else if (l_seg != r_seg) {
    return l_seg < r_seg;
  }
  return lhs.first < rhs.first;
}

void Bundle::rebuild_indices() const {
  // bundle_sorted §3: partition key_map into the three categorical
  // containers. attrs_ and named_ hold full keys (alphabetical); unnamed_
  // is an int-keyed map of per-position inner maps so the top-level
  // unnamed-position count is `unnamed_.size()` in O(1) and `tup.N` lookup
  // is O(log K + log E) where K = distinct unnamed positions and E =
  // entries under that position.
  attrs_.clear();
  named_.clear();
  unnamed_.clear();
  for (const auto& [k, v] : key_map) {
    auto seg = first_segment_view(k);
    auto cat = segment_category(seg);
    if (cat == 0) {
      attrs_.emplace(k, v);
    } else if (cat == 1) {
      named_.emplace(k, v);
    } else {
      int idx = str_tools::to_i(seg);
      unnamed_[idx].emplace(k, v);
    }
  }
}

std::string Bundle::normalize_key(std::string_view key) {
  // Post-PR3 (bundle_sorted plan §8): legacy `:N:name` / `:N:` producers
  // have all been migrated to canonical form (bare name / bare decimal
  // index). Any `:` at the start of a key segment is now an error —
  // surface it via I() so a regression is loud rather than papered over.
#ifndef NDEBUG
  size_t scan_start = 0;
  while (scan_start <= key.size()) {
    auto end = key.find('.', scan_start);
    if (end == std::string_view::npos) {
      end = key.size();
    }
    auto seg = key.substr(scan_start, end - scan_start);
    I(seg.empty() || seg.front() != ':', "legacy :N: key encoding reached Bundle; producer not migrated");
    if (end == key.size()) {
      break;
    }
    scan_start = end + 1;
  }
#endif
  return std::string(key);
}

bool Bundle::match(std::string_view a, std::string_view b) {
  // Post-bundle_sorted (Decision 1): keys are canonical — bare name,
  // decimal index, or attribute string — with no `:N:` prefix. Equality
  // reduces to plain string compare. The legacy `":0:" ↔ "0"` cross-
  // fallback that let `tup.0` find the first named slot is gone.
  return a == b;
}

size_t Bundle::match_first_partial(std::string_view a, std::string_view b) {
  // Canonical keys (no `:N:`): a is a "partial prefix match" of b iff
  // a == b (full match — return a.size()) or b starts with a followed by
  // a `.` segment separator (prefix match — return a.size()+1 to skip
  // the dot). Otherwise 0.
  if (a == b) {
    return a.size();
  }
  if (b.size() > a.size() && b[a.size()] == '.' && b.starts_with(a)) {
    return a.size() + 1;
  }
  return 0;
}

bool Bundle::match_either_partial(std::string_view a, std::string_view b) {
  // Canonical keys: true iff one is a prefix of the other at a segment
  // boundary (a == b, b == a + ".x…", or a == b + ".x…").
  if (a == b) {
    return true;
  }
  if (a.size() < b.size() && b[a.size()] == '.' && b.starts_with(a)) {
    return true;
  }
  if (b.size() < a.size() && a[b.size()] == '.' && a.starts_with(b)) {
    return true;
  }
  return false;
}

void Bundle::add_int(std::string_view key, const std::shared_ptr<Bundle const> tup) {
  I(!key.empty());
  if (tup->is_scalar()) {
    I(!has_trivial(key));  // It was deleted before
    key_map.emplace_back(key, Entry(tup->is_immutable(), tup->get_trivial()));
    rebuild_indices();
    return;
  }

  bool root = is_root_attribute(key);
  for (const auto& ent : tup->key_map) {
    if (root) {
      key_map.emplace_back(absl::StrCat(ent.first, ".", key), ent.second);
    } else {
      key_map.emplace_back(absl::StrCat(key, ".", ent.first), ent.second);
    }
  }
  rebuild_indices();
}

void Bundle::del_int(std::string_view key) {
  if (key.empty()) {
    key_map.clear();
    rebuild_indices();
    return;
  }

#if 0
  if (is_root_attribute(key)) {
    return;
  }
#endif

  Key_map_type new_map;

  bool is_attr_key = is_root_attribute(key);

  for (const auto& e : key_map) {
    if (e.first.empty()) {
      if (is_attr_key) {
        new_map.emplace_back(std::move(e));
      }
      continue;  // "" keys must be gone by now
    }

    auto e_pos = match_first_partial(key, e.first);
    if (e_pos == 0) {
      new_map.emplace_back(std::move(e));
      continue;
    }
    if (e_pos >= e.first.size()) {
      continue;  // full match?
    }

    I(e.first[e_pos] != '.');  // not . included

    auto sub_name = e.first.substr(e_pos);
    if (sub_name.substr(0, 2) == "__" && sub_name[3] != '_') {
      new_map.emplace_back(std::move(e));
      continue;  // Keep the attributes
    }
  }

  key_map.swap(new_map);
  rebuild_indices();
}

int Bundle::get_first_level_pos(std::string_view key) {
  // Canonical keys: unnamed entries are bare decimals (`"0"`, `"1"`, …).
  // Returns -1 for empty, named, or attribute keys.
  if (key.empty() || !std::isdigit(static_cast<unsigned char>(key.front()))) {
    return -1;
  }
  return str_tools::to_i(key);
}

std::string_view Bundle::get_first_level(std::string_view key) {
  auto dot_pos = key.find('.');
  if (dot_pos == std::string::npos) {
    return key;
  }

  return key.substr(0, dot_pos);
}

std::string_view Bundle::get_first_level_name(std::string_view key) {
  // Canonical keys: no `:N:` prefix to strip. First-level "name" is the
  // first segment (including decimal indices for unnamed entries — callers
  // that need to distinguish named-vs-unnamed use get_first_level_pos or
  // check the first character).
  auto dot_pos = key.find('.');
  if (dot_pos == std::string::npos) {
    return key;
  }
  return key.substr(0, dot_pos);
}

std::string_view Bundle::get_last_level(std::string_view key) {
  auto n = key.rfind('.');
  if (n == std::string::npos) {
    return key;
  }

  I(n != 0);  // name can not start with a .
  return key.substr(n + 1);
}

std::string_view Bundle::get_all_but_last_level(std::string_view key) {
  auto n = key.rfind('.');
  if (n != std::string::npos) {
    return key.substr(0, n);
  }

  return std::string_view("");
}

std::string_view Bundle::get_all_but_first_level(std::string_view key) {
  auto n = key.find('.');
  if (n != std::string::npos) {
    return key.substr(n + 1);
  }

  if (key.front() == '$' || key.front() == '%' || key.front() == '#') {
    return key.substr(1);
  }

  return std::string_view("");  // empty if no dot left
}

std::string Bundle::learn_fix(std::string_view key_sv) {
  // Canonical keys carry no `:N:` annotations, so there is nothing to
  // swap or "learn" between the lookup key and the stored keys. The
  // function survives as a no-op while callers continue to refer to it;
  // the heavy learn_fix_int state machine is gone.
  return std::string(key_sv);
}

const Bundle::Entry& Bundle::get_entry(std::string_view key) const {
  for (const auto& e : key_map) {
    if (match(e.first, key)) {
      return e.second;
    }
  }

  static Entry invalid(true, invalid_lconst);

  return invalid;
}

const Const& Bundle::get_trivial() const {
  int pos = -1;
  for (auto i = 0u; i < key_map.size(); ++i) {
    if (is_attribute(key_map[i].first)) {
      continue;
    }
    I(pos < 0);  // only scalars, so trivial can not be defined already
    pos = i;
  }

  if (pos < 0) {
    return invalid_lconst;
  }

  return key_map[pos].second.trivial;
}

bool Bundle::has_trivial(std::string_view key) const {
  for (const auto& e : key_map) {
    if (match(e.first, key)) {
      return true;
    }
  }
  return false;
}

bool Bundle::has_bundle(std::string_view key) const {
  if (key.empty()) {
    return false;
  }

  I(!key.empty());  // do not call without sub-fields

  for (const auto& e : key_map) {
    auto e_pos = match_first_partial(key, e.first);
    if (e_pos == 0) {
      continue;
    }

    return true;
  }

  return false;
}

std::shared_ptr<Bundle> Bundle::get_bundle(std::string_view key) const {
  if (key.empty()) {
    return std::make_shared<Bundle>(*this);
  }

  I(!key.empty());  // do not call without sub-fields

  std::shared_ptr<Bundle> tup;

  for (const auto& e : key_map) {
    auto e_pos = match_first_partial(key, e.first);
    if (e_pos == 0) {
      continue;
    }
    GI(e_pos < e.first.size(), e.first[e_pos] != '.');  // . not included

    if (!tup) {
      // Canonical keys: no `:N:` swap needed, the lookup key is already
      // in the form it'll appear in the new sub-bundle's name.
      tup = std::make_shared<Bundle>(absl::StrCat(name, ".", key));
    }

    if (e_pos >= e.first.size()) {
      tup->key_map.emplace_back("0", e.second);
    } else {
      auto key2 = e.first.substr(e_pos);
      I(!key2.empty());
      if (is_root_attribute(key2)) {
        tup->key_map.emplace_back(absl::StrCat("0.", key2), e.second);
      } else {
        tup->key_map.emplace_back(key2, e.second);
      }
    }
  }

  if (!is_correct()) {
    tup->set_issue();
  }

  if (tup) {
    tup->rebuild_indices();
  }
  return tup;
}

std::shared_ptr<Bundle> Bundle::get_bundle(const std::shared_ptr<Bundle const>& tup) const {
  // create a trivial or sub-bundle with the selected fields
  std::shared_ptr<Bundle> ret_tup;

  int pos = 0;
  for (const auto& e : tup->key_map) {
    auto field = e.second.trivial.to_field();

    if (!has_trivial(field)) {
      Lnast::info("bundle {} can not be indexed with {} key:{} with value {}", get_name(), tup->get_name(), e.first, field);
      return nullptr;
    }
    auto entry = get_entry(field);
    if (!ret_tup) {
      ret_tup = std::make_shared<Bundle>(get_name());
    }
    ret_tup->key_map.emplace_back(std::to_string(pos), entry);
    ++pos;
  }

  if (!tup->is_correct()) {
    ret_tup->set_issue();
  }

  if (ret_tup) {
    ret_tup->rebuild_indices();
  }
  return ret_tup;
}

void Bundle::set(std::string_view key, const std::shared_ptr<Bundle const>& tup) {
  I(!key.empty());

  if (tup == nullptr) {
    set(key, invalid_lconst);
    return;
  }

  correct = correct && tup->correct;

  I(!is_root_attribute(key));
  I(!is_attribute(key));
  // bool root_key = is_root_attribute(key);

  bool tup_scalar = tup->is_scalar();

  for (const auto& e : tup->key_map) {
    std::string key2;
    // Remove 0. from tup if tup is scalar
    if (tup_scalar && e.first.front() == '0' && (e.first.size() == 1 || e.first[1] == '.')) {
      if (e.first.size() == 1) {
        key2 = "";  // remove "0"
      } else {
        key2 = e.first.substr(2);  // remove "0."
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
}

void Bundle::set(std::string_view key_in, const Entry&& entry) {
  I(!key_in.empty());

  // normalize_key asserts the canonical form and otherwise returns the
  // key as-is; producers no longer emit `:N:` annotations.
  auto             normalized_key = normalize_key(key_in);
  std::string_view key{normalized_key};
  I(!key.empty());

  std::string uncanonical_key{key};
  bool        pending_adjust = false;
  if (is_scalar()) {
    if (key.substr(0, 2) == "__" && key[3] != '_') {  // is_root_attribute BUT not with 0.__xxx
      uncanonical_key = absl::StrCat("0.", key);
    } else {
      pending_adjust = true;
    }
  } else {
    pending_adjust = true;
  }

  // fixed_key can have name too like :0:foo.__max while uncanonical_key is 0.__max
  auto fixed_key = learn_fix(uncanonical_key);

  del_int(fixed_key);

  if (pending_adjust) {
    // NOTE: If the bundle had something like:
    // a.b.foo.__xxx = 1
    // a.c = 2
    // AND if a.c.xx is added. It should become
    // a.b.foo = 1
    // a.c.0 = 2     <---- CHANGE
    // a.c.xx...
    // AND if a.b.foo.1.bar is added. It should become
    // a.b.foo.0.__xxx = 1 <---- CHANGE
    // a.c.0 = 2
    // a.c.xx...
    // a.b.foo.1.bar ....

    std::string_view key_part{fixed_key};
    if (is_attribute(fixed_key)) {
      key_part = get_all_but_last_level(fixed_key);
    }
    for (auto& e : key_map) {
      std::string_view fpart{e.first};
      std::string_view lpart;
      if (is_attribute(e.first)) {
        fpart = get_all_but_last_level(e.first);
        lpart = get_last_level(e.first);
      }

      if (fpart.size() >= key_part.size()) {
        continue;
      }

      // NOTE: full match foo.bar == foo not foo.bar == foo match
      if (key_part[fpart.size()] == '.' && fpart == key_part.substr(0, fpart.size())) {
        if (lpart.empty()) {
          e.first = absl::StrCat(fpart, ".0");
        } else {
          e.first = absl::StrCat(fpart, ".0.", lpart);
        }
        if (e.first == fixed_key) {
          e.second = entry;
          return;
        }
      }
    }
  }

  I(!has_trivial(fixed_key));
  I(!fixed_key.empty());
  key_map.emplace_back(fixed_key, entry);

  rebuild_indices();

#ifdef DEBUG_SLOW
  for (const auto& e : key_map) {
    auto lower = get_all_but_last_level(e.first);
    if (is_attribute(e.first)) {
      lower = get_all_but_last_level(lower);
    }
    while (!lower.empty()) {
      I(!is_attribute(lower));
      if (has_trivial(lower)) {
        dump();
        std::print("OOPPPS (bundle is corrupted). Time to debug!!! {} {}\n", e.first, lower);
        exit(-3);
        return;
      }
      lower = get_all_but_last_level(lower);
    }
  }
#endif
}

bool Bundle::concat(const std::shared_ptr<Bundle const>& tup) {
  // Pyrope tuple concat (`a ++ b`) in canonical-key form:
  //   - attrs: rhs wins on collision (matches the legacy "forward as-is")
  //   - named: per-name merge — `nil` from either side drops; otherwise
  //     LHS scalar+RHS scalar packs into a `(scalar, scalar)` sub-tuple
  //     at the existing slot, and further collisions extend that sub-tuple.
  //   - unnamed: appended at the next free position (densely renumbered).
  //
  // Storage is normalize_key()-canonical: first segment is "__attr", a
  // bare name, or a decimal index — no `:N:name` encoding to parse.

  auto seg = [](std::string_view k) -> std::string_view { return first_segment_view(k); };
  auto cat = [&](std::string_view k) -> int { return segment_category(seg(k)); };

  // Snapshot LHS shape: names already present and the next free decimal slot.
  std::unordered_set<std::string> lhs_named;
  int                             next_unnamed = 0;
  for (const auto& e : key_map) {
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

  // Group RHS entries by first segment, preserving first-seen order.
  struct Rhs_group {
    std::string                                first_seg;  // canonical first segment
    int                                        category;   // 0 attr / 1 named / 2 unnamed
    std::vector<std::pair<std::string, Entry>> entries;    // (suffix incl. leading ".", entry)
  };
  std::vector<Rhs_group>                  groups;
  std::unordered_map<std::string, size_t> group_idx;
  for (const auto& it : tup->key_map) {
    auto        seg_sv = seg(it.first);
    std::string seg_s(seg_sv);
    std::string suffix;
    if (it.first.size() > seg_sv.size()) {
      I(it.first[seg_sv.size()] == '.');
      suffix = it.first.substr(seg_sv.size());  // keep leading '.'
    }
    auto [iter, inserted] = group_idx.try_emplace(seg_s, groups.size());
    if (inserted) {
      groups.push_back({seg_s, segment_category(seg_sv), {}});
    }
    groups[iter->second].entries.emplace_back(std::move(suffix), it.second);
  }

  // Emit an RHS group under a chosen first-segment, optionally inserting
  // an extra ".K" path between the segment and the group's suffix (used to
  // tuck the RHS payload under a fresh sub-tuple slot during name merge).
  auto emit_group
      = [&](const std::vector<std::pair<std::string, Entry>>& entries, std::string_view new_seg, std::string_view extra = {}) {
          for (const auto& [suffix, ent] : entries) {
            std::string new_key(new_seg);
            if (!extra.empty()) {
              new_key += '.';
              new_key.append(extra.data(), extra.size());
            }
            new_key.append(suffix);
            key_map.emplace_back(std::move(new_key), ent);
          }
        };

  // Erase every LHS entry whose first segment equals `s`.
  auto erase_top = [&](std::string_view s) {
    for (auto it = key_map.begin(); it != key_map.end();) {
      if (seg(it->first) == s) {
        it = key_map.erase(it);
      } else {
        ++it;
      }
    }
  };

  // Count next sub-position under an LHS slot (for collision-merge into a
  // growing sub-tuple). A bare-scalar entry counts as 1; existing digit
  // sub-entries pick the max+1.
  auto next_sub_pos = [&](std::string_view lhs_seg) -> int {
    int  sub_max    = -1;
    bool has_scalar = false;
    for (const auto& e : key_map) {
      if (seg(e.first) != lhs_seg) {
        continue;
      }
      if (e.first.size() == lhs_seg.size()) {
        has_scalar = true;
        continue;
      }
      auto rest_start = lhs_seg.size() + 1;
      auto next_dot   = e.first.find('.', rest_start);
      auto frag       = e.first.substr(rest_start, next_dot == std::string::npos ? std::string::npos : next_dot - rest_start);
      if (!frag.empty() && std::isdigit(static_cast<unsigned char>(frag.front()))) {
        int n = str_tools::to_i(frag);
        if (n > sub_max) {
          sub_max = n;
        }
      }
    }
    if (sub_max >= 0) {
      return sub_max + 1;
    }
    return has_scalar ? 1 : 0;
  };

  // Lazily rewrite a colliding LHS scalar entry `seg` into `seg.0`.
  auto promote_scalar_to_sub = [&](std::string_view lhs_seg) {
    for (auto& e : key_map) {
      if (e.first == lhs_seg) {
        e.first = std::string(lhs_seg) + ".0";
        return;
      }
    }
  };

  for (auto& g : groups) {
    if (g.category == 0) {  // attribute: rhs wins
      erase_top(g.first_seg);
      emit_group(g.entries, g.first_seg);
      continue;
    }

    if (g.category == 1) {  // named
      bool rhs_is_nil_scalar = g.entries.size() == 1 && g.entries[0].first.empty() && g.entries[0].second.trivial.is_nil();
      if (rhs_is_nil_scalar) {
        continue;  // RHS nil drops itself.
      }
      if (!lhs_named.count(g.first_seg)) {
        emit_group(g.entries, g.first_seg);
        lhs_named.emplace(g.first_seg);
        continue;
      }
      // Collision. If LHS is a nil placeholder, RHS takes the slot;
      // otherwise pack into a sub-tuple at the existing slot.
      bool lhs_is_nil_scalar = false;
      for (const auto& e : key_map) {
        if (e.first == g.first_seg && e.second.trivial.is_nil()) {
          lhs_is_nil_scalar = true;
          break;
        }
      }
      if (lhs_is_nil_scalar) {
        erase_top(g.first_seg);
        emit_group(g.entries, g.first_seg);
        continue;
      }
      promote_scalar_to_sub(g.first_seg);
      int sub_pos = next_sub_pos(g.first_seg);
      emit_group(g.entries, g.first_seg, std::to_string(sub_pos));
      continue;
    }

    // category == 2 (unnamed): re-number at the next free slot.
    auto new_seg = std::to_string(next_unnamed);
    emit_group(g.entries, new_seg);
    ++next_unnamed;
  }

  rebuild_indices();
  return true;
}

Const Bundle::flatten() const {
  // a_dpin = (tup[0]|(tup[1]<<tup[0].__sbits)|(tup[2]<<(tup[0..1].__sbits)|.....)

  if (!is_correct()) {
    return invalid_lconst;
  }

  std::stable_sort(key_map.begin(), key_map.end(), canonical_compare);

  Const result;
  for (const auto& e : key_map) {
    if (is_attribute(e.first)) {
      continue;
    }

    if (e.second.trivial.is_invalid()) {
      return e.second.trivial;
    }

    auto v = e.second.trivial.lsh_op(result.get_bits());
    result = result.or_op(*v->get_mask_op());
  }

  return result;
}

std::shared_ptr<Bundle> Bundle::create_assign(const std::shared_ptr<Bundle const>& rhs_tup) const {
  (void)rhs_tup;

  I(false);  // FIXME: implement it
  auto new_tup = std::make_shared<Bundle>(get_name());

  return new_tup;
}

std::shared_ptr<Bundle> Bundle::create_assign(const Const& rhs_trivial) const {
  if (rhs_trivial.is_invalid()) {
    return nullptr;
  }

  I(false);
  I(is_correct());

  std::stable_sort(key_map.begin(), key_map.end(), canonical_compare);

  auto tup = std::make_shared<Bundle>(get_name());

#if 0
  {
    Const sbits = Dlop::invalid();
    Const ubits = Dlop::invalid();

    for (const auto i = 0u; i < key_map.size(); ++i) {
      const auto &e = key_map[i];
      if (is_attribute(e.first)) {
        auto attr_txt = get_last_level(e.first);
        if (attr_txt == "__sbits")
          sbits = e.second;
        else if (attr_txt == "__ubits")
          ubits = e.second;

        I(!e.first.empty());
        tup->key_map.emplace_back(e.first, e.second);  // Keep all the attributes

        auto txt = get_all_but_last_level(e.first);

        if (txt == non_attr_field)
          continue;
        if (non_attr_field.empty()) {
          non_attr_field = txt;
          continue;
        }

        // Change in attr fields, so last attr must have an e.first
        // E.g: (..., foo.__ubits=2, bar.__sbits=3,...)
        bool ok = add_pending(rhs_trivial, pending_entries, non_attr_field, ubits, sbits);
        if (!ok)
          return nullptr;

        non_attr_field = e.first;
        ubits     = Dlop::invalid;
        sbits     = Dlop::invalid;
        continue;
      }

      if (!non_attr_field.empty() && non_attr_field != e.first) {
        // last attr did not plain entry
        // E.g: (..., foo.__ubits=2, bar=3,...)
        bool ok = add_pending(rhs_trivial, pending_entries, non_attr_field, ubits, sbits);
        if (!ok)
          return nullptr;

        if (pending_pos >= 0 && non_attr_field == key_map[pending_pos].first)
          pending_pos = -1;

        non_attr_field = "";
        ubits     = Dlop::invalid;
        sbits     = Dlop::invalid;
      }

      if (pending_pos < 0) {
        // E.g: (..., foo.__ubits=2, bar = 3,...)
        I(ubits.is_invalid());
        I(sbits.is_invalid());

        auto v_bits = e.second.get_bits();

        pending_entries.emplace_back(e.first, v_bits);
        continue;
      }

      bool ok = add_pending(rhs_trivial, pending_entries, key_map[pending_pos].first, ubits, sbits);
      if (!ok)
        return nullptr;
      pending_pos = -1;
    }

    if (pending_pos >= 0) {
      bool ok = add_pending(rhs_trivial, pending_entries, key_map[pending_pos].first, ubits, sbits);
      if (!ok)
        return nullptr;
    }
  }
#endif

  std::stable_sort(tup->key_map.begin(), tup->key_map.end(), canonical_compare);

  return tup;
}

const Bundle::Key_map_type& Bundle::get_sort_map() const {
  std::stable_sort(key_map.begin(), key_map.end(), canonical_compare);  // mutable, just faster downstream
  return key_map;
}

bool Bundle::concat(const Const& trivial) {
  int next_pos = 0;
  for (const auto& e : key_map) {
    auto seg = first_segment_view(e.first);
    if (segment_category(seg) != 2) {
      dump();
      Lnast::info("can not concat trivial:{} to bundle unordered {} field {}", trivial, get_name(), e.first);
      return false;
    }
    int n = str_tools::to_i(seg);
    if (n + 1 > next_pos) {
      next_pos = n + 1;
    }
  }

  key_map.emplace_back(std::to_string(next_pos), Entry(false, trivial));
  rebuild_indices();
  return true;
}

bool Bundle::is_scalar() const {
  auto conta = 0;
  for (const auto& e : key_map) {
    if (is_attribute(e.first)) {
      continue;
    }
    if (conta > 0) {
      return false;
    }
    ++conta;
  }
  return true;
}

bool Bundle::is_ordered(std::string_view key) const {
  for (const auto& e : key_map) {
    auto             e_pos = 0u;
    std::string_view field;
    if (key.empty()) {
      if (is_root_attribute(e.first)) {
        continue;  // attributes do not affect order
      }

      field = e.first;
    } else {
      e_pos = match_first_partial(key, e.first);
      if (e_pos == 0) {
        continue;
      }

      if (e_pos >= e.first.size()) {
        continue;  // there was no match, so still ordered
      }

      if (is_attribute(e.first)) {
        continue;  // attributes do not affect order
      }

      field = std::string_view(e.first).substr(e_pos);
    }

    auto pos = get_first_level_pos(field);
    if (pos < 0) {
      return false;  // OOPS, not ordered entry. This is not OK
    }
  }

  return true;
}

std::string_view Bundle::get_scalar_name() const {
  std::string_view sname;

  for (const auto& e : key_map) {
    std::string_view s;
    if (is_attribute(e.first)) {
      s = get_all_but_last_level(e.first);
    } else {
      s = e.first;
    }
    if (!sname.empty() && sname != s) {
      return "";
    }
    sname = s;
  }

  return sname;
}

bool Bundle::is_trivial_scalar() const {
  auto conta = 0;

  for (const auto& e : key_map) {
    std::string_view field{e.first};

    if (is_attribute(field)) {
      field = get_all_but_last_level(field);
    } else {
      if (conta > 0) {
        return false;
      }
      ++conta;
    }
    if (field == "0") {
      continue;
    }

    return false;
  }

  return true;
}

bool Bundle::has_just_attributes() const {
  for (const auto& e : key_map) {
    if (is_attribute(e.first)) {
      continue;
    }
    return false;
  }

  return true;
}

void Bundle::dump() const {
  std::print("bundle_name: {}{}\n", name, correct ? "" : " ISSUES");

  for (const auto& it : key_map) {
    std::print("  key:{} trivial:{} {}\n", it.first, it.second.trivial, it.second.immutable ? "let" : "mut");
  }
}
