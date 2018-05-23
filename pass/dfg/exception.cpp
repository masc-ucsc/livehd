#ifndef PYROPE_EXCEPTION_H_
#define PYROPE_EXCEPTION_H_

#include <string>
#include <stdexcept>

namespace Pyrope
{
  class Exception : public std::runtime_error
  {
    public:
      Exception(const std::string &m) : std::runtime_error(msg) { }

    protected:
      std::string msg;
  };

  class TypeError : public Exception
  {
    public:
      TypeError(const std::string &msg) : Exception(msg) { }
  };

  const std::string DEBUG_MSG = "Not implemented yet";

  class DebugError : public Exception
  {
    public:
      DebugError() : Exception(DEBUG_MSG) { }
      DebugError(const std::string &msg) : Exception(DEBUG_MSG + ": " + msg) { }
  };

  class LogicError : public Exception
  {
    public:
      LogicError(const std::string &msg) : Exception(msg) { }
  };
}

#endif
