//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "absl/container/flat_hash_map.h"
#include "eprp_var.hpp"

class Eprp_method {
private:
protected:
  struct Label_attr {
    Label_attr(const mmap_lib::str &_help, bool _required, const mmap_lib::str &_value)
        : help(_help), default_value(_value), required(_required){};
    const mmap_lib::str help;
    const mmap_lib::str default_value;
    const bool        required;
  };
  void add_label(const mmap_lib::str &attr, const mmap_lib::str &help, bool required, const mmap_lib::str default_value = ""_str);
  const mmap_lib::str name;

public:
  absl::flat_hash_map<mmap_lib::str, Label_attr> labels;

  const mmap_lib::str &get_name() const { return name; }

  const mmap_lib::str                        help;
  const std::function<void(Eprp_var &var)> method;

  Eprp_method(const mmap_lib::str &_name, const mmap_lib::str &_help, const std::function<void(Eprp_var &var)> &_method);
  Eprp_method(std::string_view _name, std::string_view _help, const std::function<void(Eprp_var &var)> &_method)
    :Eprp_method(mmap_lib::str(_name), mmap_lib::str(_help), _method) {
  }

  std::pair<bool, mmap_lib::str> check_labels(const Eprp_var &var) const;

  bool has_label(const mmap_lib::str &label) const;
  void add_label_optional(const mmap_lib::str &attr, const mmap_lib::str &help_txt, const mmap_lib::str default_value = ""_str) {
    add_label(attr, help_txt, false, default_value);
  };
  void               add_label_required(const mmap_lib::str &attr, const mmap_lib::str &help_txt) { add_label(attr, help_txt, true); };
  mmap_lib::str get_label_help(const mmap_lib::str &label) const;
};
