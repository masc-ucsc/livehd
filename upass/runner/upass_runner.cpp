//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_runner.hpp"

void uPass_runner::process_lnast() {
#define PROCESS_BLOCK(NAME)             \
  case Lnast_ntype::Lnast_ntype_##NAME: \
    process_##NAME();                   \
    break;

#define PROCESS_NODE(NAME)              \
  case Lnast_ntype::Lnast_ntype_##NAME: \
    write_node();                       \
    for (const auto upass : upasses) {  \
      upass->process_##NAME();          \
    }                                   \
    break;

  switch (get_raw_ntype()) {
    PROCESS_BLOCK(top)
    PROCESS_BLOCK(stmts)

    // Assignment
    PROCESS_NODE(assign)

    // Bitwidth
    PROCESS_NODE(bit_and)
    PROCESS_NODE(bit_or)
    PROCESS_NODE(bit_not)
    PROCESS_NODE(bit_xor)

    // Bitwidth Insensitive Reduce
    PROCESS_NODE(reduce_or)

    // Logical
    PROCESS_NODE(logical_and)
    PROCESS_NODE(logical_or)
    PROCESS_NODE(logical_not)

    // Arithmetic
    PROCESS_NODE(plus)
    PROCESS_NODE(minus)
    PROCESS_NODE(mult)
    PROCESS_NODE(div)
    PROCESS_NODE(mod)

    // Shift
    PROCESS_NODE(shl)
    PROCESS_NODE(sra)

    // Bit Manipulation
    PROCESS_NODE(sext)
    PROCESS_NODE(set_mask)
    PROCESS_NODE(get_mask)
    PROCESS_NODE(mask_and)
    PROCESS_NODE(mask_popcount)
    PROCESS_NODE(mask_xor)

    // Comparison
    PROCESS_NODE(ne)
    PROCESS_NODE(eq)
    PROCESS_NODE(lt)
    PROCESS_NODE(le)
    PROCESS_NODE(gt)
    PROCESS_NODE(ge)
    default: break;
  }
#undef PROCESS_BLOCK
#undef PROCESS_NODE
}

void uPass_runner::process_top() {
  move_to_child();
  do {
    process_lnast();
  } while (move_to_sibling());
  move_to_parent();
}

void uPass_runner::process_stmts() {
  move_to_child();
  do {
    process_lnast();
  } while (move_to_sibling());
  move_to_parent();
}
