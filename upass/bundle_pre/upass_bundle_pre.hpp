//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "upass_core.hpp"

// uPass_bundle_pre — runs first in the resolved pass order.
//
// Step F of docs/upass_redesign.md. Owns structural bookkeeping that
// today is scattered across attributes' 9 side maps: tuple shape,
// type_spec metadata, attr_set storage, alias chaining, sticky/pending
// propagation. Migration is incremental — see the plan for which maps
// move first.
//
// For now this is a skeleton: registered first in the resolved order
// but every hook returns Vote::keep() / no-ops. Subsequent commits move
// pieces of attributes' state into here one map at a time.
struct uPass_bundle_pre : public upass::uPass {
public:
  using upass::uPass::uPass;

  // Step E new-surface hooks. All keep() for now — bundle_pre's actual
  // work lands as pieces of upass/attributes/ migrate over.
  upass::Vote process_arith(Lnast_ntype::Lnast_ntype_int /*kind*/, Bundle* /*dst*/, const upass::Operand_vec& /*ops*/) override {
    return upass::Vote::keep();
  }
  upass::Vote process_attr_set_v(Bundle* /*dst*/, const upass::Operand_vec& /*ops*/) override { return upass::Vote::keep(); }
  upass::Vote process_type_spec_v(Bundle* /*dst*/, const upass::Operand_vec& /*ops*/) override { return upass::Vote::keep(); }
  upass::Vote process_range_v(Bundle* /*dst*/, const upass::Operand_vec& /*ops*/) override { return upass::Vote::keep(); }
  upass::Vote process_tuple_add_v(Bundle* /*dst*/, const upass::Operand_vec& /*ops*/) override { return upass::Vote::keep(); }
  upass::Vote process_tuple_concat_v(Bundle* /*dst*/, const upass::Operand_vec& /*ops*/) override { return upass::Vote::keep(); }
  upass::Vote process_tuple_set_v(Bundle* /*dst*/, const upass::Operand_vec& /*ops*/) override { return upass::Vote::keep(); }
  upass::Vote process_tuple_get_v(Bundle* /*dst*/, const upass::Operand_vec& /*ops*/) override { return upass::Vote::keep(); }
};
