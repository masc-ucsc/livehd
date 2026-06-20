//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lsp_index.hpp"

namespace livehd::lsp_index {

Index& index() {
  static Index s;  // mirrors livehd::diag::sink() (core/diag.cpp)
  return s;
}

}  // namespace livehd::lsp_index
