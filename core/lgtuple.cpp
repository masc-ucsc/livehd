//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgtuple.hpp"

#include <algorithm>
#include <charconv>

#include "lgraph.hpp"
#include "likely.hpp"

static bool tuple_sort(const std::pair<std::string, Node_pin> &lhs, const std::pair<std::string, Node_pin> &rhs) {
  return lhs.first < rhs.first;
}


std::tuple<bool, size_t, size_t> Lgtuple::match_int_advance(std::string_view a, std::string_view b, size_t a_pos, size_t b_pos) {
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

std::tuple<bool, bool, size_t> Lgtuple::match_int(std::string_view a, std::string_view b) {
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

  if (b[b_pos] == '.')
    ++b_pos;
  if (a[a_pos] == '.')
    ++a_pos;

  return std::make_tuple(a_match, b_match, b_pos);
}

void Lgtuple::append_field(std::string &a, std::string_view b) {
  if (a.empty())
    a = b;
  else
    absl::StrAppend(&a, ".", b);
}

void Lgtuple::learn_fix_int(std::string &a, std::string &b) {
  auto a_last_section = 0u;
  auto b_last_section = 0u;
  auto a_pos          = 0u;
  auto b_pos          = 0u;

  std::string new_a;
  std::string new_b;

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
        append_field(new_a, a.substr(a_last_section, a_pos - a_last_section));
        append_field(new_b, a.substr(a_last_section, a_pos - a_last_section));
      } else {
        append_field(new_a, b.substr(b_last_section, b_pos - b_last_section));
        append_field(new_b, b.substr(b_last_section, b_pos - b_last_section));
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
        append_field(new_a, a.substr(a_last_section, a_last_section - a_pos));
        ++a_pos;
        ++b_pos;
        a_last_section = a_pos;
        b_last_section = b_pos;
        append_field(new_b, a.substr(a_last_section, a_last_section - a_pos));
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
        append_field(new_a, a.substr(b_last_section, b_last_section - b_pos));
        append_field(new_b, a.substr(b_last_section, b_last_section - b_pos));
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
    append_field(new_a, b.substr(b_last_section, b_pos - b_last_section));
    if (a_pos < a.size()) {
      I(a[a_pos] == '.');
      append_field(new_a, a.substr(a_pos + 1));  // +1 to skip .
    }
  } else {
    append_field(new_a, a.substr(a_last_section));
  }
  if (b_last_match) {
    I(a[a_last_section] == ':');
    append_field(new_b, a.substr(a_last_section, a_pos - a_last_section));
    if (b_pos < b.size()) {
      I(b[b_pos] == '.');
      append_field(new_b, b.substr(b_pos + 1));  // +1 to skip .
    }
  } else {
    append_field(new_b, b.substr(b_last_section));
  }

  std::swap(a, new_a);
  std::swap(b, new_b);
}

bool Lgtuple::match(std::string_view a, std::string_view b) {
  if (a == b)
    return true;
  if (a.empty()) {
    if (b=="0" || (b.size()>3 && b.substr(0,3)==":0:"))
      return true;
  }
  if (b.empty()) {
    if (a=="0" || (a.size()>3 && a.substr(0,3)==":0:"))
      return true;
  }

  auto [m1, m2, x] = match_int(a, b);
  (void)x;
  return m1 && m2;  // both reach the end in match_int
}

size_t Lgtuple::match_first_partial(std::string_view a, std::string_view b) {
  auto [m1, m2, x] = match_int(a, b);
  (void)m2;
  if (m1)
    return x;  // a reached the end in match
  return 0;
}

bool Lgtuple::match_either_partial(std::string_view a, std::string_view b) {
  auto [m1, m2, x] = match_int(a, b);
  (void)x;
  return m1 || m2;  // either reached the end it match_int
}

