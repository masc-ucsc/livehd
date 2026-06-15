//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <vector>

#include "lnast.hpp"

namespace upass {

// Split every top-level `comb`/`pipe`/`mod` lambda `func_def` out of `ln` into
// its own `top -> [io, stmts]` Lnast (signature copied verbatim, lambda kind /
// `lg` name / generics / SourceId anchor stamped, the deferred-template flag set
// for not-fully-typed signatures, and the outer-scope comptime constants the
// body reads closure-captured at the head of the body). The extracted func_defs
// are dropped from `ln` (in-place body rebuild via replace_body) so the main
// upass walk never sees them; the returned trees are registered as siblings so
// call sites resolve through the function registry exactly as before.
//
// This is the front-end of the compile pipeline replacing the old runner-based
// `func_extract` upass (todo 2g): a direct, materialize-free tree surgery that
// is a no-op (returns {}) when `ln` defines no extractable lambda — the common
// case (and the whole win on function-free inputs, where the old pass paid a
// full re-materialization regardless). slang-origin trees already arrive in the
// extracted unit form, so they have no func_def to split and skip the rebuild.
std::vector<std::shared_ptr<Lnast>> extract_lambda_functions(const std::shared_ptr<Lnast>& ln);

}  // namespace upass
