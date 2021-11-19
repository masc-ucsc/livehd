/*
Author: Farzaneh Rabiei, GitHub: https://github.com/rabieifk
*/

#ifndef JSON_COMPOSER_H
#define JSON_COMPOSER_H

#include <vector>
#include <iostream>
#include <ostream>

using namespace std;

namespace jsn
{

class JsonComposer;
class Object{
    public:
    virtual void ToJson(const JsonComposer* jcm) const =0;
    virtual const char* JsonKey() { return NULL;}
};

typedef vector<Object*> ObjVec;

enum ElementType{
  etEndOfList,
  etString,
  etInt,
  etUInt,
  etObject,
  etVectorAsObject,
  etBool,
  etFloat,
  etArray,
};

struct JsonElement
{
  ElementType type;
  const char* key;
  union
  {
    long  i;
    ulong u;
    float f;
    bool  b;
    const char*   str;
    const Object* obj;
    const ObjVec* ovec;
    const vector<int>*  ivec;
  } value;
 
  JsonElement& operator[](const char* k){ key = k; return *this;}
  JsonElement& operator=(int v)     { value.i = v; type = etInt; return *this; }
  JsonElement& operator=(long v)    { value.i = v; type = etInt; return *this; }
  JsonElement& operator=(short v)   { value.i = v; type = etInt; return *this; }
  JsonElement& operator=(ulong v)   { value.u = v; type = etUInt; return *this; }
  JsonElement& operator=(ushort v)  { value.u = v; type = etInt; return *this; }
  JsonElement& operator=(uint v)    { value.u = v; type = etInt; return *this; }
  JsonElement& operator=(float v)   { value.f = v; type = etFloat; return *this; }
  JsonElement& operator=(bool v)    { value.b = v; type = etBool; return *this; }
  JsonElement& operator=(const char* v) {value.str = v; type = etString; return *this; }
  JsonElement& operator=(const Object* obj) {value.obj = obj; type = etObject; return *this; }
};

class JsonComposer
{
  ostream& target;

  template<typename T, typename Func> void WriteEach(const vector<T>* fvec, Func GetModel) const {
      JsonElement model[] = { {}, {} };
      if (fvec && !fvec->empty())
          for (auto itr = fvec->cbegin() ; ;) {
              GetModel(&*itr, &model[0]);
              Write(model);
              if (++itr == fvec->cend())
                  break;
              else
                  target << ", ";
          }
  }

  public:
    JsonComposer(ostream& target_medium) : target(target_medium) { }

    void Write(const JsonElement* el_list) const;

    template<typename T> void WriteVector(const vector<T>* vec) {
        target << "[" ;
        WriteEach(vec, [](T const* ent, JsonElement* key_val) { *key_val=*ent; });
        target << "]" ;
    }

    void WriteVectorAsObj(const ObjVec* ovec) const;
};

} // namespace jsn


#endif
