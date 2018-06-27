//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef PYROPE_EXCEPTION_H_
#define PYROPE_EXCEPTION_H_

#include <stdexcept>
#include <string>

#if 0

class Exception : public std::runtime_error {
public:
  Exception(const std::string &m) : std::runtime_error(msg) {}

protected:
  std::string msg;
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
#endif
