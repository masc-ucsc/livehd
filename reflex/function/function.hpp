#pragma once

#include <memory>
#include <string>

#include "function/callable_base.hpp"
#include "function/callable_with_instance.hpp"
#include "function/callable_without_instance.hpp"
#include "reflection_exception.hpp"

namespace reflex {

class function {
public:
    struct functions_container {
        std::shared_ptr<callable_base> callable_without_obj;
        std::shared_ptr<callable_base> callable_with_obj;
    };

public:
    //! ctor & dtor
    function(void) : m_name(""), m_functions{ nullptr, nullptr } {}
    function(const std::string& name, const functions_container& functions) : m_name(name), m_functions(functions) {}
    ~function(void) = default;

    //! copy ctor & assignment operator
    function(const function&) = default;
    function& operator=(const function&) = default;

    //! member function call on new instance
    //! c-style functions
    //! static member functions
    template <typename ReturnType, typename... Params>
    ReturnType invoke(Params... params) {
        if (not m_functions.callable_without_obj)
            throw reflection_exception("Invalid callable_base pointer (nullptr) for function " + m_name);

        return invoke<callable_without_instance<ReturnType(Params...)>, ReturnType, Params...>(m_functions.callable_without_obj, params...);
    }

    //! member function call on given instance
    template <typename Type, typename ReturnType, typename... Params>
    ReturnType invoke(Type* obj, Params... params) {
        if (not m_functions.callable_with_obj)
            throw reflection_exception("Function " + m_name + " can't be called with object");

        return invoke<callable_with_instance<Type, ReturnType(Params...)>, ReturnType, Type*, Params...>(m_functions.callable_with_obj, obj, params...);
    }

    //! member function call on given instance
    template <typename Type, typename ReturnType, typename... Params>
    ReturnType invoke(const std::shared_ptr<Type>& obj, Params... params) {
        if (not m_functions.callable_with_obj)
            throw reflection_exception("Function " + m_name + " can't be called with object");

        return invoke<callable_with_instance<Type, ReturnType(Params...)>, ReturnType, Type*, Params...>(m_functions.callable_with_obj, obj.get(), params...);
    }

    //! member function call on given instance
    template <typename Type, typename ReturnType, typename... Params>
    ReturnType invoke(const std::unique_ptr<Type>& obj, Params... params) {
        if (not m_functions.callable_with_obj)
            throw reflection_exception("Function " + m_name + " can't be called with object");

        return invoke<callable_with_instance<Type, ReturnType(Params...)>, ReturnType, Type*, Params...>(m_functions.callable_with_obj, obj.get(), params...);
    }

private:
    template <typename RealFunctionType, typename ReturnType, typename... Params>
    ReturnType invoke(const std::shared_ptr<callable_base>& function, Params... params) {
        auto function_with_real_type = std::dynamic_pointer_cast<RealFunctionType>(function);

        if (not function_with_real_type)
            throw reflection_exception("Invalid function signature for function " + m_name);

        return (*function_with_real_type)(params...);
    }

private:
    std::string m_name;
    functions_container m_functions;
};

} //! reflex
