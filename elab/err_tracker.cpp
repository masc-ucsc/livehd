 //  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "err_tracker.hpp"
#include "iassert.hpp"


void err_tracker::logger(const std::string &text) {
  const std::string &log_file = "logger_ErrMsg.log";
  
  FILE * f;
  f = fopen(log_file.c_str(), "a+");

  if (f == NULL) I(false, "Cannot open log_file");

  fprintf(f, "%s", text.c_str());
  fprintf(f, "\n----\n");
 // if(std::filesystem::exists(log_file)) {
 //   fmt::print("------logger.log exists ------ \n");
 //   ofile = fopen(log_file, "a");
 // } else {
 //   fmt::print("------Creating a new file. logger.log does not exist ------ \n");
 // }
  //fmt::print("{}", text);
  fclose(f);
}



void err_tracker::sot_logger(const std::string &text) {
  const std::string &log_file = "logger_TokSeq.log";
  
  FILE * f;
  f = fopen(log_file.c_str(), "a+");

  if (f == NULL) I(false, "Cannot open log_file");

  fprintf(f, "%s", text.c_str());
  fprintf(f, "\n----\n");
  
  fclose(f);
}

