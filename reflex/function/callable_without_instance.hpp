#pragma once

#include <functional>

#include "function/callable_base.hpp"

namespace reflex {

template <typename T>
class callable_without_instance;

//! function class
//! contains an std::function corresponding to the class template
//!
//! simple wrapper of std::function, but inheriting from callable_base
//! useful for storing std::function of different types in the same container
//!
//! uses partial template specialization for std::function like syntax (function<void(int)>)
template <typename ReturnType, typename... Params>
class callable_without_instance<ReturnType(Params...)> : public callable_base {
public:
    callable_without_instance(const std::function<ReturnType(Params...)>& call_on_new_instance)
      : m_fct(call_on_new_instance) {}

    ~callable_without_instance(void) = default;

    callable_without_instance(const callable_without_instance&) = default;
    callable_without_instance& operator=(const callable_without_instance&) = default;

public:
    //! functor for calling internal std::function
    ReturnType operator()(Params... params) {
        return m_fct(params...);
    }

private:
    std::function<ReturnType(Params...)> m_fct;
};

} //! reflex
