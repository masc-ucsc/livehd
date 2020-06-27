#pragma once

#include <map>
#include <string>
#include <memory>

#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/seq/for_each_i.hpp>
#include <boost/preprocessor/punctuation/comma_if.hpp>

#include "function/callable_with_instance.hpp"
#include "function/callable_without_instance.hpp"
#include "function/function.hpp"
#include "reflection_manager.hpp"
#include "reflection_exception.hpp"

namespace reflex {

//! empty class
//! used to template reflectable<> class when registering c-functions
class Void;

//! reflectable class
//! contains information about registered class (class name, member functions name and member functions pointers)
//! inherits from reflectable_base in order to store reflectables of different types in the same container
//!
//! This class registered itself in the reflection_manager and is used by the manager during reflection
template <typename Type = Void>
class reflectable : public reflectable_base {
public:
    //! ctor & dotr
    template <typename... Fcts>
    reflectable(const std::string& name, Fcts... fcts)
    : m_name(name) {
        reflection_manager::get_instance().register_reflectable(*this);
        register_function(fcts...);
    }

    ~reflectable(void) = default;

    //! copy ctor & assignment operator
    reflectable(const reflectable& reflectable) = default;
    reflectable& operator=(const reflectable& reflectable) = default;

public:
    //! unpack Fcts... variadic template
    template <typename Fct, typename... Fcts>
    void register_function(const std::pair<std::string, Fct>& fct, Fcts... fcts) {
        register_function(fct);
        register_function(fcts...);
    }

    //! register_function for member functions
    //! in order to have an equivalent of std::bind with default std::placeholders, we use closures
    //! this closure has the same signature than the registered function and simply forwards its parameters to the function
    //! the closure is stored inside a function<> object
    template <typename ReturnType, typename... Params>
    void register_function(const std::pair<std::string, ReturnType (Type::*)(Params...) const>& fct) {
        register_function(std::pair<std::string, ReturnType (Type::*)(Params...)>{
            fct.first,
            reinterpret_cast<ReturnType (Type::*)(Params...)>(fct.second)
        });
    }

    //! register_function for member functions
    //! in order to have an equivalent of std::bind with default std::placeholders, we use closures
    //! this closure has the same signature than the registered function and simply forwards its parameters to the function
    //! the closure is stored inside a function<> object
    template <typename ReturnType, typename... Params>
    void register_function(const std::pair<std::string, ReturnType (Type::*)(Params...)>& fct) {
        auto f_without_instance = [fct] (Params... params) -> ReturnType {
            return (Type().*fct.second)(std::ref(params)...);
        };

        auto f_with_instance = [fct] (Type* obj, Params... params) -> ReturnType {
            return std::bind(fct.second, obj, std::ref(params)...)();
        };

        m_functions[fct.first] = function{
            m_name + "::" + fct.first, {
                std::make_shared<callable_without_instance<ReturnType(Params...)>>(f_without_instance),
                std::make_shared<callable_with_instance<Type, ReturnType(Params...)>>(f_with_instance)
            }
        };
    }

    //! register_function for non member functions (static)
    //! same behavior as explained above
    template <typename ReturnType, typename... Params>
    void register_function(const std::pair<std::string, ReturnType (*)(Params...)>& fct) {
        auto f_without_instance = [fct] (Params... params) -> ReturnType {
            return (fct.second)(std::ref(params)...);
        };

        m_functions[fct.first] = function{
            m_name + "::" + fct.first, {
                std::make_shared<callable_without_instance<ReturnType(Params...)>>(f_without_instance),
                nullptr
            }
        };
    }

    //! get function by name
    const function& get_function(const std::string& function_name) const {
        if (not is_registered(function_name))
            throw reflection_exception("Function " + m_name + "::" + function_name + " is not registered");

        return m_functions.at(function_name);
    }

    //! return functions list
    const std::map<std::string, function> get_functions(void) const {
        return m_functions;
    }

    //! is member function registered
    bool is_registered(const std::string& function_name) const {
        return m_functions.count(function_name);
    }

    //! return reflectable class name
    const std::string& get_name(void) const {
        return m_name;
    }

private:
    //! name of the reflectable
    //! this is by this name that the reflection will operate
    std::string m_name;

    //! list of functions for this object
    //! associate function name to a function object
    std::map<std::string, function> m_functions;
};

} //! reflex
