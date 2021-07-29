
#include "eprp_method.hpp"

#include "elab_scanner.hpp"
#include "fmt/format.h"

Eprp_method::Eprp_method(const mmap_lib::str &_name, const mmap_lib::str &_help, const std::function<void(Eprp_var &var)> &_method)
    : name(_name), help(_help), method(_method){};

bool Eprp_method::has_label(const mmap_lib::str &label) const { return labels.find(label) != labels.end(); }

void Eprp_method::add_label(const mmap_lib::str &attr, const mmap_lib::str &help_txt, bool required, const mmap_lib::str &default_value) {
  labels.insert({attr, {help_txt, required, default_value}});
}

mmap_lib::str Eprp_method::get_label_help(const mmap_lib::str &label) const {
  const auto it = labels.find(label);
  if (it == labels.end()) {
    return mmap_lib::str();
  }

  return it->second.help;
}

std::pair<bool, mmap_lib::str> Eprp_method::check_labels(const Eprp_var &var) const {
  for (const auto &l : labels) {
    if (!l.second.required)
      continue;

    if (!var.has_label(l.first)) {
      mmap_lib::str err_msg(fmt::format("method {} requires label {}:, but it is missing", name, l.first));
      return std::make_pair(true, err_msg);
    }
  }

  return std::make_pair(false, mmap_lib::str());
}
