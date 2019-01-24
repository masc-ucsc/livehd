//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "absl/container/flat_hash_map.h"
#include "eprp_var.hpp"

class Eprp_method {
private:
protected:
  struct Label_attr {
    Label_attr(const std::string &_help, bool _required, const std::string &_value)
        : help(_help), default_value(_value), required(_required){};
    const std::string help;
    const std::string default_value;
    const bool        required;
  };
  void add_label(const std::string &attr, const std::string &help, bool required, const std::string &default_value = "");
  const std::string name;

public:
  absl::flat_hash_map<std::string, Label_attr> labels;

  const std::string &get_name() const { return name; }

  const std::string                        help;
  const std::function<void(Eprp_var &var)> method;

  Eprp_method(const std::string &_name, const std::string &_help, std::function<void(Eprp_var &var)> _method);

  bool check_labels(const Eprp_var &var, std::string &err_msg) const;

  bool has_label(const std::string &label) const;
  void add_label_optional(const std::string &attr, const std::string &help, const std::string &default_value = "") {
    add_label(attr, help, false, default_value);
  };
  void               add_label_required(const std::string &attr, const std::string &help) { add_label(attr, help, true); };
  const std::string &get_label_help(const std::string &label) const;
};