void Lgtuple::add_int(const std::string &key, std::shared_ptr<Lgtuple const> tup) {
  if (tup->is_scalar()) {
    I(!has_dpin(key));  // It was deleted before
    key_map.emplace_back(key, tup->get_dpin());
    return;
  }

  for (auto &ent : tup->key_map) {
    key_map.emplace_back(absl::StrCat(key, ".", ent.first), ent.second);
  }
}

void Lgtuple::reconnect_flop_if_needed(Node &flop, const std::string &flop_name, Node_pin &dpin) {
  flop.setup_driver_pin().reset_name(flop_name);

  auto s_din = flop.setup_sink_pin("din");

  if (s_din.is_connected()) {
    auto d_din = s_din.get_driver_pin();
    if (d_din == dpin)
      return;  // already connected
    XEdge::del_edge(d_din, s_din);
  }

  s_din.connect_driver(dpin);
}

#if 0
int Lgtuple::get_next_free_pos(const std::string &match) const {
  int next_tup_pos = 0;

  for (auto &it : key_map) {
		auto math_pos = match_first_partial(match, it.first);
		if (match_pos != match.size()) // not full match
			continue;

    int v = 0;

    auto last_level = get_last_level(it.first);
		if (last_level[0] == ':')
			last_level = last_level.substr(1);

    if (std::isdigit(last_level[0])) {
			std::from_chars(last_level.data(), last_level.data() + last_level.size(), v);

			if (v >= next_tup_pos) {
				next_tup_pos = v + 1;
			}
		}
  }

  return next_tup_pos;
}
#endif

int Lgtuple::get_first_level_pos(std::string_view key) {
  if (key.empty())
    return -1;

  if (key[0] == ':') {
    key = key.substr(1);
  }

  if (!std::isdigit(key[0])) {
    return -1;
  }

  int x = 0;
  std::from_chars(key.data(), key.data() + key.size(), x);
  return x;
}

std::string_view Lgtuple::get_first_level_name(std::string_view key) {
  if (key.empty())
    return key;

  if (key[0] == ':') {
    auto n = key.substr(1).find(':');
    I(n != std::string::npos);
    return key.substr(n + 1);
  }
  if (std::isdigit(key[0])) {
    return "";
  }

  return key;
}

std::string_view Lgtuple::get_last_level(std::string_view key) {
  auto n = key.find_last_of('.');
  if (n == std::string::npos)
    return key;

  I(n != 0);  // name can not start with a .

  // If there are attributes, show keep them.
  if (key.substr(n, 3) == ".__") {
    auto n2 = key.substr(0, n - 1).find_last_of('.');
    if (n2 != std::string::npos) {
      return key.substr(n2 + 1);
    }
  }
  return key.substr(n + 1);
}

std::string_view Lgtuple::get_all_but_last_level(std::string_view key) {
  auto n = key.find_last_of('.');
  if (n != std::string::npos) {
    return key.substr(0, n);
  }

  return "";
}

std::string_view Lgtuple::get_all_but_first_level(std::string_view key) {
  auto n = key.find_first_of('.');
  if (n != std::string::npos) {
    return key.substr(n + 1);
  }

  return "";  // empty if no dot left
}

std::string Lgtuple::learn_fix(std::string_view a) {
  std::string key{a};

  for (auto &e : key_map) {
    learn_fix_int(key, e.first);
  }

  return key;
}

const Node_pin &Lgtuple::get_dpin(std::string_view key) const {
  for (auto &e : key_map) {
    if (match(e.first, key))
      return e.second;
  }

  return invalid_dpin;
}

bool Lgtuple::has_dpin(std::string_view key) const {
  for (auto &e : key_map) {
    if (match(e.first, key))
      return true;
  }
  return false;
}

