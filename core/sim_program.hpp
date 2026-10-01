// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cctype>
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Simulation-only initialization IR. Variables are private to one program;
// there are no references to circuit nets. Stored separately from the circuit.
namespace livehd::sim_ir {
struct Node {
  std::string       op, text;
  int               bits = 0;
  bool              sign = false;
  std::vector<Node> kids;
};
inline void frame(std::string& out, std::string_view value) {
  out += std::to_string(value.size()) + ":";
  out += value;
}
inline void encode_node(std::string& out, const Node& n) {
  frame(out, n.op);
  frame(out, n.text);
  frame(out, std::to_string(n.bits));
  frame(out, n.sign ? "1" : "0");
  frame(out, std::to_string(n.kids.size()));
  for (const auto& k : n.kids) {
    encode_node(out, k);
  }
}
inline std::string encode(const Node& n) {
  std::string out = "sim-init:1:";
  encode_node(out, n);
  return out;
}
inline std::string take(std::string_view& in) {
  auto   sep  = in.find(':');
  size_t size = 0;
  if (sep == std::string_view::npos) {
    throw std::runtime_error("malformed simulation initialization IR");
  }
  const auto [p, ec] = std::from_chars(in.data(), in.data() + sep, size);
  if (ec != std::errc{} || p != in.data() + sep || size > in.size() - sep - 1) {
    throw std::runtime_error("malformed simulation initialization IR frame");
  }
  std::string result(in.substr(sep + 1, size));
  in.remove_prefix(sep + 1 + size);
  return result;
}
inline Node decode_node(std::string_view& in, unsigned depth = 0) {
  if (depth > 256) {
    throw std::runtime_error("simulation initialization IR nesting too deep");
  }
  Node n;
  n.op             = take(in);
  n.text           = take(in);
  n.bits           = std::stoi(take(in));
  n.sign           = take(in) == "1";
  const auto count = std::stoul(take(in));
  if (count > in.size()) {
    throw std::runtime_error("invalid simulation initialization IR child count");
  }
  for (size_t i = 0; i < count; ++i) {
    n.kids.push_back(decode_node(in, depth + 1));
  }
  return n;
}
inline Node decode(std::string_view in) {
  if (!in.starts_with("sim-init:1:")) {
    throw std::runtime_error("unsupported simulation initialization IR version");
  }
  in.remove_prefix(11);
  auto n = decode_node(in);
  if (!in.empty()) {
    throw std::runtime_error("trailing simulation initialization IR data");
  }
  return n;
}
inline std::string quote(std::string_view text) {
  std::string out = "\"";
  for (unsigned char c : text) {
    if (c >= 32 && c < 127 && c != '"' && c != '\\') {
      out += static_cast<char>(c);
    } else {
      out += '\\';
      out += char('0' + (c >> 6));
      out += char('0' + ((c >> 3) & 7));
      out += char('0' + (c & 7));
    }
  }
  return out + '"';
}
inline std::string variable(std::string_view name) {
  if (name.empty() || name.find_first_not_of("0123456789") != std::string_view::npos) {
    throw std::runtime_error("invalid simulation variable id");
  }
  return "__sv_v" + std::string(name);
}
inline std::string fit(const Node& n, const std::string& expr) {
  if (n.bits == 0) {
    return expr;
  }
  if (n.bits < 1 || n.bits > 64) {
    throw std::runtime_error("unsupported simulation integer width");
  }
  return "__lhd_sv_fit<" + std::to_string(n.bits) + "," + (n.sign ? "true" : "false") + ">(" + expr + ")";
}
inline std::string expression(const Node& n) {
  auto arg = [&](size_t i) { return expression(n.kids.at(i)); };
  if (n.op == "str") {
    return "std::string(" + quote(n.text) + ")";
  }
  if (n.op == "num") {
    uint64_t number      = 0;
    const auto [end, ec] = std::from_chars(n.text.data(), n.text.data() + n.text.size(), number);
    if (ec != std::errc{} || end != n.text.data() + n.text.size()) {
      throw std::runtime_error("invalid simulation integer");
    }
    return fit(n, std::to_string(number) + "ULL");
  }
  if (n.op == "var") {
    return variable(n.text);
  }
  if (n.op == "cast") {
    return fit(n, arg(0));
  }
  if (n.op == "test") {
    return "__lhd_sv_test(" + quote(n.text) + ")";
  }
  if (n.op == "value") {
    return "__lhd_sv_value<" + std::to_string(n.kids.at(0).bits) + "," + (n.kids.at(0).sign ? "true" : "false") + ">("
           + quote(n.text) + "," + arg(0) + ")";
  }
  if (n.op == "unary") {
    if (n.text != "!" && n.text != "~" && n.text != "-" && n.text != "+") {
      throw std::runtime_error("invalid simulation unary op");
    }
    return fit(n, "(" + n.text + "static_cast<uint64_t>(" + arg(0) + "))");
  }
  if (n.op == "binary") {
    const std::string allowed = " + - * & | ^ << >> == != < <= > >= && || ";
    if (allowed.find(" " + n.text + " ") == std::string::npos) {
      throw std::runtime_error("invalid simulation binary op");
    }
    std::string l = arg(0), r = arg(1);
    if (n.text == "+" || n.text == "-" || n.text == "*" || n.text == "<<") {
      l = "static_cast<uint64_t>(" + l + ")";
      r = "static_cast<uint64_t>(" + r + ")";
    }
    return fit(n, "(" + l + " " + n.text + " " + r + ")");
  }
  throw std::runtime_error("unsupported simulation expression " + n.op);
}
inline void validate_format(const Node& n) {
  size_t argument = 0;
  for (size_t i = 0; i < n.text.size(); ++i) {
    if (n.text[i] != '%') {
      continue;
    }
    if (++i == n.text.size()) {
      throw std::runtime_error("incomplete SV debug format");
    }
    if (n.text[i] == '%') {
      continue;
    }
    while (i < n.text.size() && std::isdigit(static_cast<unsigned char>(n.text[i]))) {
      ++i;
    }
    if (i == n.text.size() || argument == n.kids.size()) {
      throw std::runtime_error("SV debug format argument mismatch");
    }
    const auto kind = static_cast<char>(std::tolower(static_cast<unsigned char>(n.text[i])));
    if (std::string("dbhxos").find(kind) == std::string::npos) {
      throw std::runtime_error("unsupported SV debug display format");
    }
    if (n.kids[argument++].bits == 0 && kind != 's') {
      throw std::runtime_error("string debug arguments require %s");
    }
  }
  if (argument != n.kids.size()) {
    throw std::runtime_error("extra SV debug format arguments");
  }
}
inline std::string statement(const Node& n) {
  if (n.op == "seq") {
    std::string result = "{\n";
    for (const auto& k : n.kids) {
      result += statement(k);
    }
    return result + "}\n";
  }
  if (n.op == "decl") {
    return std::string(n.bits == 0 ? "std::string "
                       : n.sign    ? "int64_t "
                                   : "uint64_t ")
           + variable(n.text) + " = " + fit(n, expression(n.kids.at(0))) + ";\n";
  }
  if (n.op == "set") {
    return expression(n.kids.at(0)) + " = " + fit(n.kids.at(0), expression(n.kids.at(1))) + ";\n";
  }
  if (n.op == "expr") {
    return "(void)(" + expression(n.kids.at(0)) + ");\n";
  }
  if (n.op == "if") {
    return "if (" + expression(n.kids.at(0)) + ") {\n" + statement(n.kids.at(1)) + "}\n"
           + (n.kids.size() > 2 ? "else {\n" + statement(n.kids.at(2)) + "}\n" : "");
  }
  if (n.op == "assert") {
    return "if (!(" + expression(n.kids.at(0)) + ")) throw std::runtime_error(" + quote(n.text) + ");\n";
  }
  if (n.op == "display" || n.op == "write" || n.op == "fatal") {
    validate_format(n);
    std::string args = "{";
    for (size_t i = 0; i < n.kids.size(); ++i) {
      if (i) {
        args += ",";
      }
      const auto& k  = n.kids[i];
      args          += "__lhd_sv_arg(" + expression(k) + "," + std::to_string(k.bits) + "," + (k.sign ? "true" : "false") + ")";
    }
    args                 += "}";
    const auto formatted  = "__lhd_sv_format(" + quote(n.text) + "," + args + ")";
    if (n.op == "fatal") {
      return "throw std::runtime_error(" + formatted + ");\n";
    }
    return "__lhd_sv_print(" + formatted + (n.op == "display" ? "+\"\\n\"" : "") + ");\n";
  }
  throw std::runtime_error("unsupported simulation statement " + n.op);
}
inline std::string sv_expression(const Node& n) {
  auto arg = [&](size_t i) { return sv_expression(n.kids.at(i)); };
  if (n.op == "str") {
    return quote(n.text);
  }
  if (n.op == "num") {
    return std::to_string(n.bits) + (n.sign ? "'sd" : "'d") + n.text;
  }
  if (n.op == "var") {
    return variable(n.text);
  }
  if (n.op == "cast") {
    return n.bits ? std::string(n.sign ? "$signed(" : "$unsigned(") + std::to_string(n.bits) + "'(" + arg(0) + "))" : arg(0);
  }
  if (n.op == "test") {
    return "$test$plusargs(" + quote(n.text) + ")";
  }
  if (n.op == "value") {
    return "$value$plusargs(" + quote(n.text) + "," + arg(0) + ")";
  }
  if (n.op == "unary") {
    return "(" + n.text + arg(0) + ")";
  }
  if (n.op == "binary") {
    return "(" + arg(0) + " " + n.text + " " + arg(1) + ")";
  }
  throw std::runtime_error("unsupported SV simulation expression");
}
inline std::string sv_statement(const Node& n) {
  if (n.op == "seq") {
    std::string out = "begin\n";
    for (const auto& k : n.kids) {
      if (k.op == "decl") {
        out += std::string(k.bits ? "reg " : "string ")
               + (k.bits ? std::string(k.sign ? "signed " : "") + "[" + std::to_string(k.bits - 1) + ":0] " : "") + variable(k.text)
               + ";\n";
      }
    }
    for (const auto& k : n.kids) {
      out += sv_statement(k);
    }
    return out + "end\n";
  }
  if (n.op == "decl") {
    return variable(n.text) + " = " + sv_expression(n.kids.at(0)) + ";\n";
  }
  if (n.op == "set") {
    return sv_expression(n.kids.at(0)) + " = " + sv_expression(n.kids.at(1)) + ";\n";
  }
  if (n.op == "expr") {
    return "void'(" + sv_expression(n.kids.at(0)) + ");\n";
  }
  if (n.op == "if") {
    return "if (" + sv_expression(n.kids.at(0)) + ") begin\n" + sv_statement(n.kids.at(1)) + "end\n"
           + (n.kids.size() > 2 ? "else begin\n" + sv_statement(n.kids.at(2)) + "end\n" : "");
  }
  if (n.op == "assert") {
    return "assert (" + sv_expression(n.kids.at(0)) + ") else $fatal(1," + quote(n.text) + ");\n";
  }
  if (n.op == "display" || n.op == "write" || n.op == "fatal") {
    std::string out = "$" + n.op + "(" + (n.op == "fatal" ? "1," : "") + quote(n.text);
    for (const auto& k : n.kids) {
      out += "," + sv_expression(k);
    }
    return out + ");\n";
  }
  throw std::runtime_error("unsupported SV simulation statement");
}
}  // namespace livehd::sim_ir
