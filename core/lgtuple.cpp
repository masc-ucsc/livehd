//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgtuple.hpp"

#include <charconv>

#include "lgraph.hpp"
#include "likely.hpp"

Lgtuple::key_map_type::const_iterator Lgtuple::get_it(std::string_view key) const {
  key_map_type::const_iterator it = key_map.find(std::string{key});

  return it;
}

std::string Lgtuple::get_attribute_field(const std::string &key) {

  if (key.substr(0,2) == "__")
    return key;

  auto n = key.find(".__");

  if (n == std::string::npos)
    return ""; // no atr

  I(key.substr(n+1).find(".__") == std::string::npos);

  return key.substr(n+1);
}

std::string Lgtuple::get_last_level(const std::string &key) {
  std::string last_key{key};

  auto n = last_key.find_last_of('.');
  if (n && n != std::string::npos) {
    // If there are attributes, show keep them.
    if (last_key.substr(n,3) == ".__") {
      auto n2 = last_key.substr(0,n-1).find_last_of('.');
      if (n2 != std::string::npos) {
        return last_key.substr(n2 + 1);
      }
    }
    last_key = last_key.substr(n + 1);
  }

  return last_key;
}

std::string Lgtuple::get_all_but_last_level(const std::string &key) {
  std::string last_key{""};

  auto n = key.find_last_of('.');
  if (n != std::string::npos) {
    last_key = key.substr(0, n);
  }

  return last_key;
}

std::string Lgtuple::get_remove_first_level(const std::string &key) const {
  std::string first_level;

  auto n = key.find_first_of('.');
  if (n != std::string::npos) {
    first_level = key.substr(n + 1);
  }

  return first_level;  // empty if no dot left
}

void Lgtuple::add_int(const std::string &key, std::shared_ptr<Lgtuple const> tup) {
  if (tup->is_scalar()) {
    key_map.insert_or_assign(key, tup->get_dpin());
    return;
  }

  std::string key_base{key + "."};

  for (auto ent : tup->key_map) {
    key_map.insert_or_assign(key_base + ent.first, ent.second);
  }

  for (auto ent : tup->pos2key_map) {
    pos2key_map.insert_or_assign(key_base + ent.first, key_base + ent.second);
  }

  for (auto ent : tup->key2pos_map) {
    key2pos_map.insert_or_assign(key_base + ent.first, key_base + ent.second);
  }
}

