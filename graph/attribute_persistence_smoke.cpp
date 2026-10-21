// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "hhds/attr.hpp"

namespace {
struct Integer {
  using value_type = uint32_t;
  using storage    = hhds::flat_storage;
};
struct Text {
  using value_type = std::string;
  using storage    = hhds::flat_storage;
};
struct Dense {
  using value_type = uint32_t;
  using storage    = hhds::flat_storage;
  using layout     = hhds::dense_layout;
};
struct Flag {
  using value_type = hhds::flag;
  using storage    = hhds::flat_storage;
};
class Host : public hhds::Attr_host {
public:
  using hhds::Attr_host::load_attr_stores;
  using hhds::Attr_host::save_attr_stores;
  template <class Tag>
  hhds::AttrRef<Tag> attr(uint64_t key) {
    return {this, key};
  }

private:
  void attr_note_modified() noexcept override {}
};
std::string save(const Host& host) {
  std::ostringstream stream(std::ios::binary);
  host.save_attr_stores(stream);
  return stream.str();
}
void check(bool value) {
  if (!value) {
    throw std::runtime_error("attribute persistence changed with hash iteration order");
  }
}
}  // namespace
int main() {
  hhds::register_attr_tag<Integer>("livehd.persistence.integer");
  hhds::register_attr_tag<Text>("livehd.persistence.text");
  hhds::register_attr_tag<Dense>("livehd.persistence.dense");
  hhds::register_attr_tag<Flag>("livehd.persistence.flag");
  std::vector<uint64_t> keys(2048);
  std::iota(keys.begin(), keys.end(), uint64_t{1});
  std::mt19937 random(42);
  std::string  reference;
  for (int trial = 0; trial < 5; ++trial) {
    std::shuffle(keys.begin(), keys.end(), random);
    Host host;
    for (auto key : keys) {
      host.attr<Integer>(key).set(static_cast<uint32_t>(key * 3));
      host.attr<Text>(key).set(std::to_string(key));
      host.attr<Dense>(key).set(static_cast<uint32_t>(key));
      if (key % 3 == 0) {
        host.attr<Flag>(key).set();
      }
    }
    // Exercise erase/reinsert as well as separate hash-table allocation seeds.
    for (auto key : keys) {
      host.attr<Integer>(key).del();
      host.attr<Integer>(key).set(static_cast<uint32_t>(key * 3));
    }
    const auto bytes = save(host);
    if (trial == 0) {
      reference = bytes;
    } else {
      check(bytes == reference);
    }
    Host               loaded;
    std::istringstream input(bytes, std::ios::binary);
    loaded.load_attr_stores(input);
    for (auto key : keys) {
      check(loaded.attr<Integer>(key).get() == key * 3);
      check(loaded.attr<Text>(key).get() == std::to_string(key));
      check(loaded.attr<Dense>(key).get() == key);
      check(loaded.attr<Flag>(key).has() == (key % 3 == 0));
    }
    check(save(loaded) == reference);
    loaded.attr<Integer>(1).set(9999);
    check(save(loaded) != reference);
  }
}
