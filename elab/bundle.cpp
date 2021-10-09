//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <algorithm>

#include "bundle.hpp"
#include "likely.hpp"
#include "lnast.hpp"

using namespace std::literals;

// Custom sort, to make _ ordered first. This helps to get attributes first which helps to speedup some algorithms in bundle
static bool inline compare_less(char c1, char c2) {
  // return (std::tolower(c1) < std::tolower(c2));
  return (c1 == '_' || c1 < c2) && c2 != '_';
}

static bool bundle_sort(const std::pair<mmap_lib::str, Bundle::Entry> &lhs, const std::pair<mmap_lib::str, Bundle::Entry> &rhs) {

  auto l     = 0u;
  auto l_end = lhs.first.size();
  auto r     = 0u;
  auto r_end = rhs.first.size();

  while (l != l_end && r != r_end) {
    if (lhs.first[l] == ':') {  // Skip : from things like :3:id. Then we can sort bundles like (a=..., 333) // ":0:a" < "1"
      ++l;
      continue;
    }
    if (rhs.first[r] == ':') {
      ++r;
      continue;
    }
    if (lhs.first[l] != rhs.first[r]) {
      auto v = compare_less(lhs.first[l], rhs.first[r]);
      return v;
    }
    ++l;
    ++r;
  }

  auto v = lhs.first.size() <= rhs.first.size();  // l == l_end; // longest

  return v;
}

std::tuple<bool, size_t, size_t> Bundle::match_int_advance(const mmap_lib::str &a, const mmap_lib::str &b, size_t a_pos, size_t b_pos) {
  I(a[a_pos] == ':');
  I(b[b_pos] != ':');

  I(a_pos < a.size());  // pending :
  if (a[a_pos] == ':') {
    if (!std::isdigit(b[b_pos])) {
      ++a_pos;  // skip :
      while (a[a_pos] != ':') {
        I(a_pos < a.size());  // must have matching :
        ++a_pos;
      }
    }
    ++a_pos;  // Skip :
  }

  while (a.size() > a_pos && b.size() > b_pos) {
    if (a[a_pos] != b[b_pos]) {
      return std::make_tuple(false, a_pos, b_pos);
    }

    ++b_pos;
    ++a_pos;
    GI(b.size() < b_pos, b[b_pos] != ':');  // should not call this method for this

    if (b_pos == b.size() || b[b_pos] == '.') {
      bool m = (a_pos == a.size() || a[a_pos] == '.' || a[a_pos] == ':');
      if (a[a_pos] == ':') {
        while (a_pos < a.size() && a[a_pos] != '.') {
          ++a_pos;  // advance to the next section
        }
      }
      return std::make_tuple(m, a_pos, b_pos);
    }
    if (a_pos == a.size() || a[a_pos] == ':' || a[a_pos] == '.') {
      bool m = (b_pos == b.size() || b[b_pos] == '.');
      if (a[a_pos] == ':') {
        while (a_pos < a.size() && a[a_pos] != '.') {
          ++a_pos;  // advance to the next section
        }
      }
      return std::make_tuple(m, a_pos, b_pos);
    }
  }

  while (a_pos < a.size() && a[a_pos] != '.') {
    ++a_pos;  // advance to the next section
  }

  return std::make_tuple(true, a_pos, b_pos);
}

