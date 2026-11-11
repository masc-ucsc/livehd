// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "interned_name.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "gtest/gtest.h"

using livehd::Name;

TEST(InternedName, OneCopyPerText) {
  const std::string text = "top/core/u_alu/add_17_a_very_long_hierarchical_instance_name";
  const Name        a(text);
  const Name        b(std::string_view{text});
  const Name        c(text.c_str());
  EXPECT_EQ(&a.str(), &b.str());
  EXPECT_EQ(&a.str(), &c.str());
  EXPECT_EQ(a, b);
  EXPECT_NE(a, Name("other"));
  EXPECT_EQ(sizeof(Name), sizeof(void*));
}

TEST(InternedName, EmptyAndNull) {
  const Name empty;
  EXPECT_TRUE(empty.empty());
  EXPECT_EQ(empty, Name(""));
  EXPECT_EQ(empty, Name(static_cast<const char*>(nullptr)));
  EXPECT_EQ(&empty.str(), &Name(std::string{}).str());
}

TEST(InternedName, ReadsLikeAString) {
  const Name name("u1/reg[3]");
  EXPECT_EQ(name, "u1/reg[3]");
  EXPECT_EQ("u1/reg[3]", name);
  EXPECT_EQ(name, std::string("u1/reg[3]"));
  EXPECT_EQ(name, std::string_view("u1/reg[3]"));
  EXPECT_NE(name, "u1/reg[4]");
  EXPECT_TRUE(name.starts_with("u1/"));
  EXPECT_TRUE(name.ends_with(']'));
  EXPECT_EQ(name.find('/'), 2U);
  EXPECT_EQ(name.substr(3, 3), "reg");
  EXPECT_EQ(name.size(), 9U);
  EXPECT_EQ(name + ":" + Name("Q"), "u1/reg[3]:Q");
  EXPECT_EQ("PIN:" + name, "PIN:u1/reg[3]");
  EXPECT_EQ(std::string("x") + name, "xu1/reg[3]");
  EXPECT_EQ(std::format("{}|{:>11}", name, name), "u1/reg[3]|  u1/reg[3]");
  std::ostringstream os;
  os << name;
  EXPECT_EQ(os.str(), "u1/reg[3]");
  const std::string&     as_string = name;
  const std::string_view as_view   = name;
  EXPECT_EQ(as_string, as_view);
}

// A map keyed by Name must iterate exactly like one keyed by std::string: results are reproducible
// because nets and cells are visited in name order.
TEST(InternedName, OrdersAndHashesByText) {
  const std::vector<std::string> texts = {"b", "a", "ab", "", "B", "a/b", "a[10]", "a[2]", "PIN"};
  std::map<std::string, int>     by_string;
  std::map<Name, int>            by_name;
  std::set<Name>                 names;
  for (size_t i = 0; i < texts.size(); ++i) {
    by_string.emplace(texts[i], i);
    by_name.emplace(texts[i], i);
    names.insert(texts[i]);
  }
  ASSERT_EQ(by_string.size(), by_name.size());
  auto it = by_name.begin();
  for (const auto& [text, value] : by_string) {
    EXPECT_EQ(it->first, text);
    EXPECT_EQ(it->second, value);
    ++it;
  }
  EXPECT_LT(Name("a"), Name("b"));
  EXPECT_LT(Name("a"), "b");
  EXPECT_GT("b", Name("a"));
  for (const auto& text : texts) {
    EXPECT_EQ(std::hash<Name>{}(Name(text)), std::hash<std::string>{}(text));
  }
  // Lookups by text in containers keyed by std::string keep working.
  std::unordered_map<std::string, int> table{{"a", 1}};
  EXPECT_EQ(table.at(Name("a")), 1);
  EXPECT_NE(std::ranges::find(texts, Name("a[2]")), texts.end());
}

TEST(InternedName, SameNameFromManyThreads) {
  std::vector<const std::string*> seen(16);
  {
    std::vector<std::jthread> workers;
    for (size_t t = 0; t < seen.size(); ++t) {
      workers.emplace_back([&seen, t] {
        for (int i = 0; i < 2000; ++i) {
          const Name name(std::format("thread_shared_name_{}", i));
          if (i == 1234) {
            seen[t] = &name.str();
          }
        }
      });
    }
  }
  for (const auto* text : seen) {
    EXPECT_EQ(text, seen.front());
  }
  EXPECT_EQ(*seen.front(), "thread_shared_name_1234");
}
