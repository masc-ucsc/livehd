// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "replica_desc.hpp"

#include <string>

#include "gtest/gtest.h"
#include "node_util.hpp"

using livehd::graph_util::Replica_carry;
using livehd::graph_util::Replica_desc;
using livehd::graph_util::replica_desc_from_string;

namespace {

Replica_desc simple_desc() {
  Replica_desc d;
  d.first            = 0;
  d.step             = 1;
  d.count            = 8;
  d.index_input      = 3;
  d.activation_input = 4;
  d.carries.emplace_back(Replica_carry{/*input_pid=*/1, /*output_pid=*/2});
  return d;
}

}  // namespace

TEST(replica_desc, round_trip) {
  const auto d = simple_desc();
  ASSERT_TRUE(d.validate().empty()) << d.validate();

  std::string err;
  const auto  back = replica_desc_from_string(d.serialize(), &err);
  ASSERT_TRUE(back.has_value()) << err;
  EXPECT_EQ(*back, d);
}

TEST(replica_desc, round_trip_with_all_roles) {
  auto d               = simple_desc();
  d.next_active_output = 7;
  d.carries.emplace_back(Replica_carry{/*input_pid=*/5, /*output_pid=*/2});  // one output, two carries: legal
  ASSERT_TRUE(d.validate().empty()) << d.validate();

  std::string err;
  const auto  back = replica_desc_from_string(d.serialize(), &err);
  ASSERT_TRUE(back.has_value()) << err;
  EXPECT_EQ(*back, d);
}

TEST(replica_desc, version_is_first_field_and_unknown_version_fails) {
  const auto txt = simple_desc().serialize();
  EXPECT_TRUE(txt.starts_with("version=")) << txt;

  std::string err;
  EXPECT_FALSE(replica_desc_from_string("version=999;first=0;step=1;count=2", &err).has_value());
  EXPECT_NE(err.find("not supported"), std::string::npos) << err;

  // A payload with no version at all is a stale artifact, not a default.
  err.clear();
  EXPECT_FALSE(replica_desc_from_string("first=0;step=1;count=2", &err).has_value());
  EXPECT_FALSE(err.empty());
}

TEST(replica_desc, unknown_field_is_rejected_not_skipped) {
  std::string err;
  EXPECT_FALSE(replica_desc_from_string("version=1;first=0;step=1;count=2;wat=3", &err).has_value());
  EXPECT_NE(err.find("unknown"), std::string::npos) << err;
}

TEST(replica_desc, zero_step_rejected) {
  Replica_desc d;
  d.step  = 0;
  d.count = 4;
  EXPECT_FALSE(d.validate().empty());

  std::string err;
  EXPECT_FALSE(replica_desc_from_string("version=1;first=0;step=0;count=4", &err).has_value());
}

TEST(replica_desc, duplicate_carry_destination_rejected) {
  Replica_desc d;
  d.count = 4;
  d.carries.emplace_back(Replica_carry{/*input_pid=*/1, /*output_pid=*/2});
  d.carries.emplace_back(Replica_carry{/*input_pid=*/1, /*output_pid=*/3});
  EXPECT_NE(d.validate().find("duplicate carry destination"), std::string::npos) << d.validate();
}

TEST(replica_desc, role_ports_may_not_be_carry_endpoints) {
  {
    Replica_desc d;
    d.count       = 4;
    d.index_input = 1;
    d.carries.emplace_back(Replica_carry{/*input_pid=*/1, /*output_pid=*/2});
    EXPECT_FALSE(d.validate().empty());
  }
  {
    Replica_desc d;
    d.count            = 4;
    d.activation_input = 1;
    d.carries.emplace_back(Replica_carry{/*input_pid=*/1, /*output_pid=*/2});
    EXPECT_FALSE(d.validate().empty());
  }
  {
    // next_active without an activation input has nothing to chain into.
    Replica_desc d;
    d.count              = 4;
    d.next_active_output = 9;
    EXPECT_FALSE(d.validate().empty());
  }
}

TEST(replica_desc, zero_count_is_legal) {
  Replica_desc d;
  d.count = 0;
  d.step  = 1;
  EXPECT_TRUE(d.validate().empty()) << d.validate();

  std::string err;
  const auto  back = replica_desc_from_string(d.serialize(), &err);
  ASSERT_TRUE(back.has_value()) << err;
  EXPECT_EQ(back->count, 0u);
}

TEST(replica_desc, index_at_covers_strided_and_negative_domains) {
  Replica_desc d;
  d.first = 1;
  d.step  = 2;
  d.count = 5;
  EXPECT_EQ(d.index_at(0).value(), 1);
  EXPECT_EQ(d.index_at(4).value(), 9);

  Replica_desc n;
  n.first = 4;
  n.step  = -2;
  n.count = 3;
  ASSERT_TRUE(n.validate().empty()) << n.validate();
  EXPECT_EQ(n.index_at(0).value(), 4);
  EXPECT_EQ(n.index_at(2).value(), 0);

  // A hand-built negative-step descriptor must survive serialization even
  // though the Pyrope surface only produces positive steps today.
  std::string err;
  const auto  back = replica_desc_from_string(n.serialize(), &err);
  ASSERT_TRUE(back.has_value()) << err;
  EXPECT_EQ(*back, n);
}

TEST(replica_desc, domain_overflow_rejected) {
  Replica_desc d;
  d.first = 0;
  d.step  = (static_cast<int64_t>(1) << 62);
  d.count = 100;
  EXPECT_FALSE(d.validate().empty()) << "an overflowing domain must not validate";
}

TEST(replica_desc, index_width_is_signed_representable) {
  // 0..=15 needs 5 SIGNED bits, not 4: the emitted localparam is signed, so a
  // 4-bit port would make index 15 read as -1 inside the callee.
  Replica_desc d;
  d.first = 0;
  d.step  = 1;
  d.count = 16;
  EXPECT_EQ(d.index_signed_bits(), 5);

  Replica_desc small;
  small.first = 0;
  small.step  = 1;
  small.count = 8;
  EXPECT_EQ(small.index_signed_bits(), 4);  // max index 7 -> 3 payload bits + sign

  Replica_desc neg;
  neg.first = -8;
  neg.step  = 1;
  neg.count = 9;                          // -8 .. 0
  EXPECT_EQ(neg.index_signed_bits(), 4);  // -8 is the low end of 4-bit two's complement

  Replica_desc empty;
  empty.count = 0;
  EXPECT_EQ(empty.index_signed_bits(), 1);
}

TEST(replica_desc, carry_endpoint_queries) {
  const auto d = simple_desc();
  EXPECT_TRUE(d.is_carry_dest(1));
  EXPECT_TRUE(d.is_carry_source(2));
  EXPECT_FALSE(d.is_carry_dest(2));
  EXPECT_FALSE(d.is_carry_source(1));
}

TEST(replica_desc, malformed_payloads_fail_cleanly) {
  for (const auto* bad : {
           "",
           "version=1",
           "version=1;first=0;step=1",
           "version=1;first=x;step=1;count=2",
           "version=1;first=0;step=1;count=2;carry=5",
           "version=1;first=0;step=1;count=2;carry=a>b",
           "nonsense",
       }) {
    std::string err;
    EXPECT_FALSE(replica_desc_from_string(bad, &err).has_value()) << "should reject: " << bad;
    EXPECT_FALSE(err.empty()) << "should explain rejection of: " << bad;
  }
}
