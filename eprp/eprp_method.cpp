
#include "eprp_method.hpp"

#include "elab_scanner.hpp"
#include "fmt/format.h"

Eprp_method::Eprp_method(std::string_view _name, std::string_view _help, const std::function<void(Eprp_var &var)> &_method)
    : name(_name), help(_help), method(_method){};

bool Eprp_method::has_label(std::string_view label) const { return labels.find(label) != labels.end(); }

void Eprp_method::add_label(std::string_view attr, std::string_view help_txt, bool required, std::string_view default_value) {
  labels.insert({std::string(attr), {help_txt, required, default_value}});
}

std::string_view Eprp_method::get_label_help(std::string_view label) const {
  const auto it = labels.find(std::string(label));
  if (it == labels.end()) {
    return "";
  }

  return it->second.help;
}

std::pair<bool, std::string> Eprp_method::check_labels(const Eprp_var &var) const {
  for (const auto &l : labels) {
    if (!l.second.required)
      continue;

    if (!var.has_label(l.first)) {
      std::string err_msg(fmt::format("method {} requires label {}:, but it is missing", name, l.first));
      return std::make_pair(true, err_msg);
    }
  }

  return std::make_pair(false, "");
}
