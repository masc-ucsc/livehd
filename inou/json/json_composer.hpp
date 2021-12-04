/*
Author: Farzaneh Rabiei, GitHub:https://github.com/rabieifk
*/
#ifndef JSON_COMPOSER_H
#define JSON_COMPOSER_H

#include <vector>
#include <iostream>
#include <ostream>
#include <cstring>

using namespace std;

namespace jsn
{
    
class JsonComposer;
struct JsonElement;

struct Object
{
    virtual void ToJson(const JsonComposer* jcm) const=0;
    virtual const char* JsonKey() const { return NULL;}
};

template <class T>
struct Batch : public jsn::Object 
{
    const vector<T>* data;
    Batch(const vector<T>* d) {data = d;}
    void ToJson(const JsonComposer* jcm) const;
    virtual void WriteItem(const T* item, JsonElement* model, const JsonComposer* jcm) const=0;
};

template <class T>
struct Array : public Batch<T> 
{
  Array(const vector<T>* d) : Batch<T>(d) {}
  void WriteItem(const T* item, JsonElement* model, const JsonComposer* jcm) const;
};

template <class T>
struct VectorAsObject : public Batch<T>
{
    VectorAsObject(const vector<T>* d) : Batch<T>(d) {}
    void WriteItem(const T* item, JsonElement* model, const JsonComposer* jcm) const;
};

enum ElementType{
  etEndOfList,
  etString,
  etInt,
  etUInt,
  etBool,
  etFloat,
  etObject,
  etArray,
};

struct JsonElement
{
  ElementType type;
  const char* key;
  union JValue {
    long  i;
    ulong u;
    double f;
    bool  b;
    const char*   str; 
    const Object* obj;
    char _embedded_batch[sizeof(Array<int>)];
  } value;

  JsonElement() {type = etEndOfList;}
  template<class T> JsonElement(const char* k, T val){key = k; *this = val; }

  JsonElement& operator[](const char* k){ key = k; return *this;}
  JsonElement& operator[](const Object* obj){ key = obj->JsonKey(); return *this;}
  JsonElement& operator[](const Object& obj){ key = obj.JsonKey(); return *this;}
  
  JsonElement& operator=(int v)     { value.i = v; type = etInt; return *this; }
  JsonElement& operator=(long v)    { value.i = v; type = etInt; return *this; }
  JsonElement& operator=(short v)   { value.i = v; type = etInt; return *this; }
  JsonElement& operator=(ulong v)   { value.u = v; type = etUInt; return *this; }
  JsonElement& operator=(ushort v)  { value.u = v; type = etInt; return *this; }
  JsonElement& operator=(uint v)    { value.u = v; type = etInt; return *this; }
  JsonElement& operator=(float v)   { value.f = v; type = etFloat; return *this; }
  JsonElement& operator=(double v)  { value.f = v; type = etFloat; return *this; }
  JsonElement& operator=(bool v)    { value.b = v; type = etBool; return *this; }
  JsonElement& operator=(const char* v) {value.str = v; type = etString; return *this; }
  JsonElement& operator=(const Object& obj) {return (*this = &obj); }
  JsonElement& operator=(const Object* obj)                       {type = etObject; SetObjectPtr(obj); return *this; }
  template <class T>  JsonElement& operator=(const Array<T>* arr) {type = etArray; SetObjectPtr(arr);  return *this; }
  template <class T>  JsonElement& operator=(const vector<T>* vec) {
    type = etArray;
    auto tmp = Array {vec};
    memcpy((void*)value._embedded_batch, &tmp, sizeof(tmp));
    return *this; 
  }
  template <class T>  JsonElement& operator=(const VectorAsObject<T>& vaobj) {
    type = etObject;
    memcpy((void*)value._embedded_batch, &vaobj, sizeof(vaobj));
    return *this; 
  }
  void SetObjectPtr(const Object* obj) { value.obj = obj; ((int64_t*)&value.obj)[1]=-1; /*indicates '.obj' is active*/ }
  const Object* ActiveObject() const { return ((int64_t*)&value.obj)[1]==-1 ? value.obj : (Object*)&value._embedded_batch ; }
};

class JsonComposer
{
  ostream& target;
  
  template <class T>  friend class Batch;

  public:
    JsonComposer(ostream& target_medium) : target(target_medium) { }
    void Write(const JsonElement* el_list) const;  
};

template <class T> void jsn::Batch<T>::ToJson(jsn::JsonComposer const* jcm) const {
  JsonElement model[] = { {}, {} };
  for(auto itr=data->cbegin() ; ; ) {
    WriteItem(&(*itr), &model[0], jcm);
    if(++itr == data->cend())
      break;
    else
      jcm->target << ", ";      
  }
}

template <class T> void jsn::Array<T>::WriteItem(const T* item, JsonElement* model, const JsonComposer* jcm) const {
  model[0] = *item;
  model[0].key = NULL;
  jcm->Write(model);
}

template <class T> void jsn::VectorAsObject<T>::WriteItem(const T* item, JsonElement* model, const JsonComposer* jcm) const {
  model[0][*item] = *item;
  jcm->Write(model);
}

} // namespace jsn


#endif