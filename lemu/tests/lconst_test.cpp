
#include <iostream>
#include <iomanip>
#include <vector>
#include <iterator>

#include <vector>
#include <iostream>
#include <functional>
#include <cassert>

#include "gtest/gtest.h"
#include "fmt/format.h"

#include "lbench.hpp"
#include "lrand.hpp"
#include "mmap_map.hpp"

#include "lconst.hpp"
#include "sint.hpp"
#include "uint.hpp"

/*

  uint and sint are the low level (C++ generated) library from simlib.
  Lconst must be compatible with uint/sint. The main difference is that
  Lconst does not have a "known" at compile time number of bits. As a
  result, it uses the slower boost cpp interface.

  This unit test checks lvariable and that uint/sint are consistent with
  Lconst (still a work in progress)

*/

class Lconst_test : public ::testing::Test {
protected:
  UInt<16>  a16u;
  UInt<16>  b16u;
  UInt<64>  a64u;
  UInt<64>  b64u;
  UInt<80>  a80u;
  UInt<80>  b80u;
  UInt<128> a128u;
  UInt<128> b128u;

  SInt<16>  a16s;
  SInt<16>  b16s;
  SInt<64>  a64s;
  SInt<64>  b64s;
  SInt<80>  a80s;
  SInt<80>  b80s;
  SInt<128> a128s;
  SInt<128> b128s;

  Lconst l_a16u;
  Lconst l_b16u;
  Lconst l_a64u;
  Lconst l_b64u;
  Lconst l_a80u;
  Lconst l_b80u;
  Lconst l_a128u;
  Lconst l_b128u;

  Lconst l_a16s;
  Lconst l_b16s;
  Lconst l_a64s;
  Lconst l_b64s;
  Lconst l_a80s;
  Lconst l_b80s;
  Lconst l_a128s;
  Lconst l_b128s;

public:
  void SetUp() override {
    a16u = UInt<16>(0xcafe);
    b16u = UInt<16>(0xbebe);
    a64u = UInt<64>(0xe2bd5b4ff8b30fc8);
    b64u = UInt<64>(0x2fc353e33c6938a7);
    a80u = UInt<80>("0x987426c1f7cd7d4d693a");
    b80u = UInt<80>("0x563a0757a07b7bd27485");
    a128u = UInt<128>("0xe903646a697fcaa344d2b2aa95e47b5d");
    b128u = UInt<128>("0x56fa570ecb04adca42405f12bf28b822");

    a16s = SInt<16>(0x6dba);
    b16s = SInt<16>(0xccb2);
    a64s = SInt<64>(0x71088d1c4a5c4a02);
    b64s = SInt<64>(0xdefaa415d9062302);
    a80s = SInt<80>("0x381c1fe6bca6875922fe");
    b80s = SInt<80>("0xefbe8ae0d38ab7f36dda");
    a128s = SInt<128>("0x6e0939370acc19daec06e9c13db50674");
    b128s = SInt<128>("0xbeb828fdbac591dba8e38eeb433f563d");

    l_a16u = Lconst("0xcafe");
    l_b16u = Lconst("0xbebe");
    l_a64u = Lconst("0xe2bd5b4ff8b30fc8");
    l_b64u = Lconst("0x2fc353e33c6938a7");
    l_a80u = Lconst("0x987426c1f7cd7d4d693a");
    l_b80u = Lconst("0x563a0757a07b7bd27485");
    l_a128u = Lconst("0xe903646a697fcaa344d2b2aa95e47b5d");
    l_b128u = Lconst("0x56fa570ecb04adca42405f12bf28b822");

    l_a16s = Lconst("0x6dba");
    l_b16s = Lconst("0xccb2");
    l_a64s = Lconst("0x71088d1c4a5c4a02");
    l_b64s = Lconst("0xdefaa415d9062302");
    l_a80s = Lconst("0x381c1fe6bca6875922fe");
    l_b80s = Lconst("0xefbe8ae0d38ab7f36dda");
    l_a128s = Lconst("0x6e0939370acc19daec06e9c13db50674");
    l_b128s = Lconst("0xbeb828fdbac591dba8e38eeb433f563d");
  };
};

template<int N>
void print_method(const UInt<N> v) {
  fmt::print("{} bits:{}\n", v.to_string(), N);
}

TEST_F(Lconst_test, lvar_sizes) {

  Lconst l1("-1u8"); // 0xFF
  EXPECT_TRUE(l1.eq_op(Lconst("0xFF")));
  EXPECT_EQ(l1.get_bits(), 8);

  auto s1 = l1 + Lconst("1");
  fmt::print("s1:{} bits:{} unsign:{}\n", s1.to_pyrope(), s1.get_bits(), s1.is_unsigned());
  EXPECT_TRUE(s1.eq_op(Lconst("0x100")));
  EXPECT_EQ(s1.get_bits(), 9);


  auto s2 = l1 + Lconst("-1s");
  fmt::print("s2:{} bits:{} unsign:{}\n", s2.to_pyrope(), s2.get_bits(), s2.is_unsigned());
  EXPECT_TRUE(s2.eq_op(Lconst("0xFE")));
  EXPECT_EQ(s2.get_bits(), 8);

  auto s3 = l1 + Lconst("-1");
  fmt::print("s3:{} bits:{} unsign:{}\n", s3.to_pyrope(), s3.get_bits(), s3.is_unsigned());
  EXPECT_TRUE(s3.eq_op(Lconst("0xFE")));
  EXPECT_EQ(s3.get_bits(), 8);

  auto s4 = l1 + Lconst("0x01Fs12");
  fmt::print("s4:{} bits:{} unsign:{}\n", s4.to_pyrope(), s4.get_bits(), s4.is_unsigned());
  EXPECT_TRUE(s4.eq_op(Lconst("0x11E")));
  EXPECT_EQ(s4.get_bits(), 9);
  EXPECT_TRUE(l1.is_explicit_bits());
  EXPECT_FALSE(s4.is_explicit_bits());
}

TEST_F(Lconst_test, uint_comparison_operators) {
  EXPECT_TRUE( a16u == a16u );
  EXPECT_TRUE( a16u != b16u );
  EXPECT_TRUE( b16u <  a16u );
  EXPECT_TRUE( b16u <= a16u );
  EXPECT_TRUE( a16u <= a16u );
  EXPECT_TRUE( a16u >= a16u );
  EXPECT_TRUE( a16u >  b16u );

  EXPECT_TRUE( a64u == a64u );
  EXPECT_TRUE( a64u != b64u );
  EXPECT_TRUE( b64u <  a64u );
  EXPECT_TRUE( b64u <= a64u );
  EXPECT_TRUE( a64u <= a64u );
  EXPECT_TRUE( a64u >= a64u );
  EXPECT_TRUE( a64u >  b64u );

  EXPECT_TRUE( a80u == a80u );
  EXPECT_TRUE( a80u != b80u );
  EXPECT_TRUE( b80u <  a80u );
  EXPECT_TRUE( b80u <= a80u );
  EXPECT_TRUE( a80u <= a80u );
  EXPECT_TRUE( a80u >= a80u );
  EXPECT_TRUE( a80u >  b80u );

  EXPECT_TRUE( a128u == a128u );
  EXPECT_TRUE( a128u != b128u );
  EXPECT_TRUE( b128u <  a128u );
  EXPECT_TRUE( b128u <= a128u );
  EXPECT_TRUE( a128u <= a128u );
  EXPECT_TRUE( a128u >= a128u );
  EXPECT_TRUE( a128u >  b128u );

  EXPECT_TRUE( l_a16u == l_a16u );
  EXPECT_TRUE( l_a16u != l_b16u );
  EXPECT_TRUE( l_b16u <  l_a16u );
  EXPECT_TRUE( l_b16u <= l_a16u );
  EXPECT_TRUE( l_a16u <= l_a16u );
  EXPECT_TRUE( l_a16u >= l_a16u );
  EXPECT_TRUE( l_a16u >  l_b16u );

  EXPECT_TRUE( l_a64u == l_a64u );
  EXPECT_TRUE( l_a64u != l_b64u );
  EXPECT_TRUE( l_b64u <  l_a64u );
  EXPECT_TRUE( l_b64u <= l_a64u );
  EXPECT_TRUE( l_a64u <= l_a64u );
  EXPECT_TRUE( l_a64u >= l_a64u );
  EXPECT_TRUE( l_a64u >  l_b64u );

  EXPECT_TRUE( l_a80u == l_a80u );
  EXPECT_TRUE( l_a80u != l_b80u );
  EXPECT_TRUE( l_b80u <  l_a80u );
  EXPECT_TRUE( l_b80u <= l_a80u );
  EXPECT_TRUE( l_a80u <= l_a80u );
  EXPECT_TRUE( l_a80u >= l_a80u );
  EXPECT_TRUE( l_a80u >  l_b80u );

  EXPECT_TRUE( l_a128u == l_a128u );
  EXPECT_TRUE( l_a128u != l_b128u );
  EXPECT_TRUE( l_b128u <  l_a128u );
  EXPECT_TRUE( l_b128u <= l_a128u );
  EXPECT_TRUE( l_a128u <= l_a128u );
  EXPECT_TRUE( l_a128u >= l_a128u );
  EXPECT_TRUE( l_a128u >  l_b128u );
}

