//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.


#include "absl/container/flat_hash_map.h"

#include <algorithm>

#include "lgtuple.hpp"
#include "lgraph.hpp"
#include "likely.hpp"

using namespace std::literals;

// Custom sort, to make _ ordered first. This helps to get attributes first which helps to speedup some algorithms in lgtuple
static bool inline compare_less(char c1, char c2) {
  // return (std::tolower(c1) < std::tolower(c2));
  return (c1 == '_' || c1 < c2) && c2 != '_';
}

static bool tuple_sort(const std::pair<mmap_lib::str, Node_pin> &lhs, const std::pair<mmap_lib::str, Node_pin> &rhs) {

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

std::tuple<bool, size_t, size_t> Lgtuple::match_int_advance(const mmap_lib::str &a, const mmap_lib::str &b, size_t a_pos, size_t b_pos) {
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
    I(b[b_pos] != ':');  // should not call this method for this

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

std::tuple<bool, bool, size_t> Lgtuple::match_int(const mmap_lib::str &a, const mmap_lib::str &b) {
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

mmap_lib::str Lgtuple::append_field(const mmap_lib::str &a, const mmap_lib::str &b) {
  if (a.empty())
    return b;

  return mmap_lib::str::concat(a, ".", b);
}

std::tuple<mmap_lib::str, mmap_lib::str> Lgtuple::learn_fix_int(const mmap_lib::str &a, const mmap_lib::str &b) {
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

bool Lgtuple::match(const mmap_lib::str &a, const mmap_lib::str &b) {
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

size_t Lgtuple::match_first_partial(const mmap_lib::str &a, const mmap_lib::str &b) {
  auto [m1, m2, x] = match_int(a, b);
  (void)m2;
  if (m1)
    return x;  // a reached the end in match
  return 0;
}

bool Lgtuple::match_either_partial(const mmap_lib::str &a, const mmap_lib::str &b) {
  auto [m1, m2, x] = match_int(a, b);
  (void)x;
  return m1 || m2;  // either reached the end it match_int
}

void Lgtuple::add_int(const mmap_lib::str &key, const std::shared_ptr<Lgtuple const>& tup) {
  I(!key.empty());
  if (tup->is_scalar()) {
    I(!has_dpin(key));  // It was deleted before
    key_map.emplace_back(key, tup->get_dpin());
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

void Lgtuple::reconnect_flop_if_needed(Node &flop, const mmap_lib::str &flop_name, Node_pin &dpin) {
  flop.setup_driver_pin().reset_name(mmap_lib::str(flop_name));

  auto s_din = flop.setup_sink_pin("din");

  if (s_din.is_connected()) {
    auto d_din = s_din.get_driver_pin();
    if (d_din == dpin)
      return;  // already connected
    XEdge::del_edge(d_din, s_din);
  }

  s_din.connect_driver(dpin);
}

int Lgtuple::get_first_level_pos(const mmap_lib::str &key) {
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

mmap_lib::str Lgtuple::get_first_level_name(const mmap_lib::str &key) {
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

mmap_lib::str Lgtuple::get_canonical_name(const mmap_lib::str &key) {

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

mmap_lib::str Lgtuple::get_last_level(const mmap_lib::str &key) {
  auto n = key.rfind('.');
  if (n == std::string::npos)
    return key;

  I(n != 0);  // name can not start with a .
  return key.substr(n + 1);
}

mmap_lib::str Lgtuple::get_all_but_last_level(const mmap_lib::str &key) {
  auto n = key.rfind('.');
  if (n != std::string::npos) {
    return key.substr(0, n);
  }

  return mmap_lib::str("");
}

std::pair<Port_ID, mmap_lib::str> Lgtuple::convert_key_to_io(const mmap_lib::str &key) {
  size_t skip=0;

  if (key[skip] == '$' || key[skip] == '%') {
    ++skip;
  }
  if (key[skip] == '.')
    ++skip;

  if (key[skip] != ':') {
    return std::pair(Port_invalid, key.substr(skip));
  }

  auto key2 = key.substr(skip);

  if (!std::isdigit(key2.front())) {
    Lgraph::error("name should have digit after position specified :digits: not {}\n", key2);
  }

  auto n = key2.find(':');
  if (n == std::string::npos) {
    Lgraph::error("name should have a format like :digits:name not {}\n", key2);
  }

  auto x = key2.to_i();
  I(x == key2.substr(0,n).to_i());

  return std::pair(static_cast<Port_ID>(x), key2.substr(n + 1));
}

mmap_lib::str Lgtuple::get_all_but_first_level(const mmap_lib::str &key) {
  auto n = key.find('.');
  if (n != std::string::npos) {
    return key.substr(n + 1);
  }

  return mmap_lib::str("");  // empty if no dot left
}

mmap_lib::str Lgtuple::learn_fix(const mmap_lib::str &a) {
  mmap_lib::str key{a};

  for (auto &e : key_map) {
    std::tie(key, e.first) = learn_fix_int(key, e.first);
  }

  return key;
}

const Node_pin &Lgtuple::get_dpin(const mmap_lib::str &key) const {
  for (auto &e : key_map) {
    if (match(e.first, key))
      return e.second;
  }

  return invalid_dpin;
}

const Node_pin &Lgtuple::get_dpin() const {
  int pos = -1;
  for (auto i = 0u; i < key_map.size(); ++i) {
    if (is_attribute(key_map[i].first))
      continue;
    I(pos < 0);  // only scalars, so dpin can not be defined already
    pos = i;
  }

  if (pos < 0)
    return invalid_dpin;

  return key_map[pos].second;
}

bool Lgtuple::has_dpin(const mmap_lib::str &key) const {
  for (auto &e : key_map) {
    if (match(e.first, key))
      return true;
  }
  return false;
}

std::shared_ptr<Lgtuple> Lgtuple::get_sub_tuple(const mmap_lib::str &key) const {
  if (key.empty()) {
    return std::make_shared<Lgtuple>(*this);
  }

  I(!key.empty());  // do not call without sub-fields

  std::shared_ptr<Lgtuple> tup;

  for (auto &e : key_map) {
    const mmap_lib::str &entry(e.first);
    auto             e_pos = match_first_partial(key, entry);
    if (e_pos == 0)
      continue;
    GI(e_pos<entry.size(), entry[e_pos] != '.');  // . not included

    if (!tup) {
      mmap_lib::str key_with_pos{key};
      mmap_lib::str expanded{e.first};
      std::tie(key_with_pos, expanded) = learn_fix_int(key_with_pos, expanded);
      tup = std::make_shared<Lgtuple>(mmap_lib::str::concat(name, ".", key_with_pos));
    }

    if (e_pos >= entry.size()) {
      tup->key_map.emplace_back("0", e.second);
    } else {
      auto key2 = entry.substr(e_pos);
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

std::shared_ptr<Lgtuple> Lgtuple::get_sub_tuple(const std::shared_ptr<Lgtuple const>& tup) const {
  // create a dpin or subtuple with the selected fields
  std::shared_ptr<Lgtuple> ret_tup;

  int pos = 0;
  for (const auto &e : tup->key_map) {
    mmap_lib::str e_name{e.first};
    (void)e_name;
    auto e_node = e.second.get_node();
    if (!e_node.is_type_const()) {
      Lgraph::info("tuple {} can not be indexed with {} key:{} because it is not constant", get_name(), tup->get_name(), e.first);
      return nullptr;
    }
    auto        v = e_node.get_type_const();
    mmap_lib::str txt;
    if (v.is_i()) {
      txt = mmap_lib::str(v.to_i());
    } else {
      txt = v.to_pyrope();
    }
    auto dpin = get_dpin(txt);
    if (dpin.is_invalid()) {
      Lgraph::info("tuple {} can not be indexed with {} key:{} with value {}", get_name(), tup->get_name(), e.first, txt);
      return nullptr;
    }
    if (!ret_tup) {
      ret_tup = std::make_shared<Lgtuple>(get_name());
    }
    ret_tup->key_map.emplace_back(std::to_string(pos), dpin);
    ++pos;
  }

  if (!tup->is_correct())
    ret_tup->set_issue();
  return ret_tup;
}

void Lgtuple::del(const mmap_lib::str &key) {
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
    mmap_lib::str entry{e.first};
    if (entry.empty()) {
      if (is_attr_key) {
        new_map.emplace_back(std::move(e));
      }
      continue;  // "" keys must be gone by now
    }

    auto e_pos = match_first_partial(key, entry);
    if (e_pos == 0) {
      new_map.emplace_back(std::move(e));
      continue;
    }
    if (e_pos >= entry.size())
      continue;  // full match?

    I(entry[e_pos] != '.');  // not . included

    auto sub_name = entry.substr(e_pos);
    if (sub_name.substr(0, 2) == "__" && sub_name[3] != '_') {
      new_map.emplace_back(std::move(e));
      continue;  // Keep the attributes
    }
  }

  key_map.swap(new_map);
}

void Lgtuple::add(const mmap_lib::str &key, const std::shared_ptr<Lgtuple const>& tup) {
  I(!key.empty());

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
      add(key, e.second);
    } else {
      auto key3 = mmap_lib::str::concat(key, ".", key2);
      add(key3, e.second);
    }
  }
}

void Lgtuple::add(const mmap_lib::str &key, const Node_pin &dpin) {
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

  del(fixed_key);

  if (pending_adjust) {
    // NOTE: If the tuple had something like:
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
          e.second = dpin;
          return;
        }
      }
    }
  }

  I(!has_dpin(fixed_key));
  I(!fixed_key.empty());
  key_map.emplace_back(fixed_key, dpin);

#ifdef DEBUG_SLOW
  for (const auto &e : key_map) {
    auto lower = get_all_but_last_level(e.first);
    if (is_attribute(e.first)) {
      lower = get_all_but_last_level(lower);
    }
    while (!lower.empty()) {
      I(!is_attribute(lower));
      if (has_dpin(lower)) {
        dump();
        fmt::print("OOPPPS (tuple is corrupted). Time to debug!!! {} {}\n", e.first, lower);
        exit(-3);
        return;
      }
      lower = get_all_but_last_level(lower);
    }
  }
#endif
}

bool Lgtuple::concat(const std::shared_ptr<Lgtuple const>& tup) {
  bool ok = true;

  std::vector<std::pair<mmap_lib::str, Node_pin>> delayed_numbers;

  for (auto &it : tup->key_map) {
    if (has_dpin(it.first)) {
      if (std::isdigit(it.first.front()) && is_single_level(it.first)) {
        delayed_numbers.emplace_back(it.first, it.second);
        continue;
      }
      dump();
      tup->dump();
      Lgraph::info("tuples {} and {} can not concat for key:{}", get_name(), tup->get_name(), it.first);
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
        Lgraph::info("can not concat tuple pin to tuple unordered {} field {}", get_name(), e.first);
        return false;
      }
      if (x > max_pos)
        max_pos = x;
    }
    for (const auto &e : delayed_numbers) {
      auto x = e.first.to_i();
      mmap_lib::str new_key(std::to_string(x + max_pos + 1));

      key_map.emplace_back(new_key, e.second);
    }
  }

  return ok;
}

std::pair<Node, Node_pin> Lgtuple::flatten_field(Node &result_node, Node_pin &dpin, Node_pin &start_bit_dpin, Node_pin &sbits_dpin,
                                                 Node_pin &ubits_dpin) {
  // returns mask_dpin for given dpin
  //
  // mask = (1<<(dpin.get_bits())-1)<<start_bit_dpin

  Node_pin mask_dpin;
  Node_pin new_bits_dpin;

  if (dpin.is_type_const()) {
    auto v      = dpin.get_type_const();
    auto v_bits = v.get_bits();
    auto v_mask = v.get_mask_value();

    auto just_mask_dpin = result_node.create_const(v_mask).setup_driver_pin();

    if (start_bit_dpin.is_invalid()) {
      new_bits_dpin = result_node.create_const(v_bits).setup_driver_pin();
      mask_dpin     = just_mask_dpin;
    } else {
      auto shl_node = result_node.create(Ntype_op::SHL);
      shl_node.setup_sink_pin("a").connect_driver(just_mask_dpin);
      shl_node.setup_sink_pin("B").connect_driver(start_bit_dpin);

      mask_dpin = shl_node.setup_driver_pin();

      auto add_node = result_node.create(Ntype_op::Sum);
      add_node.setup_sink_pin("A").connect_driver(result_node.create_const(v_bits));
      add_node.setup_sink_pin("A").connect_driver(start_bit_dpin);
      new_bits_dpin = add_node.setup_driver_pin();
    }
  } else {
    Node_pin bits_dpin;

    if (!sbits_dpin.is_invalid()) {
      bits_dpin = sbits_dpin;
    } else if (!ubits_dpin.is_invalid()) {
      auto add_node = result_node.create(Ntype_op::Sum);
      add_node.setup_sink_pin("A").connect_driver(ubits_dpin);
      add_node.setup_sink_pin("A").connect_driver(result_node.create_const(1));

      bits_dpin = add_node.setup_driver_pin();
    } else {
      auto attr_node = result_node.create(Ntype_op::AttrGet);
      attr_node.setup_sink_pin("field").connect_driver(result_node.create_const(Lconst::from_string("__sbits")));
      attr_node.setup_sink_pin("parent").connect_driver(dpin);

      bits_dpin = attr_node.setup_driver_pin();
    }

    auto shl_node = result_node.create(Ntype_op::SHL);
    shl_node.setup_sink_pin("a").connect_driver(result_node.create_const(1));
    shl_node.setup_sink_pin("B").connect_driver(bits_dpin);
    auto sub_node = result_node.create(Ntype_op::Sum);
    sub_node.setup_sink_pin("A").connect_driver(shl_node);
    sub_node.setup_sink_pin("B").connect_driver(result_node.create_const(1));

    auto just_mask_dpin = sub_node.setup_driver_pin();

    if (start_bit_dpin.is_invalid()) {
      new_bits_dpin = bits_dpin;
      mask_dpin     = just_mask_dpin;
    } else {
      auto shl2_node = result_node.create(Ntype_op::SHL);
      shl2_node.setup_sink_pin("a").connect_driver(just_mask_dpin);
      shl2_node.setup_sink_pin("B").connect_driver(start_bit_dpin);

      mask_dpin = shl2_node.setup_driver_pin();

      auto add_node = result_node.create(Ntype_op::Sum);
      add_node.setup_sink_pin("A").connect_driver(bits_dpin);
      add_node.setup_sink_pin("A").connect_driver(start_bit_dpin);
      new_bits_dpin = add_node.setup_driver_pin();
    }
  }

  auto set_mask_node = result_node.create(Ntype_op::Set_mask);
  set_mask_node.setup_sink_pin("a").connect_driver(result_node);
  set_mask_node.setup_sink_pin("mask").connect_driver(mask_dpin);
  set_mask_node.setup_sink_pin("value").connect_driver(dpin);

  return std::pair(set_mask_node, new_bits_dpin);
}

Node_pin Lgtuple::flatten() const {
  // a_dpin = (tup[0]|(tup[1]<<tup[0].__sbits)|(tup[2]<<(tup[0..1].__sbits)|.....)

  I(is_correct());  // Do not call flatten for incorrect tuples of tuples with issues (invalid dpin)
  Node_pin a_dpin;
  bool     all_const = true;

  std::stable_sort(key_map.begin(), key_map.end(), tuple_sort);  // mutable (no semantic check. Just faster to process)

  for (auto &e : key_map) {
    if (is_attribute(e.first))
      continue;

    a_dpin = e.second;
    if (e.second.is_invalid())
      return invalid_dpin;

    if (!e.second.is_type_const()) {
      all_const = false;
      break;
    }
  }
  if (a_dpin.is_invalid())
    return a_dpin;

  if (all_const) {
    Lconst result;
    for (auto &e : key_map) {
      if (is_attribute(e.first))
        continue;
      auto v = e.second.get_type_const();
      v      = v << result.get_bits();
      result = result.or_op(v.get_mask_op());
    }
    return a_dpin.get_node().create_const(result).get_driver_pin();
  }

  Node     result_node = a_dpin.get_node().create_const(0);
  Node_pin bit_chain_dpin;

  Node_pin last_non_attr_dpin;
  Node_pin sbits_dpin;
  Node_pin ubits_dpin;

  for (auto &e : key_map) {
    if (is_attribute(e.first)) {
      auto attr_txt = get_last_level(e.first);
      if (attr_txt == "__sbits")
        sbits_dpin = e.second;
      else if (attr_txt == "__ubits")
        ubits_dpin = e.second;

      if (!last_non_attr_dpin.is_invalid()) {
        auto attr_set_node = result_node.create(Ntype_op::AttrSet);
        attr_set_node.setup_sink_pin("parent").connect_driver(last_non_attr_dpin);
        attr_set_node.setup_sink_pin("value").connect_driver(e.second);
        attr_set_node.setup_sink_pin("field").connect_driver(result_node.create_const(Lconst::from_string(attr_txt)));

        last_non_attr_dpin = attr_set_node.setup_driver_pin();
      }
      continue;
    }

    if (!last_non_attr_dpin.is_invalid()) {
      std::tie(result_node, bit_chain_dpin)
          = Lgtuple::flatten_field(result_node, last_non_attr_dpin, bit_chain_dpin, sbits_dpin, ubits_dpin);
      sbits_dpin = invalid_dpin;  // clear attr
      ubits_dpin = invalid_dpin;  // clear attr
    }

    last_non_attr_dpin = e.second;
  }

  if (!last_non_attr_dpin.is_invalid()) {
    std::tie(result_node, bit_chain_dpin)
        = Lgtuple::flatten_field(result_node, last_non_attr_dpin, bit_chain_dpin, sbits_dpin, ubits_dpin);
  }

  return result_node.setup_driver_pin();
}

std::shared_ptr<Lgtuple> Lgtuple::create_assign(const std::shared_ptr<Lgtuple const>& rhs_tup) const {
  (void)rhs_tup;

  I(false);  // FIXME: implement it
  auto new_tup = std::make_shared<Lgtuple>(get_name());

  return new_tup;
}

bool Lgtuple::add_pending(Node &node, std::vector<std::pair<mmap_lib::str, Node_pin>> &pending_entries,
                          const mmap_lib::str &entry_txt, const Node_pin &ubits_dpin, const Node_pin &sbits_dpin) {
  I(!entry_txt.empty());

  if (!sbits_dpin.is_invalid()) {
    pending_entries.emplace_back(entry_txt, sbits_dpin);
  } else {
    if (ubits_dpin.is_invalid()) {
      Lgraph::info("unable to infer {} size for dp assign (more iterations?)", entry_txt);
      return false;
    }
    auto add_one_node = node.create(Ntype_op::Sum);
    add_one_node.setup_sink_pin("A").connect_driver(ubits_dpin);
    add_one_node.setup_sink_pin("A").connect_driver(node.create_const(1));

    pending_entries.emplace_back(entry_txt, add_one_node.setup_driver_pin());
  }

  return true;
}

std::shared_ptr<Lgtuple> Lgtuple::create_assign(const Node_pin &rhs_dpin) const {
  I(is_correct());

  std::stable_sort(key_map.begin(), key_map.end(), tuple_sort);

  auto tup = std::make_shared<Lgtuple>(get_name());

  // Each field in the LHS must have a size
  auto rhs_node = rhs_dpin.get_node();

  std::vector<std::pair<mmap_lib::str, Node_pin>> pending_entries;
  {
    Node_pin         sbits_dpin;
    Node_pin         ubits_dpin;
    int              pending_pos = -1;  // <0, no pending
    mmap_lib::str non_attr_field;

    for (auto i = 0u; i < key_map.size(); ++i) {
      const auto &e = key_map[i];
      if (is_attribute(e.first)) {
        auto attr_txt = get_last_level(e.first);
        if (attr_txt == "__sbits")
          sbits_dpin = e.second;
        else if (attr_txt == "__ubits")
          ubits_dpin = e.second;

        I(!e.first.empty());
        tup->key_map.emplace_back(e.first, e.second);  // Keep all the attributes

        auto txt = get_all_but_last_level(e.first);

        if (txt == non_attr_field)
          continue;
        if (non_attr_field.empty()) {
          non_attr_field = txt;
          continue;
        }

        // Change in attr fields, so last attr must have an entry
        // E.g: (..., foo.__ubits=2, bar.__sbits=3,...)
        bool ok = add_pending(rhs_node, pending_entries, non_attr_field, ubits_dpin, sbits_dpin);
        if (!ok)
          return nullptr;

        non_attr_field = e.first;
        ubits_dpin     = invalid_dpin;
        sbits_dpin     = invalid_dpin;
        continue;
      }

      if (!non_attr_field.empty() && non_attr_field != e.first) {
        // last attr did not plain entry
        // E.g: (..., foo.__ubits=2, bar=3,...)
        bool ok = add_pending(rhs_node, pending_entries, non_attr_field, ubits_dpin, sbits_dpin);
        if (!ok)
          return nullptr;

        if (pending_pos >= 0 && non_attr_field == key_map[pending_pos].first)
          pending_pos = -1;

        non_attr_field = "";
        ubits_dpin     = invalid_dpin;
        sbits_dpin     = invalid_dpin;
      }

      if (pending_pos < 0 && !e.second.is_type_const()) {
        // E.g: (..., foo.__ubits=2, bar = non_const,...)
        pending_pos = i;
        sbits_dpin  = invalid_dpin;
        ubits_dpin  = invalid_dpin;
        continue;
      }

      if (pending_pos < 0) {
        // E.g: (..., foo.__ubits=2, bar = 3,...)
        I(e.second.is_type_const());
        I(ubits_dpin.is_invalid());
        I(sbits_dpin.is_invalid());

        auto v      = e.second.get_type_const();
        auto v_bits = v.get_bits();

        pending_entries.emplace_back(e.first, rhs_node.create_const(v_bits).setup_driver_pin());
        sbits_dpin = invalid_dpin;
        ubits_dpin = invalid_dpin;
        continue;
      }

      bool ok = add_pending(rhs_node, pending_entries, key_map[pending_pos].first, ubits_dpin, sbits_dpin);
      if (!ok)
        return nullptr;
      pending_pos = -1;
    }

    if (pending_pos >= 0) {
      bool ok = add_pending(rhs_node, pending_entries, key_map[pending_pos].first, ubits_dpin, sbits_dpin);
      if (!ok)
        return nullptr;
    }
  }

  Node_pin current_rhs_dpin = rhs_dpin;

  for (auto i = 0u; i < pending_entries.size(); ++i) {
    auto &e = pending_entries[i];

    auto sext_node = rhs_node.create(Ntype_op::Sext);  // sext(rhs, bits)
    sext_node.setup_sink_pin("a").connect_driver(current_rhs_dpin);
    sext_node.setup_sink_pin("b").connect_driver(e.second);

    I(!e.first.empty());
    tup->key_map.emplace_back(e.first, sext_node.setup_driver_pin());

    if ((i + 1) < pending_entries.size()) {  // no need for last
      auto sra_node = rhs_node.create(Ntype_op::SRA);
      sra_node.setup_sink_pin("a").connect_driver(current_rhs_dpin);
      sra_node.setup_sink_pin("b").connect_driver(e.second);

      current_rhs_dpin = sra_node.setup_driver_pin();
    }
  }

  std::stable_sort(tup->key_map.begin(), tup->key_map.end(), tuple_sort);

  return tup;
}

const Lgtuple::Key_map_type &Lgtuple::get_sort_map() const {
  std::stable_sort(key_map.begin(), key_map.end(), tuple_sort);  // mutable (no semantic check. Just faster to process)
  return key_map;
}

bool Lgtuple::concat(const Node_pin &dpin) {
  auto max_pos = 0;
  for (const auto &e : key_map) {
    int x = 0;
    if (e.first.is_i()) {
      x = e.first.to_i();
    } else if (e.first.front() == ':' && std::isdigit(e.first[1])) {
      x = e.first.substr(1).to_i();
    } else {
      dump();
      Lgraph::info("can not concat pin:{} to tuple unordered {} field {}", dpin.debug_name(), get_name(), e.first);
      return false;
    }
    if (x > max_pos)
      max_pos = x;
  }

  mmap_lib::str new_key(std::to_string(max_pos + 1));

  key_map.emplace_back(new_key, dpin);

  return true;
}

std::tuple<std::shared_ptr<Lgtuple>, bool> Lgtuple::get_mux_tup(const std::vector<std::shared_ptr<Lgtuple const>> &tup_list) {
  I(tup_list.size() > 1);  // nothing to merge?

  // 1st
  //
  //  -Create a fixing_tup with all the entries in tup_list
  //
  //  -If all the tup_list keys point to the same dpin, do not create mux
  //
  //  -Each tuples may have diff name (:0:a, a, 0) which sould be unified/fixed
  //
  // Put the keys after learning (may collapse entries)
  auto fixing_tup = std::make_shared<Lgtuple>(tup_list[0]->get_name());

  // find all the possible keys
  absl::flat_hash_map<mmap_lib::str, Node_pin> key_entries;
  bool                                       first_iter = true;
  for (const auto &tup : tup_list) {
    if (!tup->is_correct())
      fixing_tup->set_issue();

    for (const auto &e : tup->get_map()) {
      auto it = key_entries.find(e.first);
      if (it == key_entries.end()) {
        if (first_iter || is_attribute(e.first)) {
          key_entries.emplace(e.first, e.second);  // There can be replicates like :0:a, a, 0
        } else {
          key_entries.emplace(e.first, invalid_dpin);  // There can be replicates like :0:a, a, 0
        }
      } else if (!it->second.is_invalid()) {
        if (e.second.is_invalid() || e.second != it->second) {
          it->second.invalidate();
        }
      }
    }
    first_iter = false;
  }

  for (const auto &it : key_entries) {
    bool        found = false;
    mmap_lib::str key{it.first};

    for (auto &e : fixing_tup->key_map) {
      std::tie(key, e.first) = learn_fix_int(key, e.first);
      if (key == e.first) {
        e.first = key;                // Put new expanded name
        if (is_attribute(e.first)) {  // Attributes merge if invalid from others
          if (e.second.is_invalid()) {
            e.second = it.second;
          } else if (it.second.is_invalid()) {
            // keep e.second
          } else if (it.second != e.second) {  // bocanth valid but different
            e.second.invalidate();
          }
        } else if (e.second != it.second) {  // Non-attributes invalidate
          e.second.invalidate();
        }
        found = true;
        break;
      }
    }
    if (!found) {
      I(!key.empty());
      fixing_tup->key_map.emplace_back(key, it.second);
    }
  }

  if (fixing_tup->key_map.empty() || (fixing_tup->key_map.size() == 1 && fixing_tup->key_map[0].first == "0")) {
    // Either nothing or key == ""
    return std::tuple(nullptr, false);
  }

  for (const auto &e : fixing_tup->get_map()) {
    if (is_attribute(e.first))
      continue;  // Attributes can not from different paths

    for (const auto &tup : tup_list) {
      if (!tup->has_dpin(e.first)) {
        return std::tuple(fixing_tup, true);  // No need to connect (still pending iterations)
      }
      auto dpin = tup->get_dpin(e.first);
      if (dpin.is_invalid()) {
        return std::tuple(fixing_tup, true);  // No need to connect (still pending iterations)
      }
    }
  }

  return std::tuple(fixing_tup, false);
}

std::vector<Node::Compact> Lgtuple::make_mux(Node &mux_node, Node_pin &sel_dpin,
                                             const std::vector<std::shared_ptr<Lgtuple const>> &tup_list) {
  I(is_correct());

  // 2nd
  //
  //  Create mux if needed, not needed when:
  //
  //  Same dpin in both sizes (e.second.invalid()
  //
  //  Reuse the original mux_node (must reconnect edges)

  std::vector<Node_pin> mux_input_dpins;
  mux_input_dpins.resize(tup_list.size() + 1);  // +1 for sel
  auto n_inputs = 0u;
  for (auto &e : mux_node.inp_edges()) {
    auto pid = e.sink.get_pid();
    I(pid < mux_input_dpins.size());
    mux_input_dpins[pid] = e.driver;
    ++n_inputs;
  }

  if (n_inputs < mux_input_dpins.size()) {
    Node_pin error_dpin;
    for (auto &e : mux_input_dpins) {
      if (!e.is_invalid())
        continue;

      if (error_dpin.is_invalid()) {
        error_dpin = mux_node.create(Ntype_op::CompileErr).setup_driver_pin();
      }
      e = error_dpin;
    }
  }

  std::vector<Node::Compact> mux_list;

  bool mux_node_reused = false;
  for (auto &e : key_map) {
    if (!e.second.is_invalid()) {  // No need to create mux
      continue;
    }

    if (is_attribute(e.first)) {
      continue;
    }

    Node node;
    if (mux_node_reused) {
      node = mux_node.create(Ntype_op::Mux);
      node.setup_sink_pin_raw(0).connect_driver(sel_dpin);
    } else {
      for (auto &spin : mux_node.inp_connected_pins()) {
        if (spin.get_pid())  // keep sel
          spin.del();
      }

      node            = mux_node;
      mux_node_reused = true;
    }
    mux_list.emplace_back(node.get_compact());

    for (auto i = 0u; i < tup_list.size(); ++i) {
      Node_pin dpin;
      if (tup_list[i]->has_dpin(e.first)) {
        dpin = tup_list[i]->get_dpin(e.first);
        I(!dpin.is_invalid());

        if (dpin.is_graph_io()) {
          // NOTE: all the pins but the IOs should have the constraints already.
          // This Attr could be set always but slower and redundant
          for (auto &attr_it : tup_list[i]->get_level_attributes(e.first)) {
            if (Ntype::has_sink(Ntype_op::Flop, mmap_lib::str(attr_it.first.substr(2))))
              continue;  // Do not create attr for flop config (handled in cprop directly)

            fmt::print("adding attr:{}\n", attr_it.first);
            attr_it.second.get_node().dump();

            auto attr_node = dpin.create(Ntype_op::AttrSet);
            {
              auto attr     = get_last_level(attr_it.first);
              auto key_dpin = dpin.create_const(Lconst::from_string(attr)).setup_driver_pin();
              attr_node.setup_sink_pin("field").connect_driver(key_dpin);
            }
            { attr_node.setup_sink_pin("value").connect_driver(attr_it.second); }
            attr_node.setup_sink_pin("parent").connect_driver(dpin);
            dpin = attr_node.setup_driver_pin("Y");
          }
        }
      } else {
        dpin = mux_input_dpins[i + 1];
      }
      node.setup_sink_pin_raw(i + 1).connect_driver(dpin);
    }

    e.second = node.setup_driver_pin();
  }

  return mux_list;
}

std::tuple<mmap_lib::str, bool> Lgtuple::get_flop_name(const Node &flop) const {
  bool             first_flop = true;
  mmap_lib::str    flop_root_name;
  if (flop.get_driver_pin().has_name()) {
    flop_root_name = flop.get_driver_pin().get_name();
    if (has_dpin(flop_root_name))
      first_flop = false;  // Do not allow to use flop directly

    auto n = flop_root_name.find('.');
    if (n != std::string::npos)
      flop_root_name = flop_root_name.substr(0, n);
  } else {
    flop_root_name = name;
  }

  return std::tuple(flop_root_name, first_flop);
}

std::tuple<std::shared_ptr<Lgtuple>, bool> Lgtuple::get_flop_tup(Node &flop) const {
  I(flop.is_type(Ntype_op::Flop));

  auto [flop_root_name, first_flop] = get_flop_name(flop);

  std::shared_ptr<Lgtuple> ret_tup            = std::make_shared<Lgtuple>(flop_root_name);
  bool                     pending_iterations = false;

  if (!is_correct()) {
    ret_tup->set_issue();
    pending_iterations = true;
  }

  auto *lg = flop.get_class_lgraph();

  for (auto &e : key_map) {
    if (e.second.is_invalid()) {
      pending_iterations = true;
    }
    if (is_attribute(e.first))
      continue;

    auto [attr, new_flop_name] = get_flop_attr_name(flop_root_name, e.first);
    I(attr.empty());

    auto dpin = Node_pin::find_driver_pin(lg, new_flop_name);

    if (!dpin.is_invalid()) {
      auto node = dpin.get_node();
      if (node == flop)
        first_flop = false;
    } else if (first_flop) {
      dpin = flop.setup_driver_pin();
      dpin.reset_name(new_flop_name);
      first_flop = false;
    } else {
      I(!e.first.empty());  // "" should be the first in sort, so always first_flop

      dpin = flop.create(Ntype_op::Flop).setup_driver_pin();
      dpin.set_name(new_flop_name);
    }

    I(!e.first.empty());
    ret_tup->key_map.emplace_back(e.first, dpin);
  }

  return std::tuple(ret_tup, pending_iterations);
}

std::pair<mmap_lib::str, mmap_lib::str> Lgtuple::get_flop_attr_name(const mmap_lib::str &flop_root_name, const mmap_lib::str &cname) {
  auto attr = get_attribute(cname);

  mmap_lib::str new_flop_name;
  if (attr.empty()) {
    new_flop_name = mmap_lib::str::concat(flop_root_name, ".", cname);
  } else {
    new_flop_name = mmap_lib::str::concat(flop_root_name, ".", get_all_but_last_level(cname));
  }

  return std::pair(attr, new_flop_name);
}

std::shared_ptr<Lgtuple> Lgtuple::make_flop(Node &flop) const {
  I(is_correct());

  auto [flop_root_name, first_flop] = get_flop_name(flop);
  (void)first_flop;

  std::shared_ptr<Lgtuple> ret_tup;

  std::stable_sort(key_map.begin(), key_map.end(), tuple_sort);  // mutable (no semantic check. Just faster to process)

  auto *lg = flop.get_class_lgraph();

  std::vector<Node>                             all_flops;
  std::vector<std::pair<mmap_lib::str, Node_pin>> multi_flop_attrs;

  for (auto &e : key_map) {
    auto [attr, new_flop_name] = get_flop_attr_name(flop_root_name, e.first);

    auto flop_dpin = Node_pin::find_driver_pin(lg, new_flop_name);

    if (attr.empty()) {            // NON-ATTR PATH
      I(!flop_dpin.is_invalid());  // get_flop_tup ran first, so it should be there
      auto flop_node = flop_dpin.get_node();

      all_flops.emplace_back(flop_node);

      I(!e.second.is_invalid());
      reconnect_flop_if_needed(flop_node, new_flop_name, e.second);

      if (!ret_tup) {
        ret_tup = std::make_shared<Lgtuple>(flop_root_name);
      }
      I(!e.first.empty());
      ret_tup->key_map.emplace_back(e.first, flop_dpin);
      continue;
    }
    // ATTR PATH

    // key_map is sorted. The field before the tuple must be created (or it
    // does not exist, in which case, nothing to do)

    I(is_root_attribute(attr));  // Anything like __initial.3 should become 3.__initial
    if (Ntype::has_sink(Ntype_op::Flop, attr.substr(2))) {
      auto new_flop_name_with_attr = mmap_lib::str::concat(flop_root_name, ".", e.first);
      multi_flop_attrs.emplace_back(new_flop_name_with_attr, e.second);
      continue;
    }

    if (flop_dpin.is_invalid()) {
      Lgraph::info("found attribute:{} but could not bind to flop:{} (missing). It may be OK until convergence",
                   attr,
                   new_flop_name);
      continue;
    }
    auto flop_node = flop_dpin.get_node();

    // If not a FLOP attribute, it may be a plain attribute

    auto flop_din = flop_node.setup_sink_pin("din");
    if (flop_din.is_connected()) {
      auto parent_node = flop_din.get_driver_pin().get_node();
      if (parent_node.is_type(Ntype_op::AttrSet)) {
        auto attr2_dpin = parent_node.get_sink_pin("field").get_driver_pin();
        I(!attr2_dpin.is_invalid());
        I(attr2_dpin.is_type_const());
        auto attr2 = attr2_dpin.get_type_const().to_pyrope();
        if (attr2 == attr)
          continue;  // same attribute already set (can it have different value??)
      }
    }

    auto attr_node = flop_node.create(Ntype_op::AttrSet);
    {
      auto key_dpin = flop_node.create_const(Lconst::from_string(get_last_level(attr))).setup_driver_pin();
      attr_node.setup_sink_pin("field").connect_driver(key_dpin);
    }
    { attr_node.setup_sink_pin("value").connect_driver(e.second); }
    auto flop_din_driver = flop_din.get_driver_pin();
    if (flop_din_driver.is_invalid()) {
      // Disconnected flop?
      Lgraph::info("flop:{} seems disconnected. May be fine or intentional but strange", new_flop_name);
    } else {
      XEdge::del_edge(flop_din_driver, flop_din);
      attr_node.setup_sink_pin("parent").connect_driver(flop_din_driver);
    }

    flop_din.connect_driver(attr_node.setup_driver_pin("Y"));
  }

  I(ret_tup->is_correct());

  std::stable_sort(multi_flop_attrs.begin(), multi_flop_attrs.end(), tuple_sort);  // mutable (no semantic check. Just faster to process)

  for (auto &it : multi_flop_attrs) {
    auto root = get_all_but_last_level(it.first);
    auto attr = get_last_level(it.first);
    I(is_root_attribute(attr));
    attr = attr.substr(2);  // remove __

    for (auto &node : all_flops) {
      if (!root.empty()) {
        auto n = node.get_driver_pin().get_name();
        if (n.substr(0, root.size()) != root)
          continue;  // Only matches update
      }

      auto flop_spin = node.setup_sink_pin(attr);
      if (flop_spin.is_connected()) {
        auto dpin2 = flop_spin.get_driver_pin();
        if (dpin2 == it.second) {  // already correctly connected. Nothing to do
          continue;
        }
        XEdge::del_edge(dpin2, flop_spin);
      }
      flop_spin.connect_driver(it.second);
    }
  }

  return ret_tup;
}

std::vector<std::pair<mmap_lib::str, Node_pin>> Lgtuple::get_level_attributes(const mmap_lib::str &key) const {
  I(!is_root_attribute(key));

  std::vector<std::pair<mmap_lib::str, Node_pin>> v;

  if (key.empty() || is_scalar()) {
    for (const auto &e : key_map) {
      if (!is_attribute(e.first))
        continue;
      v.emplace_back(e.first, e.second);
    }

    return v;
  }

  for (const auto &e : key_map) {
    mmap_lib::str entry{e.first};
    auto             e_pos = match_first_partial(key, entry);
    if (e_pos == 0 || e_pos >= entry.size())
      continue;

    I(entry[e_pos] != '.');  // . not included

    if (!is_root_attribute(entry.substr(e_pos)))
      continue;

    v.emplace_back(entry.substr(e_pos), e.second);
  }

  return v;
}

bool Lgtuple::is_scalar() const {
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

bool Lgtuple::is_ordered() const {
  for (const auto &e : key_map) {
    if (is_root_attribute(e.first))
      continue;

    auto pos = get_first_level_pos(e.first);
    if (pos < 0)
      return false;
  }
  return true;
}

mmap_lib::str Lgtuple::get_scalar_name() const {
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

bool Lgtuple::is_trivial_scalar() const {
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

bool Lgtuple::has_just_attributes() const {
  for (const auto &e : key_map) {
    if (is_attribute(e.first)) {
      continue;
    }
    return false;
  }

  return true;
}

void Lgtuple::dump() const {
  fmt::print("tuple_name: {}{}\n", name, correct ? "" : " ISSUES");
  for (const auto &it : key_map) {
    fmt::print("  key: {} dpin: {}\n", it.first, it.second.debug_name());
  }
}
