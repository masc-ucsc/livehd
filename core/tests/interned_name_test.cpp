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

using livehd::Pin_label;

TEST(PinLabel, IsItsText) {
  const Pin_label cell("top/u1/reg[3]:Q");
  EXPECT_FALSE(cell.bare());
  EXPECT_EQ(cell.instance(), "top/u1/reg[3]");
  EXPECT_EQ(cell.pin(), "Q");
  EXPECT_EQ(cell.str(), "top/u1/reg[3]:Q");
  EXPECT_EQ(cell, "top/u1/reg[3]:Q");
  EXPECT_EQ(cell.size(), cell.str().size());
  EXPECT_EQ(cell.rfind(':'), cell.str().rfind(':'));
  EXPECT_EQ(cell.find(':'), cell.str().find(':'));
  EXPECT_EQ(cell.substr(0, cell.rfind(':')), "top/u1/reg[3]");
  EXPECT_EQ(cell.substr(cell.rfind(':') + 1), "Q");
  EXPECT_EQ(sizeof(Pin_label), 2 * sizeof(void*));

  const Pin_label io("data_o[7]");
  EXPECT_TRUE(io.bare());
  EXPECT_TRUE(io.instance().empty());
  EXPECT_EQ(io.pin(), "data_o[7]");
  EXPECT_EQ(io.str(), "data_o[7]");
  EXPECT_EQ(io.rfind(':'), std::string::npos);

  // The label of a connection: "PIN" is the top-level terminal marker of the DEF model.
  EXPECT_EQ(Pin_label(Name("PIN"), Name("data_o[7]")), io);
  EXPECT_EQ(Pin_label(Name("top/u1/reg[3]"), Name("Q")), cell);
  EXPECT_EQ(Pin_label(Name("a"), Name("b:c")).str(), "a:b:c");
  EXPECT_EQ(Pin_label(Name("a"), Name("b:c")), Pin_label("a:b:c"));

  EXPECT_TRUE(Pin_label().empty());
  EXPECT_EQ(Pin_label(), Pin_label(""));
  EXPECT_EQ(Pin_label().str(), "");
  EXPECT_FALSE(Pin_label(":").empty());
  EXPECT_EQ(Pin_label(":").str(), ":");
  EXPECT_NE(Pin_label("PIN:x"), Pin_label("x"));
  EXPECT_EQ(Pin_label("PIN:x").str(), "PIN:x");
  EXPECT_EQ(std::format("{}", cell), "top/u1/reg[3]:Q");
  EXPECT_EQ("x/" + cell, "x/top/u1/reg[3]:Q");
}

// Label order is text order, including the cases where comparing instance then pin would differ:
// "a0:x" sorts before "a:x" although "a" sorts before "a0".
TEST(PinLabel, OrdersLikeTheJoinedText) {
  std::vector<std::string> texts = {"",    ":",     "a", "a:x",   "a0:x", "a:",     "a::x",    "ab",      "a:b",   "ab:", "b",
                                    "PIN", "PIN:x", "x", "a/b:c", "a/b",  "a[1]:Q", "a[10]:Q", "a[1]0:Q", "z:a:b", "z:a", "z"};
  for (const char i : std::string("ab:/0")) {
    for (const char j : std::string("ab:/0")) {
      for (const char k : std::string("ab:/0")) {
        texts.push_back(std::string{i, j, k});
        texts.push_back(std::string{i, j});
      }
    }
  }
  std::map<std::string, int> by_text;
  std::map<Pin_label, int>   by_label;
  for (size_t i = 0; i < texts.size(); ++i) {
    by_text.emplace(texts[i], i);
    by_label.emplace(texts[i], i);
  }
  ASSERT_EQ(by_text.size(), by_label.size());
  auto it = by_label.begin();
  for (const auto& [text, value] : by_text) {
    EXPECT_EQ(it->first.str(), text);
    EXPECT_EQ(it->second, value);
    ++it;
  }
  for (const auto& a : texts) {
    for (const auto& b : texts) {
      const Pin_label la(a), lb(b);
      EXPECT_EQ(la < lb, a < b) << a << " vs " << b;
      EXPECT_EQ(la == lb, a == b) << a << " vs " << b;
      EXPECT_EQ(la < b, a < b) << a << " vs text " << b;
      EXPECT_EQ(a < lb, a < b) << "text " << a << " vs " << b;
      EXPECT_EQ(la == b, a == b) << a << " vs text " << b;
      EXPECT_EQ(la == lb, std::hash<Pin_label>{}(la) == std::hash<Pin_label>{}(lb) && la == lb);
    }
    EXPECT_EQ(Pin_label(a).str(), a);
    EXPECT_EQ(Pin_label(a).size(), a.size());
    EXPECT_EQ(Pin_label(a).rfind(':'), a.rfind(':'));
    EXPECT_EQ(Pin_label(a).find(':'), a.find(':'));
    EXPECT_EQ(Pin_label(a).rfind('/'), a.rfind('/'));
    EXPECT_EQ(Pin_label(a).find('/'), a.find('/'));
  }
}