TEST_F(Lconst_test, uint_constructors) {
  EXPECT_TRUE( a16u == UInt<16>("0xcafe") );
  EXPECT_TRUE( a16u == UInt<16>(a16u) );
  EXPECT_TRUE( a64u == UInt<64>(a64u) );
  EXPECT_TRUE( a80u == UInt<80>(a80u) );
  EXPECT_TRUE( a128u == UInt<128>(a128u) );
  EXPECT_TRUE( a16u == UInt<16>(0xfcafe) );
  EXPECT_TRUE( UInt<128>(0x1) == UInt<128>("0x1") );
  EXPECT_TRUE( a80u == UInt<80>(std::array<uint64_t,2>({0x9874, 0x26c1f7cd7d4d693a})) );
  EXPECT_TRUE( a128u == UInt<128>(std::array<uint64_t,2>({0xe903646a697fcaa3, 0x44d2b2aa95e47b5d})) );
}

TEST_F(Lconst_test, uint_pad_operator) {
  EXPECT_TRUE( a16u == a16u.pad<16>() );
  EXPECT_TRUE( a16u.pad<64>() == UInt<64>(0xcafe) );
  EXPECT_TRUE( a64u.pad<200>() == a64u.pad<200>() );
  EXPECT_TRUE( a64u.pad<200>() != b64u.pad<200>() );
}

TEST_F(Lconst_test, uint_cat_operator) {
  EXPECT_TRUE( a16u.cat(b16u) == UInt<32>(0xcafebebe) );
  EXPECT_TRUE( a16u.cat(a64u) == UInt<80>("0xcafee2bd5b4ff8b30fc8") );
  EXPECT_TRUE( a64u.cat(a16u) == UInt<80>("0xe2bd5b4ff8b30fc8cafe") );
  EXPECT_TRUE( a16u.cat(a80u) == UInt<96>("0xcafe987426c1f7cd7d4d693a") );
  EXPECT_TRUE( a80u.cat(a16u) == UInt<96>("0x987426c1f7cd7d4d693acafe") );
  EXPECT_TRUE( a16u.cat(a128u) == UInt<144>("0xcafee903646a697fcaa344d2b2aa95e47b5d") );
  EXPECT_TRUE( a128u.cat(a16u) == UInt<144>("0xe903646a697fcaa344d2b2aa95e47b5dcafe") );
  EXPECT_TRUE( a80u.cat(a128u) == UInt<208>("0x987426c1f7cd7d4d693ae903646a697fcaa344d2b2aa95e47b5d") );
  EXPECT_TRUE( a128u.cat(a80u) == UInt<208>("0xe903646a697fcaa344d2b2aa95e47b5d987426c1f7cd7d4d693a") );
  EXPECT_TRUE( a128u.cat(b128u) == UInt<256>("0xe903646a697fcaa344d2b2aa95e47b5d56fa570ecb04adca42405f12bf28b822") );
}

TEST_F(Lconst_test, uint_add_operator) {
  EXPECT_TRUE( a16u + b16u == UInt<17>(0x189bc) );

  for (int i = 0; i < 100; ++i) {
    // get 2 random 64+48 bit number
    __int128  t1 = rand();
    __int128  t2 = rand();
    t1 <<= rand() % 48;
    t2 <<= rand() % 48;
    t1 += rand();
    t2 += rand();
    UInt<112> v1(std::array<uint64_t, 2>({(uint64_t)(t1 >> 64), (uint64_t)t1}));
    UInt<112> v2(std::array<uint64_t, 2>({(uint64_t)(t2 >> 64), (uint64_t)t2}));
    auto      v3a = v1 + v2;
    auto      t3  = t1 + t2;
    UInt<113> v3b(std::array<uint64_t, 2>({(uint64_t)(t3 >> 64), (uint64_t)t3}));
    EXPECT_EQ(v3a, v3b);
  }

  for (int i = 0; i < 100; ++i) {
    // get 2 random 64+48 bit number
    __int128  t1 = rand();
    __int128  t2 = rand();
    t1 <<= rand() % 48;
    t2 <<= rand() % 48;
    t1 += rand();
    t2 += rand();
    UInt<128+32> v1(std::array<uint64_t, 3>({0, (uint64_t)(t1 >> 64), (uint64_t)t1}));
    UInt<112> v2(std::array<uint64_t, 2>({(uint64_t)(t2 >> 64), (uint64_t)t2}));
    auto      v3a = v1 + v2;
    auto      t3  = t1 + t2;
    UInt<113> v3b(std::array<uint64_t, 2>({(uint64_t)(t3 >> 64), (uint64_t)t3}));
    EXPECT_EQ(v3a, v3b);
  }

  EXPECT_TRUE( a64u + b64u == UInt<65>("0x11280af33351c486f") );
  EXPECT_TRUE( a80u + b80u == UInt<81>("0xeeae2e199848f91fddbf") );
  EXPECT_TRUE( a128u + b128u == UInt<129>("0x13ffdbb793484786d871311bd550d337f") );

  EXPECT_TRUE( l_a64u + l_b64u == Lconst("0x11280af33351c486f") );
  EXPECT_TRUE( l_a80u + l_b80u == Lconst("0xeeae2e199848f91fddbf") );
  EXPECT_TRUE( l_a128u + l_b128u == Lconst("0x13ffdbb793484786d871311bd550d337f") );
}

TEST_F(Lconst_test, uint_addw_operator) {
  EXPECT_TRUE( a16u.addw(b16u) == UInt<16>(0x89bc) );
  EXPECT_TRUE( a64u.addw(b64u) == UInt<64>("0x1280af33351c486f") );
  EXPECT_TRUE( a80u.addw(b80u) == UInt<80>("0xeeae2e199848f91fddbf") );
  EXPECT_TRUE( a128u.addw(b128u) == UInt<128>("0x3ffdbb793484786d871311bd550d337f") );
}

TEST_F(Lconst_test, uint_sub_operator) {
  EXPECT_TRUE( a16u - b16u == SInt<17>(0xc40) );
  EXPECT_TRUE( a64u - b64u == SInt<65>("0xb2fa076cbc49d721") );
  EXPECT_TRUE( a80u - b80u == SInt<81>("0x423a1f6a5752017af4b5") );
  EXPECT_TRUE( a128u - b128u == SInt<129>("0x92090d5b9e7b1cd902925397d6bbc33b") );
}

TEST_F(Lconst_test, uint_negate_operator) {
  EXPECT_TRUE( -a16u == SInt<17>(0x13502) );
  EXPECT_TRUE( -a64u == SInt<65>("0x11d42a4b0074cf038") );
  EXPECT_TRUE( -a80u == SInt<81>("0x1678bd93e083282b296c6") );
  EXPECT_TRUE( -a128u == SInt<129>("0x116fc9b959680355cbb2d4d556a1b84a3") );
  EXPECT_TRUE( (-(-a16u)) == SInt<18>(a16u.pad<18>()) );
  EXPECT_TRUE( (-(-a64u)) == SInt<66>(a64u.pad<66>()) );
  EXPECT_TRUE( (-(-a80u)) == SInt<82>(a80u.pad<82>()) );
  EXPECT_TRUE( (-(-a128u)) == SInt<130>(a128u.pad<130>()) );
}

TEST_F(Lconst_test, uint_mult_operator) {
  EXPECT_TRUE( a16u * b16u == UInt<32>(0x973f2c84) );
  EXPECT_TRUE( a64u * b64u == UInt<128>("0x2a4dc44ce497c914d9d3df0ec14b0b78") );
  EXPECT_TRUE( a80u * b80u == UInt<160>("0x335993b54d4bc81d37835773f77fa4765c79f322") );
  EXPECT_TRUE( a128u * b128u == UInt<256>("0x4f2b00496d758f68469327504061b9045f77243f5cfda64ce9fb69abca8b3a5a") );
}

TEST_F(Lconst_test, uint_div_operator) {
  EXPECT_TRUE( a16u / b16u == UInt<16>(1) );
  EXPECT_TRUE( a64u / b64u == UInt<64>(4) );
}

TEST_F(Lconst_test, uint_mod_operator) {
  EXPECT_TRUE( a16u % b16u == UInt<16>(0xc40) );
  EXPECT_TRUE( a64u % b64u == UInt<64>(0x23b00bc3070e2d2c) );
}

TEST_F(Lconst_test, uint_not_operator) {
  EXPECT_TRUE( ~a16u == UInt<16>(0x3501) );
  EXPECT_TRUE( ~a64u == UInt<64>(0x1d42a4b0074cf037) );
  EXPECT_TRUE( ~a80u == UInt<80>("0x678bd93e083282b296c5") );
  EXPECT_TRUE( ~a128u == UInt<128>("0x16fc9b959680355cbb2d4d556a1b84a2") );
}

TEST_F(Lconst_test, uint_and_operator) {
  EXPECT_TRUE( (a16u & b16u) == UInt<16>(0x8abe) );
  EXPECT_TRUE( (a64u & b64u) == UInt<64>(0x2281534338210880) );
  EXPECT_TRUE( (a80u & b80u) == UInt<80>("0x10300641a04979406000") );
  EXPECT_TRUE( (a128u & b128u) == UInt<128>("0x4002440a490488824040120295203800") );
}