std::tuple<bool, bool, size_t> Bundle::match_int(const mmap_lib::str &a, const mmap_lib::str &b) {
  auto a_last_section = 0u;
  auto b_last_section = 0u;
  auto a_pos          = 0u;
  auto b_pos          = 0u;

  while (a_pos < a.size() && b_pos < b.size()) {
    if (a[a_pos] == b[b_pos]) {
      ++a_pos;
      ++b_pos;
      if (a_pos >= a.size() || b_pos >= b.size() || a[a_pos] == '.' || b[b_pos] == '.') {
        a_last_section = a_pos + 1;
        b_last_section = b_pos + 1;
      }
      continue;
    } else if (a_pos != a_last_section || b_pos != b_last_section) {
      return std::make_tuple(false, false, b_last_section);
    }
    if (a[a_last_section] == ':' && b[b_last_section] != ':') {
      I(b[b_last_section] != ':');

      if (std::isdigit(b[b_last_section])) {
        bool m;
        std::tie(m, a_pos, b_pos) = match_int_advance(a, b, a_last_section, b_last_section);
        if (!m)
          return std::make_tuple(false, false, b_last_section);
      } else {
        I(a[a_pos] == ':');
        ++a_pos;  // skip first
        while (a[a_pos] != ':') {
          I(a_pos < a.size());  // pending :
          ++a_pos;
        }
        ++a_pos;  // :
        continue;
      }

    } else if (b[b_last_section] == ':') {
      I(a[a_last_section] != ':');
      I(b[b_last_section] == ':');

      if (std::isdigit(a[a_last_section])) {
        bool m;
        std::tie(m, b_pos, a_pos) = match_int_advance(b, a, b_last_section, a_last_section);  // swap order
        if (!m)
          return std::make_tuple(false, false, b_last_section);
      } else {
        I(b[b_pos] == ':');
        ++b_pos;  // skip first
        while (b[b_pos] != ':') {
          I(b_pos < b.size());  // pending :
          ++b_pos;
        }
        ++b_pos;  // :
        continue;
      }
    } else {
      I(a[a_pos] != b[b_pos]);
      return std::make_tuple(false, false, b_last_section);
    }
    I(a_pos == a.size() || a[a_pos] == '.');
    I(b_pos == b.size() || b[b_pos] == '.');
    ++a_pos;
    ++b_pos;
    a_last_section = a_pos;
    b_last_section = b_pos;
  }

  bool a_match = (a_pos >= a.size()) && (b_pos >= b.size() || b[b_pos] == '.');
  bool b_match = (b_pos >= b.size()) && (a_pos >= a.size() || a[a_pos] == '.');

  if (b.size()>b_pos && b[b_pos] == '.')
    ++b_pos;
  if (a.size()>a_pos && a[a_pos] == '.')
    ++a_pos;

  return std::make_tuple(a_match, b_match, b_pos);
}

mmap_lib::str Bundle::append_field(const mmap_lib::str &a, const mmap_lib::str &b) {
  if (a.empty())
    return b;

  return mmap_lib::str::concat(a, ".", b);
}

