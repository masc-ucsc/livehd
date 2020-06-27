#pragma once

#include <list>
#include <string>
#include <algorithm>

#include "function/function.hpp"
#include "reflectable/reflectable_base.hpp"
#include "reflection_exception.hpp"

namespace reflex {

//! singleton class
//! this class makes the reflection
//! it stores reflectable object that are used for reflection
class reflection_manager {
public:
    //! singleton
    static reflection_manager& get_instance(void) {
        static reflection_manager instance;
        return instance;
    }

    //! register a new reflectable
    //! this reflectable will later can be used for reflection
    void register_reflectable(const reflectable_base& reflectable) {
        m_types.push_back(std::cref(reflectable));
    }

    //! make reflection
    template <typename ReturnType, typename... Params>
    ReturnType invoke(const std::string& class_name, const std::string& function_name, Params... params) {
        auto reflectable = find_reflectable(class_name);
        auto fct = reflectable.get().get_function(function_name);

        return fct.template invoke<ReturnType, Params...>(params...);
    }

    //! make reflection
    template <typename Type, typename ReturnType, typename... Params>
    ReturnType invoke(Type* obj, const std::string& class_name, const std::string& function_name, Params... params) {
        auto reflectable = find_reflectable(class_name);
        auto fct = reflectable.get().get_function(function_name);

        return fct.template invoke<Type, ReturnType, Params...>(obj, params...);
    }

    //! make reflection
    template <typename Type, typename ReturnType, typename... Params>
    ReturnType invoke(const std::shared_ptr<Type>& obj, const std::string& class_name, const std::string& function_name, Params... params) {
        auto reflectable = find_reflectable(class_name);
        auto fct = reflectable.get().get_function(function_name);

        return fct.template invoke<Type, ReturnType, Params...>(obj, params...);
    }

    //! make reflection
    template <typename Type, typename ReturnType, typename... Params>
    ReturnType invoke(const std::unique_ptr<Type>& obj, const std::string& class_name, const std::string& function_name, Params... params) {
        auto reflectable = find_reflectable(class_name);
        auto fct = reflectable.get().get_function(function_name);

        return fct.template invoke<Type, ReturnType, Params...>(obj, params...);
    }

    const reflectable_base& get_class(const std::string& class_name) {
        return find_reflectable(class_name).get();
    }

    const reflectable_base& get_functions(void) const {
        return find_reflectable("").get();
    }

    const std::vector<std::string> all_names() const {
      std::vector<std::string> names;
      for (const auto& v : m_types) {
        names.emplace_back(v.get().get_name());
      }

      return names;
    }

private:
    const std::reference_wrapper<const reflectable_base>& find_reflectable(const std::string& class_name) const {
        auto it = std::find_if(m_types.begin(), m_types.end(), [class_name](const auto& type) {
            return type.get().get_name() == class_name;
        });

        if (it == m_types.end())
            throw reflection_exception("Class " + class_name + " is not registered");

        return *it;
    }

private:
    //! ctor & dtor
    reflection_manager(void) = default;
    ~reflection_manager(void) = default;

    //! copy ctor & assignment operator
    reflection_manager(const reflection_manager& manager) = default;
    reflection_manager& operator=(const reflection_manager& manager) = default;

private:
    //! registered types
    std::list<std::reference_wrapper<const reflectable_base>> m_types;
};

} //! reflex