TEST_F(Lconst_test, uint_or_operator) {
  EXPECT_TRUE( (a16u | b16u) == UInt<16>(0xfefe) );
  EXPECT_TRUE( (a64u | b64u) == UInt<64>(0xefff5beffcfb3fef) );
  EXPECT_TRUE( (a80u | b80u) == UInt<80>("0xde7e27d7f7ff7fdf7dbf") );
  EXPECT_TRUE( (a128u | b128u) == UInt<128>("0xfffb776eeb7fefeb46d2ffbabfecfb7f") );
}

TEST_F(Lconst_test, uint_xor_operator) {
  EXPECT_TRUE( (a16u ^ b16u) == UInt<16>(0x7440) );
  EXPECT_TRUE( (a64u ^ b64u) == UInt<64>(0xcd7e08acc4da376f) );
  EXPECT_TRUE( (a80u ^ b80u) == UInt<80>("0xce4e219657b6069f1dbf") );
  EXPECT_TRUE( (a128u ^ b128u) == UInt<128>("0xbff93364a27b67690692edb82accc37f") );
}

TEST_F(Lconst_test, uint_andr_operator) {
  EXPECT_TRUE( (a16u.andr()) == UInt<1>(0x0) );
  EXPECT_TRUE( ((~UInt<16>(0)).andr()) == UInt<1>(1) );
  EXPECT_TRUE( (a64u.andr()) == UInt<1>(0x0) );
  EXPECT_TRUE( ((~UInt<64>(0)).andr()) == UInt<1>(1) );
  EXPECT_TRUE( (a80u.andr()) == UInt<1>(0x0) );
  EXPECT_TRUE( ((~UInt<80>(0)).andr()) == UInt<1>(1) );
  EXPECT_TRUE( (a128u.andr()) == UInt<1>(0x0) );
  EXPECT_TRUE( ((~UInt<128>(0)).andr()) == UInt<1>(1) );
}

TEST_F(Lconst_test, uint_orr_operator) {
  EXPECT_TRUE( (a16u.orr()) == UInt<1>(0x1) );
  EXPECT_TRUE( (UInt<16>(0).andr()) == UInt<1>(0) );
  EXPECT_TRUE( (a64u.orr()) == UInt<1>(0x1) );
  EXPECT_TRUE( (UInt<64>(0).andr()) == UInt<1>(0) );
  EXPECT_TRUE( (a80u.orr()) == UInt<1>(0x1) );
  EXPECT_TRUE( (UInt<80>(0).andr()) == UInt<1>(0) );
  EXPECT_TRUE( (a128u.orr()) == UInt<1>(0x1) );
  EXPECT_TRUE( (UInt<128>(0).andr()) == UInt<1>(0) );
}

TEST_F(Lconst_test, uint_xorr_operator) {
  EXPECT_TRUE( (a16u.xorr()) == UInt<1>(0x1) );
  EXPECT_TRUE( (b16u.xorr()) == UInt<1>(0x0) );
  EXPECT_TRUE( (a64u.xorr()) == UInt<1>(0x1) );
  EXPECT_TRUE( (b64u.xorr()) == UInt<1>(0x0) );
  EXPECT_TRUE( (a80u.xorr()) == UInt<1>(0x1) );
  EXPECT_TRUE( (b80u.xorr()) == UInt<1>(0x1) );
  EXPECT_TRUE( (a128u.xorr()) == UInt<1>(0x0) );
  EXPECT_TRUE( (b128u.xorr()) == UInt<1>(0x1) );
}

TEST_F(Lconst_test, uint_bits_operator) {
  EXPECT_TRUE( (a16u.bits<11,4>()) == UInt<8>(0xaf) );
  EXPECT_TRUE( (a64u.bits<47,24>()) == UInt<24>(0x5b4ff8) );
  EXPECT_TRUE( (a80u.bits<79,64>()) == UInt<16>(0x9874) );
  EXPECT_TRUE( (a80u.bits<71,56>()) == UInt<16>(0x7426) );
  EXPECT_TRUE( (a128u.bits<111,96>()) == UInt<16>(0x646a) );
  EXPECT_TRUE( (a128u.bits<71,56>()) == UInt<16>(0xa344) );
}

TEST_F(Lconst_test, uint_head_operator) {
  EXPECT_TRUE( (a16u.head<8>()) == UInt<8>(0xca) );
  EXPECT_TRUE( (a64u.head<64>()) == a64u );
  EXPECT_TRUE( (a64u.head<16>()) == UInt<16>(0xe2bd) );
  EXPECT_TRUE( (a80u.head<24>()) == UInt<24>(0x987426) );
  EXPECT_TRUE( (a128u.head<32>()) == UInt<32>(0xe903646a) );
}

TEST_F(Lconst_test, uint_tail_operator) {
  EXPECT_TRUE( (a16u.tail<8>()) == UInt<8>(0xfe) );
  EXPECT_TRUE( (a64u.tail<0>()) == a64u );
  EXPECT_TRUE( (a64u.tail<16>()) == UInt<48>(0x5b4ff8b30fc8) );
  EXPECT_TRUE( (a80u.tail<8>()) == UInt<72>("0x7426c1f7cd7d4d693a") );
  EXPECT_TRUE( (a128u.tail<32>()) == UInt<96>("0x697fcaa344d2b2aa95e47b5d") );
}

TEST_F(Lconst_test, uint_static_shifts) {
  EXPECT_TRUE( a16u.shl<0>() == a16u );
  EXPECT_TRUE( a16u.shl<4>() == UInt<20>(0xcafe0) );
  EXPECT_TRUE( a64u.shl<8>() == UInt<72>("0xe2bd5b4ff8b30fc800") );
  EXPECT_TRUE( a80u.shl<60>() == UInt<140>("0x987426c1f7cd7d4d693a000000000000000") );
  EXPECT_TRUE( a128u.shl<72>() == UInt<200>("0xe903646a697fcaa344d2b2aa95e47b5d000000000000000000") );

  EXPECT_TRUE( a16u.shlw<0>() == a16u );
  EXPECT_TRUE( a16u.shlw<4>() == UInt<16>(0xafe0) );
  EXPECT_TRUE( a64u.shlw<8>() == UInt<64>("0xbd5b4ff8b30fc800") );
  EXPECT_TRUE( a80u.shlw<60>() == UInt<80>("0xd693a000000000000000") );
  EXPECT_TRUE( a128u.shlw<72>() == UInt<128>("0xd2b2aa95e47b5d000000000000000000") );

  EXPECT_TRUE( a16u.shr<0>() == a16u );
  EXPECT_TRUE( a16u.shr<8>() == UInt<8>(0xca) );
  EXPECT_TRUE( a64u.shr<16>() == UInt<48>(0xe2bd5b4ff8b3) );
  EXPECT_TRUE( a80u.shr<24>() == UInt<56>(0x987426c1f7cd7d) );
  EXPECT_TRUE( a128u.shr<48>() == UInt<80>("0xe903646a697fcaa344d2"));
}

TEST_F(Lconst_test, uint_dynamic_shifts) {
  EXPECT_TRUE( (a16u << UInt<1>(0)) == UInt<17>(0xcafe) );
  EXPECT_TRUE( (a16u << UInt<4>(4)) == UInt<31>(0xcafe0) );
  EXPECT_TRUE( (a64u << UInt<4>(8)) == UInt<79>("0xe2bd5b4ff8b30fc800") );
  EXPECT_TRUE( (a80u << UInt<5>(12)) == UInt<111>("0x987426c1f7cd7d4d693a000") );
  EXPECT_TRUE( (a128u << UInt<6>(16)) == UInt<191>("0xe903646a697fcaa344d2b2aa95e47b5d0000") );

  EXPECT_TRUE( (a16u.dshlw(UInt<1>(0))) == a16u );
  EXPECT_TRUE( (a16u.dshlw(UInt<4>(4))) == UInt<16>(0xafe0) );
  EXPECT_TRUE( (a64u.dshlw(UInt<4>(8))) == UInt<64>("0xbd5b4ff8b30fc800") );
  EXPECT_TRUE( (a80u.dshlw(UInt<6>(60))) == UInt<80>("0xd693a000000000000000") );
  EXPECT_TRUE( (a128u.dshlw(UInt<7>(72))) == UInt<128>("0xd2b2aa95e47b5d000000000000000000") );

  EXPECT_TRUE( (a16u >> UInt<1>(0)) == UInt<16>(0xcafe) );
  EXPECT_TRUE( (a16u >> UInt<4>(4)) == UInt<16>(0x0caf) );
  EXPECT_TRUE( (a64u >> UInt<4>(8)) == UInt<64>("0xe2bd5b4ff8b30f") );
  EXPECT_TRUE( (a80u >> UInt<5>(12)) == UInt<80>("0x987426c1f7cd7d4d6") );
  EXPECT_TRUE( (a128u >> UInt<6>(16)) == UInt<128>("0xe903646a697fcaa344d2b2aa95e4") );
}

TEST_F(Lconst_test, uint_conversion) {
  EXPECT_TRUE( a16u.asUInt() == a16u );
  EXPECT_TRUE( a16u.asSInt() == SInt<16>("0xcafe") );
  EXPECT_TRUE( a16u.cvt() == SInt<17>(0xcafe) );
}