std::tuple<mmap_lib::str, mmap_lib::str> Bundle::learn_fix_int(const mmap_lib::str &a, const mmap_lib::str &b) {
  auto a_last_section = 0u;
  auto b_last_section = 0u;
  auto a_pos          = 0u;
  auto b_pos          = 0u;

  mmap_lib::str new_a;
  mmap_lib::str new_b;

  bool a_last_match;
  bool b_last_match;

  while (true) {  // FSM main loop

    // STEP: Advance up to mismatch
    while (a_pos < a.size() && b_pos < b.size() && a[a_pos] == b[b_pos]) {
      if (likely(a[a_pos] != '.')) {
        ++a_pos;
        ++b_pos;
        continue;
      }
      I(b[b_pos] == '.');
      if (a[a_last_section] == ':') {
        new_a = append_field(new_a, a.substr(a_last_section, a_pos - a_last_section));
        new_b = append_field(new_b, a.substr(a_last_section, a_pos - a_last_section));
      } else {
        new_a = append_field(new_a, b.substr(b_last_section, b_pos - b_last_section));
        new_b = append_field(new_b, b.substr(b_last_section, b_pos - b_last_section));
      }
      ++a_pos;
      ++b_pos;
      a_last_section = a_pos;
      b_last_section = b_pos;
    }

    // STEP: Did we reach the end of string?
    if (a_pos >= a.size() || b_pos >= b.size()) {
      a_last_match = (b[b_last_section] == ':') && (b_pos >= b.size() || b[b_pos] == '.');
      b_last_match = (a[a_last_section] == ':') && (a_pos >= a.size() || a[a_pos] == '.');
      break;
    }

    // STEP: Fully populated entries should match already
    if (a_last_section != a_pos || b_last_section != b_pos) {
      I(a[a_pos] != b[b_pos]);
      a_last_match = false;
      b_last_match = false;
      break;
    }

    if (a[a_last_section] == ':' && b[b_last_section] != ':') {
      I(b[b_last_section] != ':');

      if (std::isdigit(b[b_last_section])) {
        bool m;
        std::tie(m, a_pos, b_pos) = match_int_advance(a, b, a_pos, b_pos);
        if (a_pos == a.size() || b_pos == b.size() || !m) {
          a_last_match = false;
          b_last_match = m;
          break;
        }
        new_a = append_field(new_a, a.substr(a_last_section, a_last_section - a_pos));
        ++a_pos;
        ++b_pos;
        a_last_section = a_pos;
        b_last_section = b_pos;
        new_b = append_field(new_b, a.substr(a_last_section, a_last_section - a_pos));
      } else {
        I(a[a_pos] == ':');
        ++a_pos;  // skip first
        while (a[a_pos] != ':') {
          I(a_pos < a.size());  // pending :
          ++a_pos;
        }
        ++a_pos;  // :
      }
      continue;
    } else if (b[b_last_section] == ':') {
      I(a[a_last_section] != ':');
      I(b[b_last_section] == ':');

      if (std::isdigit(a[a_last_section])) {
        bool m;
        std::tie(m, b_pos, a_pos) = match_int_advance(b, a, b_pos, a_pos);  // swap order
        if (a_pos == a.size() || b_pos == b.size() || !m) {
          a_last_match = m;
          b_last_match = false;
          break;
        }
        new_a = append_field(new_a, a.substr(b_last_section, b_last_section - b_pos));
        new_b = append_field(new_b, a.substr(b_last_section, b_last_section - b_pos));
        ++a_pos;
        ++b_pos;
        a_last_section = a_pos;
        b_last_section = b_pos;
      } else {
        I(b[b_pos] == ':');
        ++b_pos;  // skip first
        while (b[b_pos] != ':') {
          I(b_pos < b.size());  // pending :
          ++b_pos;
        }
        ++b_pos;  // :
      }
      continue;
    } else {
      a_last_match = false;
      b_last_match = false;
      break;
    }

    I(false);  // never reaches this place
  }

  // Finish the rest of the swap (if match) and add the rest
  if (a_last_match) {
    I(b[b_last_section] == ':');
    new_a = append_field(new_a, b.substr(b_last_section, b_pos - b_last_section));
    if (a_pos < a.size()) {
      I(a[a_pos] == '.');
      new_a = append_field(new_a, a.substr(a_pos + 1));  // +1 to skip .
    }
  } else {
    new_a = append_field(new_a, a.substr(a_last_section));
  }
  if (b_last_match) {
    I(a[a_last_section] == ':');
    new_b = append_field(new_b, a.substr(a_last_section, a_pos - a_last_section));
    if (b_pos < b.size()) {
      I(b[b_pos] == '.');
      new_b = append_field(new_b, b.substr(b_pos + 1));  // +1 to skip .
    }
  } else {
    new_b = append_field(new_b, b.substr(b_last_section));
  }

  return std::make_tuple(new_a, new_b);
}

bool Bundle::match(const mmap_lib::str &a, const mmap_lib::str &b) {
  if (a == b)
    return true;
  if (a.empty()) {
    if (b == "0" || (b.size() > 3 && b.substr(0, 3) == ":0:"))
      return true;
  }
  if (b.empty()) {
    if (a == "0" || (a.size() > 3 && a.substr(0, 3) == ":0:"))
      return true;
  }

  auto [m1, m2, x] = match_int(a, b);
  (void)x;
  return m1 && m2;  // both reach the end in match_int
}

size_t Bundle::match_first_partial(const mmap_lib::str &a, const mmap_lib::str &b) {
  auto [m1, m2, x] = match_int(a, b);
  (void)m2;
  if (m1)
    return x;  // a reached the end in match
  return 0;
}

bool Bundle::match_either_partial(const mmap_lib::str &a, const mmap_lib::str &b) {
  auto [m1, m2, x] = match_int(a, b);
  (void)x;
  return m1 || m2;  // either reached the end it match_int
}

