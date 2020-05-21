

#include <vector>
#include <iostream>
#include <functional>
#include <cassert>

#include "gtest/gtest.h"
#include "fmt/format.h"

#include "lbench.hpp"
#include "mmap_map.hpp"

#include "sint.hpp"
#include "uint.hpp"

class Simlib_number : public ::testing::Test {

public:
  void SetUp() override {};
};

// https://github.com/niekbouman/ctbignum/search?q=to_big_int&unscoped_q=to_big_int
//
// https://blog.mattbierner.com/stupid-template-tricks-stdintegral_constant-user-defined-literal/

template<int N>
void print_method(const UInt<N> v) {
  fmt::print("{} bits:{}\n", v.to_string(), N);
}

TEST_F(Simlib_number, Trivial) {
  Lbench b("trivial");

  constexpr auto v_10 = 10_prp;
  constexpr auto v_10b = 0xa_prp;
  static_assert(v_10==v_10b, "same at compile time");

  auto v_170 = 0xaa_prp;

  UInt<4>  a16u_10(0xa);
  EXPECT_EQ( a16u_10   , v_10b);

  //static_assert(v_10.cat(v_10) ==  v_170);

  print_method(v_10);
  print_method(v_10b);

  UInt<1>   a1u(0x1);
  UInt<16>  a16u(0xcafe);
  UInt<16>  b16u(0xbebe);
  UInt<64>  a64u(0xe2bd5b4ff8b30fc8);
  UInt<64>  b64u(0x2fc353e33c6938a7);
  UInt<80>  a80u("0x987426c1f7cd7d4d693a");
  UInt<80>  b80u("0x563a0757a07b7bd27485");
  UInt<128> a128u("0xe903646a697fcaa344d2b2aa95e47b5d");
  UInt<128> b128u("0x56fa570ecb04adca42405f12bf28b822");

  print_method(a1u);
  print_method(a16u);

  fmt::print("UInt<1> has sizeof {}\n", sizeof(a1u));
  fmt::print("UInt<16> has sizeof {}\n", sizeof(a16u));
  fmt::print("UInt<64> has sizeof {}\n", sizeof(a64u));
  fmt::print("UInt<80> has sizeof {}\n", sizeof(a80u));
  fmt::print("UInt<128> has sizeof {}\n", sizeof(a128u));

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

#if 0
  auto base_ptr = data.data();
  auto& big_int_ref = *base_ptr;

    auto x = reinterpret_cast<big_int<Len> *>(base_ptr + i);
    auto y = reinterpret_cast<big_int<Len> *>(base_ptr + i + Len);
#endif
}

TEST_F(Simlib_number, Storage) {
  Lbench b("storage");

  uint64_t data = 3;

  auto val16_ptr = reinterpret_cast<UInt<16> *>(&data);
  auto &val16 = *val16_ptr;

  auto val8_ptr = reinterpret_cast<UInt<8> *>(&data);
  auto &val8 = *val8_ptr;

  EXPECT_EQ(sizeof(val8),1);
  EXPECT_EQ(sizeof(val16),8);

  print_method(val8);
  print_method(val16);

  EXPECT_EQ(val8, 0x03_prp);
  EXPECT_EQ(val16, 0x0003_prp);

  auto val7_ptr = reinterpret_cast<UInt<7> *>(&data);
  auto &val7 = *val7_ptr;

  auto val18_ptr = reinterpret_cast<UInt<18> *>(&data);
  auto &val18 = *val18_ptr;

  print_method(val7);
  print_method(val18);

  // FIXME: EXPECT_EQ(0x1f58d11f58d11f58d1_prp, 076543210765432107654321_prp);

  print_method(012345_prp);
  print_method(0x9876543210987654321_prp);
  print_method(076543210765432107654321_prp);
  fmt::print("verilog: {}\n", (076543210765432107654321_prp).to_verilog());

  mmap_lib::map<uint32_t, std::string_view> map;

  map.set(12345, (0x12345_prp).to_string());
  EXPECT_TRUE(map.has(12345));
  auto v = map.get(12345);
  EXPECT_EQ(v, "0x12345");
}