TEST_F(Lconst_test, sint_comparison_operators) {
  EXPECT_TRUE( a16s == a16s );
  EXPECT_TRUE( a16s != b16s );
  EXPECT_TRUE( b16s <  a16s );
  EXPECT_TRUE( b16s <= a16s );
  EXPECT_TRUE( a16s <= a16s );
  EXPECT_TRUE( a16s >= a16s );
  EXPECT_TRUE( a16s >  b16s );

  EXPECT_TRUE( a64s == a64s );
  EXPECT_TRUE( a64s != b64s );
  EXPECT_TRUE( b64s <  a64s );
  EXPECT_TRUE( b64s <= a64s );
  EXPECT_TRUE( a64s <= a64s );
  EXPECT_TRUE( a64s >= a64s );
  EXPECT_TRUE( a64s >  b64s );

  EXPECT_TRUE( a80s == a80s );
  EXPECT_TRUE( a80s != b80s );
  EXPECT_TRUE( b80s <  a80s );
  EXPECT_TRUE( b80s <= a80s );
  EXPECT_TRUE( a80s <= a80s );
  EXPECT_TRUE( a80s >= a80s );
  EXPECT_TRUE( a80s >  b80s );

  EXPECT_TRUE( a128s == a128s );
  EXPECT_TRUE( a128s != b128s );
  EXPECT_TRUE( b128s <  a128s );
  EXPECT_TRUE( b128s <= a128s );
  EXPECT_TRUE( a128s <= a128s );
  EXPECT_TRUE( a128s >= a128s );
  EXPECT_TRUE( a128s >  b128s );
}

TEST_F(Lconst_test, sint_constructors) {
  EXPECT_TRUE( b16s == SInt<16>(0xccb2) );
  EXPECT_TRUE( b16s == SInt<16>(b16s) );
  EXPECT_TRUE( b64s == SInt<64>(b64s) );
  EXPECT_TRUE( b80s == SInt<80>(b80s) );
  EXPECT_TRUE( b128s == SInt<128>(b128s) );
  EXPECT_TRUE( a80s == SInt<80>(std::array<uint64_t,2>({0x381c, 0x1fe6bca6875922fe})) );
  EXPECT_TRUE( a128s == SInt<128>(std::array<uint64_t,2>({0x6e0939370acc19da, 0xec06e9c13db50674})) );
}

TEST_F(Lconst_test, sint_pad_operator) {
  EXPECT_TRUE( a16s == a16s.pad<16>() );
  EXPECT_TRUE( a16s.pad<64>() == SInt<64>(0x6dba) );
  EXPECT_TRUE( a64s.pad<200>() == a64s.pad<100>().pad<200>() );
  EXPECT_TRUE( a64u.pad<200>() != b64u.pad<200>() );
  EXPECT_TRUE( b16s.pad<64>() == SInt<64>(0xffffffffffffccb2) );
}

TEST_F(Lconst_test, sint_cat_operator) {
  EXPECT_TRUE( a16s.cat(b16s) == SInt<32>("0x6dbaccb2") );
  EXPECT_TRUE( b16s.cat(b64s) == SInt<80>("0xccb2defaa415d9062302") );
  EXPECT_TRUE( b64s.cat(b16s) == SInt<80>("0xdefaa415d9062302ccb2") );
  EXPECT_TRUE( b16s.cat(b80s) == SInt<96>("0xccb2efbe8ae0d38ab7f36dda") );
  EXPECT_TRUE( b80s.cat(b16s) == SInt<96>("0xefbe8ae0d38ab7f36ddaccb2") );
  EXPECT_TRUE( b16s.cat(b128s) == SInt<144>("0xccb2beb828fdbac591dba8e38eeb433f563d") );
  EXPECT_TRUE( b128s.cat(b16s) == SInt<144>("0xbeb828fdbac591dba8e38eeb433f563dccb2") );
  EXPECT_TRUE( b80s.cat(b128s) == SInt<208>("0xefbe8ae0d38ab7f36ddabeb828fdbac591dba8e38eeb433f563d") );
  EXPECT_TRUE( b128s.cat(b80s) == SInt<208>("0xbeb828fdbac591dba8e38eeb433f563defbe8ae0d38ab7f36dda") );
  EXPECT_TRUE( b128s.cat(b128s) == SInt<256>("0xbeb828fdbac591dba8e38eeb433f563dbeb828fdbac591dba8e38eeb433f563d") );
}

TEST_F(Lconst_test, sint_add_operator) {
  EXPECT_TRUE( a16s + b16s == SInt<17>(0x3a6c) );
  EXPECT_TRUE( b16s + b16s == SInt<17>(0x19964) );
  EXPECT_TRUE( a64s + b64s == SInt<65>("0x5003313223626d04") );
  EXPECT_TRUE( b64s + b64s == SInt<65>("0x1bdf5482bb20c4604") );
  EXPECT_TRUE( a80s + b80s == SInt<81>("0x27daaac790313f4c90d8") );
  EXPECT_TRUE( b80s + b80s == SInt<81>("0x1df7d15c1a7156fe6dbb4") );
  EXPECT_TRUE( a128s + b128s == SInt<129>("0x2cc16234c591abb694ea78ac80f45cb1") );
  EXPECT_TRUE( b128s + b128s == SInt<129>("0x17d7051fb758b23b751c71dd6867eac7a") );
  EXPECT_TRUE( SInt<64>(1) + SInt<64>(-1) == SInt<65>(0) );
  EXPECT_TRUE( SInt<64>(-1) + SInt<64>(-1) == SInt<65>(-2) );
}

TEST_F(Lconst_test, sint_addw_operator) {
  EXPECT_TRUE( a16s.addw(b16s) == SInt<16>(0x3a6c) );
  EXPECT_TRUE( a64s.addw(b64s) == SInt<64>("0x5003313223626d04") );
  EXPECT_TRUE( a80s.addw(b80s) == SInt<80>("0x27daaac790313f4c90d8") );
  EXPECT_TRUE( a128s.addw(b128s) == SInt<128>("0x2cc16234c591abb694ea78ac80f45cb1") );
  EXPECT_TRUE( SInt<64>(1).addw(SInt<64>(-1)) == SInt<64>(0) );
  EXPECT_TRUE( SInt<64>(-1).addw(SInt<64>(-1)) == SInt<64>(-2) );
}

TEST_F(Lconst_test, sint_sub_operator) {
  EXPECT_TRUE( a16s - b16s == SInt<17>(0xa108) );
  EXPECT_TRUE( b16s - a16s == SInt<17>(0x15ef8) );
  EXPECT_TRUE( b16s - SInt<16>(0) == b16s.pad<17>() );
  EXPECT_TRUE( a64s - b64s == SInt<65>("0x920de90671562700") );
  EXPECT_TRUE( b64s - a64s == SInt<65>("0x16df216f98ea9d900") );
  EXPECT_TRUE( b64s - SInt<64>(0) == b64s.pad<65>() );
  EXPECT_TRUE( a80s - b80s == SInt<81>("0x485d9505e91bcf65b524") );
  EXPECT_TRUE( b80s - a80s == SInt<81>("0x1b7a26afa16e4309a4adc") );
  EXPECT_TRUE( b80s - SInt<80>(0) == b80s.pad<81>() );
  EXPECT_TRUE( a128s - b128s == SInt<129>("0xaf511039500687ff43235ad5fa75b037") );
  EXPECT_TRUE( b128s - a128s == SInt<129>("0x150aeefc6aff97800bcdca52a058a4fc9") );
  EXPECT_TRUE( b128s - SInt<128>(0) == b128s.pad<129>() );
}

TEST_F(Lconst_test, sint_negate_operator) {
  EXPECT_TRUE( -a16s == SInt<17>(0x19246) );
  EXPECT_TRUE( -a64s == SInt<65>("0x18ef772e3b5a3b5fe") );
  EXPECT_TRUE( -a80s == SInt<81>("0x1c7e3e019435978a6dd02") );
  EXPECT_TRUE( -a128s == SInt<129>("0x191f6c6c8f533e62513f9163ec24af98c") );
  EXPECT_TRUE( (-(-a16s)) == SInt<18>(a16s.pad<18>()) );
  EXPECT_TRUE( (-(-a64s)) == SInt<66>(a64s.pad<66>()) );
  EXPECT_TRUE( (-(-a80s)) == SInt<82>(a80s.pad<82>()) );
  EXPECT_TRUE( (-(-a128s)) == SInt<130>(a128s.pad<130>()) );

  EXPECT_TRUE( -b16s == SInt<17>(0x334e) );
  EXPECT_TRUE( -b64s == SInt<65>("0x21055bea26f9dcfe") );
  EXPECT_TRUE( -b80s == SInt<81>("0x1041751f2c75480c9226") );
  EXPECT_TRUE( -b128s == SInt<129>("0x4147d702453a6e24571c7114bcc0a9c3") );
  EXPECT_TRUE( (-(-b16s)) == SInt<18>(b16s.pad<18>()) );
  EXPECT_TRUE( (-(-b64s)) == SInt<66>(b64s.pad<66>()) );
  EXPECT_TRUE( (-(-b80s)) == SInt<82>(b80s.pad<82>()) );
  EXPECT_TRUE( (-(-b128s)) == SInt<130>(b128s.pad<130>()) );
}

