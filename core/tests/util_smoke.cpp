// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <string>

#include "gtest/gtest.h"
#include "hash_util.hpp"
#include "json_util.hpp"
#include "str_tools.hpp"

namespace {

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
  static_assert(livehd::hash_util::fnv1a64_u64(0x0123456789abcdefULL, livehd::hash_util::kFnv1a64_offset)
                == 0x37eb3f3347761c55ULL);
}

TEST(StrTools, CanonicalEntityName) {
  EXPECT_EQ(str_tools::canonical_entity_name("file.foo__u8_s16_bool"), "foo");
  EXPECT_EQ(str_tools::canonical_entity_name("file.foo__named"), "foo__named");
  EXPECT_EQ(str_tools::canonical_entity_name("foo"), "foo");
}

}  // namespace
