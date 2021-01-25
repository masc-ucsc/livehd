//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <charconv>

#include "lgtuple.hpp"
#include "lgraph.hpp"
#include "likely.hpp"

std::string Lgtuple::get_last_level(const std::string &key) const {
  std::string last_key{key};

  auto n = last_key.find_last_of('.');
  if (n != std::string::npos) {
    last_key = last_key.substr(n+1);
  }

  return last_key;
}

void Lgtuple::add_int(std::string_view key, std::shared_ptr<Lgtuple> tup) {
  std::string key_base{key};
  key_base = key_base + ".";

  for(auto ent:tup->key_map) {
    key_map.emplace(key_base + ent.first, ent.second);
  }

  for(auto ent:tup->pos2key_map) {
    pos2key_map.emplace(key_base + ent.first, key_base + ent.second);
  }
  for(auto ent:tup->key2pos_map) {
    key2pos_map.emplace(key_base + ent.first, key_base + ent.second);
  }
}


std::shared_ptr<Lgtuple> Lgtuple::make_merge(Node_pin &sel_dpin, const std::vector<std::shared_ptr<Lgtuple>> &tup_list) {
	(void)sel_dpin;

  I(tup_list.size()>1); // nothing to merge?

  auto tup0 = tup_list[0];

	auto new_tup = std::make_shared<Lgtuple>(tup0->get_name());

  for(auto i=0u;i<tup_list.size();++i) {
    auto tup = tup_list[i];
    tup->dump();
    I(tup->get_name() == tup0->get_name());
    for(const auto &it:tup->get_map()) {
      (void)it;
      I(tup->pos2key_map.empty()); // FIXME: only unordered for the moment
    }
  }

	return new_tup;
}

int Lgtuple::get_pos(std::string_view key) const {
  auto last_key = get_last_level(std::string{key});

  if (last_key[0]>='0' && last_key[0]<='9') {
    int x;
    std::from_chars(last_key.data(), last_key.data() + last_key.size(), x);
    return x;
  }

  auto k2p_it = key2pos_map.find(std::string{key});
  if (k2p_it == key2pos_map.end())
    return -1;

  last_key = get_last_level(k2p_it->second);
  I(last_key[0]>='0' && last_key[0]<='9');

  int x;
  std::from_chars(last_key.data(), last_key.data() + last_key.size(), x);
  return x;
}

const Node_pin &Lgtuple::get_dpin(int pos, std::string_view key) const {
  auto it = get_it(pos, key);
  if (unlikely(it == key_map.end())) {
    LGraph::info("pos:{} key:{} does not exist in tuple:{}\n", pos, key, name);
    dump();
    return invalid_dpin;
  }

  return it->second;
}

const Node_pin &Lgtuple::get_dpin(int pos) const {
  auto it = get_it(pos);
  if (unlikely(it == key_map.end())) {
    LGraph::info("pos:{} does not exist in tuple:{}\n", pos, name);
    dump();
    return invalid_dpin;
  }

  return it->second;
}

const Node_pin &Lgtuple::get_dpin(std::string_view key) const {
  auto it = get_it(key);
  if (unlikely(it == key_map.end())) {
    LGraph::info("key:{} does not exist in tuple:{}\n", key, name);
    dump();
    return invalid_dpin;
  }

  return it->second;
}

std::shared_ptr<Lgtuple> Lgtuple::get_sub_tuple(int pos, std::string_view key) const {
  (void)pos;
  (void)key;
  std::shared_ptr<Lgtuple> tup;

  I(false); // FIXME: implement it

  return tup;
}

std::shared_ptr<Lgtuple> Lgtuple::get_sub_tuple(int pos) const {
  (void)pos;
  std::shared_ptr<Lgtuple> tup;

  I(false); // FIXME: implement it

  return tup;
}

std::shared_ptr<Lgtuple> Lgtuple::get_sub_tuple(std::string_view key) const {
  (void)key;
  std::shared_ptr<Lgtuple> tup;

  I(false); // FIXME: implement it

  return tup;
}