TEST_F(Lconst_test, sint_mult_operator) {
  EXPECT_TRUE( a16s * b16s == SInt<32>(0xea028354) );
  EXPECT_TRUE( a64s * b64s == SInt<128>("0xf16b880f2bad048691fd4b72a0e2da04") );
  EXPECT_TRUE( a80s * b80s == SInt<160>("0xfc6fe531cae4d5f834f4831b7dc6f5fbfee7f24c") );
  EXPECT_TRUE( a128s * b128s == SInt<256>("0xe3f0c77f6f1ce87a5d5735256c8addf7a2a5210cf49a1af0917e727f76d981a4") );
  EXPECT_TRUE( b16s * b16s == SInt<32>(0xa482bc4) );
  EXPECT_TRUE( b64s * b64s == SInt<128>("0x044261cf16323e9d07bfb5d30ce18c04") );
  EXPECT_TRUE( b80s * b80s == SInt<160>("0x1083f6094f8beff28a26e6d6335b98f66ff5da4") );
  EXPECT_TRUE( b128s * b128s == SInt<256>("0x10a58f581efee2a4d90812cc128d304f3a498bebb936e0afcbcc36cd7d130a89") );
  EXPECT_TRUE( SInt<16>(-1) * SInt<16>(-1) == SInt<32>(1) );
  EXPECT_TRUE( SInt<80>(-1) * SInt<80>(-1) == SInt<160>(1) );
  EXPECT_TRUE( SInt<128>(-1) * SInt<128>(-1) == SInt<256>(1) );
  EXPECT_TRUE( SInt<512>(-1) * SInt<512>(-1) == SInt<1024>(1) );
}

TEST_F(Lconst_test, sint_div_operator) {
  EXPECT_TRUE( a16s / b16s == SInt<17>(0x1fffe) );
  EXPECT_TRUE( a64s / b64s == SInt<65>("0x1fffffffffffffffd") );
  EXPECT_TRUE( a64s / a16s == SInt<65>("0x107b710ae332f") );
  EXPECT_TRUE( b64s / b16s == SInt<65>("0xa4c48cb11e2b") );
}

TEST_F(Lconst_test, sint_mod_operator) {
  EXPECT_TRUE( a16s % b16s == SInt<16>(0x71e) );
  EXPECT_TRUE( a64s % b64s == SInt<64>(0xdf8795dd56eb308) );
  EXPECT_TRUE( a64s % a16s == SInt<16>(0x16dc) );
  EXPECT_TRUE( b64s % b16s == SInt<16>(0xe51c) );
}

TEST_F(Lconst_test, sint_not_operator) {
  EXPECT_TRUE( ~a16s == UInt<16>(0x9245) );
  EXPECT_TRUE( ~a64s == UInt<64>(0x8ef772e3b5a3b5fd) );
  EXPECT_TRUE( ~a80s == UInt<80>("0xc7e3e019435978a6dd01") );
  EXPECT_TRUE( ~a128s == UInt<128>("0x91f6c6c8f533e62513f9163ec24af98b") );
}

TEST_F(Lconst_test, sint_and_operator) {
  EXPECT_TRUE( (a16s & b16s) == UInt<16>(0x4cb2) );
  EXPECT_TRUE( (a64s & b64s) == UInt<64>(0x5008841448040202L) );
  EXPECT_TRUE( (a80s & b80s) == UInt<80>("0x281c0ae09082875120da") );
  EXPECT_TRUE( (a128s & b128s) == UInt<128>("0x2e0828350ac411daa80288c101350634") );
}

TEST_F(Lconst_test, sint_or_operator) {
  EXPECT_TRUE( (a16s | b16s) == UInt<16>(0xedba) );
  EXPECT_TRUE( (a64s | b64s) == UInt<64>(0xfffaad1ddb5e6b02) );
  EXPECT_TRUE( (a80s | b80s) == UInt<80>("0xffbe9fe6ffaeb7fb6ffe") );
  EXPECT_TRUE( (a128s | b128s) == UInt<128>("0xfeb939ffbacd99dbece7efeb7fbf567d") );
}

TEST_F(Lconst_test, sint_xor_operator) {
  EXPECT_TRUE( (a16s ^ b16s) == UInt<16>(0xa108) );
  EXPECT_TRUE( (a64s ^ b64s) == UInt<64>(0xaff22909935a6900) );
  EXPECT_TRUE( (a80s ^ b80s) == UInt<80>("0xd7a295066f2c30aa4f24") );
  EXPECT_TRUE( (a128s ^ b128s) == UInt<128>("0xd0b111cab009880144e5672a7e8a5049") );
}

TEST_F(Lconst_test, sint_andr_operator) {
  EXPECT_TRUE( (a16s.andr()) == UInt<1>(0x0) );
  EXPECT_TRUE( ((~UInt<16>(0)).asSInt().andr()) == UInt<1>(1) );
  EXPECT_TRUE( (a64s.andr()) == UInt<1>(0x0) );
  EXPECT_TRUE( ((~UInt<64>(0)).asSInt().andr()) == UInt<1>(1) );
  EXPECT_TRUE( (a80s.andr()) == UInt<1>(0x0) );
  EXPECT_TRUE( ((~UInt<80>(0)).asSInt().andr()) == UInt<1>(1) );
  EXPECT_TRUE( (a128s.andr()) == UInt<1>(0x0) );
  EXPECT_TRUE( ((~UInt<128>(0)).asSInt().andr()) == UInt<1>(1) );
}

TEST_F(Lconst_test, sint_orr_operator) {
  EXPECT_TRUE( (a16s.orr()) == UInt<1>(0x1) );
  EXPECT_TRUE( (SInt<16>(0).andr()) == UInt<1>(0) );
  EXPECT_TRUE( (a64s.orr()) == UInt<1>(0x1) );
  EXPECT_TRUE( (SInt<64>(0).andr()) == UInt<1>(0) );
  EXPECT_TRUE( (a80s.orr()) == UInt<1>(0x1) );
  EXPECT_TRUE( (SInt<80>(0).andr()) == UInt<1>(0) );
  EXPECT_TRUE( (a128s.orr()) == UInt<1>(0x1) );
  EXPECT_TRUE( (SInt<128>(0).andr()) == UInt<1>(0) );
}

TEST_F(Lconst_test, sint_xorr_operator) {
  EXPECT_TRUE( (a16s.xorr()) == UInt<1>(0x0) );
  EXPECT_TRUE( (b16s.xorr()) == UInt<1>(0x0) );
  EXPECT_TRUE( (a64s.xorr()) == UInt<1>(0x1) );
  EXPECT_TRUE( (b64s.xorr()) == UInt<1>(0x1) );
  EXPECT_TRUE( (a80s.xorr()) == UInt<1>(0x0) );
  EXPECT_TRUE( (b80s.xorr()) == UInt<1>(0x1) );
  EXPECT_TRUE( (a128s.xorr()) == UInt<1>(0x1) );
  EXPECT_TRUE( (b128s.xorr()) == UInt<1>(0x1) );
}

TEST_F(Lconst_test, sint_bits_operator) {
  EXPECT_TRUE( (a16s.bits<11,4>()) == UInt<8>(0xdb) );
  EXPECT_TRUE( (a64s.bits<47,24>()) == UInt<24>(0x8d1c4a) );
  EXPECT_TRUE( (a80s.bits<79,64>()) == UInt<16>(0x381c) );
  EXPECT_TRUE( (a80s.bits<71,56>()) == UInt<16>(0x1c1f) );
  EXPECT_TRUE( (a128s.bits<111,96>()) == UInt<16>(0x3937) );
  EXPECT_TRUE( (a128s.bits<71,56>()) == UInt<16>(0xdaec) );
}

TEST_F(Lconst_test, sint_head_operator) {
  EXPECT_TRUE( (a16s.head<8>()) == UInt<8>(0x6d) );
  EXPECT_TRUE( (a64s.head<64>()) == a64s.asUInt() );
  EXPECT_TRUE( (a64s.head<16>()) == UInt<16>(0x7108) );
  EXPECT_TRUE( (a80s.head<24>()) == UInt<24>(0x381c1f) );
  EXPECT_TRUE( (a128s.head<32>()) == UInt<32>(0x6e093937) );

  EXPECT_TRUE( (b16s.head<8>()) == UInt<8>(0xcc) );
  EXPECT_TRUE( (b64s.head<16>()) == UInt<16>(0xdefa) );
  EXPECT_TRUE( (b80s.head<24>()) == UInt<24>(0xefbe8a) );
  EXPECT_TRUE( (b128s.head<32>()) == UInt<32>(0xbeb828fd) );
}

TEST_F(Lconst_test, sint_tail_operator) {
  EXPECT_TRUE( (a16s.tail<8>()) == UInt<8>(0xba) );
  EXPECT_TRUE( (a64s.tail<0>()) == a64s.asUInt() );
  EXPECT_TRUE( (a64s.tail<16>()) == UInt<48>(0x8d1c4a5c4a02) );
  EXPECT_TRUE( (a80s.tail<8>()) == UInt<72>("0x1c1fe6bca6875922fe") );
  EXPECT_TRUE( (a128s.tail<32>()) == UInt<96>("0x0acc19daec06e9c13db50674") );
}

