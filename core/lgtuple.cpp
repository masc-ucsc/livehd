//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgtuple.hpp"

#include <charconv>

#include "lgraph.hpp"
#include "likely.hpp"

std::tuple<bool, size_t, size_t> Lgtuple::match_int_advance(std::string_view a, std::string_view b, size_t a_pos, size_t b_pos) {
  I(a[a_pos]==':');
  I(b[b_pos]!=':');

	I(a_pos < a.size()); // pending :
	if (a[a_pos] == ':') {
		if (!std::isdigit(b[b_pos])) {
			++a_pos; // skip :
			while(a[a_pos] != ':') {
				I(a_pos<a.size()); // must have matching :
				++a_pos;
			}
		}
		++a_pos; // Skip :
	}

	while(a.size()>a_pos && b.size()>b_pos) {
		if (a[a_pos] != b[b_pos]) {
			return std::make_tuple(false, a_pos, b_pos);
		}

    ++b_pos;
    ++a_pos;
    I(b[b_pos] != ':'); // should not call this method for this

    if (b_pos == b.size() || b[b_pos] == '.') {
			bool m = (a_pos == a.size() || a[a_pos] == '.' || a[a_pos] == ':');
			if (a[a_pos] == ':') {
				while(a_pos < a.size() && a[a_pos] != '.') {
					++a_pos; // advance to the next section
				}
			}
			return std::make_tuple(m, a_pos, b_pos);
		}
    if (a_pos == a.size() || a[a_pos] == ':' || a[a_pos] == '.') {
			bool m = (b_pos == b.size() || b[b_pos] == '.');
			if (a[a_pos] == ':') {
				while(a_pos < a.size() && a[a_pos] != '.') {
					++a_pos; // advance to the next section
				}
			}
			return std::make_tuple(m, a_pos, b_pos);
		}
  }

	while(a_pos < a.size() && a[a_pos] != '.') {
		++a_pos; // advance to the next section
	}

  return std::make_tuple(true, a_pos, b_pos);
}

