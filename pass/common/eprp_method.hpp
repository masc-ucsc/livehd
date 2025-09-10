//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "absl/container/flat_hash_map.h"
#include "eprp_var.hpp"

class Eprp_method {
private:
protected:
  struct Label_attr {
    Label_attr(std::string_view _help, bool _required, std::string_view _value)
        : help(_help), default_value(_value), required(_required) {};
    const std::string help;
    const std::string default_value;
    const bool        required;
  };
  void              add_label(std::string_view attr, std::string_view help, bool required, std::string_view default_value = "");
  const std::string name;

public:
  absl::flat_hash_map<std::string, Label_attr> labels;

  [[nodiscard]] std::string_view get_name() const { return name; }

  const std::string                        help;
  const std::function<void(Eprp_var &var)> method;

  Eprp_method(std::string_view _name, std::string_view _help, const std::function<void(Eprp_var &var)> &_method);

  [[nodiscard]] std::pair<bool, std::string> check_labels(const Eprp_var &var) const;

  [[nodiscard]] bool has_label(std::string_view label) const;
  void               add_label_optional(std::string_view attr, std::string_view help_txt, std::string_view default_value = "") {
    add_label(attr, help_txt, false, default_value);
  };
  void add_label_required(std::string_view attr, std::string_view help_txt) { add_label(attr, help_txt, true); };
  [[nodiscard]] std::string_view get_label_help(std::string_view label) const;
};