TEST_F(Lconst_test, sint_static_shifts) {
  EXPECT_TRUE( a16s.shl<0>() == a16s );
  EXPECT_TRUE( a16s.shl<4>() == SInt<20>(0x6dba0) );
  EXPECT_TRUE( a64s.shl<8>() == SInt<72>("0x71088d1c4a5c4a0200") );
  EXPECT_TRUE( a80s.shl<60>() == SInt<140>("0x381c1fe6bca6875922fe000000000000000") );
  EXPECT_TRUE( a128s.shl<72>() == SInt<200>("0x6e0939370acc19daec06e9c13db50674000000000000000000") );

  EXPECT_TRUE( a16s.shlw<0>() == a16s );
  EXPECT_TRUE( a16s.shlw<4>() == SInt<16>(0xdba0) );
  EXPECT_TRUE( a64s.shlw<8>() == SInt<64>("0x088d1c4a5c4a0200") );
  EXPECT_TRUE( a80s.shlw<60>() == SInt<80>("0x922fe000000000000000") );
  EXPECT_TRUE( a128s.shlw<72>() == SInt<128>("0x06e9c13db50674000000000000000000") );

  EXPECT_TRUE( a16s.shr<0>() == a16s );
  EXPECT_TRUE( a16s.shr<8>() == SInt<8>(0x6d) );
  EXPECT_TRUE( a64s.shr<16>() == SInt<48>(0x71088d1c4a5c) );
  EXPECT_TRUE( a80s.shr<24>() == SInt<56>(0x381c1fe6bca687) );
  EXPECT_TRUE( a128s.shr<48>() == SInt<80>("0x6e0939370acc19daec06"));
}

TEST_F(Lconst_test, sint_dynamic_shifts) {
  EXPECT_TRUE( (a16s << UInt<1>(0)) == SInt<17>(0x6dba) );
  EXPECT_TRUE( (a16s << UInt<4>(4)) == SInt<31>(0x6dba0) );
  EXPECT_TRUE( (a64s << UInt<4>(8)) == SInt<79>("0x71088d1c4a5c4a0200") );
  EXPECT_TRUE( (a80s << UInt<5>(12)) == SInt<111>("0x381c1fe6bca6875922fe000") );
  EXPECT_TRUE( (a128s << UInt<6>(16)) == SInt<191>("0x6e0939370acc19daec06e9c13db506740000") );

  EXPECT_TRUE( (b16s << UInt<1>(0)) == SInt<17>(0x1ccb2) );
  EXPECT_TRUE( (b16s << UInt<4>(4)) == SInt<31>(0x7ffccb20) );
  EXPECT_TRUE( (b64s << UInt<4>(8)) == SInt<79>("0x7fdefaa415d906230200") );
  EXPECT_TRUE( (b80s << UInt<5>(12)) == SInt<111>("0x7ffffefbe8ae0d38ab7f36dda000") );
  EXPECT_TRUE( (b128s << UInt<6>(16)) == SInt<191>("0x7fffffffffffbeb828fdbac591dba8e38eeb433f563d0000") );

  EXPECT_TRUE( (a16s.dshlw(UInt<1>(0))) == SInt<16>(0x6dba) );
  EXPECT_TRUE( (a16s.dshlw(UInt<4>(4))) == SInt<16>(0xdba0) );
  EXPECT_TRUE( (a64s.dshlw(UInt<4>(8))) == SInt<64>("0x088d1c4a5c4a0200") );
  EXPECT_TRUE( (a80s.dshlw(UInt<5>(12))) == SInt<80>("0xc1fe6bca6875922fe000") );
  EXPECT_TRUE( (a128s.dshlw(UInt<6>(16))) == SInt<128>("0x39370acc19daec06e9c13db506740000") );

  EXPECT_TRUE( (b16s.dshlw(UInt<1>(0))) == SInt<16>(0xccb2) );
  EXPECT_TRUE( (b16s.dshlw(UInt<4>(4))) == SInt<16>(0xcb20) );
  EXPECT_TRUE( (b64s.dshlw(UInt<4>(8))) == SInt<64>("0xfaa415d906230200") );
  EXPECT_TRUE( (b80s.dshlw(UInt<6>(60))) == SInt<80>("0x36dda000000000000000") );
  EXPECT_TRUE( (b128s.dshlw(UInt<7>(72))) == SInt<128>("0xe38eeb433f563d000000000000000000") );

  EXPECT_TRUE( (a16s >> UInt<1>(0)) == SInt<16>(0x6dba) );
  EXPECT_TRUE( (a16s >> UInt<4>(4)) == SInt<16>(0x06db) );
  EXPECT_TRUE( (a64s >> UInt<4>(8)) == SInt<64>("0x71088d1c4a5c4a") );
  EXPECT_TRUE( (a80s >> UInt<5>(12)) == SInt<80>("0x381c1fe6bca687592") );
  EXPECT_TRUE( (a128s >> UInt<6>(16)) == SInt<128>("0x6e0939370acc19daec06e9c13db5") );

  EXPECT_TRUE( (b16s >> UInt<1>(0)) == SInt<16>(0xccb2) );
  EXPECT_TRUE( (b16s >> UInt<4>(4)) == SInt<16>(0xfccb) );
  EXPECT_TRUE( (b64s >> UInt<4>(8)) == SInt<64>("0xffdefaa415d90623") );
  EXPECT_TRUE( (b80s >> UInt<5>(12)) == SInt<80>("0xfffefbe8ae0d38ab7f36") );
  EXPECT_TRUE( (b128s >> UInt<6>(16)) == SInt<128>("0xffffbeb828fdbac591dba8e38eeb433f") );
}

TEST_F(Lconst_test, sint_conversion) {
  EXPECT_TRUE( a16s.asUInt() == UInt<16>("0x6dba") );
  EXPECT_TRUE( a16s.asSInt() == a16s );
  EXPECT_TRUE( a16s.cvt() == SInt<16>(0x6dba) );
}

TEST_F(Lconst_test, mixed_add_operator) {
  EXPECT_TRUE( a16u + b16s == SInt<17>(0x97b0) );
  EXPECT_TRUE( a64u + b64s == SInt<65>("0xc1b7ff65d1b932ca") );
  EXPECT_TRUE( a80u + b80s == SInt<81>("0x8832b1a2cb583540d714") );
  EXPECT_TRUE( a128u + b128s == SInt<129>("0xa7bb8d6824455c7eedb64195d923d19a") );
  EXPECT_TRUE( b16s + a16u == SInt<17>(0x97b0) );
  EXPECT_TRUE( b64s + a64u == SInt<65>("0xc1b7ff65d1b932ca") );
  EXPECT_TRUE( b80s + a80u == SInt<81>("0x8832b1a2cb583540d714") );
  EXPECT_TRUE( b128s + a128u == SInt<129>("0xa7bb8d6824455c7eedb64195d923d19a") );
}

TEST_F(Lconst_test, mixed_sub_operator) {
  EXPECT_TRUE( a16u - b16s == SInt<17>(0xfe4c) );
  EXPECT_TRUE( a64u - b64s == SInt<65>("0x103c2b73a1facecc6") );
  EXPECT_TRUE( a80u - b80s == SInt<81>("0xa8b59be12442c559fb60") );
  EXPECT_TRUE( a128u - b128s == SInt<129>("0x12a4b3b6caeba38c79bef23bf52a52520") );
  EXPECT_TRUE( b16s - a16u == SInt<17>(0x101b4) );
  EXPECT_TRUE( b64s - a64u == SInt<65>("0xfc3d48c5e053133a") );
  EXPECT_TRUE( b80s - a80u == SInt<81>("0x1574a641edbbd3aa604a0") );
  EXPECT_TRUE( b128s - a128u == SInt<129>("0xd5b4c4935145c7386410dc40ad5adae0") );
}

TEST_F(Lconst_test, mixed_mul_operator) {
  EXPECT_TRUE( a16u * b16s == SInt<32>(0xd7518c9c) );
  EXPECT_TRUE( a64u * b64s == SInt<128>("0xe2c0d81f3550c17f8cc2ad9b533e7790") );
  EXPECT_TRUE( a80u * b80s == SInt<160>("0xf651c2566302169937ff4a396485514e01c74d64") );
  EXPECT_TRUE( a128u * b128s == SInt<256>("0xc494bfdc37540a963a9a7ad576771522f488399b3bc2e87c8c1164e32bc5a329") );
  EXPECT_TRUE( b16s * a16u == SInt<32>(0xd7518c9c) );
  EXPECT_TRUE( b64s * a64u == SInt<128>("0xe2c0d81f3550c17f8cc2ad9b533e7790") );
  EXPECT_TRUE( b80s * a80u == SInt<160>("0xf651c2566302169937ff4a396485514e01c74d64") );
  EXPECT_TRUE( b128s * a128u == SInt<256>("0xc494bfdc37540a963a9a7ad576771522f488399b3bc2e87c8c1164e32bc5a329") );
}


