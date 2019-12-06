//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef LGBENCH_H
#define LGBENCH_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include <chrono>
#include <iostream>
#include <vector>
#include <sstream>

#include "likely.hpp"

class Lbench {
private:
  int parseLine(char *line) {
    // This assumes that a digit will be found and the line ends in " Kb".
    int         i = strlen(line);
    const char *p = line;
    while(*p < '0' || *p > '9')
      p++;
    line[i - 3] = '\0';
    i           = atoi(p);
    return i;
  }

  int getValue() { // Note: this value is in KB!
    FILE *file   = fopen("/proc/self/status", "r");
    int result = -1;
    char line[128];

    while(fgets(line, 128, file) != nullptr) {
      if(strncmp(line, "VmRSS:", 6) == 0) {
        result = parseLine(line);
        break;
      }
    }
    fclose(file);
    return result;
  }

  static inline bool perf_setup   = false;
  static inline bool perf_enabled = false;
  pid_t perf_pid=0;

protected:
  typedef std::chrono::time_point<std::chrono::system_clock> Time_Point;
  struct Time_Sample {
    Time_Point  tp;
    int         mem;
    std::string name;
  };
  std::vector<Time_Sample> record;
  const std::string        sample_name;
  Time_Point               start_time;
  int                      start_mem;
  bool                     end_called;

  void perf_start(const std::string& name) {
    if (unlikely(!perf_setup)) {
      const char *do_perf = getenv("LGBENCH_PERF");
      if (do_perf==nullptr) {
        perf_enabled = false;
      }else if (do_perf[0]=='0') {
        perf_enabled = false;
      }else{
        perf_enabled = true;
      }
      if (access("/usr/bin/perf", X_OK) == -1) {
        std::cerr << "ERROR: lgbench could not find /usr/bin/perf in system\n";
        exit(-3);
      }

      perf_setup=true;
    }
    if (!perf_enabled)
      return;

    std::string filename = name.find(".data") == std::string::npos ? (name + ".data") : name;

    std::stringstream s;
    s << getpid();
    perf_pid = fork();
    if (perf_pid == 0) {
      auto fd=open("/dev/null",O_RDWR);
      dup2(fd,1);
      dup2(fd,2);
      exit(execl("/usr/bin/perf","perf","record","-o",filename.c_str(),"-p",s.str().c_str(),nullptr));
    }
  }

  void perf_stop() {
    if (!perf_enabled)
      return;
    // Kill profiler
    kill(perf_pid,SIGINT);
    waitpid(perf_pid,nullptr,0);
  }

public:
  explicit Lbench(const std::string &name)
      : sample_name(name) {
    end_called = false;
    start();
    perf_start(name);
  };

  ~Lbench() {
    if(end_called)
      return;
    end();
    perf_stop();
  }

  void start() {
    start_time = std::chrono::system_clock::now();
    start_mem  = getValue();
  }

  void sample(const std::string &name) {
    Time_Sample s;
    s.tp   = std::chrono::system_clock::now();
    s.mem  = getValue();
    s.name = name;

    record.push_back(s);
  }

  void end() {

    Time_Point tp = std::chrono::system_clock::now();

    Time_Point prev     = start_time;
    int        prev_mem = start_mem;

    for(const auto &s : record) {
      std::chrono::duration<double> t = s.tp - prev;

      if(s.name == "end" && t.count() < 0.01)
        continue;

      int m;
      if(prev_mem > s.mem)
        m = prev_mem - s.mem;
      else
        m = s.mem - prev_mem;

      std::cerr << s.name << " in " << t.count() << " secs, " << m << " KB delta " << s.mem << "KB abs\n";

      prev     = s.tp;
      prev_mem = s.mem;
    }

    std::chrono::duration<double> t = tp - start_time;
    std::cerr << sample_name << " in " << t.count() << " secs total\n";
  }
};
#endif