std::shared_ptr<Lgtuple> Lgtuple::make_merge(Node_pin &sel_dpin, const std::vector<std::shared_ptr<Lgtuple const>> &tup_list) {
  (void)sel_dpin;
  I(tup_list.size() > 1);  // nothing to merge?

#if 0
  auto cur_tup = tup_list.back();
  auto cur_id  = 0u;
  tup_list.pop_back();

  auto new_tup = std::make_shared<Lgtuple>(cur_tup->get_name());

  std::vector<Node_pin> tup_dpins;

  for(auto it:cur_tup) {
    auto it_name = it->first;
    auto it_dpin = it->second;

    tup_dpins.clear();
    tup_dpins.resize(tup_list.size());
    tup_dpins[cur_id] = it_dpin;

    for (auto i = cur_id+1; i < tup_list.size(); ++i) {
      auot it2 = tup_list[i].find(it_name);
      if (it2 != tup_list[i].end()) {
        tup_dpins[i] = it2->second;
      }
    }

    bool all_same_dpin = true;
    for(auto dpin:dup_dpins) {
      if (dpin != it_dpin) {
        all_same_dpin = false;
        break;
      }
    }
    if (all_same_dpin) {
      new_tup.add(it_pos, it_name, it_dpin);
    }

  }

  return new_tup;

#else
  std::vector<key_map_type::const_iterator> its;
  for (const auto &tup : tup_list) {
    its.emplace_back(tup->key_map.begin());
  }

  auto tup0    = tup_list[0];
  auto new_tup = std::make_shared<Lgtuple>(tup0->get_name());

  while (true) {
    bool  same_names = true;
    bool  same_dpins = true;
    auto &it0        = its[0];
    int   use_pos    = tup0->get_pos(it0->first);
    for (auto i = 1u; i < its.size(); ++i) {
      same_names &= (it0->first  == its[i]->first);
      same_dpins &= (it0->second == its[i]->second);
      auto v = tup_list[i]->get_pos(its[i]->first);
      if (v != -1 && v != use_pos) {
        LGraph::info("tuples {} and {} have fields {} and {} at different positions {} vs {}",
                     tup0->get_name(),
                     tup_list[i]->get_name(),
                     it0->first,
                     its[i]->first,
                     tup0->get_pos(it0->first),
                     tup_list[i]->get_pos(its[i]->first));
        return nullptr;
      }
    }

    if (same_names && same_dpins) {
      new_tup->add(it0->first, it0->second);
    } else if (same_names && !same_dpins) {
      auto mux_node = sel_dpin.get_class_lgraph()->create_node(Ntype_op::Mux);

      mux_node.setup_sink_pin("0").connect_driver(sel_dpin);
      for (auto i = 0u; i < its.size(); ++i) {
        mux_node.setup_sink_pin_raw(i + 1).connect_driver(its[i]->second);
        I(mux_node.get_sink_pin(std::to_string(i + 1)).get_pid() == i + 1);
      }
      new_tup->add(it0->first, mux_node.setup_driver_pin());

    } else {
      std::string name_list;
      for (auto &it : its) {
        if (name_list.empty())
          name_list = it->first;
        else
          name_list += " vs " + it->first;
      }
      LGraph::info("tuples {} at mux do not match names {}", tup0->get_name(), name_list);
      return nullptr;
    }

    for (auto &it : its) {
      ++it;
    }

    if (it0 == tup0->key_map.end()) {
      bool failed = false;
      for (auto i = 1u; i < its.size(); ++i) {
        if (its[i] != tup_list[i]->key_map.end()) {
          LGraph::info("tuple {} has an extra field {} not in the other control paths", tup_list[i]->get_name(), its[i]->first);
          failed = true;
        }
      }
      if (failed)
        return nullptr;

      break;
    }
  }
  return new_tup;
#endif
}

std::tuple<Node_pin, std::shared_ptr<Lgtuple>> Lgtuple::make_select(Node_pin &sel_dpin) const {
  Node_pin              invalid;
  std::vector<Node_pin> mux_dpins;
  // std::vector<std::shared_ptr<Lgtuple const>> mux_tups;

  for (const auto &ent : key_map) {
    I(get_last_level(ent.first) == ent.first);  // FIXME: No sub-tuples for the moment

    size_t pos = 0;
    if (std::isdigit(ent.first[0])) {
      pos = std::atoi(ent.first.c_str());
    } else {
      const auto it = key2pos_map.find(ent.first);
      if (it == key2pos_map.end()) {
        LGraph::info("tuple {} has a non ordered field {}. Impossible to index by number", get_name(), ent.first);
        return std::tuple(invalid, nullptr);
      }
      pos = std::atoi(it->second.c_str());
    }
    if (pos >= mux_dpins.size())
      mux_dpins.resize(pos + 1);

    mux_dpins[pos] = ent.second;
  }

  auto mux_node = sel_dpin.get_class_lgraph()->create_node(Ntype_op::Mux);
  mux_node.setup_sink_pin("0").connect_driver(sel_dpin);

  int pid = 1;
  for (const auto &dpin : mux_dpins) {
    mux_node.setup_sink_pin_raw(pid).connect_driver(dpin);
    ++pid;
  }

  return std::tuple(mux_node.setup_driver_pin(), nullptr);
}

int Lgtuple::get_pos(std::string_view key) const {
  auto last_key = get_last_level(std::string{key});

  if (last_key[0] >= '0' && last_key[0] <= '9') {
    int x = 0;
    std::from_chars(last_key.data(), last_key.data() + last_key.size(), x);
    return x;
  }

  auto k2p_it = key2pos_map.find(std::string{key});
  if (k2p_it == key2pos_map.end())
    return -1;

  last_key = get_last_level(k2p_it->second);
  I(last_key[0] >= '0' && last_key[0] <= '9');

  int x = 0;
  std::from_chars(last_key.data(), last_key.data() + last_key.size(), x);
  return x;
}