TEST_F(Lconst_test, Trivial) {
  Lbench bench("trivial");

  constexpr auto v_10 = 10_uint;
  constexpr auto v_10b = 0xa_uint;
  static_assert(v_10==v_10b, "same at compile time");

  UInt<4>  a16u_10(0xa);
  EXPECT_EQ( a16u_10   , v_10b);

  print_method(v_10);
  print_method(v_10b);

  UInt<1>   a1u(0x1);

  print_method(a1u);
  print_method(a16u);

  fmt::print("UInt<1> has sizeof {}\n", sizeof(a1u));
  fmt::print("UInt<16> has sizeof {}\n", sizeof(a16u));
  fmt::print("UInt<64> has sizeof {}\n", sizeof(a64u));
  fmt::print("UInt<80> has sizeof {}\n", sizeof(a80u));
  fmt::print("UInt<128> has sizeof {}\n", sizeof(a128u));

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

  {
    auto a = 0x10_uint;
    auto b = 0x20_uint;
    auto c = a + b;
    EXPECT_EQ(c, 0x30_uint);
  }

  {
    auto a = 0x10_uint;
    auto b = 0x2_uint;
    auto c = a + b;
    EXPECT_EQ(c, 0x12_uint);
  }
}

TEST_F(Lconst_test, Storage) {
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

  EXPECT_EQ(val8, 0x03_uint);
  EXPECT_EQ(val16, 0x0003_uint);

  auto val7_ptr = reinterpret_cast<UInt<7> *>(&data);
  auto &val7 = *val7_ptr;

  auto val18_ptr = reinterpret_cast<UInt<18> *>(&data);
  auto &val18 = *val18_ptr;

  print_method(val7);
  print_method(val18);

  // FIXME: EXPECT_EQ(0x1f58d11f58d11f58d1_uint, 076543210765432107654321_uint);

  print_method(012345_uint);
  print_method(0x9876543210987654321_uint);
  print_method(076543210765432107654321_uint);
  fmt::print("verilog: {}\n", (076543210765432107654321_uint).to_verilog());

  mmap_lib::map<uint32_t, std::string_view> map;

  map.set(12345, (0x12345_uint).to_string());
  EXPECT_TRUE(map.has(12345));
  auto v = map.get(12345);
  EXPECT_EQ(v, "0x12345");
}

TEST_F(Lconst_test, used_bits) {
  auto a = 0x010_uint;
  print_method(a);
  fmt::print("a used bits:{}\n", a.bit_length());

  auto b = 0x010_uint; // runtime or compile time
  print_method(b);
  fmt::print("b used bits:{}\n", b.bit_length());

  uint64_t v = 0x10;
  auto uint_ptr = reinterpret_cast<UInt<8> *>(&v);
  auto &u=*uint_ptr;
  fmt::print("u used bits:{}\n", u.bit_length());
}

TEST_F(Lconst_test, boost) {
  auto a = 0x0000010_uint;
  auto b = 0x0100030_uint;

  auto c = a * b;

  using boost::multiprecision::cpp_int;

  cpp_int b_a("0x010");
  cpp_int b_b(b.to_string_hex());

  cpp_int b_c = b_a * b_b;

  std::vector<unsigned char> v;
  export_bits(b_c, std::back_inserter(v), 8);

  cpp_int b_c_recover;
  import_bits(b_c_recover,v.begin(), v.end());

  EXPECT_EQ(b_c.str(),b_c_recover.str());

  cpp_int c_x(c.to_string_hex());

  EXPECT_EQ(c_x.str(),b_c_recover.str());
}

TEST_F(Lconst_test, trivial_vals) {
  Lconst p_1(1);
  Lconst p_2(2);
  Lconst n_1 = p_1 - p_2;

  EXPECT_EQ(p_1.to_i(), 1);
  EXPECT_EQ(p_2.to_i(), 2);
  EXPECT_EQ(n_1.to_i(), -1);

  EXPECT_EQ(Lconst("-2").to_i(), -2);
  EXPECT_EQ(Lconst("0").to_i(), 0);
  EXPECT_EQ(Lconst("1").to_i(), 1);
  EXPECT_EQ(Lconst("3278").to_i(), 3278);
  EXPECT_EQ(Lconst("-13278").to_i(), -13278);

  EXPECT_EQ(Lconst("-2").to_pyrope(), "-2");
  EXPECT_EQ(Lconst("0").to_pyrope(), "0");
  EXPECT_EQ(Lconst("1").to_pyrope(), "1");
}

TEST_F(Lconst_test, hexa_check) {

#if 0
  Lbench b("const_attr");

  unlink("lgdb_attr/c_map");

  mmap_lib::map<uint32_t, std::string_view> c_map("lgdb_attr","c_map");
#endif

  Lrand<size_t> rnd;
  const size_t n_const = rnd.between(200,3000);

  std::vector<std::string> rnd_list;
  Lrand_range<int> num_digits(1,200);
  Lrand_range<char> hex1_digits('0','9');
  Lrand_range<char> hex2_digits('a','f');
  Lrand_range<char> hex3_digits('A','F');
  Lrand<bool> flip;

  rnd_list.resize(n_const);
  for(auto i=0u;i<n_const;++i) {
    rnd_list[i] = "0x";
    for(auto j=num_digits.any();j>0;--j) {
      if (flip.any())
        rnd_list[i].append(1, hex1_digits.any());
      else if (flip.any())
        rnd_list[i].append(1, hex2_digits.any());
      else
        rnd_list[i].append(1, hex3_digits.any());
    }
  }

  for(auto i=0u;i<n_const;++i) {
    //fmt::print("num:{}\n",rnd_list[i]);

    boost::multiprecision::cpp_int c(rnd_list[i]); // read hexa

    std::stringstream ss;
    ss << std::hex << c;

    std::string orig;
    bool skip_leading_zeroes=true;
    for (const auto ch : rnd_list[i].substr(2)) {
      if (ch=='_')
        continue;
      if (skip_leading_zeroes && ch=='0')
        continue;
      skip_leading_zeroes = false;
      orig.append(1, std::tolower(ch));
    }
    if (orig.empty())
      orig = "0";

    EXPECT_EQ(ss.str(), orig);

    Lconst a(rnd_list[i]);
    EXPECT_EQ(a.get_raw_num(), c);
  }
}

TEST_F(Lconst_test, dec_check) {

  boost::multiprecision::cpp_int b10("10");
  Lconst p10("10");
  EXPECT_EQ(p10.get_raw_num(), b10);

  Lconst p123("123");
  boost::multiprecision::cpp_int b123("123");
  EXPECT_EQ(p123.get_raw_num(), b123);

  Lrand<size_t> rnd;
  const size_t n_const = rnd.between(200,3000);

  std::vector<std::string> rnd_list;
  Lrand_range<int> num_digits(1,200);
  Lrand_range<char> hex1_digits('0','9');
  Lrand_range<char> hex2_digits('1','9');
  Lrand<bool> flip;

  rnd_list.resize(n_const);
  for(auto i=0u;i<n_const;++i) {
    if (flip.any())
      rnd_list[i].append(1,'-');
    rnd_list[i].append(1,hex2_digits.any());
    for(auto j=num_digits.any();j>0;--j) {
      rnd_list[i].append(1, hex1_digits.any());
    }
  }

  for(auto i=0u;i<n_const;++i) {

    boost::multiprecision::cpp_int c(rnd_list[i]);

    std::string padded;
    for (const auto ch: rnd_list[i]) {
      if (flip.any()) padded.append(1, '_');
      padded.append(1, ch);
    }
    if (flip.any()) {
      bool is_sign;
      if (flip.any()) {
        is_sign = false;
        padded.append(1, 'u');
      } else {
        is_sign = true;
        padded.append(1, 's');
      }

      if (flip.any()) {
        auto nbits = 0;
        if (c<0)
          nbits = msb(-c)+1;
        else if (c==0)
          nbits = 1;
        else
          nbits = msb(c)+1;
        if (is_sign)
          nbits++;

        if (flip.any()) {
          nbits += num_digits.any();
        }
        if (is_sign) {
          padded.append(std::to_string(nbits));
          padded.append("bits");
        }
      }
    }

    Lconst a1(rnd_list[i]);
    EXPECT_EQ(a1.get_raw_num(), c);

    Lconst a2(padded);
    if (a2.get_raw_num() != c) {
      a2.dump();
      a1.dump();
    }
    EXPECT_EQ(a2.get_raw_num(), c);

    auto fmt_a = a1.to_pyrope();
    Lconst b(fmt_a);

#if 0
    fmt::print("orig:{}\n",rnd_list[i]);
    fmt::print("  a1:{}\n",a1.to_pyrope());
    fmt::print("  a2:{}\n",a2.to_pyrope());
    fmt::print("padd:{}\n",padded);
    fmt::print("   b:{}\n",b.to_pyrope());
#endif

    EXPECT_EQ(b.get_raw_num(), c);
  }
}

TEST_F(Lconst_test, string) {

  Lconst a("cadena");
  EXPECT_EQ(a.to_string(), "cadena");

  EXPECT_EQ(Lconst("a longer chain of text").to_string(), "a longer chain of text");

  EXPECT_EQ(Lconst("__longer chain of text").to_string(), "__longer chain of text");
  EXPECT_EQ(Lconst("_").to_string(), "_");
  EXPECT_EQ(Lconst("a").to_string(), "a");

  EXPECT_TRUE(Lconst("_").is_string());
  EXPECT_FALSE(Lconst("0").is_string());

  EXPECT_EQ(Lconst("0").get_raw_num(), 0);
}