void Bundle::add_int(const mmap_lib::str &key, const std::shared_ptr<Bundle const> tup) {
  I(!key.empty());
  if (tup->is_scalar()) {
    I(!has_trivial(key));  // It was deleted before
    key_map.emplace_back(key, Entry(tup->is_immutable(), tup->get_trivial()));
    return;
  }

  bool root = is_root_attribute(key);
  for (auto &ent : tup->key_map) {
    if (root) {
      key_map.emplace_back(mmap_lib::str::concat(ent.first, ".", key), ent.second);
    } else {
      key_map.emplace_back(mmap_lib::str::concat(key, ".", ent.first), ent.second);
    }
  }
}

void Bundle::del_int(const mmap_lib::str &key) {
  if (key.empty()) {
    key_map.clear();
    return;
  }

#if 0
  if (is_root_attribute(key)) {
    return;
  }
#endif

  Key_map_type new_map;

  bool is_attr_key = is_root_attribute(key);

  for (auto &e : key_map) {
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
    if (e_pos >= e.first.size())
      continue;  // full match?

    I(e.first[e_pos] != '.');  // not . included

    auto sub_name = e.first.substr(e_pos);
    if (sub_name.substr(0, 2) == "__" && sub_name[3] != '_') {
      new_map.emplace_back(std::move(e));
      continue;  // Keep the attributes
    }
  }

  key_map.swap(new_map);
}

int Bundle::get_first_level_pos(const mmap_lib::str &key) {
  if (key.empty())
    return -1;

  auto skip=0u;

  if (key[skip] == ':') {
    ++skip;
  }

  if (!std::isdigit(key[skip])) {
    return -1;
  }

  return key.substr(skip).to_i();
}

mmap_lib::str Bundle::get_first_level(const mmap_lib::str &key) {
  auto dot_pos = key.find('.');
  if (dot_pos == std::string::npos)
    return key;

  return key.substr(0, dot_pos);
}

mmap_lib::str Bundle::get_first_level_name(const mmap_lib::str &key) {
  auto dot_pos = key.find('.');
  if (key.size() > 0 && key.front() != ':') {
    if (dot_pos == std::string::npos)
      return key;
    return key.substr(0, dot_pos);
  }

  auto n = key.substr(1).find(':');
  if (dot_pos == std::string::npos)
    return key.substr(1 + 1 + n);
  return key.substr(1 + 1 + n, dot_pos - 1 - 1 - n);
}

mmap_lib::str Bundle::get_canonical_name(const mmap_lib::str &key) {

  mmap_lib::str key2{key};

  // Remove xxx.0.0.0
  while (key2.size() > 1 && key2.back() == '0') {
    auto sz = key2.size();
    if (key2.substr(sz - 2, sz) == ".0") {
      key2 = key2.substr(0, sz - 2);
      continue;
    }
    break;
  }

  if (key2 == "0")
    key2 = "";

  return key2;
}

mmap_lib::str Bundle::get_last_level(const mmap_lib::str &key) {
  auto n = key.rfind('.');
  if (n == std::string::npos)
    return key;

  I(n != 0);  // name can not start with a .
  return key.substr(n + 1);
}

mmap_lib::str Bundle::get_all_but_last_level(const mmap_lib::str &key) {
  auto n = key.rfind('.');
  if (n != std::string::npos) {
    return key.substr(0, n);
  }

  return mmap_lib::str("");
}

std::pair<int, mmap_lib::str> Bundle::convert_key_to_io(const mmap_lib::str &key) {
  size_t skip=0;

  if (key[skip] == '$' || key[skip] == '%') {
    ++skip;
  }
  if (key[skip] == '.')
    ++skip;

  if (key[skip] != ':') {
    return std::pair(-1, key.substr(skip));
  }

  auto key2 = key.substr(skip+1);

  if (!std::isdigit(key2.front())) {
    throw Lnast::error("name should have digit after position specified :digits: not {}\n", key2);
  }

  auto n = key2.find(':');
  if (n == std::string::npos) {
    throw Lnast::error("name should have a format like :digits:name not {}\n", key2);
  }

  auto x = key2.to_i();
  I(x == key2.substr(0,n).to_i());

  return std::pair(x, key2.substr(n + 1));
}

mmap_lib::str Bundle::get_all_but_first_level(const mmap_lib::str &key) {
  auto n = key.find('.');
  if (n != std::string::npos) {
    return key.substr(n + 1);
  }

	if (key.front() == '$' || key.front() == '%' || key.front() == '#')
		return key.substr(1);

  return mmap_lib::str("");  // empty if no dot left
}