const Node_pin &Lgtuple::get_dpin(int pos) const {
  auto it = get_it(pos);
  if (unlikely(it == key_map.end())) {
    LGraph::info("pos:{} does not exist in tuple:{}", pos, name);
    dump();
    return invalid_dpin;
  }

  return it->second;
}

const Node_pin &Lgtuple::get_dpin(std::string_view key) const {
  auto it = get_it(key);
  if (unlikely(it == key_map.end())) {
    LGraph::info("key:{} does not exist in tuple:{}", key, name);
    dump();
    return invalid_dpin;
  }

  return it->second;
}

// get_dpin == get_value_driver_pin
const Node_pin &Lgtuple::get_dpin(int pos, std::string_view key) const {
  auto it = get_it(pos, key);
  if (unlikely(it == key_map.end())) {
    LGraph::info("pos:{} key:{} does not exist in tuple:{}", pos, key, name);
    dump();
    return invalid_dpin;
  }

  return it->second;
}

std::shared_ptr<Lgtuple> Lgtuple::get_sub_tuple(int pos, std::string_view key) const {
  (void)pos;
  (void)key;

  I(pos >= 0 || !key.empty());  // do not call without sub-fields

  std::shared_ptr<Lgtuple> tup;

  auto it      = get_lower_it(pos, key);
  auto str_pos = std::to_string(pos);
  while (it != key_map.end()) {
    if (str_pos.size() <= it->first.size() && it->first.substr(0, str_pos.size()) == str_pos) {
      if (!tup)
        tup = std::make_shared<Lgtuple>("");  // no name

      auto rest_name = get_remove_first_level(it->first);
      tup->key_map.insert_or_assign(rest_name, it->second);
    } else if (key.size() && key.size() <= it->first.size() && it->first.substr(0, key.size()) == key) {
      if (!tup)
        tup = std::make_shared<Lgtuple>("");  // no name

      auto rest_name = get_remove_first_level(it->first);
      tup->key_map.insert_or_assign(rest_name, it->second);
      if (rest_name.size()) {
        auto it2 = key2pos_map.find(it->first);
        if (it2 != key2pos_map.end()) {
          tup->key2pos_map.insert_or_assign(rest_name, it2->second);
          tup->pos2key_map.insert_or_assign(it2->second, rest_name);
        }
      }
    } else {
      break;
    }
    ++it;
  }

  return tup;
}

std::shared_ptr<Lgtuple> Lgtuple::get_sub_tuple(int pos) const {
  (void)pos;
  std::shared_ptr<Lgtuple> tup;

  I(false);  // FIXME: implement it

  return tup;
}

std::shared_ptr<Lgtuple> Lgtuple::get_sub_tuple(std::string_view key) const {
  (void)key;
  std::shared_ptr<Lgtuple> tup;

  I(false);  // FIXME: implement it

  return tup;
}

void Lgtuple::del(std::string_view key) {
  if (key.empty())
    return;

  std::string key1_match{key};
  std::string key2_match{std::string{key} + "."};
  std::string key2_attr_match{std::string{key} + ".__"};

  for (auto it = key_map.begin(); it != key_map.end();) {  // delete any match to key (even subkeys like key.foo.bar)
    const auto &n = it->first;
    if (key1_match.size() == n.size() && strcasecmp(n.c_str(), key1_match.data()) == 0) {  // exact
      auto k2p_it = key2pos_map.find(it->first);
      if (k2p_it != key2pos_map.end()) {
        pos2key_map.erase(k2p_it->second);
        key2pos_map.erase(k2p_it);
      }
      it = key_map.erase(it);
    } else if (key2_match.size() <= n.size() && strncasecmp(n.c_str(), key2_match.data(), key2_match.size()) == 0) {  // subkeys
      if (key2_attr_match.size() <= n.size() && strncasecmp(n.c_str(), key2_attr_match.c_str(), key2_attr_match.size()) == 0) {
        ++it;  // Do not remove hidden or attributes
      } else {
        auto k2p_it = key2pos_map.find(it->first);
        if (k2p_it != key2pos_map.end()) {
          pos2key_map.erase(k2p_it->second);
          key2pos_map.erase(k2p_it);
        }
        it = key_map.erase(it);
      }
    } else {
      ++it;
    }
  }
}

