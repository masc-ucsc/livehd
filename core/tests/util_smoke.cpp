// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <cstdint>
#include <limits>
#include <string>

#include "gtest/gtest.h"
#include "hash_util.hpp"
#include "json_util.hpp"
#include "str_tools.hpp"

namespace {

TEST(StrTools, ParsesSignedIntegers) {
  for (int value : {std::numeric_limits<int>::min(), -3, -1, 0, 1, 3, std::numeric_limits<int>::max()}) {
    const auto text = std::to_string(value);
    EXPECT_TRUE(str_tools::is_i(text)) << text;
    EXPECT_EQ(str_tools::to_i(text), value) << text;
  }
  EXPECT_EQ(str_tools::to_i("-0"), 0);
  for (auto text : {"", "-", "invalid", "99999999999999999999", "-99999999999999999999"}) {
    EXPECT_FALSE(str_tools::is_i(text)) << text;
    EXPECT_EQ(str_tools::to_i(text), 0) << text;
  }
  // Graph port-name decoding relies on reading just the numeric prefix.
  EXPECT_EQ(str_tools::to_i("12addr"), 12);
}

TEST(StrTools, FormatsFullUnsignedRange) {
  for (uint64_t value : {uint64_t{0},
                         uint64_t{9},
                         uint64_t{1000000000000000000},
                         uint64_t{10000000000000000000ULL},
                         std::numeric_limits<uint64_t>::max()}) {
    EXPECT_EQ(str_tools::to_s(value), std::to_string(value));
  }
}

TEST(StrTools, OptionTruthPreservesAsciiSemantics) {
  for (auto value : {"", "0", "false", "FaLsE", "NO", "oFf"}) {
    EXPECT_FALSE(str_tools::option_is_true(value)) << value;
  }
  for (auto value : {"1", "true", "yes", " false", "false ", "offx", "a long arbitrary option value"}) {
    EXPECT_TRUE(str_tools::option_is_true(value)) << value;
  }
  EXPECT_TRUE(str_tools::ascii_iequals("AbC", "aBc"));
  EXPECT_FALSE(str_tools::ascii_iequals("AbC", "aB"));
  EXPECT_FALSE(str_tools::ascii_iequals("\xc0", "\xe0"));
  EXPECT_TRUE(str_tools::ascii_iequals("\xc0", "\xc0"));
}

TEST(JsonUtil, EscapesEveryControlByte) {
  std::string input;
  for (unsigned char byte = 0; byte < 0x20; ++byte) {
    input.push_back(static_cast<char>(byte));
  }
  input += "\"\\plain";

  EXPECT_EQ(livehd::json_util::escape(input),
            "\\u0000\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007"
            "\\b\\t\\n\\u000b\\f\\r\\u000e\\u000f"
            "\\u0010\\u0011\\u0012\\u0013\\u0014\\u0015\\u0016\\u0017"
            "\\u0018\\u0019\\u001a\\u001b\\u001c\\u001d\\u001e\\u001f"
            "\\\"\\\\plain");
}

TEST(HashUtil, StablePrimitives) {
  constexpr auto text_hash = livehd::hash_util::fnv1a64("LiveHD");
  static_assert(text_hash == 0xbd707647b6ed94b3ULL);
  static_assert(livehd::hash_util::mix64(0x123456789abcdef0ULL) == 0x18b8c062f6f42398ULL);
  EXPECT_EQ(livehd::hash_util::combine64(1, 2), 0xf9122d6051144cc9ULL);

  // Seeded chaining splices fragments; the u64 fold matches the byte-fed form.
  static_assert(livehd::hash_util::fnv1a64("bc", livehd::hash_util::fnv1a64("a")) == livehd::hash_util::fnv1a64("abc"));
  static_assert(livehd::hash_util::fnv1a64_u64(0x0123456789abcdefULL, livehd::hash_util::kFnv1a64_offset) == 0x37eb3f3347761c55ULL);
}

TEST(StrTools, CanonicalEntityName) {
  EXPECT_EQ(str_tools::canonical_entity_name("file.foo__u8_s16_bool"), "foo");
  EXPECT_EQ(str_tools::canonical_entity_name("file.foo__U8_S16_Bool"), "foo");
  EXPECT_EQ(str_tools::canonical_entity_name("file.foo__named"), "foo__named");
  EXPECT_EQ(str_tools::canonical_entity_name("foo"), "foo");
}

}  // namespace