mmap_lib::str Bundle::learn_fix(const mmap_lib::str &a) {
  mmap_lib::str key{a};

  for (auto &e : key_map) {
    std::tie(key, e.first) = learn_fix_int(key, e.first);
  }

  return key;
}

Bundle::Entry Bundle::get_entry(const mmap_lib::str &key) const {
  for (auto &e : key_map) {
    if (match(e.first, key))
      return e.second;
  }

  return Entry(true, Lconst::invalid());
}

Lconst Bundle::get_trivial() const {
  int pos = -1;
  for (auto i = 0u; i < key_map.size(); ++i) {
    if (is_attribute(key_map[i].first))
      continue;
    I(pos < 0);  // only scalars, so trivial can not be defined already
    pos = i;
  }

  if (pos < 0)
    return Lconst::invalid();

  return key_map[pos].second.trivial;
}

bool Bundle::has_trivial(const mmap_lib::str &key) const {
  for (auto &e : key_map) {
    if (match(e.first, key))
      return true;
  }
  return false;
}

bool Bundle::has_bundle(const mmap_lib::str &key) const {
  if (key.empty()) {
    return false;
  }

  I(!key.empty());  // do not call without sub-fields

  for (auto &e : key_map) {
    auto             e_pos = match_first_partial(key, e.first);
    if (e_pos == 0)
      continue;

    return true;
  }

  return false;
}

std::shared_ptr<Bundle> Bundle::get_bundle(const mmap_lib::str &key) const {
  if (key.empty()) {
    return std::make_shared<Bundle>(*this);
  }

  I(!key.empty());  // do not call without sub-fields

  std::shared_ptr<Bundle> tup;

  for (const auto &e : key_map) {
    auto             e_pos = match_first_partial(key, e.first);
    if (e_pos == 0)
      continue;
    GI(e_pos<e.first.size(), e.first[e_pos] != '.');  // . not included

    if (!tup) {
      mmap_lib::str key_with_pos{key};
      mmap_lib::str expanded{e.first};
      std::tie(key_with_pos, expanded) = learn_fix_int(key_with_pos, expanded);
      tup = std::make_shared<Bundle>(mmap_lib::str::concat(name, ".", key_with_pos));
    }

    if (e_pos >= e.first.size()) {
      tup->key_map.emplace_back("0", e.second);
    } else {
      auto key2 = e.first.substr(e_pos);
      I(!key2.empty());
      if (is_root_attribute(key2)) {
        tup->key_map.emplace_back(mmap_lib::str::concat("0."_str, key2), e.second);
      } else {
        tup->key_map.emplace_back(key2, e.second);
      }
    }
  }

  if (!is_correct())
    tup->set_issue();

  return tup;
}

