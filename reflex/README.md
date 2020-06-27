# Reflex
C++14 Reflection Library.

## Requirements
* C++14
* Boost.PP includes

## Compiling
The library is `headers only`, so there is nothing to build and to link with your program.

Instead, you just have to include `<reflex/reflex>` in your source files and everything will work as expected.

However, you may need to install the library includes somewhere in your computer: for this, we use `cmake`. Just follow these steps:

```bash
git clone https://github.com/Cylix/reflex.git
cd reflex
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/destination/path ..
make install
```

## Examples and Tests
If you want to build the examples or the tests, follow these steps:

```bash
git clone https://github.com/Cylix/reflex.git
cd reflex
./install_deps.sh # necessary only for building tests
mkdir build
cd build
cmake .. -DBUILD_TESTING=true # tests
cmake .. -DBUILD_EXAMPLES=true # examples
cmake .. -DBUILD_TESTING=true -DBUILD_EXAMPLES=true # tests and examples
make -j
```

## Register class
In order to enable reflection, we must explicitly "register" our class.

For this, `reflex` provides a simple macro: `REGISTER_CLASS_FUNCTIONS(type, list of functions)`.
* `Type` is the type on which we want to enable reflection
* `List of functions` is the list of member functions (non-static and static member functions) we want to be able to call during reflection. These functions can have any signature (there is no restriction concerning the return value type, the number of parameters and the types of these parameters).

The list of functions must be formatted in a specific way: `(fct1)(fct2)(fct3)...`.
This format is the format used by Boost.PP (which is used in this library) and must be respected in order to compile.

For example, if we have the following class:

```cpp
class SomeClass {
public:
    int fct(void) {
        std::cout << "fct()" << std::endl;
    }

    static void other_fct(const std::string&, float) {
        std::cout << "other_fct()" << std::endl;
    }
};
```

We just need to do `REGISTER_CLASS_FUNCTIONS(SomeClass, (fct)(other_fct))` and this will register `SomeClass` and its two member functions for reflection.

For namespaced classes, `REGISTER_CLASS_FUNCTIONS` can take an extra parameter: `REGISTER_CLASS_FUNCTIONS((some_namespace)(nested_namespace), SomeClass, (fct)(other_fct))`.
Namespaces use the Boost.PP syntax with parentheses.
The macro must be called outside any namespaces in order to avoid conflicts.

## Register C-Style functions
Reflection is not only limited to class member functions. It can be also used on C-style functions.

For this, `reflex` provides a simple macro: `REGISTER_FUNCTIONS(list of functions)`.
* `List of functions` is the list of functions we want to be able to call during reflection. These functions can have any signature (there is no restriction concerning the return value type, the number of parameters and the types of these parameters).

The list of functions must be formatted in a specific way: `(fct1)(fct2)(fct3)...`.
This format is the format used by Boost.PP (which is used in this library) and must be respected in order to compile.

For example, if we have the following class:

```cpp
int fct(void) {
    std::cout << "fct()" << std::endl;
}

void other_fct(const std::string&, float) {
    std::cout << "other_fct()" << std::endl;
}
};
```

We just need to do `REGISTER_FUNCTIONS((fct)(other_fct))` and this will register the two functions for reflection.

For namespaced functions, `REGISTER_FUNCTIONS` can take an extra parameter: `REGISTER_FUNCTIONS((some_namespace)(nested_namespace), (fct)(other_fct))`.
Namespaces use the Boost.PP syntax with parentheses.
The macro must be called outside any namespaces in order to avoid conflicts.

## Making Reflection
Each time we register a type and its member functions, it stores this type into the `reflection_manager` singleton class.

This class is the class which does the reflection. By calling `reflection_manager::invoke<RetVal, Params...>("class_name", "function_name", ...)`, this will call `class_name::function_name` (on a new object).

A facility function with a more elegant syntax is also provided: `reflection_maker<RetVal(Params...)>::invoke("class_name", "function_name", ...)`. It provides the std::function template syntax which is more readable.

If we take the previous example, by calling `reflex::reflection_maker<void(const std::string&, float)>::invoke("SomeClass", "other_fct", some_str, some_float);`, we will invoke `SomeClass::fct`.

`reflection_maker::invoke` is overloaded for C-Style functions: `reflection_maker<RetVal(Params...)>::invoke("function_name", ...)`.

## Making Reflection with instance
By default, reflection for member function is done on a new object.

It is however possible to make reflection on a custom object instance.

`reflex::reflection_maker<void(const std::string&, float)>::invoke(&some_obj, "SomeClass", "other_fct", some_str, some_float);` will invoke `SomeClass::other_fct` on some_obj instance.

This feature is available for pointer, std::shared_ptr and std::unique_ptr of the object.

# How does it work
`REGISTER_CLASS_FUNCTIONS` and `REGISTER_FUNCTIONS` macros are based on the `REGISTER_REFLECTABLE` macro
The `REGISTER_REFLECTABLE` macro uses variadic macro parameters and works with Boost.PP in order to iterate through the list of functions.

This macro will create a static object of type `reflectable<SomeClass>`.
For example `REGISTER_CLASS_FUNCTIONS(SomeClass, (fct)(other_fct))` will generates the following code:

```cpp
static reflectable<SomeClass> reflectable_SomeClass(
    "SomeClass",
    { "fct", &SomeClass::fct },
    { "other_fct", &SomeClass::other_fct }
);
```

Another example: `REGISTER_FUNCTIONS((fct)(other_fct))` will generates the following code:

```cpp
static reflectable<reflex::Void> reflectable_(
    "",
    { "fct", &fct },
    { "other_fct", &other_fct }
);
```


This generation is done at compile time but the registration is only effective at runtime at the beginning of the execution. Registering a class for reflection will impact on the compilation time and at program startup (when all static variables are initialized).

The constructor of a reflectable object registers itself into the reflectable_manager that we can use for reflection.

If you want to read more about the implementation of this library, you may be interested in an article I've written about it: [Reflection in C++14](http://blog.simon-ninon.fr/reflection-in-c-plus-plus-14/).

## Examples
Some examples are provided in this repositories:
* [reflection_non_member_function.cpp](examples/reflection_non_member_function.cpp) shows reflection for c-style functions and static member functions.
* [reflection_with_instance.cpp](examples/reflection_with_instance.cpp) provides an example of basic reflection on member functions.
* [reflection_without_instance.cpp](examples/reflection_without_instance.cpp) details how to make reflection on member functions with an existing object instance.

## To Do
| Type | Description | Priority | Status |
|------|-------------|----------|--------|
| Improvement | Improve test coverage | High | To Do |
| Improvement | Add full documentation | Moderate | To Do |
| Improvement | Clear syntax for templates (<ReturnType, Params...> -> <ReturnType(Params)> whenever it is possible) | Low | To Do |
| Improvement | Handling case of multiple REGISTER_FUNCTIONS (or multiple REGISTER_CLASS_FUNCTIONS for same type) in a single project | Moderate | To Do |
| Feature | Reflection for variables | Low | To Do |
| Feature | Reflection for overloads | Medium | To Do |

## Author
[Simon Ninon](http://simon-ninon.fr)
