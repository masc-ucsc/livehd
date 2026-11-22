// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "clock_function.hpp"

#include <bit>
#include <cctype>
#include <map>
#include <utility>

namespace livehd::liberty {
namespace {
// Evaluate the entire independent pin basis at once. Reject unknown tokens,
// absent pins and unfinished expressions; never use a sampled signature.
// Symbolic evaluation of a wide product of literals, optionally complemented.
// Unrepresentable intermediate expressions stay unknown, never approximated.
struct Literal_product {
  bool                   valid = true, inverted = false;
  std::map<size_t, bool> literals;
  explicit Literal_product(int value) : inverted(!value) {}
  static Literal_product pin(size_t i) {
    Literal_product p{1};
    p.literals.emplace(i, true);
    return p;
  }
  Literal_product operator~() const {
    auto p = *this;
    if (p.literals.size() == 1) {
      p.literals.begin()->second = !p.literals.begin()->second;
    } else {
      p.inverted = !p.inverted;
    }
    return p;
  }
  Literal_product& operator&=(const Literal_product& other) {
    if (!valid || !other.valid) {
      valid = false;
    } else if (literals.empty()) {
      if (!inverted) {
        *this = other;
      }
    } else if (other.literals.empty()) {
      if (other.inverted) {
        *this = other;
      }
    } else if (inverted || other.inverted) {
      valid = false;
    } else {
      for (const auto& [pin_id, level] : other.literals) {
        auto [it, added] = literals.emplace(pin_id, level);
        if (!added && it->second != level) {
          *this = Literal_product{0};
          break;
        }
      }
    }
    return *this;
  }
  Literal_product& operator|=(const Literal_product& other) {
    auto p        = ~*this;
    p            &= ~other;
    return *this  = ~p;
  }
  Literal_product& operator^=(const Literal_product& other) {
    if (other.valid && other.literals.empty()) {
      if (!other.inverted) {
        *this = ~*this;
      }
    } else {
      valid = false;
    }
    return *this;
  }
};
template <class Value>
class Expression {
public:
  Expression(std::string_view expression, const std::vector<std::string>& pins, auto make_pin) : text(expression) {
    for (size_t i = 0; i < pins.size(); ++i) {
      variables.emplace(pins[i], make_pin(i));
    }
  }
  std::optional<Value> parse() {
    auto value = disjunction();
    space();
    if (!valid || position != text.size()) {
      return {};
    }
    return value;
  }

private:
  std::string_view             text;
  size_t                       position = 0;
  bool                         valid    = true;
  std::map<std::string, Value> variables;
  void                         space() {
    while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position]))) {
      ++position;
    }
  }
  bool take(char token) {
    space();
    if (position < text.size() && text[position] == token) {
      ++position;
      return true;
    }
    return false;
  }
  // Liberty precedence, loosest to tightest: OR (`+` `|`), AND (`*` `&` and
  // juxtaposition), XOR (`^`), then inversion (`!` prefix, `'` postfix) --
  // OpenSTA's LibExprParse.yy declares `%left '+' '|'`, `%left '*' '&'`,
  // `%left '^'`, `%left '!' '\''` in that order. So `A^B*C` is `(A^B)*C`.
  Value disjunction() {
    auto value = conjunction();
    while (take('+') || take('|')) {
      value |= conjunction();
    }
    return value;
  }
  Value conjunction() {
    auto value = exclusive();
    while (valid) {
      if (take('*') || take('&')) {
        value &= exclusive();
        continue;
      }
      space();
      if (position < text.size()
          && (text[position] == '(' || text[position] == '!' || text[position] == '~'
              || std::isalnum(static_cast<unsigned char>(text[position])) || text[position] == '_')) {
        value &= exclusive();
        continue;
      }
      break;
    }
    return value;
  }
  Value exclusive() {
    auto value = literal();
    while (take('^')) {
      value ^= literal();
    }
    return value;
  }
  Value literal() {
    const bool inverse = take('!') || take('~');
    if (inverse) {
      return ~literal();
    }
    Value value{0};
    if (take('(')) {
      value = disjunction();
      if (!take(')')) {
        valid = false;
      }
    } else {
      space();
      const auto begin = position;
      while (position < text.size() && (std::isalnum(static_cast<unsigned char>(text[position])) || text[position] == '_')) {
        ++position;
      }
      const auto name  = std::string(text.substr(begin, position - begin));
      const auto found = variables.find(name);
      if (name == "0") {
        value = Value{0};
      } else if (name == "1") {
        value = ~Value{0};
      } else if (found != variables.end()) {
        value = found->second;
      } else {
        valid = false;
      }
    }
    while (take('\'')) {
      value = ~value;
    }
    return value;
  }
};

}  // namespace

std::optional<uint64_t> function_truth(std::string_view function, const std::vector<std::string>& inputs) {
  if (inputs.empty() || inputs.size() > 6) {
    return {};
  }
  return Expression<uint64_t>(function,
                              inputs,
                              [](size_t i) {
                                uint64_t bits = 0;
                                for (uint32_t p = 0; p < 64; ++p) {
                                  bits |= uint64_t{(p >> i) & 1} << p;
                                }
                                return bits;
                              })
      .parse();
}

std::optional<Clock_gate_form> clock_gate_form(std::string_view function, const std::vector<std::string>& inputs) {
  if (inputs.size() < 2) {
    return {};
  }
  Clock_gate_form gate;
  if (inputs.size() <= 6) {
    const auto truth = function_truth(function, inputs);
    if (!truth) {
      return {};
    }
    const auto mask        = ~uint64_t{0} >> (64 - (1u << inputs.size()));
    auto       exceptional = *truth & mask;
    gate.active_output     = std::has_single_bit(exceptional);
    if (!gate.active_output) {
      exceptional = (~exceptional) & mask;
    }
    if (!std::has_single_bit(exceptional)) {
      return {};
    }
    const auto row = std::countr_zero(exceptional);
    for (size_t i = 0; i < inputs.size(); ++i) {
      gate.levels.push_back((row >> i) & 1);
    }
  } else {
    const auto form = Expression<Literal_product>(function, inputs, Literal_product::pin).parse();
    if (!form || !form->valid || form->literals.size() != inputs.size()) {
      return {};
    }
    gate.active_output = !form->inverted;
    for (size_t i = 0; i < inputs.size(); ++i) {
      gate.levels.push_back(form->literals.at(i));
    }
  }
  return gate;
}

}  // namespace livehd::liberty