std::tuple<bool,bool, size_t> Lgtuple::match_int(std::string_view a, std::string_view b) {

  auto a_last_section = 0u;
  auto b_last_section = 0u;
  auto a_pos = 0u;
  auto b_pos = 0u;

  while(a_pos < a.size() && b_pos < b.size()) {

    if (a[a_pos] == b[b_pos]) {
      ++a_pos;
      ++b_pos;
      if (a_pos>=a.size() || b_pos>=b.size() || a[a_pos]=='.' || b[b_pos]=='.') {
        a_last_section = a_pos+1;
        b_last_section = b_pos+1;
      }
      continue;
    }else if (a_pos != a_last_section || b_pos != b_last_section) {
      return std::make_tuple(false, false, b_last_section);
		}
    if (a[a_last_section] == ':' && b[b_last_section] != ':') {
      I(b[b_last_section] != ':');

      if (std::isdigit(b[b_last_section])) {
				bool m;
				std::tie(m, a_pos, b_pos) = match_int_advance(a, b, a_last_section, b_last_section);
        if (!m)
          return std::make_tuple(false,false, b_last_section);
      }else{
				I(a[a_pos]==':');
				++a_pos; // skip first
        while(a[a_pos] != ':') {
          I(a_pos < a.size()); // pending :
          ++a_pos;
        }
				++a_pos; // :
				continue;
      }

    }else if (b[b_last_section] == ':') {
      I(a[a_last_section] != ':');
      I(b[b_last_section] == ':');

      if (std::isdigit(a[a_last_section])) {
				bool m;
				std::tie(m, b_pos, a_pos) = match_int_advance(b, a, b_last_section, a_last_section); // swap order
        if (!m)
          return std::make_tuple(false, false, b_last_section);
      }else{
				I(b[b_pos]==':');
				++b_pos; // skip first
        while(b[b_pos] != ':') {
          I(b_pos < b.size()); // pending :
          ++b_pos;
        }
				++b_pos; // :
				continue;
      }
    }else{
			I(a[a_pos] != b[b_pos]);
      return std::make_tuple(false, false, b_last_section);
    }
		I(a_pos == a.size() || a[a_pos]=='.');
		I(b_pos == b.size() || b[b_pos]=='.');
		++a_pos;
		++b_pos;
		a_last_section = a_pos;
		b_last_section = b_pos;
  }

	bool a_match = (a_pos>=a.size()) && (b_pos>=b.size() || b[b_pos]=='.');
	bool b_match = (b_pos>=b.size()) && (a_pos>=a.size() || a[a_pos]=='.');

	if (b[b_pos]=='.')
		++b_pos;
	if (a[a_pos]=='.')
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
  auto a_pos = 0u;
  auto b_pos = 0u;

	std::string new_a;
	std::string new_b;

	bool a_last_match;
	bool b_last_match;

  while(true) { // FSM main loop

		// STEP: Advance up to mismatch
		while (a_pos<a.size() && b_pos<b.size() && a[a_pos]==b[b_pos]) {
			if(likely(a[a_pos]!='.')) {
				++a_pos;
				++b_pos;
				continue;
			}
			I(b[b_pos]=='.');
			if (a[a_last_section] == ':') {
				append_field(new_a, a.substr(a_last_section, a_pos-a_last_section));
				append_field(new_b, a.substr(a_last_section, a_pos-a_last_section));
			}else{
				append_field(new_a, b.substr(b_last_section, b_pos-b_last_section));
				append_field(new_b, b.substr(b_last_section, b_pos-b_last_section));
			}
			++a_pos;
			++b_pos;
			a_last_section = a_pos;
			b_last_section = b_pos;
		}

		// STEP: Did we reach the end of string?
		if (a_pos >= a.size() || b_pos >= b.size()) {
			a_last_match = (b[b_last_section]==':') && (b_pos>=b.size() || b[b_pos]=='.');
			b_last_match = (a[a_last_section]==':') && (a_pos>=a.size() || a[a_pos]=='.');
			break;
		}

		// STEP: Fully populated entries should match already
    if (a_last_section!=a_pos || b_last_section!=b_pos) {
			I(a[a_pos]!=b[b_pos]);
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
				append_field(new_a, a.substr(a_last_section, a_last_section-a_pos));
				++a_pos;
				++b_pos;
				a_last_section = a_pos;
				b_last_section = b_pos;
				append_field(new_b, a.substr(a_last_section, a_last_section-a_pos));
      }else{
				I(a[a_pos]==':');
				++a_pos; // skip first
        while(a[a_pos] != ':') {
          I(a_pos < a.size()); // pending :
          ++a_pos;
        }
				++a_pos; // :
      }
			continue;
    }else if (b[b_last_section] == ':') {
      I(a[a_last_section] != ':');
      I(b[b_last_section] == ':');

      if (std::isdigit(a[a_last_section])) {
				bool m;
				std::tie(m, b_pos, a_pos) = match_int_advance(b, a, b_pos, a_pos); // swap order
        if (a_pos == a.size() || b_pos == b.size() || !m) {
					a_last_match = m;
					b_last_match = false;
					break;
				}
				append_field(new_a, a.substr(b_last_section, b_last_section-b_pos));
				append_field(new_b, a.substr(b_last_section, b_last_section-b_pos));
				++a_pos;
				++b_pos;
				a_last_section = a_pos;
				b_last_section = b_pos;
      }else{
				I(b[b_pos]==':');
				++b_pos; // skip first
        while(b[b_pos] != ':') {
          I(b_pos < b.size()); // pending :
          ++b_pos;
        }
				++b_pos; // :
      }
			continue;
    }else{
			a_last_match = false;
			b_last_match = false;
			break;
		}

		I(false); // never reaches this place
  }

	// Finish the rest of the swap (if match) and add the rest
	if (a_last_match) {
		I(b[b_last_section]==':');
		append_field(new_a, b.substr(b_last_section, b_pos-b_last_section));
		if (a_pos < a.size()) {
			I(a[a_pos]=='.');
			append_field(new_a, a.substr(a_pos+1)); // +1 to skip .
		}
	}else{
		append_field(new_a, a.substr(a_last_section));
	}
	if (b_last_match) {
		I(a[a_last_section]==':');
		append_field(new_b, a.substr(a_last_section, a_pos-a_last_section));
		if (b_pos < b.size()) {
			I(b[b_pos]=='.');
			append_field(new_b, b.substr(b_pos+1)); // +1 to skip .
		}
	}else{
		append_field(new_b, b.substr(b_last_section));
	}

	std::swap(a,new_a);
	std::swap(b,new_b);
}

