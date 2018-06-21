//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "Context.hpp"
#include "Type.hpp"
using Pyrope::Type;
using Pyrope::VarID;

#include <cassert>

const Pyrope::pyrsize WORD_WIDTH = 1;

int main(int argc, char **argv)
{
  Pyrope::Context context;

  VarID a = "a";
  VarID b = "b";
  VarID c = "c";
  VarID d = "d";

  const Type uns1 = Type::create_unsigned(1);
  const Type uns2 = Type::create_unsigned(2);

  context.add(a, uns1);
  context.add(b, Type());
  context.add(c, uns2);
  context.add(d, uns2);

  assert(context.get(a) == uns1);
  assert(context.get(b) != uns1);
  assert(context.get(c) == uns2);
  assert(context.get(a) != context.get(c));
  assert(context.get(c) == context.get(d));

  printf("...done\n");
  return 0;
}
