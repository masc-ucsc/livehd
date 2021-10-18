//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>
#ifndef __linux__
#include <mach/mach.h>
#endif

#include "likely.hpp"
#include "linux-perf-events.hpp"

class Lbench {
private:
  LinuxEvents<PERF_TYPE_HARDWARE> linux;

  int parseLine(char *line) const {
    // This assumes that a digit will be found and the line ends in " Kb".
    int         i = strlen(line);
    const char *p = line;
    while (*p < '0' || *p > '9') p++;
    line[i - 3] = '\0';
    i           = atoi(p);
    return i;
  }

  int getValue() const;

  static inline bool perf_setup   = false;
  static inline bool perf_enabled = false;
  pid_t              perf_pid     = 0;

protected:
#ifdef __x86_64__
  using Time_Point=uint64_t;
#else
  typedef std::chrono::time_point<std::chrono::system_clock> Time_Point;
#endif

  struct Time_Sample {
    Time_Point  tp;
    size_t      ninst;
    size_t      ncycles;
    size_t      nbr_misses;
    std::string name;
  };
  std::vector<Time_Sample> record;
  const std::string        sample_name;
  static inline Time_Point global_start_time;
  Time_Point               start_time;
  bool                     end_called;

  void perf_start(const std::string &name);
  void perf_stop();

#ifdef __x86_64__
  uint64_t get_cycles() const {
    unsigned int lo;
    unsigned int hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return (((uint64_t)hi << 32) | lo)>>10; // >>10 to decrease size /1K
  }
#else
  Time_Point get_cycles() const {
    return std::chrono::system_clock::now();
  }
#endif

public:
  explicit Lbench(const std::string &name) : sample_name(name) {
    end_called = false;
    perf_start(name);

    const std::vector<int> evts{
#if 0
        PERF_COUNT_HW_CPU_CYCLES,
        PERF_COUNT_HW_INSTRUCTIONS,
        PERF_COUNT_HW_BRANCH_MISSES,
        PERF_COUNT_HW_CACHE_REFERENCES
#endif
    };
    linux.setup(evts);

    start();
  }

  ~Lbench() {
    if (end_called)
      return;
    end();
    perf_stop();
    linux.close();
  }

  void start();
  void sample(const std::string &name);
  double get_secs() const;

  void end();
  static inline int tfd = -1;
};

