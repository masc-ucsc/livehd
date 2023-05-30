//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fcntl.h>
#include <unistd.h>

#include "iassert.hpp"
#include "err_tracker.hpp"

void err_tracker::logger(std::string_view text) {

  if (logger_fd<0) {
    logger_fd = open("logger_err.log", O_WRONLY|O_APPEND|O_CREAT, 0644);
    if (logger_fd<0) {
      fmt::print("ERROR: could not open logger_err.log file for logging [{}]\n", text);
      exit(-3);
    }
  }

  auto sz1 = write(logger_fd, text.data(), text.size());
  auto sz2 = write(logger_fd,"\n----\n", 6*sizeof(char));
  I(sz1);
  I(sz2);
}

