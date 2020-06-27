#pragma once

#include <map>
#include <memory>

#include "function/function.hpp"

namespace reflex {

//! reflectable<> base class
//! used for storing reflectable<> of different types in the same container
class reflectable_base {
public:
    //! ctor & dotr
    reflectable_base(void) = default;
    virtual ~reflectable_base(void) = default;

    //! copy ctor & assignment operator
    reflectable_base(const reflectable_base& reflectable) = default;
    reflectable_base& operator=(const reflectable_base& reflectable) = default;

public:
    //! returns the name of the registered reflectable class
    virtual const std::string& get_name(void) const = 0;

    //! get function by name
    virtual const function& get_function(const std::string& function_name) const = 0;

    //! return all functions
    virtual const std::map<std::string, function> get_functions(void) const = 0;

    //! is member function registered
    virtual bool is_registered(const std::string& function_name) const = 0;
};

} //! reflex