void Lgtuple::del(int pos) {
  //I(false);
  auto str_pos = std::to_string(pos);
  auto p2k_it  = pos2key_map.find(str_pos);
  if (p2k_it == pos2key_map.end())
    return del(str_pos);  // case: there is no key

  return del(p2k_it->second);  // case: there is key and pos
}

void Lgtuple::add(int pos, std::string_view key, std::shared_ptr<Lgtuple const> tup) {
  bool only_attr_add = true;
  for (const auto &it : tup->key_map) {
    if (it.first.size() > 2 && it.first.substr(0, 2) == "__")
      continue;
    only_attr_add = false;
    break;
  }
  if (!only_attr_add)
    del(key);

  std::string key_str{key};
  if (pos >= 0 && key.empty()) {  // fix position
    key_str = std::to_string(pos);
  } else if (pos >= 0 && !std::isdigit(key_str[0])) {  // fix position & key
    pos2key_map.insert_or_assign(std::to_string(pos), key_str);
    key2pos_map.insert_or_assign(key_str, std::to_string(pos));
  } else if (pos < 0 && key.empty() && !key_map.empty()) {  // append (no key or position)
    auto v  = get_next_free_pos("");
    key_str = std::to_string(v);
  }

  add_int(key_str, tup);
}

void Lgtuple::add(int pos, std::string_view key, const Node_pin &dpin) {
  del(key);

  std::string key_str{key};
  if (pos >= 0 && key.empty()) {  // fix position
    key_str = std::to_string(pos);
  } else if (pos >= 0 && !std::isdigit(key_str[0])) {  // fix position & key
    pos2key_map.insert_or_assign(std::to_string(pos), key_str);
    key2pos_map.insert_or_assign(key_str, std::to_string(pos));
  } else if (pos < 0 && key.empty() && !key_map.empty()) {  // append (no key or position)
    auto v  = get_next_free_pos("");
    key_str = std::to_string(v);
  }

  key_map.insert_or_assign(key_str, dpin);
}

void Lgtuple::add(std::string_view key, std::shared_ptr<Lgtuple const> tup) {
  del(key);

  add_int(std::string{key}, tup);
}

void Lgtuple::add(std::string_view key, const Node_pin &dpin) {
  auto        k2p_it = key2pos_map.find(key);
  std::string pos_match;
  if (k2p_it != key2pos_map.end()) {
    pos_match = k2p_it->second;
  }

  del(key);

  if (!pos_match.empty()) {  // preserve pos if existed before
    pos2key_map.insert_or_assign(pos_match, key);
    key2pos_map.insert_or_assign(key, pos_match);
  }

  key_map.insert_or_assign(std::string{key}, dpin);
}

void Lgtuple::add(int pos, std::shared_ptr<Lgtuple const> tup) {
  auto        str_pos = std::to_string(pos);
  auto        p2k_it  = pos2key_map.find(str_pos);
  std::string key_match;
  if (p2k_it == pos2key_map.end()) {
    key_match = p2k_it->second;
  }

  del(pos);

  if (!key_match.empty()) {  // preserve key name if existed before
    pos2key_map.insert_or_assign(str_pos, key_match);
    key2pos_map.insert_or_assign(key_match, str_pos);
  }

  add_int(str_pos, tup);
}

