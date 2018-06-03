#ifndef PYROPE_EXCEPTION_H_
#define PYROPE_EXCEPTION_H_

#include <stdexcept>
#include <string>

class Exception : public std::runtime_error {
public:
  Exception(const std::string &m) : std::runtime_error(msg) {}

protected:
  std::string msg;
};

class Type_Error : public Exception {
public:
  Type_Error(const std::string &msg) : Exception(msg) {}
};

const std::string DEBUG_MSG = "Not implemented yet";

class Debug_Error : public Exception {
public:
  Debug_Error() : Exception(DEBUG_MSG) {}
  Debug_Error(const std::string &msg) : Exception(DEBUG_MSG + ": " + msg) {}
};

class Logic_Error : public Exception {
public:
  Logic_Error(const std::string &msg) : Exception(msg) {}
};

#endif
