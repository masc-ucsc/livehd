#pragma once

#include <string>

#include "reflectable/reflectable.hpp"
#include "reflection_manager.hpp"
#include "reflectable/macros.hpp"

namespace reflex {

template <typename T>
struct reflection_maker;

//! partial template specialization is not available for functions
//! so we can't have an std::function like syntax for reflection_manager::make_reflection<>
//! we must make reflection_manager::make_reflection<void, int, int> and can't do reflection_manager::make_reflection<void(int, int)>
//! this is not really convenient...
//!
//! we get around of this limitation by wrapping this function call inside a simple struct
//! this struct is partially specialized
//! this way, we can do make_reflection<void(int, int)>::invoke(...)
template <typename ReturnType, typename... Params>
struct reflection_maker<ReturnType(Params...)> {
    static ReturnType invoke(const std::string& class_name, const std::string& function_name, Params... params) {
        return reflection_manager::get_instance().invoke<ReturnType, Params...>(class_name, function_name, params...);
    }

    template <typename Type>
    static ReturnType invoke(Type* obj, const std::string& class_name, const std::string& function_name, Params... params) {
        return reflection_manager::get_instance().invoke<Type, ReturnType, Params...>(obj, class_name, function_name, params...);
    }

    template <typename Type>
    static ReturnType invoke(const std::shared_ptr<Type>& obj, const std::string& class_name, const std::string& function_name, Params... params) {
        return reflection_manager::get_instance().invoke<Type, ReturnType, Params...>(obj, class_name, function_name, params...);
    }

    template <typename Type>
    static ReturnType invoke(const std::unique_ptr<Type>& obj, const std::string& class_name, const std::string& function_name, Params... params) {
        return reflection_manager::get_instance().invoke<Type, ReturnType, Params...>(obj, class_name, function_name, params...);
    }

    static ReturnType invoke(const std::string& function_name, Params... params) {
        return reflection_manager::get_instance().invoke<ReturnType, Params...>("", function_name, params...);
    }
};

} //! reflex