void Lgtuple::add(int pos, const Node_pin &dpin) {
  auto        str_pos = std::to_string(pos);
  auto        p2k_it  = pos2key_map.find(str_pos);
  std::string key_match;
  if (p2k_it != pos2key_map.end()) {
    key_match = p2k_it->second;
  }

  del(pos);

  if (!key_match.empty()) {  // preserve key name if existed before
    pos2key_map.insert_or_assign(str_pos, key_match);
    key2pos_map.insert_or_assign(key_match, str_pos);
    key_map.insert_or_assign(key_match, dpin);
  } else {
    key_map.insert_or_assign(str_pos, dpin);
  }
}

void Lgtuple::add(const Node_pin &dpin) { add("", dpin); }

int Lgtuple::get_next_free_pos(const std::string &match) const {
  int next_tup_pos = 0;
  for (auto it : key_map) {
    int v = 0;
    if (strncasecmp(it.first.c_str(), match.c_str(), match.size()) != 0)
      continue;

    auto last_level = get_last_level(it.first);
    if (std::isdigit(last_level[0])) {
      v = std::atoi(last_level.c_str());
    } else {
      const auto it2 = key2pos_map.find(it.first);
      if (it2 != key2pos_map.end()) {
        auto str = get_last_level(it2->second);
        v        = std::atoi(str.c_str());
      } else {
        continue;
      }
    }

    if (v >= next_tup_pos) {
      next_tup_pos = v + 1;
    }
  }

  return next_tup_pos;
}

bool Lgtuple::concat(std::shared_ptr<Lgtuple const> tup) {
  bool ok = true;

  for (auto it : tup->key_map) {
    if (key_map.find(it.first) == key_map.end()) {
      key_map.insert_or_assign(it.first, it.second);
      continue;
    }

    auto last_level = get_last_level(it.first);
    if (!std::isdigit(last_level[0])) {
      dump();
      tup->dump();
      LGraph::info("tuples {} and {} can not concat for key:{}", get_name(), tup->get_name(), it.first);
      ok = false;
      continue;
    }
    I(tup->pos2key_map.find(it.first) == tup->pos2key_map.end());  // It was a pos>=0, key.empty(), so no pos2key_map

    auto str     = get_all_but_last_level(it.first);
    auto v       = get_next_free_pos(str);
    auto pos_str = std::to_string(v);

    if (str.empty())
      key_map.insert_or_assign(pos_str, it.second);
    else
      key_map.insert_or_assign(str + "." + pos_str, it.second);
  }

  for (auto it : tup->pos2key_map) {
    if (pos2key_map.find(it.first) != pos2key_map.end()) {
      I(!ok);  // already reported error
      continue;
    }
    pos2key_map.insert_or_assign(it.first, it.second);
    key2pos_map.insert_or_assign(it.first, it.second);
  }

  return ok;
}

std::vector<std::pair<std::string, Node_pin>> Lgtuple::get_level_attributes(int pos, std::string_view key) const {
  std::vector<std::pair<std::string, Node_pin>> v;
  I(key.substr(0, 3) != "___");

  if (key.size() > 2 && key.substr(0, 2) == "__")
    return v;  // no recursive attributes

  auto it      = get_lower_it(pos, key);
  auto str_pos = std::to_string(pos);

  while (it != key_map.end()) {
    if ((str_pos.size() > it->first.size() || it->first.substr(0, str_pos.size()) != str_pos)
        && (key.size() > it->first.size() || it->first.substr(0, key.size()) != key)) {
      return v;
    }
    auto attr_pos = it->first.find(".__");
    if (attr_pos != std::string::npos) {
      v.emplace_back(it->first.substr(attr_pos+1), it->second);
    }else if (it->first.substr(0,2) == "__") {
      v.emplace_back(it->first, it->second);
    }
    ++it;
  }
  return v;
}

void Lgtuple::dump() const {
  fmt::print("tuple_name:{}\n", name);
  for (const auto &it : key_map) {
    auto it_pos = key2pos_map.find(it.first);
    if (it_pos == key2pos_map.end()) {
      fmt::print("  key:{} dpin:{}\n", it.first, it.second.debug_name());
    } else {
      fmt::print("  key:{}@{} dpin:{}\n", it.first, it_pos->second, it.second.debug_name());
    }
  }
}
