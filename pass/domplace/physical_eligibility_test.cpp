// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "physical_eligibility.hpp"

#include <functional>
#include <span>

#include "dlop.hpp"
#include "gtest/gtest.h"

namespace livehd::domplace {
namespace {
using synth::State_role;

// A frozen region whose state rows are built one per case. Only what the
// eligibility reads is populated: source rows, bits, clock controls, frozen
// inputs/outputs, the native source kinds and the state init.
struct Fixture {
  usyn::Stateful_region    region;
  std::vector<std::string> names{"clk", "other", "", "x"};
  usyn::Xsignal            clk, other, gclk, x;
  enum Control : uint32_t { direct, other_port, gated_output, inverted, logic, none };

  Fixture() {
    region.frozen.emplace();
    auto& net   = *region.frozen;
    clk         = net.native.input("clk_b0");
    other       = net.native.input("other_b0");
    gclk        = net.native.input("__icg0_gclk");
    x           = net.native.input("x_b0");
    net.inputs  = {clk, other, gclk, x};
    net.outputs = {
        {"c_clk", clk},
        {"c_other", other},
        {"c_gclk", gclk},
        {"c_inv", ~clk},
        {"c_and", net.native.land(clk, x)}
    };
  }
  // One single-bit register row; `edit` adjusts its source semantics.
  void add(Control control, const std::function<void(synth::Source_state&)>& edit = {}, char init = 'x') {
    auto&               net   = *region.frozen;
    const auto          index = static_cast<uint32_t>(region.state_bits.size());
    const auto          name  = "s" + std::to_string(index);
    const auto          q     = net.native.input(name);
    synth::Source_state s;
    s.name          = name;
    s.role          = State_role::register_candidate;
    s.bits          = 1;
    s.stages        = 1;
    s.clock.present = true;
    s.clock.bits    = 1;
    if (edit) {
      edit(s);
    }
    region.source.sources.push_back(s);
    region.source.bits.push_back({index, 0, 0, index, name, {}, {}});
    if (control != none) {
      region.source.controls.push_back({index, synth::State_control_kind::clock, {static_cast<uint32_t>(control)}});
    }
    net.state.push_back({name, init, q, q, usyn::unknown_clock_domain, {}});
    region.state_bits.push_back(index);
  }
  std::vector<Reason> reasons(std::string_view clock = "clk") const { return reasons_of(names, clock); }
  std::vector<Reason> reasons_of(std::span<const std::string> input_names, std::string_view clock) const {
    std::vector<Reason> result;
    const auto          rows = physical_eligibility(region, input_names, clock);
    for (uint32_t i = 0; i < rows.size(); ++i) {
      EXPECT_EQ(rows[i].state, i);
      result.push_back(rows[i].reason);
    }
    return result;
  }
};
}  // namespace

TEST(PhysicalEligibility, EveryRetentionCodeHasItsOwnRow) {
  Fixture f;
  f.add(Fixture::direct);
  f.add(Fixture::direct, [](auto& s) { s.enable.present = true; });  // an enable is folded into D
  f.add(Fixture::direct, [](auto& s) {                               // an async reset whose control the region does not expose
    s.reset.present = true;
    s.async_reset   = true;
  });
  f.add(Fixture::direct, [](auto& s) {
    s.initial.present = true;
    s.power_on        = true;
  });
  f.add(Fixture::direct, {}, '1');  // an initialized frozen state without a source flag
  f.add(Fixture::direct, [](auto& s) { s.neg_clock = true; });
  f.add(Fixture::direct, [](auto& s) { s.clock_edge_known = false; });
  f.add(Fixture::gated_output, [](auto& s) { s.icg = 0; });
  f.add(Fixture::logic);
  f.add(Fixture::inverted);
  f.add(Fixture::gated_output);  // a black-box/ICG output that is no region port
  f.add(Fixture::none);
  f.add(Fixture::direct, [](auto& s) { s.clock.bits = 2; });
  f.add(Fixture::direct, [](auto& s) { s.clock.constant = true; });
  f.add(Fixture::none, [](auto& s) { s.clock.present = false; });
  f.add(Fixture::other_port);
  f.add(Fixture::direct, [](auto& s) { s.role = State_role::transparent_latch; });
  f.add(Fixture::direct, [](auto& s) { s.role = State_role::icg; });
  f.add(Fixture::direct, [](auto& s) { s.role = State_role::memory; });
  f.add(Fixture::direct, [](auto& s) { s.role = State_role::opaque; });
  f.add(Fixture::direct, [](auto& s) { s.role = State_role::hierarchy; });
  const std::vector<Reason> expected{Reason::candidate,
                                     Reason::candidate,
                                     Reason::reset_internal,
                                     Reason::initial_value,
                                     Reason::initial_value,
                                     Reason::negative_edge,
                                     Reason::unknown_edge,
                                     Reason::gated_clock,
                                     Reason::derived_clock,
                                     Reason::derived_clock,
                                     Reason::derived_clock,
                                     Reason::derived_clock,
                                     Reason::multibit_clock,
                                     Reason::constant_clock,
                                     Reason::constant_clock,
                                     Reason::clock_domain,
                                     Reason::transparent_latch,
                                     Reason::icg,
                                     Reason::memory,
                                     Reason::opaque,
                                     Reason::opaque};
  EXPECT_EQ(f.reasons(), expected);

  // The selected domain is a port name: selecting `other` swaps the two rows.
  const auto swapped = f.reasons("other");
  EXPECT_EQ(swapped[0], Reason::clock_domain);
  EXPECT_EQ(swapped[15], Reason::candidate);
}

TEST(PhysicalEligibility, DefinitionRegionClocksAreNotTracedToTheDesignRoot) {
  // A region other than the design top has no design input names: a register
  // on its clock port is retained even when the port is spelled like the
  // selected clock, since each instance may drive it from another root.
  Fixture f;
  f.add(Fixture::direct);
  f.add(Fixture::other_port);
  f.add(Fixture::inverted);
  f.add(Fixture::direct, [](auto& s) { s.icg = 0; });
  f.add(Fixture::direct, [](auto& s) { s.async_reset = s.reset.present = true; });
  const std::vector<Reason> expected{Reason::definition_clock,
                                     Reason::definition_clock,
                                     Reason::derived_clock,
                                     Reason::gated_clock,
                                     Reason::definition_clock};
  EXPECT_EQ(f.reasons_of({}, "clk"), expected);
  // A top-region input that is not a design input (a crossing) has no name.
  f.names[0].clear();
  EXPECT_EQ(f.reasons()[0], Reason::derived_clock);
}

TEST(PhysicalEligibility, ResetsMoveToTheStorageResetPin) {
  // U22: a reset leaves D for the stage's RST pin. A synchronous one needs the
  // net its reset pin reads (a hint: a region input or register); an
  // asynchronous one rides its control output; the value must be a known bit.
  Fixture f;
  f.add(Fixture::direct, [](auto& s) {
    s.reset.present = true;
    s.reset.bits    = 1;
  });                                   // sync, hinted below
  f.add(Fixture::direct, [](auto& s) {  // sync, active low, inverted pin
    s.reset.present    = true;
    s.reset.bits       = 1;
    s.neg_reset        = true;
    s.initial.present  = true;
    s.initial.constant = true;
    s.initial.value    = *Dlop::create_integer(1);
  });
  f.add(Fixture::direct, [](auto& s) {
    s.reset.present = true;
    s.reset.bits    = 1;
  });                                   // sync, no hint
  f.add(Fixture::direct, [](auto& s) {  // async with its control
    s.reset.present = true;
    s.reset.bits    = 1;
    s.async_reset   = true;
  });
  f.add(Fixture::direct, [](auto& s) {  // a reset value that is not a constant
    s.reset.present   = true;
    s.reset.bits      = 1;
    s.initial.present = true;
  });
  f.region.source.controls.push_back({3, synth::State_control_kind::async_reset, {4}});
  std::vector<usyn::Physical_hint> hints(5);
  hints[0].reset_input    = 3;
  hints[1].reset_input    = 3;
  hints[1].reset_inverted = true;
  hints[4].reset_input    = 3;
  const auto rows         = physical_eligibility(f.region, f.names, "clk", hints);
  ASSERT_EQ(rows.size(), 5);
  EXPECT_EQ(rows[0].reason, Reason::candidate);
  ASSERT_TRUE(rows[0].reset);
  EXPECT_EQ(*rows[0].reset, (usyn::Physical_reset{false, false, false, usyn::Physical_reset::Source::input, 3}));
  // negreset on a pin that reads the net inverted asserts at net 1.
  ASSERT_TRUE(rows[1].reset);
  EXPECT_EQ(*rows[1].reset, (usyn::Physical_reset{false, false, true, usyn::Physical_reset::Source::input, 3}));
  EXPECT_EQ(rows[2].reason, Reason::reset_internal);
  EXPECT_FALSE(rows[2].reset);
  ASSERT_TRUE(rows[3].reset);
  EXPECT_EQ(*rows[3].reset, (usyn::Physical_reset{true, false, false, usyn::Physical_reset::Source::control, 4}));
  EXPECT_EQ(rows[4].reason, Reason::reset_value);
}

TEST(PhysicalEligibility, ScopeBarriersAndMalformedCorrespondence) {
  Fixture f;
  f.add(Fixture::direct);
  f.region.source.scope = synth::State_scope::memory;
  EXPECT_EQ(f.reasons(), std::vector<Reason>{Reason::memory});
  f.region.source.scope = synth::State_scope::opaque;
  EXPECT_EQ(f.reasons(), std::vector<Reason>{Reason::opaque});
  f.region.source.scope = synth::State_scope::logic;
  EXPECT_EQ(f.reasons(), std::vector<Reason>{Reason::candidate});

  auto broken          = f.region;
  broken.state_bits[0] = 7;
  EXPECT_TRUE(physical_eligibility(broken, f.names, "clk").empty());
  broken.state_bits.clear();
  EXPECT_TRUE(physical_eligibility(broken, f.names, "clk").empty());
  auto unfrozen = f.region;
  unfrozen.frozen.reset();
  EXPECT_TRUE(physical_eligibility(unfrozen, f.names, "clk").empty());
}

TEST(PhysicalEligibility, FoldedRegistersKeepOnlyAsynchronousResets) {
  // A folded register's synchronous reset acts only on enabled edges of its
  // gate: an RST pin on the root clock would reset with the gate closed.
  Fixture f;
  for (const bool async : {false, true}) {
    f.add(Fixture::gated_output, [async](auto& s) {
      s.reset.present = true;
      s.reset.bits    = 1;
      s.async_reset   = async;
    });
  }
  f.region.source.controls.push_back({1, synth::State_control_kind::async_reset, {4}});
  std::vector<usyn::Physical_hint> hints(2);
  for (auto& hint : hints) {
    hint.code        = "fold";
    hint.root_input  = 0;
    hint.gclk_input  = 2;
    hint.reset_input = 3;
  }
  const auto rows = physical_eligibility(f.region, f.names, "clk", hints);
  ASSERT_EQ(rows.size(), 2);
  EXPECT_EQ(rows[0].reason, Reason::gated_sync_reset);
  EXPECT_FALSE(rows[0].reset);
  EXPECT_EQ(rows[1].reason, Reason::candidate);
  ASSERT_TRUE(rows[1].reset);
  EXPECT_TRUE(rows[1].reset->async);
  EXPECT_EQ(reason_code(Reason::gated_sync_reset), "gated-sync-reset");
}

TEST(ReasonCodes, SpellingsAreStableAndOnlyDominoCodesConvert) {
  EXPECT_EQ(reason_code(Reason::candidate), "candidate");
  EXPECT_EQ(reason_code(Reason::async_reset), "async-reset");
  EXPECT_EQ(reason_code(Reason::identity_endpoint), "identity-endpoint");
  EXPECT_EQ(reason_code(Reason::selection_budget), "selection-budget");
  EXPECT_EQ(reason_code(Reason::domino_area), "domino-area");
  EXPECT_EQ(reason_code(Reason::definition_clock), "definition-clock");
  EXPECT_EQ(reason_code(Reason::polarity_search), "polarity-search");
  for (uint8_t r = 0; r <= static_cast<uint8_t>(Reason::polarity_search); ++r) {
    const auto reason = static_cast<Reason>(r);
    EXPECT_NE(reason_code(reason), "invalid");
    EXPECT_EQ(converts(reason), reason == Reason::domino_area || reason == Reason::domino_all);
  }
}

}  // namespace livehd::domplace
