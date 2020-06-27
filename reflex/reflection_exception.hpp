#pragma once

#include <stdexcept>

namespace reflex {

class reflection_exception : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
    using std::runtime_error::what;
};

} //! reflex
