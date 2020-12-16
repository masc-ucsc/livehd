//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <cassert>
#include <stdio.h>
#include <stdlib.h>
#include "boolector.h"

#define BV1_EXAMPLE_NUM_BITS 8

/* We verify the XOR swap algorithm. The XOR bitwise operation can
 * be used to swap variables without using a temporary variable:
 * int x, y;
 * ...
 * x = x ^ y
 * y = x ^ y
 * x = x ^ y
 */

int
main (void)
{
  Btor *btor;
  BoolectorNode *x, *y, *temp, *old_x, *old_y, *eq1, *eq2, *and_op, *formula;
  BoolectorSort s;
  int result;

  btor = boolector_new ();
  s    = boolector_bitvec_sort (btor, BV1_EXAMPLE_NUM_BITS);
  x    = boolector_var (btor, s, NULL);
  y    = boolector_var (btor, s, NULL);

  /* remember initial values of x and_op y */
  old_x = boolector_copy (btor, x);
  old_y = boolector_copy (btor, y);

  /* x = x ^ y */
  temp = boolector_xor (btor, x, y);
  boolector_release (btor, x);
  x = temp;

  /* y = x ^ y */
  temp = boolector_xor (btor, x, y);
  boolector_release (btor, y);
  y = temp;

  /* x = x ^ y */
  temp = boolector_xor (btor, x, y);
  boolector_release (btor, x);
  x = temp;

  /* Now, we have to show that old_x = y and_op old_y = x */
  eq1 = boolector_eq (btor, old_x, y);
  eq2 = boolector_eq (btor, old_y, x);
  and_op = boolector_and (btor, eq1, eq2);

  /* In order to prove that this is a theorem, we negate the whole
   * formula and_op show that the negation is unsatisfiable */
  formula = boolector_not (btor, and_op);

  /* We assert the formula and_op call Boolector */
  boolector_assert (btor, formula);
  result = boolector_sat (btor);
  printf ("Expect: unsat\n");
  printf ("Boolector: %s\n",
          result == BOOLECTOR_SAT
              ? "sat"
              : (result == BOOLECTOR_UNSAT ? "unsat" : "unknown"));
  if (result != BOOLECTOR_UNSAT) abort ();

  /* cleanup */
  boolector_release (btor, x);
  boolector_release (btor, old_x);
  boolector_release (btor, y);
  boolector_release (btor, old_y);
  boolector_release (btor, eq1);
  boolector_release (btor, eq2);
  boolector_release (btor, and_op);
  boolector_release (btor, formula);
  boolector_release_sort (btor, s);
  assert (boolector_get_refs (btor) == 0);
  boolector_delete (btor);
  return 0;
}
