
#include "dlop.hpp"

std::shared_ptr<Dlop> Dlop::add_op(std::shared_ptr<Dlop> other) {

  // TODO: deal with extra
  auto dlop = std::make_shared<Dlop>(Type::Integer, size); // TODO: it can be size+1 (get_bits)

  assert(size == other->size); // TODO:

  Blop::addn(dlop->base, dlop->size, base, other->base);

  return dlop;
}
