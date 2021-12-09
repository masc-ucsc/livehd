/*
Author: Farzaneh Rabiei, GitHub: https://github.com/rabieifk
*/
#include "json_composer.hpp"
#include <vector>
#include <iostream>
#include <ostream>

using namespace std;

namespace jsn
{
    
void JsonComposer::Write(const JsonElement* property) const {
  if (property == NULL || property->type == etEndOfList)
    return;

  while(true) {
    auto val = &property->value;
    if (property->key)
      target << '"' << property->key << "\":\t";
    switch (property->type)
    {
    case etEndOfList: break;
    case etInt:     target << val->i; break;
    case etUInt:    target << val->u; break;
    case etBool:    target << (val->b ? "true" : "false"); break;
    case etFloat:   target << val->f; break;
    case etString:
      target << '"' << val->str << '"';
      break;
    case etObject: 
      target << "{";
      WriteObject(property);
      target << "}";
      break;
    case etNested:
      target << "{";
      Write(val->nested);
      target << "}";
      break;
    case etArray:
      target << "[";
      WriteObject(property);
      target << "]";
      break;
    }
    property++;
    if (property->type == etEndOfList)
      break;
    target << ",\n";
  }
}

} // namespase jsn
