// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// The small Tcl the SDC reader runs constraint files in (see sdc.hpp): the Tcl syntax (words, braces, quotes,
// `$var`, `[command]`, backslashes, comments), lists, `expr`, and the control and list commands constraint
// files use. Everything is a string, as in Tcl. Commands that are not built in are asked of a host (the SDC
// commands). It is internal to pass/timing.
namespace livehd::timing::fast::tcl {

struct Error : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Tcl `string match`: `*`, `?`, `[a-z]`, backslash.
bool                     match(std::string_view pattern, std::string_view text, bool nocase = false);
std::vector<std::string> list_split(std::string_view text);
std::string              list_quote(std::string_view element);
std::string              list_join(const std::vector<std::string>& elements);
// A number as Tcl prints it: an integer without a point, a real with one.
std::string              number_text(double value, bool integer);
bool                     to_number(std::string_view text, double& value, bool* integer = nullptr);

class Interp {
public:
  using Words = std::vector<std::string>;
  // A command the interpreter does not have: returns true and the result when the host runs it.
  using Host = std::function<bool(Words& words, std::string& result)>;
  // The text two list elements are compared by (`lsearch`): a host maps an object handle to its name.
  using Plain = std::function<std::string(std::string_view)>;

  explicit Interp(Host host, Plain plain = {});

  // Runs a script and returns the result of its last command. Throws Error.
  std::string eval(std::string_view script);
  // Runs a script one top-level command at a time: an error skips that command and is handed to `on_error`
  // with the name of the command, and the script goes on.
  void run(std::string_view script, const std::function<void(const std::string& command, const std::string& message)>& on_error);
  // The line (from 1) of the top-level command `run` is executing.
  [[nodiscard]] size_t line() const { return line_; }

  [[nodiscard]] bool               has_variable(std::string_view name) const;
  [[nodiscard]] const std::string& variable(std::string_view name) const;
  void                             set_variable(std::string_view name, std::string value);
  // `expr` on a string.
  std::string                      expression(std::string_view text);
  [[nodiscard]] bool               truth(std::string_view expression_text);

private:
  struct Raw {
    enum class Kind : uint8_t { bare, braced, quoted } kind = Kind::bare;
    std::string_view text;
  };
  struct Proc {
    std::vector<std::pair<std::string, std::string>> parameters;  // name, default ("\x01" for none)
    std::string                                      body;
  };
  struct Frame {
    std::unordered_map<std::string, std::string> variables;
    std::unordered_set<std::string>              globals;
  };
  struct Break {};
  struct Continue {};
  struct Return {
    std::string value;
  };
  friend class Expression;

  // Reads the raw words of the command that starts at or after `i`; false at the end of the script.
  static bool scan(std::string_view script, size_t& i, std::vector<Raw>& words);
  static size_t skip_bracket(std::string_view text, size_t open);
  static size_t skip_brace(std::string_view text, size_t open);
  std::string substitute(std::string_view text);
  std::string word(const Raw& raw);
  std::string invoke(Words& words);
  std::string builtin(Words& words, bool& known);
  std::string call(const Proc& proc, Words& words);
  [[nodiscard]] const Frame& frame_of(std::string_view& name) const;
  [[nodiscard]] Frame&       frame_of(std::string_view& name);
  [[nodiscard]] std::string  plain(std::string_view text) const { return plain_ ? plain_(text) : std::string(text); }

  std::string do_if(Words& w);
  std::string do_foreach(Words& w);
  std::string do_lsearch(Words& w);
  std::string do_string(Words& w);
  std::string do_format(Words& w);
  std::string do_regexp(Words& w);
  std::string do_regsub(Words& w);
  std::string do_lsort(Words& w);

  Host                                  host_;
  Plain                                 plain_;
  std::vector<Frame>                    frames_;
  std::unordered_map<std::string, Proc> procs_;
  unsigned                              depth_ = 0;
  size_t                                line_  = 0;
};

}  // namespace livehd::timing::fast::tcl
