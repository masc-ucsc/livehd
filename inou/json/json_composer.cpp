
/*
Author: Farzaneh Rabiei, GitHub:https://github.com/rabieifk
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
  
  for (auto val = &property->value ; true ; ) {
    if (property->key)
      target << '"' << property->key << "\": ";
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
      property->ActiveObject()->ToJson(this);
      cout << "}";
      break;
    case etArray:
      target << "[";
      property->ActiveObject()->ToJson(this);
      target << "]\n";
      break;
    }
    property++;
    if (property->type == etEndOfList)
      break;
    target << ",\n";
  }
}


} //jsn