TEST_F(Lconst_test, binary) {
  Lconst a("0b1100u4bits");
  Lconst b("12u4bits");
  a.dump();
  b.dump();
  EXPECT_TRUE(a == b);
  EXPECT_EQ(a, b);

  Lconst c("0b01?10?1");
  EXPECT_EQ(c.to_string(), "01?10?1");
  EXPECT_EQ(c.to_pyrope(), "0b01?10?1u7bits");
  EXPECT_EQ(c.to_verilog(), "7'b01?10?1");

  Lconst d("___0b1_1x1_");
  EXPECT_EQ(d.to_string(), "11x1");
  EXPECT_EQ(d.to_pyrope(), "0b11x1u4bits");
  EXPECT_EQ(d.to_verilog(), "4'b11x1");
  EXPECT_EQ(d.to_yosys(),   "11x1");

  Lconst e("_-__0b1_");
  EXPECT_EQ(e.to_string(), "_-__0b1_");
  EXPECT_EQ(e.to_pyrope(), "'_-__0b1_'");
  EXPECT_EQ(e.to_verilog(), "\"_-__0b1_\"");

  Lconst f("0b1_0100");
  EXPECT_EQ(f.to_pyrope(), "0x14");
  EXPECT_EQ(f.to_verilog(), "'h14");
  EXPECT_EQ(f.to_yosys(), "10100");

  Lconst g("0bxxxx_xxxx_u8bits");
  EXPECT_EQ(g.to_pyrope(),   "0bxxxxxxxxu8bits");
  EXPECT_EQ(g.to_verilog(), "8'bxxxxxxxx");
  EXPECT_EQ(g.to_yosys()    , "xxxxxxxx");
  Lconst h("0bxxxxxxxxu8bits");
  EXPECT_EQ(h,g);
}

TEST_F(Lconst_test, serialize) {

  mmap_lib::map<uint32_t, Lconst::Container> map;

  Lconst a(255);
  Lconst b("0xFFu_300bits");
  Lconst c("0x1234567890abcdef1234567890abcdefu_300bits");

  map.set(1, a.serialize());
  map.set(2, b.serialize());
  map.set(3, c.serialize());

  Lconst s_a(map.get(1));
  Lconst s_b(map.get(2));
  Lconst s_c(map.get(3));

  b.dump();
  s_b.dump();
  fmt::print("  a:{}\n",a.to_pyrope());
  fmt::print("s_a:{}\n",s_a.to_pyrope());
  fmt::print("  b:{}\n",b.to_pyrope());
  fmt::print("s_b:{}\n",s_b.to_pyrope());
  fmt::print("  c:{}\n",c.to_pyrope());
  fmt::print("s_c:{}\n",s_c.to_pyrope());

  EXPECT_EQ(a, s_a);
  EXPECT_EQ(b, s_b);
  EXPECT_EQ(c, s_c);
}

TEST_F(Lconst_test, serialize2a) {
  {
    unlink("tmp_lemu/const");
    mmap_lib::map<uint32_t, Lconst::Container> map("tmp_lemu", "const");

    Lconst a(255);
    Lconst b("0xFFu_300bits");
    Lconst c("0x1234567890abcdef1234567890abcdefu_300bits");

    map.set(1, a.serialize());
    map.set(2, b.serialize());
    map.set(3, c.serialize());
  }
  {
    mmap_lib::map<uint32_t, Lconst::Container> map("tmp_lemu","const");

    Lconst s_a(map.get(1));
    Lconst s_b(map.get(2));
    Lconst s_c(map.get(3));

    Lconst a(255);
    Lconst b("0xFFu_300bits");
    Lconst c("0x1234567890abcdef1234567890abcdefu_300bits");

    b.dump();
    s_b.dump();
    fmt::print("  a:{}\n",a.to_pyrope());
    fmt::print("s_a:{}\n",s_a.to_pyrope());
    fmt::print("  b:{}\n",b.to_pyrope());
    fmt::print("s_b:{}\n",s_b.to_pyrope());
    fmt::print("  c:{}\n",c.to_pyrope());
    fmt::print("s_c:{}\n", s_c.to_pyrope());

    EXPECT_EQ(a, s_a);
    EXPECT_EQ(b, s_b);
    EXPECT_EQ(c, s_c);
  }
}

TEST_F(Lconst_test, zerocase) {

  Lconst zero;
  EXPECT_EQ(zero.get_bits(), 1);
  EXPECT_EQ(Lconst(0).get_bits(), 1);
  EXPECT_EQ(Lconst(0,3).get_bits(), 3);

  EXPECT_EQ(Lconst("0x0").get_bits(), 1);
  EXPECT_EQ(Lconst("0").get_bits(), 1);
  EXPECT_EQ(Lconst("0u7").get_bits(), 7);
  EXPECT_EQ(Lconst("0s").get_bits(), 1);
  EXPECT_EQ(Lconst("0s4").get_bits(), 4);

}

TEST_F(Lconst_test, cpp_int_vs_lconst) {

  using boost::multiprecision::cpp_int;

  cpp_int a(-1);
  cpp_int b("0xFF");

  cpp_int c_and = a & b;
  cpp_int d_and = a & a;

  auto a_not = a;
  a_not = ~a;
  auto b_not = b;
  b_not = ~b;
  fmt::print("{} s:{} p:{}\n", a.str(), a.sign(), a_not.str());
  fmt::print("{} s:{} p:{}\n", b.str(), b.sign(), b_not.str());

  auto c_and_not = c_and;
  auto d_and_not = d_and;
  c_and_not = ~c_and;
  d_and_not = ~d_and;
  fmt::print("{} = {} & {} s:{} ~:{}\n", c_and.str(), a.str(), b.str(), c_and.sign(), c_and_not.str());
  fmt::print("{} = {} & {} s:{} ~:{}\n", d_and.str(), a.str(), a.str(), c_and.sign(), d_and_not.str());

  cpp_int c_or = a | b;
  cpp_int d_or = a | a;

  auto c_or_not = c_or;
  auto d_or_not = d_or;
  c_or_not = ~c_or;
  d_or_not = ~d_or;
  fmt::print("{} = {} | {} s:{} ~:{}\n", c_or.str(), a.str(), b.str(), c_or.sign(), c_or_not.str());
  fmt::print("{} = {} | {} s:{} ~:{}\n", d_or.str(), a.str(), a.str(), c_or.sign(), d_or_not.str());


  Lconst l_a("-1");
  Lconst l_b("0xFF");

  Lconst l_c_and = l_a.and_op(l_b);
  Lconst l_d_and = l_a.and_op(l_a);

  EXPECT_EQ(l_c_and.to_i(), c_and.convert_to<int>());
  EXPECT_EQ(l_d_and.to_i(), d_and.convert_to<int>());

  Lconst l_c_or = l_a.or_op(l_b);
  Lconst l_d_or = l_a.or_op(l_a);

  EXPECT_EQ(l_c_or.to_i(), c_or.convert_to<int>());
  EXPECT_EQ(l_d_or.to_i(), d_or.convert_to<int>());

  // Same/diff are not the same

  cpp_int c_eq = a == b;
  cpp_int d_eq = a == a;

  fmt::print("{} = {} == {} s:{}\n", c_eq.str(), a.str(), b.str(), c_eq.sign());
  fmt::print("{} = {} == {} s:{}\n", d_eq.str(), a.str(), a.str(), c_eq.sign());

  Lconst l_c_eq = l_a.eq_op(l_b);
  Lconst l_d_eq = l_a.eq_op(l_a);

  EXPECT_EQ(c_eq, 0);
  EXPECT_EQ(d_eq, 1);

  EXPECT_EQ(l_c_eq, 1); // Sign extended to match
  EXPECT_EQ(l_d_eq, 1);
}

TEST_F(Lconst_test, lconst_add) {
  {
    auto a = Lconst("0xFF") + Lconst("-1");
    EXPECT_TRUE(a.is_unsigned());
    EXPECT_EQ(a.to_i(), 254);
    EXPECT_EQ(a.get_bits(), 8);
  }
  {
    auto a = Lconst("0xFFu") + Lconst("-1");
    EXPECT_TRUE(a.is_unsigned());
    EXPECT_EQ(a.to_i(), 254);
    EXPECT_EQ(a.get_bits(), 8);
  }
  {
    auto a = Lconst("0xFFu") + Lconst("-1s");
    EXPECT_TRUE(a.is_unsigned());
    EXPECT_EQ(a.to_i(), 254);
    EXPECT_EQ(a.get_bits(), 8);
  }
  {
    auto a = Lconst("0xFFs") + Lconst("-1");
    EXPECT_TRUE(!a.is_unsigned()); // SIGNED
    EXPECT_EQ(a.to_i(), 254);
    EXPECT_EQ(a.get_bits(), 9);
  }
  {
    auto a = Lconst("1s") + Lconst("-1");
    EXPECT_TRUE(!a.is_unsigned());  // SIGNED
    EXPECT_EQ(a.to_i(), 0);
    EXPECT_EQ(a.get_bits(), 1);
  }
  {
    auto a = Lconst("-1u") + Lconst("-1u");
    EXPECT_TRUE(a.is_unsigned());
    EXPECT_EQ(a.to_i(), -2);
    EXPECT_EQ(a.get_bits(), 2);
  }
  {
    auto a = Lconst("0b0?") + Lconst("1");
    EXPECT_FALSE(a.is_i());
    EXPECT_EQ(a.to_pyrope(), "0b??u2bits");
  }

}
