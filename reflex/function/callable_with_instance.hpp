#pragma once

#include <functional>

#include "function/callable_base.hpp"

namespace reflex {

template <typename T, typename U>
class callable_with_instance;

//! callable_with_instance class
//! contains an std::function corresponding to the class template
//!
//! simple wrapper of std::function, but inheriting from callable_base
//! useful for storing std::function of different types in the same container
//!
//! uses partial template specialization for std::function like syntax (function<void(int)>)
template <typename Type, typename ReturnType, typename... Params>
class callable_with_instance<Type, ReturnType(Params...)> : public callable_base {
public:
    callable_with_instance(const std::function<ReturnType(Type*, Params...)>& call_on_given_instance)
      : m_fct(call_on_given_instance) {}

    ~callable_with_instance(void) = default;

    callable_with_instance(const callable_with_instance&) = default;
    callable_with_instance& operator=(const callable_with_instance&) = default;

public:
    ReturnType operator()(Type* obj, Params... params) {
        return m_fct(obj, params...);
    }

private:
    std::function<ReturnType(Type*, Params...)> m_fct;
};

} //! reflex
