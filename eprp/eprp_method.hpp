#ifndef EPRP_METHOD_H
#define EPRP_METHOD_H

#include "eprp_var.hpp"

class Eprp_method {
private:
protected:
  struct Label_attr {
    Label_attr(const std::string &_help, bool _required)
      :help(_help)
       ,required(_required) {
       };
    const std::string help;
    const bool required;
  };
  void add_label(const std::string &attr, const std::string &help, bool required);
public:
  std::map<std::string, Label_attr, eprp_casecmp_str> labels;
  const std::string name;
  const std::string help;
  const std::function<void(Eprp_var &var)> method;

  Eprp_method(const std::string &_name, const std::string &_help, std::function<void(Eprp_var &var)> _method);

  std::string check_labels(const Eprp_var &var) const; // return not const string because it creates error messages

  bool has_label(const std::string &label) const;
  void add_label_optional(const std::string &attr, const std::string &help, const std::string &default_value = "") {
    add_label(attr,help,false);
  };
  void add_label_required(const std::string &attr, const std::string &help) {
    add_label(attr,help,true);
  };
  const std::string &get_label_help(const std::string &label) const;
};

#endif
