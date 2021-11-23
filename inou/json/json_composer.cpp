
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
    
void JsonComposer::Write(const JsonElement* el_list) const {
  if (el_list == NULL || el_list->type == etEndOfList)
    return;
  
  while(true) {
    if (el_list->key)
      target << '"' << el_list->key << "\": ";
    switch (el_list->type)
    {
    case etEndOfList: break;
    case etInt:     target << el_list->value.i; break;
    case etUInt:    target << el_list->value.u; break;
    case etBool:    target << (el_list->value.b ? "true" : "false"); break;
    case etFloat:   target << el_list->value.f; break;
    case etString:
      target << '"' << el_list->value.str << '"';
      break;
    case etObject: 
      target << "{";
      el_list->value.obj->ToJson(this);
      cout << "}";
      break;
    case etVectorAsObject: 
      WriteVectorAsObj(el_list->value.ovec);
      break;
    case etArray:
      target << "[";
      auto vec = el_list->value.ivec;
      if (vec->size()>0)
          for(auto it = vec->begin() ; ; ) {
            target << *it;
            if (++it != vec->end())
              target << ", ";
            else
                break;
          }
      target << "]\n";
      break;
    }
    el_list++;
    if (el_list->type != etEndOfList)
      target << ",\n";
    else
      break;
  }
}

void JsonComposer::WriteVectorAsObj(const ObjVec* objvec) const {
    target << "{" ;
    WriteEach(objvec, [](Object* const* vcell, JsonElement* jEl) { 
      (*jEl)[(*vcell)->JsonKey()]=*vcell; 
    });
    target << "}"<< endl ;
}



} //jsn