std::shared_ptr<Bundle> Bundle::get_bundle(const std::shared_ptr<Bundle const>& tup) const {
  // create a trivial or sub-bundle with the selected fields
  std::shared_ptr<Bundle> ret_tup;

  int pos = 0;
  for (const auto &e : tup->key_map) {
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

  if (!tup->is_correct())
    ret_tup->set_issue();

  return ret_tup;
}


void Bundle::set(const mmap_lib::str &key, const std::shared_ptr<Bundle const>& tup) {
  I(!key.empty());

  if (tup == nullptr) {
    set(key, Lconst::invalid());
    return;
  }

  correct = correct && tup->correct;

  I(!is_root_attribute(key));
  I(!is_attribute(key));
  // bool root_key = is_root_attribute(key);

  bool tup_scalar = tup->is_scalar();

  for (const auto &e : tup->key_map) {
    mmap_lib::str key2;
    // Remove 0. from tup if tup is scalar
    if (tup_scalar && e.first.front() == '0' && (e.first.size() == 1 || e.first[1] == '.')) {
      if (e.first.size() == 1)
        key2 = mmap_lib::str("");  // remove "0"
      else
        key2 = e.first.substr(2);  // remove "0."
    } else {
      key2 = e.first;
    }

    if (key2.empty()) {
      set(key, e.second);
    } else {
      auto key3 = mmap_lib::str::concat(key, ".", key2);
      set(key3, e.second);
    }
  }
}

void Bundle::set(const mmap_lib::str &key, const Entry &&entry) {
  I(!key.empty());

  mmap_lib::str uncanonical_key{key};
  bool        pending_adjust = false;
  if (is_scalar()) {
    if (key.substr(0, 2) == "__" && key[3] != '_') {  // is_root_attribute BUT not with 0.__xxx
      uncanonical_key = mmap_lib::str::concat("0.", key);
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

    mmap_lib::str key_part{fixed_key};
    if (is_attribute(fixed_key)) {
      key_part = get_all_but_last_level(fixed_key);
    }
    for (auto &e : key_map) {
      mmap_lib::str fpart{e.first};
      mmap_lib::str lpart;
      if (is_attribute(e.first)) {
        fpart = get_all_but_last_level(e.first);
        lpart = get_last_level(e.first);
      }

      if (fpart.size() >= key_part.size())
        continue;

      // NOTE: full match foo.bar == foo not foo.bar == foo match
      if (key_part[fpart.size()] == '.' && fpart == key_part.substr(0, fpart.size())) {
        if (lpart.empty())
          e.first = mmap_lib::str::concat(fpart, ".0"sv);
        else
          e.first = mmap_lib::str::concat(fpart, ".0.", lpart);
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

#ifdef DEBUG_SLOW
  for (const auto &e : key_map) {
    auto lower = get_all_but_last_level(e.first);
    if (is_attribute(e.first)) {
      lower = get_all_but_last_level(lower);
    }
    while (!lower.empty()) {
      I(!is_attribute(lower));
      if (has_trivial(lower)) {
        dump();
        fmt::print("OOPPPS (bundle is corrupted). Time to debug!!! {} {}\n", e.first, lower);
        exit(-3);
        return;
      }
      lower = get_all_but_last_level(lower);
    }
  }
#endif
}

bool Bundle::concat(const std::shared_ptr<Bundle const>& tup) {
  bool ok = true;

  std::vector<std::pair<mmap_lib::str, Lconst>> delayed_numbers;

  for (auto &it : tup->key_map) {
    if (has_trivial(it.first)) {
      if (std::isdigit(it.first.front()) && is_single_level(it.first)) {
        delayed_numbers.emplace_back(it.first, it.second.trivial);
        continue;
      }
      dump();
      tup->dump();
      Lnast::info("bundle {} and {} can not concat for key:{}", get_name(), tup->get_name(), it.first);
      ok = false;
      continue;
    }
    auto fixed_key = learn_fix(it.first);
    I(!fixed_key.empty());
    key_map.emplace_back(fixed_key, it.second);
  }

  if (delayed_numbers.size()) {
    auto max_pos = 0;
    for (const auto &e : key_map) {
      int x = 0;
      if (e.first.is_i()) {
        x = e.first.to_i();
      } else if (e.first.front() == ':' && std::isdigit(e.first[1])) {
        x = e.first.substr(1).to_i();
      } else {
        dump();
        Lnast::info("can not concat bundle pin to bundle unordered {} field {}", get_name(), e.first);
        return false;
      }
      if (x > max_pos)
        max_pos = x;
    }
    for (const auto &e : delayed_numbers) {
      auto x = e.first.to_i();
      mmap_lib::str new_key(std::to_string(x + max_pos + 1));

      key_map.emplace_back(new_key, Entry(false, e.second));
    }
  }

  return ok;
}

Lconst Bundle::flatten() const {
  // a_dpin = (tup[0]|(tup[1]<<tup[0].__sbits)|(tup[2]<<(tup[0..1].__sbits)|.....)

  if (!is_correct())
    return Lconst::invalid();

  std::stable_sort(key_map.begin(), key_map.end(), bundle_sort);

  Lconst result;
  for (auto &e : key_map) {
    if (is_attribute(e.first))
      continue;

    if (e.second.trivial.is_invalid())
      return e.second.trivial;

    auto v = e.second.trivial << result.get_bits();
    result = result.or_op(v.get_mask_op());
  }

  return result;
}

std::shared_ptr<Bundle> Bundle::create_assign(const std::shared_ptr<Bundle const>& rhs_tup) const {
  (void)rhs_tup;

  I(false);  // FIXME: implement it
  auto new_tup = std::make_shared<Bundle>(get_name());

  return new_tup;
}

std::shared_ptr<Bundle> Bundle::create_assign(const Lconst &rhs_trivial) const {
  if (rhs_trivial.is_invalid())
    return nullptr;

  I(false);
  I(is_correct());

  std::stable_sort(key_map.begin(), key_map.end(), bundle_sort);

  auto tup = std::make_shared<Bundle>(get_name());

#if 0
  {
    Lconst sbits = Lconst::invalid();
    Lconst ubits = Lconst::invalid();

    for (auto i = 0u; i < key_map.size(); ++i) {
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
        ubits     = Lconst::invalid;
        sbits     = Lconst::invalid;
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
        ubits     = Lconst::invalid;
        sbits     = Lconst::invalid;
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

  std::stable_sort(tup->key_map.begin(), tup->key_map.end(), bundle_sort);

  return tup;
}

const Bundle::Key_map_type &Bundle::get_sort_map() const {
  std::stable_sort(key_map.begin(), key_map.end(), bundle_sort);  // mutable (no semantic check. Just faster to process)
  return key_map;
}

bool Bundle::concat(const Lconst &trivial) {
  auto max_pos = 0;
  for (const auto &e : key_map) {
    int x = 0;
    if (e.first.is_i()) {
      x = e.first.to_i();
    } else if (e.first.front() == ':' && std::isdigit(e.first[1])) {
      x = e.first.substr(1).to_i();
    } else {
      dump();
      Lnast::info("can not concat trivial:{} to bundle unordered {} field {}", trivial, get_name(), e.first);
      return false;
    }
    if (x > max_pos)
      max_pos = x;
  }

  mmap_lib::str new_key(std::to_string(max_pos + 1));

  key_map.emplace_back(new_key, Entry(false, trivial));

  return true;
}

bool Bundle::is_scalar() const {
  auto conta = 0;
  for (const auto &e : key_map) {
    if (is_attribute(e.first))
      continue;
    if (conta > 0)
      return false;
    ++conta;
  }
  return true;
}

bool Bundle::is_ordered(const mmap_lib::str key) const {
  for (const auto &e : key_map) {
    auto e_pos = 0u;
    mmap_lib::str field;
    if (key.empty()) {
      if (is_root_attribute(e.first))
        continue;  // attributes do not affect order

      field = e.first;
    }else{
      e_pos = match_first_partial(key, e.first);
      if (e_pos == 0)
        continue;

      if (e_pos >= e.first.size())
        continue; // there was no match, so still ordered

      if (is_attribute(e.first))
        continue;  // attributes do not affect order

      field = e.first.substr(e_pos);
    }

    auto pos = get_first_level_pos(field);
    if (pos < 0)
      return false; // OOPS, not ordered entry. This is not OK
  }

  return true;
}

mmap_lib::str Bundle::get_scalar_name() const {
  mmap_lib::str sname;

  for (const auto &e : key_map) {
    mmap_lib::str s;
    if (is_attribute(e.first)) {
      s = get_all_but_last_level(e.first);
    } else {
      s = e.first;
    }
    if (!sname.empty() && sname != s)
      return mmap_lib::str();
    sname = s;
  }

  return sname;
}

bool Bundle::is_trivial_scalar() const {
  auto conta = 0;

  for (const auto &e : key_map) {
    mmap_lib::str field{e.first};

    if (is_attribute(field)) {
      field = get_all_but_last_level(field);
    } else {
      if (conta > 0)
        return false;
      ++conta;
    }
    if (field == "0")
      continue;

    return false;
  }

  return true;
}

bool Bundle::has_just_attributes() const {
  for (const auto &e : key_map) {
    if (is_attribute(e.first)) {
      continue;
    }
    return false;
  }

  return true;
}

void Bundle::dump() const {
  fmt::print("bundle_name: {}{}\n", name, correct ? "" : " ISSUES");

  for (const auto &it : key_map) {
    fmt::print("  key:{} trivial:{} {}\n"
      ,it.first
      ,it.second.trivial
      ,it.second.immutable?"let":"mut"
    );
  }
}
