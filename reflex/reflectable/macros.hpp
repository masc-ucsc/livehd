#pragma once

#include "reflectable/reflectable.hpp"

//! convert value to string
#define __REFLEX_TO_STRING(val) #val


//! macro "overloading" (up to 3 args)
#define __REFLEX_GET_MACRO_3(_1, _2, _3, NAME, ...) NAME
//! macro "overloading" (up to 2 args)
#define __REFLEX_GET_MACRO_2(_1, _2, NAME, ...) NAME


//! open namespace (Boost.PP "callback")
#define __REFLEX_OPEN_NAMESPACE(r, data, namespace_) namespace namespace_ {
//! close namespace (Boost.PP "callback")
#define __REFLEX_CLOSE_NAMESPACE(r, data, namespace_) }


//! Build namespace from Boost.PP list
#define __REFLEX_GET_NAMESPACE(r, data, i, namespace_) BOOST_PP_IF(i, "::",)__REFLEX_TO_STRING(namespace_)
#define __REFLEX_BUILD_NAMESPACE(namespace_) BOOST_PP_SEQ_FOR_EACH_I( __REFLEX_GET_NAMESPACE,, namespace_ )


//! Boost.PP "callback" for each item
//! generates a pair associating function name to function pointer
#define __REFLEX_MAKE_REGISTERABLE_FUNCTION(r, type, i, function) \
BOOST_PP_COMMA_IF(i) std::make_pair(std::string(__REFLEX_TO_STRING(function)), &type::function)

#define __REFLEX_MAKE_REGISTERABLE_FUNCTION_WITHOUT_TYPE(r, type, i, function) \
BOOST_PP_COMMA_IF(i) std::make_pair(std::string(__REFLEX_TO_STRING(function)), &function)


//! Macro for generating static reflectable for namespaced class functions
#define __REFLEX_REGISTER_NAMESPACED_CLASS_FUNCTIONS(namespace_, type, functions) \
BOOST_PP_SEQ_FOR_EACH( __REFLEX_OPEN_NAMESPACE,, namespace_ ) \
    static reflex::reflectable<type> \
    reflectable_##type(__REFLEX_BUILD_NAMESPACE(namespace_) "::" #type, \
                       BOOST_PP_SEQ_FOR_EACH_I( __REFLEX_MAKE_REGISTERABLE_FUNCTION, type, functions )); \
BOOST_PP_SEQ_FOR_EACH( __REFLEX_CLOSE_NAMESPACE,, namespace_ )


//! Macro for generating static reflectable for namespaced functions
#define __REFLEX_REGISTER_NAMESPACED_FUNCTIONS(namespace_, functions) \
BOOST_PP_SEQ_FOR_EACH( __REFLEX_OPEN_NAMESPACE,, namespace_ ) \
    static reflex::reflectable<> \
    reflectable_##type(__REFLEX_BUILD_NAMESPACE(namespace_), \
                       BOOST_PP_SEQ_FOR_EACH_I( __REFLEX_MAKE_REGISTERABLE_FUNCTION_WITHOUT_TYPE,, functions )); \
BOOST_PP_SEQ_FOR_EACH( __REFLEX_CLOSE_NAMESPACE,, namespace_ )


//! Macro for generating static reflectable for class functions
#define __REFLEX_REGISTER_GLOBAL_CLASS_FUNCTIONS(type, functions) \
static reflex::reflectable<type> \
reflectable_##type(#type, BOOST_PP_SEQ_FOR_EACH_I( __REFLEX_MAKE_REGISTERABLE_FUNCTION, \
                                                   type, \
                                                   functions ));


//! Macro for generating static reflectable for functions
#define __REFLEX_REGISTER_GLOBAL_FUNCTIONS(functions) \
static reflex::reflectable<> \
reflectable_("", BOOST_PP_SEQ_FOR_EACH_I( __REFLEX_MAKE_REGISTERABLE_FUNCTION,, functions ));


//! Macros intended for library user
//! Generates a static reflectable object initialized with list of functions to register for reflection
//! Macros are overloaded in order to handle namespace if necessary
#define REGISTER_CLASS_FUNCTIONS(...) __REFLEX_GET_MACRO_3(__VA_ARGS__, __REFLEX_REGISTER_NAMESPACED_CLASS_FUNCTIONS, __REFLEX_REGISTER_GLOBAL_CLASS_FUNCTIONS)(__VA_ARGS__)
#define REGISTER_FUNCTIONS(...) __REFLEX_GET_MACRO_2(__VA_ARGS__, __REFLEX_REGISTER_NAMESPACED_FUNCTIONS, __REFLEX_REGISTER_GLOBAL_FUNCTIONS)(__VA_ARGS__)
