/*
Author: Farzaneh Rabiei, GitHub: https://github.com/rabieifk
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
  virtual void ToJson(const JsonComposer* jcm) const = 0;
  virtual const char* JsonKey() const { return NULL; }
};

template <typename T>
struct Range 
{
  using IterType = typename T::const_iterator;
  using ItemType = typename std::iterator_traits<IterType>::value_type;
  void WriteEach(IterType start, IterType end, const JsonComposer* jcm) const;
  virtual void WriteItem(const ItemType* item, JsonElement* model, const JsonComposer* jcm) const = 0;
};

template <class T, typename Tlambda>
struct RangeWriter : public Object, Range<T>
{
  using IterT = typename T::const_iterator;
  using ItemT = typename std::iterator_traits<IterT>::value_type;
 
  IterT start;
  IterT end;
  void (*write_item_callback)(const ItemT* , JsonElement* , const JsonComposer* );

  RangeWriter(T* src, Tlambda item_writer)      { start=src->cbegin(); end = src->cend(); write_item_callback = item_writer; }
  void ToJson(const JsonComposer* jcm) const    { this->WriteEach(start, end, jcm); }
  void WriteItem(const ItemT* item, JsonElement* model, const JsonComposer* jcm) const { write_item_callback(item, model, jcm); }
};

template <typename T>
struct Batch : public Object, Range<vector<T> > 
{
  const vector<T>* data;
  Batch(const vector<T>* d) {data = d;}
  void ToJson(const JsonComposer* jcm) const { if (data) this->WriteEach(data->cbegin(), data->cend(), jcm); }
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

template <class T, typename Tlambda>
struct BatchWriter : public Batch<T> 
{
  using  WriterCallback = void (*)(const T* , JsonElement* , const JsonComposer* );
  WriterCallback write_item_callback;

  BatchWriter(const vector<T>* src, Tlambda item_writer) : Batch<T>(src) { write_item_callback = item_writer; }
  void WriteItem(const T* item, JsonElement* model, const JsonComposer* jcm) const { write_item_callback(item, model, jcm); };
};

enum ElementType{
  etEndOfList,
  etString,
  etInt,
  etUInt,
  etBool,
  etFloat,
  etObject,
  etNested,
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
    const JsonElement* nested;
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
  JsonElement& operator=(const char* v) { value.str = v; type = etString; return *this; }
  JsonElement& operator=(const JsonElement* n)   { type = etNested; value.nested = n; return *this; }
  JsonElement& operator=(const Object& obj) {return (*this = &obj); }
  JsonElement& operator=(const Object* obj)                       { type = etObject; SetObjectPtr(obj); return *this; }
  template <class T>  JsonElement& operator=(const Array<T>* arr) { type = etArray; SetObjectPtr(arr);  return *this; }
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
  
  template <class T>  friend class Range;
  void WriteObject(const JsonElement* prop) const{
    auto obj = prop->ActiveObject();
    if (obj)
      obj->ToJson(this);
  }
  public:
    JsonComposer(ostream& target_medium) : target(target_medium) { }
    void Write(const JsonElement* el_list) const;  
};


template <class T> void jsn::Range<T>::WriteEach(IterType start, IterType end, const JsonComposer* jcm) const {
  if (start == end) 
    return;
  JsonElement model[] = { {}, {} };
  for(auto itr=start ; ; ) {
    WriteItem(&(*itr), &model[0], jcm);
    if(++itr == end)
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