void Lgtuple::del(std::string_view key) {
  std::string key1_match{key};
  std::string key2_match{std::string{key} + "."};

  for(auto it=key_map.begin(); it!=key_map.end(); ) { // delete any match to key (even subkeys like key.foo.bar)
    const auto &n = it->first;
    if (key1_match.size() == n.size() && strcasecmp(n.c_str(), key1_match.data())==0) { // exact
      auto k2p_it = key2pos_map.find(it->first);
      if (k2p_it != key2pos_map.end()) {
        pos2key_map.erase(k2p_it->second);
        key2pos_map.erase(k2p_it);
      }
      it = key_map.erase(it);
    }else if (key2_match.size() <= n.size() && strncasecmp(n.c_str(), key2_match.data(), key2_match.size())==0) { // subkeys
      auto k2p_it = key2pos_map.find(it->first);
      if (k2p_it != key2pos_map.end()) {
        pos2key_map.erase(k2p_it->second);
        key2pos_map.erase(k2p_it);
      }
      it = key_map.erase(it);
    }else{
      ++it;
    }
  }
}

void Lgtuple::del(int pos) {
  auto str_pos = std::to_string(pos);
  auto p2k_it = pos2key_map.find(str_pos);
  if (p2k_it == pos2key_map.end())
    return del(str_pos); // case: there is no key

  return del(p2k_it->second); // case: there is key and pos
}

void Lgtuple::add(int pos, std::string_view key, std::shared_ptr<Lgtuple> tup) {
  del(key);

  if (!std::isdigit(key[0])) {
    pos2key_map.emplace(std::to_string(pos), key);
    key2pos_map.emplace(key, std::to_string(pos));
  }else{
    I(std::to_string(pos) == key);
  }

  add_int(key, tup);
}

void Lgtuple::add(int pos, std::string_view key, const Node_pin &dpin) {
  del(key);

  if (!std::isdigit(key[0])) {
    pos2key_map.emplace(std::to_string(pos), key);
    key2pos_map.emplace(key, std::to_string(pos));
  }else{
    I(std::to_string(pos) == key);
  }

  key_map.emplace(key, dpin);
}

void Lgtuple::add(std::string_view key, std::shared_ptr<Lgtuple> tup) {
  del(key);

  add_int(key, tup);
}

void Lgtuple::add(std::string_view key, const Node_pin &dpin) {
  auto k2p_it = key2pos_map.find(key);
  std::string pos_match;
  if (k2p_it != key2pos_map.end()) {
    pos_match = k2p_it->second;
  }

  del(key);

  if (!pos_match.empty()) { // preserve pos if existed before
    pos2key_map.emplace(pos_match, key);
    key2pos_map.emplace(key, pos_match);
  }

  key_map.emplace(key, dpin);
}

void Lgtuple::add(int pos, std::shared_ptr<Lgtuple> tup) {
  auto str_pos = std::to_string(pos);
  auto p2k_it = pos2key_map.find(str_pos);
  std::string key_match;
  if (p2k_it == pos2key_map.end()) {
    key_match = p2k_it->second;
  }

  del(pos);

  if (!key_match.empty()) { // preserve key name if existed before
    pos2key_map.emplace(str_pos, key_match);
    key2pos_map.emplace(key_match, str_pos);
  }

  add_int(str_pos, tup);
}

void Lgtuple::add(int pos, const Node_pin &dpin) {
  auto str_pos = std::to_string(pos);
  auto p2k_it = pos2key_map.find(str_pos);
  std::string key_match;
  if (p2k_it != pos2key_map.end()) {
    key_match = p2k_it->second;
  }

  del(pos);

  if (!key_match.empty()) { // preserve key name if existed before
    pos2key_map.emplace(str_pos, key_match);
    key2pos_map.emplace(key_match, str_pos);
    key_map.emplace(key_match, dpin);
  }else{
    key_map.emplace(str_pos, dpin);
  }
}

void Lgtuple::add(const Node_pin &dpin) {
  add("", dpin);
}

std::vector<std::pair<std::string_view, Node_pin>> Lgtuple::get_level_attributes() const {

  std::vector<std::pair<std::string_view, Node_pin>> v;

  I(false); // FIXME: implement it

#if 0

  for(auto it=key2pos.begin(); it!=key2pos.end(); ++it) {
    if (it->first.substr(0,2) != "__")
      continue;

    v.emplace_back(it->first, pos2tuple[it->second]->get_value_dpin());
  }
#endif

  return v;
}

void Lgtuple::dump() const {
  fmt::print("tuple name:{}\n", name);
  for(const auto &it:key_map) {
    auto it_pos = key2pos_map.find(it.first);
    if (it_pos == key2pos_map.end()) {
      fmt::print("  key:{} dpin:{}\n", it.first, it.second.debug_name());
    }else{
      fmt::print("  key:{}:{} dpin:{}\n", it.first, it_pos->second, it.second.debug_name());
    }
  }
}

