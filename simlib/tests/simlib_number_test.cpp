

#include <vector>
#include <iostream>
#include <functional>

#include "gtest/gtest.h"
#include "fmt/format.h"

#include "sint.hpp"
#include "uint.hpp"

class Simlib_number : public ::testing::Test {

public:
  void SetUp() override {};
};

TEST_F(Simlib_number, Trivial) {

  UInt<16>  a16u(0xcafe);
  UInt<16>  b16u(0xbebe);
  UInt<64>  a64u(0xe2bd5b4ff8b30fc8);
  UInt<64>  b64u(0x2fc353e33c6938a7);
  UInt<80>  a80u("0x987426c1f7cd7d4d693a");
  UInt<80>  b80u("0x563a0757a07b7bd27485");
  UInt<128> a128u("0xe903646a697fcaa344d2b2aa95e47b5d");
  UInt<128> b128u("0x56fa570ecb04adca42405f12bf28b822");

  SInt<16>  a16s(0x6dba);
  SInt<16>  b16s(0xccb2);
  SInt<64>  a64s(0x71088d1c4a5c4a02);
  SInt<64>  b64s(0xdefaa415d9062302);
  SInt<80>  a80s("0x381c1fe6bca6875922fe");
  SInt<80>  b80s("0xefbe8ae0d38ab7f36dda");
  SInt<128> a128s("0x6e0939370acc19daec06e9c13db50674");
  SInt<128> b128s("0xbeb828fdbac591dba8e38eeb433f563d");

  EXPECT_EQ( a16u.cat(b16u)   , UInt<32>(0xcafebebe) );
  EXPECT_EQ( a16u.cat(a64u)   , UInt<80>("0xcafee2bd5b4ff8b30fc8") );
  EXPECT_EQ( a64u.cat(a16u)   , UInt<80>("0xe2bd5b4ff8b30fc8cafe") );
  EXPECT_EQ( a16u.cat(a80u)   , UInt<96>("0xcafe987426c1f7cd7d4d693a") );
  EXPECT_EQ( a80u.cat(a16u)   , UInt<96>("0x987426c1f7cd7d4d693acafe") );
  EXPECT_EQ( a16u.cat(a128u)  , UInt<144>("0xcafee903646a697fcaa344d2b2aa95e47b5d") );
  EXPECT_EQ( a128u.cat(a16u)  , UInt<144>("0xe903646a697fcaa344d2b2aa95e47b5dcafe") );
  EXPECT_EQ( a80u.cat(a128u)  , UInt<208>("0x987426c1f7cd7d4d693ae903646a697fcaa344d2b2aa95e47b5d") );
  EXPECT_EQ( a128u.cat(a80u)  , UInt<208>("0xe903646a697fcaa344d2b2aa95e47b5d987426c1f7cd7d4d693a") );
  EXPECT_EQ( a128u.cat(b128u) , UInt<256>("0xe903646a697fcaa344d2b2aa95e47b5d56fa570ecb04adca42405f12bf28b822") );
}

