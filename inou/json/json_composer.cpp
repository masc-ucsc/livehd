/*
  Author: Farzaneh Rabiei, GitHub: https://github.com/rabieifk
*/
#include "json_composer.hpp"

#include <iostream>
#include <ostream>
#include <vector>

using namespace std;

namespace jsn {

void JsonComposer::Write(const JsonElement* property) const {
  if (property == NULL || property->type == etEndOfList)
    return;

  indent += {'\t'};
  while (true) {
    auto last_written_key = property->key;
    if (property->key)
      target << indent << '"' << property->key << "\":\t";
    auto val = &property->value;
    switch (property->type) {
      case etEndOfList: break;
      case etInt: target << val->i; break;
      case etUInt: target << val->u; break;
      case etBool: target << (val->b ? "true" : "false"); break;
      case etFloat: target << val->f; break;
      case etString: target << '"' << val->str << '"'; break;
      case etObject:
        target << "{" << endl;
        WriteObject(property);
        target << endl << indent << "}";
        break;
      case etNested:
        target << "{" << endl;
        Write(val->nested);
        target << endl << indent << "}";
        break;
      case etArray:
        target << "[";
        WriteObject(property);
        target << "]";
        break;
    }
    property++;
    current_key = last_written_key;
    if (property->type == etEndOfList)
      break;
    WriteDelimiter();  // writes ",\n"
  }

  if (indent.size() > 0)
    indent = indent.substr(0, indent.size() - 1);  // before leaving reduce indentation one level
}

}  // namespace jsn
