//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "aeg_rules.hpp"

#include <array>

#include "runtime/match.hpp"

namespace upass::aeg {

namespace {

namespace rt = livehd::aegraph::rt;

// Simplify rules whose result is one statement over named operands and whose
// guards follow from constants, operation semantics and the 0/1 domain of a
// declared Bool input. Integer input widths and inferred ranges are not seeded.
constexpr std::array<std::string_view, 55> kAllowed = {
    // generic natives
    "const_fold_bank",
    "annihilate",
    "ident_drop",
    "single_operand",
    "idempotent",
    "sum_cancel",
    "xor_pair_cancel",
    "flatten",
    "and_absorb",
    "or_absorb",
    "cmp_same",
    "and_mask_get_mask",
    "shift_zero",
    "shift_compose",
    // generic patterns
    "and_not_self",
    "or_not_self",
    "xor_not_self",
    "sum_not_self",
    "eq_diff_zero",
    "eq_masked_sum_const_add",
    "eq_masked_sum_const_sub",
    "eq_sum_const_sub",
    "lt_const_sum_add",
    "lt_const_sum_sub",
    "gt_const_sum_add",
    "gt_const_sum_sub",
    "mult_pow2_shl",
    "not_not",
    "div_one",
    "div_self",
    "div_cancel_product",
    "getmask_getmask",
    "getmask_getmask_zero",
    "getmask_not_getmask",
    "getmask_shl_low",
    "getmask_shl_up",
    "getmask_sra",
    "getmask_sext_low",
    "sext_getmask",
    "sext_sext",
    "shl_shl",
    "sra_sra",
    // structural and Bool-domain guards
    "xor_bool_one",
    "eq_distinct",
    "ror_bool",
    "bool_complement",
    "eq_bool_chain",
    "and_cover_complement",
    "or_combine_complements",
    "and_combine_complements",
    "lt_always",
    "lt_never",
    "gt_always",
    "gt_never",
    "getmask_above",
};

constexpr std::string_view kAlt      = "alt: needs extraction with a cost model (graph engine only)";
constexpr std::string_view kNotImp   = "matches a Mux, Set_mask, Concat or Rem node, which the tree never imports";
constexpr std::string_view kRange    = "post-bitwidth range guard (todo 2a-treeaeg step 4; upass README invariant #6)";
constexpr std::string_view kLaneFits = "lane/fits native: guard reads widths the tree does not seed; pending audit";

constexpr std::array<Excluded_rule, 72> kExcluded = {{
    {"or_factor_common", kAlt},
    {"or_consensus", kAlt},
    {"and_factor_common", kAlt},
    {"and_consensus", kAlt},
    {"sum_factor_products", kAlt},
    {"diff_factor_products", kAlt},
    {"and_parity", kAlt},
    {"and_parity_window", kAlt},
    {"and_bool_parity", kAlt},
    {"or_bool_parity", kAlt},
    {"or_bool_equal", kAlt},
    {"or_bool_mux", kAlt},
    {"and_bool_mux", kAlt},
    {"mux_gate_word", kAlt},
    {"mux_gate_word_false", kAlt},
    {"or_mask_mux", kAlt},
    {"product_move_zero_gate", kAlt},
    {"product_move_mask_gate", kAlt},
    {"bit_run_reduction", kAlt},
    {"div_mulshift16", kAlt},
    {"div_mulshift31", kAlt},
    {"div_mulshift8", kAlt},
    {"div_pow2_trunc", kAlt},
    {"eq_masked_diff_zero", kAlt},
    {"eq_sum_const_add", kAlt},
    {"getmask_not_bit", kAlt},
    {"getmask_not_fits", kAlt},
    {"getmask_shl_straddle", kAlt},
    {"gt_sum_const_add", kAlt},
    {"gt_sum_const_sub", kAlt},
    {"lt_sum_const_add", kAlt},
    {"lt_sum_const_sub", kAlt},
    {"mux_lane_vector", kAlt},
    {"mux_oob_sel", kAlt},
    {"mux_setmask_arm", kAlt},
    {"mux_setmask_pair", kAlt},
    {"not_xor_alt", kAlt},
    {"or_pack_to_concat", kAlt},
    {"or_shifted_windows", kAlt},
    {"rem_via_div", kAlt},
    {"sum_double", kAlt},
    {"concat_merge_slices", kNotImp},
    {"getmask_setmask_inside", kNotImp},
    {"getmask_setmask_outside", kNotImp},
    {"setmask_overwrite", kNotImp},
    {"setmask_own_bits", kNotImp},
    {"rem_one", kNotImp},
    {"rem_self", kNotImp},
    {"rem_idempotent", kNotImp},
    {"div_rem_reconstruct", kNotImp},
    {"mux_sel_arm", kNotImp},
    {"getmask_concat_straddle", kNotImp},
    {"mux_guard_accumulation", kNotImp},
    {"setmask_zero_noop", kNotImp},
    {"mux_nonzero_sel", kNotImp},
    {"mux_zero_sel", kNotImp},
    {"mux_sel_not", kNotImp},
    {"mux_bool_const_arms", kNotImp},
    {"mux_const_arm_logic", kNotImp},
    {"div_pow2", kRange},
    {"rem_pow2", kRange},
    {"div_shl_pow2", kRange},
    {"concat_single_fits", kRange},
    {"concat_zero_top", kRange},
    {"sext_fits", kRange},
    {"getmask_fits", kLaneFits},
    {"getmask_lane", kLaneFits},
    {"sra_lane", kLaneFits},
    {"getmask_sum_chain", kLaneFits},
    {"getmask_drop_terms", kLaneFits},
    {"concat_lanes", kLaneFits},
    {"setmask_to_concat", kLaneFits},
}};

}  // namespace

std::span<const std::string_view> allowed_rules() { return kAllowed; }
std::span<const Excluded_rule>    excluded_rules() { return kExcluded; }

const std::vector<uint8_t>& allowed_rule_mask() {
  static const std::vector<uint8_t> mask = [] {
    auto&                reg = rt::Registry::instance();
    std::vector<uint8_t> m(reg.size() + 1, 0);
    for (const auto name : kAllowed) {
      if (const auto id = reg.find(name)) {
        m[*id] = 1;
      }
    }
    return m;
  }();
  return mask;
}

}  // namespace upass::aeg
