/*
  Author: Farzaneh Rabiei, GitHub: https://github.com/rabieifk
*/

#ifndef JSON_COMPOSER_H
#define JSON_COMPOSER_H

#include <cstring>
#include <iostream>
#include <ostream>
#include <vector>

using namespace std;

namespace jsn {

class JsonComposer;
struct JsonElement;

struct Object {
  virtual void        ToJson(const JsonComposer* jcm) const = 0;
  virtual const char* JsonKey() const { return nullptr; }
};

template <typename T>
struct Batch : public Object
{
  using IterT = typename T::const_iterator;
  using ItemType = typename std::iterator_traits<IterT>::value_type;

  const T* data;
  Batch(const T* d) {data = d;}
  void ToJson(const JsonComposer* jcm) const { if (data) this->WriteEach(data->cbegin(), data->cend(), jcm); }

  void WriteEach(IterT start, IterT end, const JsonComposer* jcm) const;
  virtual void WriteItem(const ItemType* item, JsonElement* model, const JsonComposer* jcm) const = 0;
};

template <class T>
struct Array : public Batch<T> {
  using ItemType = typename std::iterator_traits<typename T::const_iterator>::value_type;
  
  Array(const T* d): Batch<T>(d) {}
  void WriteItem(const ItemType* item, JsonElement* model, const JsonComposer* jcm) const;
};

template <class T>
struct VectorAsObject : public Batch<T> {
  using ItemType = typename std::iterator_traits<typename T::const_iterator>::value_type;
  
  VectorAsObject(const T* d): Batch<T>(d) {}
  void WriteItem(const ItemType* item, JsonElement* model, const JsonComposer* jcm) const;
};

template <class T, typename Tlambda>
struct BatchWriter : public Batch<T> {
  using ItemT = typename std::iterator_traits<typename T::const_iterator>::value_type;
  using WriterCallback = void (*)(ItemT const*, JsonElement*, const JsonComposer*);

  WriterCallback write_item_callback;

  BatchWriter(const T* src, Tlambda item_writer) : Batch<T>(src) { write_item_callback = item_writer; }
  void WriteItem(ItemT const* item, JsonElement* model, const JsonComposer* jcm) const { write_item_callback(item, model, jcm); }
};

enum ElementType {
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

struct JsonElement {
  ElementType type;
  const char* key;
  union JValue {
    long               i;
    unsigned long      u;
    double             f;
    bool               b;
    const char*        str;
    const Object*      obj;
    const JsonElement* nested;
    char               _embedded_batch[sizeof(Array<vector<int>>)];
  } value;

  JsonElement() { type = etEndOfList; }
  template <class T>
  JsonElement(const char* k, T val) {
    key   = k;
    *this = val;
  }

  JsonElement& operator[](const char* k) {
    key = k;
    return *this;
  }
  JsonElement& operator[](const Object* obj) {
    key = obj->JsonKey();
    return *this;
  }
  JsonElement& operator[](const Object& obj) {
    key = obj.JsonKey();
    return *this;
  }

  JsonElement& operator=(int v) {
    value.i = v;
    type    = etInt;
    return *this;
  }
  JsonElement& operator=(long v) {
    value.i = v;
    type    = etInt;
    return *this;
  }
  JsonElement& operator=(short v) {
    value.i = v;
    type    = etInt;
    return *this;
  }
  JsonElement& operator=(unsigned long v) {
    value.u = v;
    type    = etUInt;
    return *this;
  }
  JsonElement& operator=(ushort v) {
    value.u = v;
    type    = etInt;
    return *this;
  }
  JsonElement& operator=(uint v) {
    value.u = v;
    type    = etInt;
    return *this;
  }
  JsonElement& operator=(float v) {
    value.f = v;
    type    = etFloat;
    return *this;
  }
  JsonElement& operator=(double v) {
    value.f = v;
    type    = etFloat;
    return *this;
  }
  JsonElement& operator=(bool v) {
    value.b = v;
    type    = etBool;
    return *this;
  }
  JsonElement& operator=(const char* v) {
    value.str = v;
    type      = etString;
    return *this;
  }
  JsonElement& operator=(const JsonElement* n) {
    type         = etNested;
    value.nested = n;
    return *this;
  }
  JsonElement& operator=(const Object& obj) { return (*this = &obj); }
  JsonElement& operator=(const Object* obj) {
    type = etObject;
    SetObjectPtr(obj);
    return *this;
  }
  template <class T>
  JsonElement& operator=(const Array<T>* arr) {
    type = etArray;
    SetObjectPtr(arr);
    return *this;
  }
  template <class T>
  JsonElement& operator=(const vector<T>* vec) {
    type = etArray;
    auto to_embed = Array {vec}; // create a jsn::Object wrapper for vector<T>
    void *array_ptr = (void*)&to_embed;
    memcpy((void*)&value, array_ptr, sizeof(to_embed)); // embed the wrapper
    return *this;
  }
  template <class T>
  JsonElement& operator=(const VectorAsObject<T>& vaobj) {
    type = etObject;
    void *vec_ptr = (void*)&vaobj;
    memcpy((void*)&value, &vec_ptr, sizeof(vaobj));
    return *this;
  }
  void SetObjectPtr(const Object* obj) {
    value.obj                 = obj;
    ((int64_t*)&value.obj)[1] = -1; /*indicates '.obj' is active*/
  }
  const Object* ActiveObject() const { return ((int64_t*)&value.obj)[1] == -1 ? value.obj : (Object*)&value._embedded_batch; }
};

class JsonComposer {
  ostream& target;
  mutable std::string indent;
  mutable const char* current_key;

  template <class T>
  friend struct Batch;  
  void WriteObject(const JsonElement* prop) const {
    auto obj = prop->ActiveObject();
    if (obj)
      obj->ToJson(this);
  }
  void WriteDelimiter() const {target << (current_key? ",\n" : ", ");}

public:
  JsonComposer(ostream& target_medium) : target(target_medium) { indent = ""; current_key = nullptr; }
  void Write(const JsonElement* el_list) const;
};

template <class T>
void jsn::Batch<T>::WriteEach(IterT start, IterT end, const JsonComposer* jcm) const {
  if (start == end)
    return;
  JsonElement model[] = {{}, {}};
  for (auto itr = start;;) {
    WriteItem(&(*itr), &model[0], jcm);
    if (++itr == end)
      break;
    else
      jcm->WriteDelimiter();
  }
}

template <class T>
void jsn::Array<T>::WriteItem(const ItemType* item, JsonElement* model, const JsonComposer* jcm) const {
  model[0]     = *item;
  model[0].key = nullptr;
  jcm->Write(model);
}

template <class T>
void jsn::VectorAsObject<T>::WriteItem(const ItemType* item, JsonElement* model, const JsonComposer* jcm) const {
  model[0][*item] = *item;
  jcm->Write(model);
}

}  // namespace jsn

#endif