bool Lgtuple::match(std::string_view a, std::string_view b) {
	auto [m1,m2, x] = match_int(a,b);
	(void)x;
	return m1 && m2; // both reach the end in match_int
}

size_t Lgtuple::match_first_partial(std::string_view a, std::string_view b) {
	auto [m1,m2,x] = match_int(a,b);
	(void)m2;
	if (m1)
		return x; // a reached the end in match
	return 0;
}

bool Lgtuple::match_either_partial(std::string_view a, std::string_view b) {
	auto [m1,m2,x] = match_int(a,b);
	(void)x;
	return m1 || m2; // either reached the end it match_int
}

void Lgtuple::add_int(const std::string &key, std::shared_ptr<Lgtuple const> tup) {
  if (tup->is_scalar()) {
		I(has_dpin(key)); // It was deleted before
    key_map.emplace_back(key, tup->get_dpin());
    return;
  }

  for (auto &ent : tup->key_map) {
    key_map.emplace_back(absl::StrCat(key, ".", ent.first), ent.second);
  }
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
		I(n!=std::string::npos);
		return key.substr(n+1);
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

	I(n!=0); // name can not start with a .

	// If there are attributes, show keep them.
	if (key.substr(n,3) == ".__") {
		auto n2 = key.substr(0,n-1).find_last_of('.');
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

	for(auto &e:key_map) {
		learn_fix_int(key, e.first);
	}

	return key;
}

const Node_pin &Lgtuple::get_dpin(std::string_view key) const {
	for(auto &e:key_map) {
		if (match(e.first, key))
			return e.second;
	}
  #ifndef NDEBUG
	  LGraph::info("key:{} does not exist in tuple:{}", key, name); // may be OK if dead code eliminated
  #endif
	return invalid_dpin;
}

bool Lgtuple::has_dpin(std::string_view key) const {
	for(auto &e:key_map) {
		if (match(e.first, key))
			return true;
	}
	return false;
}

std::shared_ptr<Lgtuple> Lgtuple::get_sub_tuple(std::string_view key) const {

  I(!key.empty());  // do not call without sub-fields

  std::shared_ptr<Lgtuple> tup;

	for(auto &e:key_map) {
		std::string_view entry(e.first);
		auto e_pos = match_first_partial(key, entry);
		if (e_pos==0)
			continue;
		I(entry[e_pos] != '.'); // . not included

		if (!tup) {
			std::string key_with_pos{key};
			std::string expanded{e.first};
			learn_fix_int(key_with_pos, expanded);
			tup = std::make_shared<Lgtuple>(absl::StrCat(name, ".", key_with_pos));
		}

		if (e_pos>entry.size())
			tup->key_map.emplace_back("", e.second);
		else
			tup->key_map.emplace_back(entry.substr(e_pos), e.second);
	}

	return tup;
}

void Lgtuple::del(std::string_view key) {
  if (key.empty()) {
		key_map.clear();
    return;
	}

	Key_map_type new_map;

	for(auto i=0u; i<key_map.size(); ++i) {
		std::string_view entry{key_map[i].first};
		auto e_pos = match_first_partial(key, entry);
		if (e_pos==0) {
			new_map.emplace_back(std::move(key_map[i]));
			continue;
		}
		if (e_pos>=entry.size())
			continue; // full match?

		I(entry[e_pos] != '.'); // not . included

		if (is_root_attribute(entry.substr(e_pos))) {
			new_map.emplace_back(std::move(key_map[i]));
			continue; // Keep the attributes
		}
	}

	key_map.swap(new_map);
}

void Lgtuple::add(std::string_view key, std::shared_ptr<Lgtuple const> tup) {
  bool only_attr_add = true;
  for (const auto &it : tup->key_map) {
		if (is_root_attribute(it.first))
			continue;
    only_attr_add = false;
    break;
  }
  if (!only_attr_add)
    del(key);

	auto fixed_key = learn_fix(key);

  add_int(fixed_key, tup);
}

void Lgtuple::add(std::string_view key, const Node_pin &dpin) {
  del(key);

	auto fixed_key = learn_fix(key);

  key_map.emplace_back(fixed_key, dpin);
}

bool Lgtuple::append_tuple(std::shared_ptr<Lgtuple const> tup) {
  bool ok = true;

	std::vector<std::pair<std::string,Node_pin>> delayed_numbers;

  for (auto &it : tup->key_map) {
		if (has_dpin(it.first)) {
			if (std::isdigit(it.first[0]) && is_single_level(it.first)) {
				delayed_numbers.emplace_back(it.first, it.second);
				continue;
			}
      dump();
      tup->dump();
      LGraph::info("tuples {} and {} can not concat for key:{}", get_name(), tup->get_name(), it.first);
      ok = false;
      continue;
    }
		auto fixed_key = learn_fix(it.first);
		key_map.emplace_back(fixed_key, it.second);
  }

	if (delayed_numbers.size()) {
		auto max_pos=0;
		for (const auto &e:key_map) {
			if (e.first.empty()) {
				dump();
				LGraph::info("can not append pin to tuple {} when some are unnamed", get_name());
				return false;
			}
			int x = 0;
			if (std::isdigit(e.first[0])) {
				std::from_chars(e.first.data(), e.first.data() + e.first.size(), x);
			}else if (e.first[0]==':' && std::isdigit(e.first[1])) {
				std::from_chars(e.first.data()+1, e.first.data() + e.first.size()-1, x);
			}else{
				dump();
				LGraph::info("can not append pin to tuple unordered {} field {}", get_name(), e.first);
				return false;
			}
			if (x>max_pos)
				max_pos = x;
		}
		for(const auto &e:delayed_numbers) {
			int x = 0;
			std::from_chars(e.first.data(), e.first.data() + e.first.size(), x);
			key_map.emplace_back(std::to_string(x+max_pos+1), e.second);
		}
	}

  return ok;
}

bool Lgtuple::append_tuple(const Node_pin &dpin) {

	if (key_map.size() == 1 && key_map[0].first.empty()) {
		key_map[0].first = "0";
		key_map.emplace_back("1", dpin);
		return true;
	}

	auto max_pos=0;
  for (const auto &e:key_map) {
		if (e.first.empty()) {
      dump();
      LGraph::info("can not append pin to tuple {} when some are unnamed", get_name());
      return false;
    }
		int x = 0;
		if (std::isdigit(e.first[0])) {
			std::from_chars(e.first.data(), e.first.data() + e.first.size(), x);
		}else if (e.first[0]==':' && std::isdigit(e.first[1])) {
			std::from_chars(e.first.data()+1, e.first.data() + e.first.size()-1, x);
		}else{
      dump();
      LGraph::info("can not append pin to tuple unordered {} field {}", get_name(), e.first);
      return false;
		}
		if (x>max_pos)
			max_pos = x;
  }

	key_map.emplace_back(std::to_string(max_pos+1), dpin);

  return true;
}

std::shared_ptr<Lgtuple> Lgtuple::make_mux(Node_pin &sel_dpin, const std::vector<std::shared_ptr<Lgtuple const>> &tup_list) {
  (void)sel_dpin;
  I(tup_list.size() > 1);  // nothing to merge?

  auto fixing_tup = std::make_shared<Lgtuple>(tup_list.back()->get_name());

	std::vector<size_t> keymap_counter;

	for(const auto &tup:tup_list) {
		for(const auto &e:tup->get_map()) {
			auto fixed_key = fixing_tup->learn_fix(e.first);
			bool found     = false;

			if (keymap_counter.size() < fixing_tup->key_map.size()) {
				keymap_counter.resize(fixing_tup->key_map.size());
			}

			for(auto i=0u;i<fixing_tup->key_map.size();++i) {
				if (fixing_tup->key_map[i].first != fixed_key) { // for fixed, a direct strcmp works
					continue;
				}
				if (fixing_tup->key_map[i].second != e.second) { // Diff dpin will need a mux
					fixing_tup->key_map[i].second = invalid_dpin;
				}

				keymap_counter[i]++;

				found = true;
				break;
			}
			if (!found) {
				fixing_tup->key_map.emplace_back(fixed_key, e.second);
			}
		}
	}

	for(auto e_index = 0u;e_index < fixing_tup->key_map.size(); ++e_index) {
		auto &e = fixing_tup->key_map[e_index];
		if (!e.second.is_invalid() && keymap_counter[e_index] == (tup_list.size()-1))
			continue;

		auto mux_node = sel_dpin.get_class_lgraph()->create_node(Ntype_op::Mux);
		mux_node.setup_sink_pin_raw(0).connect_driver(sel_dpin);

		for (auto i = 0u; i < tup_list.size(); ++i) {
			const auto &dpin = tup_list[i]->get_dpin(e.first);
			if (dpin.is_invalid()) {
				auto cerr = sel_dpin.get_class_lgraph()->create_node(Ntype_op::CompileErr);
				mux_node.setup_sink_pin_raw(i+1).connect_driver(cerr.setup_driver_pin());
			}else{
				mux_node.setup_sink_pin_raw(i+1).connect_driver(dpin);
			}
		}

		e.second = mux_node.setup_driver_pin();
	}

	return fixing_tup;
}

std::shared_ptr<Lgtuple> Lgtuple::make_flop(Node &flop) {
	I(flop.is_type(Ntype_op::Flop));

	if (is_scalar())
		return nullptr;

	std::string_view flop_name;
	if (flop.get_driver_pin().has_name())
		flop_name = flop.get_driver_pin().get_name();
	else
		flop_name = name;

  auto ret_tup = std::make_shared<Lgtuple>(flop_name);

	bool first_flop=false;
	for(auto &e:key_map) {
		if (e.second.get_node() == flop)
			continue; // no loop to itself
		if (is_attribute(e.first)) {
			fmt::print("FIXME 2: Set the flop to attribute:{}\n",e.first);
			continue;
		}
		Node node;
		if (first_flop) {
			node = flop;
			first_flop = false;
		}else{
			node = flop.get_class_lgraph()->create_node(Ntype_op::Flop);
		}
		node.setup_sink_pin("din").connect_driver(e.second);
		ret_tup->key_map.emplace_back(e.first, node.setup_driver_pin());
	}

	return ret_tup;
}

std::vector<std::pair<std::string, Node_pin>> Lgtuple::get_level_attributes(std::string_view key) const {

  I(!is_root_attribute(key));

  std::vector<std::pair<std::string, Node_pin>> v;

	for(const auto &e:key_map) {
		std::string_view entry{e.first};
		auto e_pos = match_first_partial(key, entry);
		if (e_pos==0 || e_pos>=entry.size())
			continue;

		I(entry[e_pos] != '.'); // . not included

		if (!is_root_attribute(entry.substr(e_pos)))
			continue;

		v.emplace_back(entry.substr(e_pos), e.second);
	}

  return v;
}

void Lgtuple::dump() const {
  fmt::print("tuple_name: {}\n", name);
  for (const auto &it : key_map) {
		fmt::print("  key: {} dpin: {}\n", it.first, it.second.debug_name());
  }
}
