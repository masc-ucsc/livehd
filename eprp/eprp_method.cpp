
#include "spdlog/spdlog.h"
#include "eprp_method.hpp"

Eprp_method::Eprp_method(const std::string &_name, const std::string &_help, std::function<void(Eprp_var &var)> _method)
  :name(_name)
   ,help(_help)
   ,method(_method) {
   };

bool Eprp_method::has_label(const std::string &label) const {
  return labels.find(label) != labels.end();
}

void Eprp_method::add_label(const std::string &attr, const std::string &help, bool required, const std::string& default_value) {
  labels.insert({attr, {help, required, default_value} });
}

const std::string &Eprp_method::get_label_help(const std::string &label) const {
  const auto it = labels.find(label);
  if (it == labels.end()) {
    static const std::string empty = "";
    return empty;
  }

  return it->second.help;
}

bool Eprp_method::check_labels(const Eprp_var &var, std::string &err_msg) const {

  for(const auto &l:labels) {
    if (!l.second.required)
      continue;

    if (!var.has_label(l.first)) {
      err_msg = fmt::format("method {} requires label {}:, but it is missing", name, l.first);
      return true;
    }
  }

  return false;
}

