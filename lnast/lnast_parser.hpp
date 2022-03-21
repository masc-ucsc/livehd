//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lnast.hpp"
#include "lhtree.hpp"

#include <iostream>
#include <stack>
#include <fmt/format.h>
#include <fmt/os.h>
#include <fmt/color.h>

class Lnast_parser {
public:
  explicit Lnast_parser(std::istream&);
  std::unique_ptr<Lnast> parse_all();

protected:
  std::istream& is;
};
