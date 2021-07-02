// https://github.com/WojciechMula/toys/blob/master/000helpers/linux-perf-events.h
#pragma once
#ifdef __linux__

#include <asm/unistd.h>        // for __NR_perf_event_open
#include <linux/perf_event.h>  // for perf event constants
#include <sys/ioctl.h>         // for ioctl
#include <unistd.h>            // for syscall

#include <cerrno>   // for errno
#include <cstring>  // for memset
#include <iostream>
#include <stdexcept>
#include <vector>
#include <mutex>

template <int TYPE = PERF_TYPE_HARDWARE>
class LinuxEvents {
  inline static std::mutex lgs_mutex;
  static inline std::vector<int>     fds;  // Shared across calls
  static inline bool    working = true;
  perf_event_attr       attribs{};
  size_t                num_events{};
  std::vector<uint64_t> temp_result_vec{};
  std::vector<uint64_t> ids{};

public:
  LinuxEvents() {}

  void setup(const std::vector<int> &config_vec) {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    if (!working)
      return;
    if (!fds.empty())
      return;

    memset(&attribs, 0, sizeof(attribs));
    attribs.type           = TYPE;
    attribs.size           = sizeof(attribs);
    attribs.disabled       = 1;
    attribs.exclude_kernel = 1;
    attribs.exclude_hv     = 1;

    attribs.sample_period     = 0;
    attribs.read_format       = PERF_FORMAT_GROUP | PERF_FORMAT_ID;
    const int           pid   = 0;   // the current process
    const int           cpu   = -1;  // all CPUs
    const unsigned long flags = 0;

    int group  = -1;  // no group
    num_events = config_vec.size();
    ids.resize(config_vec.size());
    uint32_t i = 0;
    for (auto config : config_vec) {
      attribs.config = config;
      int fd         = static_cast<int>(syscall(__NR_perf_event_open, &attribs, pid, cpu, group, flags));
      if (fd == -1) {
        working = false;  // silent case when perf counters do not exist
        // std::cerr << "perf_event_open failed (no perf counters)\n";
      }
      ioctl(fd, PERF_EVENT_IOC_ID, &ids[i++]);
      if (group == -1) {
        group = fd;
      }
      fds.emplace_back(fd);
    }
    temp_result_vec.resize(num_events * 2 + 1);
  }

  void close() {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    if (fds.empty())
      return;
    for(auto &fd:fds)
      ::close(fd);
    fds.clear();
  }

  ~LinuxEvents() { close(); }
  inline void start() {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    if (fds.empty())
      return;

    if (ioctl(fds[0], PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) == -1) {
      report_error("linux-perf-events ioctl(PERF_EVENT_IOC_RESET)");
    }

    if (ioctl(fds[0], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == -1) {
      report_error("linux-perf-events ioctl(PERF_EVENT_IOC_ENABLE)");
    }
  }

  inline void stop(std::vector<uint64_t> &results) {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    if (fds.empty())
      return;

    if (ioctl(fds[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) == -1) {
      report_error("linux-perf-events ioctl(PERF_EVENT_IOC_DISABLE)");
    }

    lseek(fds[0], SEEK_SET, 0);
    if (read(fds[0], temp_result_vec.data(), temp_result_vec.size() * 8) == -1) {
      report_error("linux-perf-events read_stop");
    }
    // our actual results are in slots 1,3,5, ... of this structure
    // we really should be checking our ids obtained earlier to be safe
    for (uint32_t i = 1; i < temp_result_vec.size(); i += 2) {
      results[i / 2] = temp_result_vec[i];
    }
  }

  inline void sample(std::vector<uint64_t> &results) {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    if (fds.empty())
      return;
    if (read(fds[0], temp_result_vec.data(), temp_result_vec.size() * 8) == -1) {
      report_error("linux-perf-events read_sample");
    }
    // our actual results are in slots 1,3,5, ... of this structure
    // we really should be checking our ids obtained earlier to be safe
    for (uint32_t i = 1; i < temp_result_vec.size(); i += 2) {
      results[i / 2] = temp_result_vec[i];
    }
  }

  bool is_working() const { return working; }

private:
  void report_error(const std::string &context) {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    if (working)
      std::cerr << (context + ": " + std::string(strerror(errno))) << std::endl;
    working = false;
    fds.clear();
  }
};
#else
#define PERF_TYPE_HARDWARE 0
template <int TYPE = PERF_TYPE_HARDWARE>
class LinuxEvents {
public:
  void        setup(const std::vector<int> &config_vec) {}
  inline void start() {}

  inline void sample(std::vector<uint64_t> &results) {}

  inline void stop(std::vector<uint64_t> &results) {}

  bool is_working() const { return false; }

  inline void close() {}
};
#endif