std::shared_ptr<Lgtuple> Lgtuple::get_sub_tuple(std::string_view key) const {
  if (key.empty()) {
    return std::make_shared<Lgtuple>(*this);
  }

  I(!key.empty());  // do not call without sub-fields

  std::shared_ptr<Lgtuple> tup;

  for (auto &e : key_map) {
    std::string_view entry(e.first);
    if (key=="0" && e.first.empty()) {
      if (!tup)
        tup = std::make_shared<Lgtuple>(get_name());
      tup->key_map.emplace_back("", e.second);
      continue;
    }
    auto             e_pos = match_first_partial(key, entry);
    if (e_pos == 0)
      continue;
    I(entry[e_pos] != '.');  // . not included

    if (!tup) {
      std::string key_with_pos{key};
      std::string expanded{e.first};
      learn_fix_int(key_with_pos, expanded);
      tup = std::make_shared<Lgtuple>(absl::StrCat(name, ".", key_with_pos));
    }

    if (e_pos > entry.size())
      tup->key_map.emplace_back("", e.second);
    else
      tup->key_map.emplace_back(entry.substr(e_pos), e.second);
  }

  return tup;
}

std::shared_ptr<Lgtuple> Lgtuple::get_sub_tuple(std::shared_ptr<Lgtuple const> tup) const {
  // create a dpin or subtuple with the selected fields
  std::shared_ptr<Lgtuple> ret_tup;

  int pos = 0;
  for (auto e : tup->key_map) {
    std::string_view e_name{e.first};
    (void)e_name;
    auto e_node = e.second.get_node();
    if (!e_node.is_type_const()) {
      Lgraph::info("tuple {} can not be indexed with {} key:{} because it is not constant", get_name(), tup->get_name(), e.first);
      return nullptr;
    }
    auto        v = e_node.get_type_const();
    std::string txt;
    if (v.is_i()) {
      txt = std::to_string(v.to_i());
    } else {
      txt = v.to_string();
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

  return ret_tup;
}

void Lgtuple::del(std::string_view key) {
  if (key.empty()) {
    key_map.clear();
    return;
  }

  Key_map_type new_map;

  bool is_attr_key = is_root_attribute(key);

  for (auto i = 0u; i < key_map.size(); ++i) {
    std::string_view entry{key_map[i].first};
    if (entry.empty()) {
      if (is_attr_key) {
        new_map.emplace_back(std::move(key_map[i]));
      }
      continue; // "" keys must be gone by now
    }

    auto             e_pos = match_first_partial(key, entry);
    if (e_pos == 0) {
      new_map.emplace_back(std::move(key_map[i]));
      continue;
    }
    if (e_pos >= entry.size())
      continue;  // full match?

    I(entry[e_pos] != '.');  // not . included

    if (is_root_attribute(entry.substr(e_pos))) {
      new_map.emplace_back(std::move(key_map[i]));
      continue;  // Keep the attributes
    }
  }

  key_map.swap(new_map);
}

void Lgtuple::add(std::string_view key, std::shared_ptr<Lgtuple const> tup) {
  I(!key.empty());

  for (const auto &it : tup->key_map) {
    if (it.first.empty()) {
      add(key, it.second);
    } else {
      std::string new_key = absl::StrCat(key, ".", it.first);
      add(new_key, it.second);
    }
  }
}

void Lgtuple::add(std::string_view key, const Node_pin &dpin) {

  if (!key_map.empty()) {
    if (key.empty()) {
      key = "0";
    }
    for(auto &it:key_map) {
      if (it.first.empty()) {
        it.first = "0";
      }else if (is_root_attribute(it.first)) {
        it.first = absl::StrCat("0.", it.first);
      }
    }
  }

  auto fixed_key = learn_fix(key);

  del(key);

  key_map.emplace_back(fixed_key, dpin);

  if (is_trivial_scalar()) {
    for(auto &it:key_map) {
      if (it.first == "0") {
        it.first = "";
      }else if (is_attribute(it.first)) {
        auto f = get_all_but_last_level(it.first);
        if (f == "0") {
          auto attr = get_last_level(it.first);
          it.first = attr;
        }
      }
    }
  }
}

bool Lgtuple::concat(std::shared_ptr<Lgtuple const> tup) {
  bool ok = true;

  std::vector<std::pair<std::string, Node_pin>> delayed_numbers;

  for (auto &it : tup->key_map) {
    if (has_dpin(it.first)) {
      if (std::isdigit(it.first[0]) && is_single_level(it.first)) {
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
    key_map.emplace_back(fixed_key, it.second);
  }

  if (delayed_numbers.size()) {
    auto max_pos = 0;
    for (const auto &e : key_map) {
      if (e.first.empty()) {
        dump();
        Lgraph::info("can not concat pin to tuple {} when some are unnamed", get_name());
        return false;
      }
      int x = 0;
      if (std::isdigit(e.first[0])) {
        std::from_chars(e.first.data(), e.first.data() + e.first.size(), x);
      } else if (e.first[0] == ':' && std::isdigit(e.first[1])) {
        std::from_chars(e.first.data() + 1, e.first.data() + e.first.size() - 1, x);
      } else {
        dump();
        Lgraph::info("can not concat pin to tuple unordered {} field {}", get_name(), e.first);
        return false;
      }
      if (x > max_pos)
        max_pos = x;
    }
    for (const auto &e : delayed_numbers) {
      int x = 0;
      std::from_chars(e.first.data(), e.first.data() + e.first.size(), x);
      key_map.emplace_back(std::to_string(x + max_pos + 1), e.second);
    }
  }

  return ok;
}

bool Lgtuple::concat(const Node_pin &dpin) {
  if (key_map.size() == 1 && key_map[0].first.empty()) {
#if 0
    // Not right to concat
    if (key_map[0].second.is_type_const() && dpin.is_type_const()) {
      auto v1 = key_map[0].second.get_node().get_type_const();
      auto v2 = dpin.get_node().get_type_const();

      auto res = v2.concat_op(v1);

      auto new_dpin = dpin.get_lg()->create_node_const(res).setup_driver_pin();
      key_map[0].second = new_dpin;
      return true;
    }
#endif
    key_map[0].first = "0";
    key_map.emplace_back("1", dpin);
    return true;
  }

  auto max_pos = 0;
  for (const auto &e : key_map) {
    if (e.first.empty()) {
      dump();
      Lgraph::info("can not concat pin to tuple {} when some are unnamed", get_name());
      return false;
    }
    int x = 0;
    if (std::isdigit(e.first[0])) {
      std::from_chars(e.first.data(), e.first.data() + e.first.size(), x);
    } else if (e.first[0] == ':' && std::isdigit(e.first[1])) {
      std::from_chars(e.first.data() + 1, e.first.data() + e.first.size() - 1, x);
    } else {
      dump();
      Lgraph::info("can not concat pin to tuple unordered {} field {}", get_name(), e.first);
      return false;
    }
    if (x > max_pos)
      max_pos = x;
  }

  key_map.emplace_back(std::to_string(max_pos + 1), dpin);

  return true;
}

std::shared_ptr<Lgtuple> Lgtuple::make_mux(Node &mux_node, Node_pin &sel_dpin,
                                           const std::vector<std::shared_ptr<Lgtuple const>> &tup_list) {
  I(tup_list.size() > 1);  // nothing to merge?

  // 1st
  //
  //  -Create a fixing_tup with all the entries in tup_list
  //
  //  -If all the tup_list keys point to the same dpin, do not create mux
  //
  //  -Each tuples may have diff name (:0:a, a, 0) which sould be unified/fixed

  // find all the possible keys
  absl::flat_hash_map<std::string, Node_pin> key_entries;
  for (auto tup : tup_list) {
    for (const auto &e : tup->get_map()) {
      auto it = key_entries.find(e.first);
      if (it == key_entries.end()) {
        key_entries.emplace(e.first, e.second);  // There can be replicates like :0:a, a, 0
      }else if (!it->second.is_invalid() && e.second != it->second) {
        it->second.invalidate();
      }
    }
  }

  // Put the keys after learning (may collapse entries)
  auto fixing_tup = std::make_shared<Lgtuple>(tup_list[0]->get_name());

  for (auto it : key_entries) {
    bool found = false;
    std::string key{it.first};

    for (auto &e : fixing_tup->key_map) {
      learn_fix_int(key, e.first);
      if (key == e.first) {
        e.first = key; // Put new expanded name
        if (is_attribute(e.first)) { // Attributes merge if invalid from others
          if (e.second.is_invalid()) {
            e.second = it.second;
          }else if (it.second.is_invalid()) {
            // keep e.second
          }else if (it.second != e.second) { // both valid but different
            e.second.invalidate();
          }
        }else if (e.second != it.second) { // Non-attributes invalidate
          e.second.invalidate();
        }
        found = true;
        break;
      }
    }
    if (!found) {
      fixing_tup->key_map.emplace_back(key, it.second);
    }
  }

  if (fixing_tup->key_map.empty() || (fixing_tup->key_map.size()==1 && fixing_tup->key_map[0].first.empty()) ) {
    // Either nothing or key == ""
    return nullptr;
  }

  // 2nd
  //
  //  Create mux if needed, not needed when:
  //
  //  Same dpin in both sizes (e.second.invalid()
  //
  //  Reuse the original mux_node (must reconnect edges)

  std::vector<Node_pin> mux_input_dpins;
  mux_input_dpins.resize(tup_list.size() + 1);  // +1 for sel
  auto n_inputs=0u;
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

  bool mux_node_reused = false;
  for (auto e_index = 0u; e_index < fixing_tup->key_map.size(); ++e_index) {
    auto &e = fixing_tup->key_map[e_index];
    if (!e.second.is_invalid()) {  // No need to create mux
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

    for (auto i = 0u; i < tup_list.size(); ++i) {
      Node_pin dpin;
      if (tup_list[i]->has_dpin(e.first)) {
        dpin = tup_list[i]->get_dpin(e.first);
        I(!dpin.is_invalid());
      } else {
        dpin = mux_input_dpins[i + 1];
      }
      node.setup_sink_pin_raw(i + 1).connect_driver(dpin);
    }

    e.second = node.setup_driver_pin();
  }

  return fixing_tup;
}

std::shared_ptr<Lgtuple> Lgtuple::make_flop(Node &flop) const {
  I(flop.is_type(Ntype_op::Flop));

  std::string_view flop_name;
  if (flop.get_driver_pin().has_name())
    flop_name = flop.get_driver_pin().get_name();
  else
    flop_name = name;

  bool        first_flop = true;
  std::string flop_root_name;

  if (is_single_level(flop_name)) {
    flop_root_name = flop_name;
  } else {
    flop_root_name = get_first_level_name(flop_name);
    if (has_dpin(flop_name))
      first_flop = false;
  }

  std::shared_ptr<Lgtuple> ret_tup;

  std::sort(key_map.begin(), key_map.end(), tuple_sort); // mutable (no semantic check. Just faster to process)

  auto *lg   = flop.get_class_lgraph();

  for (auto &e : key_map) {
    if (e.second.get_node() == flop)
      continue;  // no loop to itself

    std::string new_flop_name;
    if(e.first.empty())
      new_flop_name = flop_root_name;
    else
      new_flop_name = absl::StrCat(flop_root_name, ".", e.first);

    if (is_attribute(e.first)) {
      // key_map is sorted. The field before the tuple must be created (or it
      // does not exist, in which case, nothing to do)

      auto attr          = get_last_level(e.first);
      auto key           = get_all_but_last_level(new_flop_name);
      auto dpin          = Node_pin::find_driver_pin(lg, key);
      if (dpin.is_invalid()) {
        Lgraph::info("found attribute:{} but could not bind to flop:{} (missing). It may be OK until convergence", attr, new_flop_name);
        continue;
      }
      auto flop_node = dpin.get_node();

      if (Ntype::is_valid_sink(Ntype_op::Flop, attr.substr(2))) { // Is this a FLOP attribute (reset, initial...)
        auto flop_spin = flop_node.setup_sink_pin(attr.substr(2));
        if (flop_spin.is_connected()) {
          auto dpin2 = flop_spin.get_driver_pin();
          if (dpin2 == dpin) { // already correctly connected. Nothing to do
            continue;
          }
          XEdge::del_edge(dpin2, flop_spin);
        }
        flop_spin.connect_driver(e.second);
      } else {
        // If not a FLOP attribute, it may be a plain attribute

        auto flop_din = flop_node.setup_sink_pin("din");
        if (flop_din.is_connected()) {
          auto parent_node = flop_din.get_driver_pin().get_node();
          if (parent_node.is_type(Ntype_op::AttrSet)) {
            auto attr2_dpin = parent_node.get_sink_pin("field").get_driver_pin();
            I(!attr2_dpin.is_invalid());
            auto attr2 = attr2_dpin.get_name();
            if (attr2 == attr)
              continue; // same attribute already set (can it have different value??)
          }
        }

        auto attr_node = lg->create_node(Ntype_op::AttrSet);
        {
          auto key_dpin = lg->create_node_const(attr).setup_driver_pin();
          attr_node.setup_sink_pin("field").connect_driver(key_dpin);
        }
        {
          attr_node.setup_sink_pin("value").connect_driver(e.second);
        }
        auto flop_din_driver = flop_din.get_driver_pin();
        if (flop_din_driver.is_invalid()) {
          // Disconnected flop?
          Lgraph::info("flop:{} seems disconnected. May be fine or intentional but strange", new_flop_name);
        }else{
          XEdge::del_edge(flop_din_driver, flop_din);
          attr_node.setup_sink_pin("name").connect_driver(flop_din_driver);
        }

        flop_din.connect_driver(attr_node.setup_driver_pin("Y"));
      }
      continue;
    }
    Node node;


    if (first_flop) {
      node       = flop;
      first_flop = false;
    } else {
      I(!e.first.empty()); // "" should be the first in sort, so always first_flop

      auto  dpin = Node_pin::find_driver_pin(lg, new_flop_name);
      if (dpin.is_invalid()) {
        node = lg->create_node(Ntype_op::Flop);
        // Just clone the Flop fields/attributes
        for (const auto &e2 : flop.inp_edges()) {
          if (e2.sink.get_pin_name() == "din")
            continue;
          auto spin = node.setup_sink_pin(e2.sink.get_pin_name());
          spin.connect_driver(e2.driver);
        }
      } else {
        node = dpin.get_node();
      }
    }

    reconnect_flop_if_needed(node, new_flop_name, e.second);

    if (!ret_tup) {
      ret_tup = std::make_shared<Lgtuple>(flop_root_name);
    }
    ret_tup->key_map.emplace_back(e.first, node.setup_driver_pin());
  }

  return ret_tup;
}

std::vector<std::pair<std::string, Node_pin>> Lgtuple::get_level_attributes(std::string_view key) const {
  I(!is_root_attribute(key));

  std::vector<std::pair<std::string, Node_pin>> v;

  if (key.empty() || is_scalar()) {
    for (const auto &e : key_map) {
      if (!is_attribute(e.first))
        continue;
      v.emplace_back(e.first, e.second);
    }

    return v;
  }

  for (const auto &e : key_map) {
    std::string_view entry{e.first};
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
  for(const auto &e:key_map) {
    if (is_attribute(e.first))
      continue;
    if (conta>0)
      return false;
    ++conta;
  }
  return true;
}

bool Lgtuple::is_trivial_scalar() const {
  auto conta       = 0;

  for(const auto &e:key_map) {
    std::string_view field{e.first};

    if (is_attribute(field)) {
      field = get_all_but_last_level(field);
    }else{
      if (conta>0)
        return false;
      ++conta;
    }
    if (field.empty() || field == "0")
      continue;

    return false;
  }

  return true;
}


void Lgtuple::dump() const {
  fmt::print("tuple_name: {}\n", name);
  for (const auto &it : key_map) {
    fmt::print("  key: {} dpin: {}\n", it.first, it.second.debug_name());
  }
}
