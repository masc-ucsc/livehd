// https://github.com/WojciechMula/toys/blob/master/000helpers/linux-perf-events.h
#pragma once
#ifdef __linux__

#include <asm/unistd.h>       // for __NR_perf_event_open
#include <linux/perf_event.h> // for perf event constants
#include <sys/ioctl.h>        // for ioctl
#include <unistd.h>           // for syscall

#include <cerrno>  // for errno
#include <cstring> // for memset
#include <stdexcept>

#include <iostream>
#include <vector>

template <int TYPE = PERF_TYPE_HARDWARE>
class LinuxEvents {
  static inline int fd=-1; // Shared across calls
  static inline bool working = true;
  perf_event_attr attribs{};
  size_t num_events{};
  std::vector<uint64_t> temp_result_vec{};
  std::vector<uint64_t> ids{};

public:
  LinuxEvents() {}

  void setup(const std::vector<int> &config_vec) {
    if (!working)
      return;
    if (fd!=-1)
      return;

    memset(&attribs, 0, sizeof(attribs));
    attribs.type = TYPE;
    attribs.size = sizeof(attribs);
    attribs.disabled = 1;
    attribs.exclude_kernel = 1;
    attribs.exclude_hv = 1;

    attribs.sample_period = 0;
    attribs.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;
    const int pid = 0;  // the current process
    const int cpu = -1; // all CPUs
    const unsigned long flags = 0;

    int group = fd; // no group
    num_events = config_vec.size();
    ids.resize(config_vec.size());
    uint32_t i = 0;
    for (auto config : config_vec) {
      attribs.config = config;
      fd = static_cast<int>(syscall(__NR_perf_event_open, &attribs, pid, cpu, group, flags));
      if (fd == -1) {
        std::cerr << "i:" << i << " val:" << config << "\n";
        report_error("perf_event_open");
      }
      ioctl(fd, PERF_EVENT_IOC_ID, &ids[i++]);
      if (group == -1) {
        group = fd;
      }
    }
    fd = group;

    temp_result_vec.resize(num_events * 2 + 1);
  }

  void close() {
    if (fd == -1) return;
    ::close(fd);
    fd = -1;
  }

  ~LinuxEvents() {
    close();
  }
  inline void start() {
    if (fd == -1)
      return;

    if (ioctl(fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) == -1) {
      report_error("ioctl(PERF_EVENT_IOC_RESET)");
    }

    if (ioctl(fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == -1) {
      report_error("ioctl(PERF_EVENT_IOC_ENABLE)");
    }
  }

  inline void stop(std::vector<uint64_t> &results) {
    if (fd == -1)
      return;

    if (ioctl(fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) == -1) {
      report_error("ioctl(PERF_EVENT_IOC_DISABLE)");
    }

    lseek(fd, SEEK_SET, 0);
    if (read(fd, temp_result_vec.data(), temp_result_vec.size() * 8) == -1) {
      report_error("read_stop");
    }
    // our actual results are in slots 1,3,5, ... of this structure
    // we really should be checking our ids obtained earlier to be safe
    for (uint32_t i = 1; i < temp_result_vec.size(); i += 2) {
      results[i / 2] = temp_result_vec[i];
    }
  }

  inline void sample(std::vector<uint64_t> &results) {
    if (fd == -1)
      return;
    if (read(fd, temp_result_vec.data(), temp_result_vec.size() * 8) == -1) {
      report_error("read_sample");
    }
    // our actual results are in slots 1,3,5, ... of this structure
    // we really should be checking our ids obtained earlier to be safe
    for (uint32_t i = 1; i < temp_result_vec.size(); i += 2) {
      results[i / 2] = temp_result_vec[i];
    }
  }

  bool is_working() const {
    return working;
  }

private:
  void report_error(const std::string &context) {
    if (working)
      std::cerr << (context + ": " + std::string(strerror(errno))) << std::endl;
    working = false;
    fd = -1; // disable all the APIs
  }
};
#else
#define PERF_TYPE_HARDWARE 0
template <int TYPE = PERF_TYPE_HARDWARE>
class LinuxEvents {
public:
  void setup(const std::vector<int> &config_vec) {}
  inline void start() { }

  inline void sample(std::vector<uint64_t> &results) { }

  inline void stop(std::vector<uint64_t> &results) { }

  bool is_working() const { return false; }

  inline void close() {}
};
#endif
