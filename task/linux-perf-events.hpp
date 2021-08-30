// based on https://github.com/WojciechMula/toys/blob/master/000helpers/linux-perf-events.h
#pragma once
#if 0

#include <sys/mman.h>
#include <asm/unistd.h>        // for __NR_perf_event_open
#include <linux/perf_event.h>  // for perf event constants
#include <sys/ioctl.h>         // for ioctl
#include <unistd.h>            // for syscall

#include <cerrno>   // for errno
#include <cstring>  // for memset
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <vector>
#include <cassert>

template <int TYPE = PERF_TYPE_HARDWARE>
class LinuxEvents {
  static inline int64_t         *mmap_addr=nullptr;
  static inline std::mutex       lgs_mutex;
  static inline int              perf_fd = -1;    // Shared across calls
  static inline bool             working = true;  // did not try or tried and works
  perf_event_attr                attribs{};
  size_t                         num_events{};
  std::vector<int64_t>           result_start{};
  std::vector<int64_t>           result_stop{};
  std::vector<int64_t>           result_total{};
  std::vector<uint64_t>          ids{};
  static inline int              nesting=0;

public:
  LinuxEvents() {}

  void setup(const std::vector<int> &config_vec) {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    if (!working)
      return;
    ++nesting;
    if (nesting>1)
      return;

    assert(perf_fd<0);

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
      perf_fd = static_cast<int>(syscall(__NR_perf_event_open, &attribs, pid, cpu, group, flags));
      if (perf_fd<0) {
        working = false;  // silent case when perf counters do not exist
        // std::cerr << "perf_event_open failed (no perf counters)\n";
      }
      ioctl(perf_fd, PERF_EVENT_IOC_ID, &ids[i++]);
      if (group == -1) {
        group = perf_fd;
      }
    }
    result_start.resize(num_events * 2 + 1);
    result_stop.resize(num_events * 2 + 1);
    result_total.resize(num_events * 2 + 1);

    if (ioctl(perf_fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) == -1) {
      report_error("linux-perf-events ioctl(PERF_EVENT_IOC_RESET)");
    }

    if (ioctl(perf_fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == -1) {
      report_error("linux-perf-events ioctl(PERF_EVENT_IOC_ENABLE)");
    }

    void *addr = mmap(NULL, 4096, PROT_READ, MAP_SHARED, perf_fd, 0);
    assert(addr != MAP_FAILED);
    mmap_addr = static_cast<int64_t *>(addr);
  }

  void close() {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    if (perf_fd<0)
      return;

    assert(working);

    --nesting;
    if (nesting)
      return;

    if (ioctl(perf_fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) == -1) {
      report_error("linux-perf-events ioctl(PERF_EVENT_IOC_DISABLE)");
    }

    ::close(perf_fd);
    perf_fd = -1;
  }

  ~LinuxEvents() { close(); }
  inline void start() {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    if (perf_fd<0)
      return;

#if 0
    lseek(perf_fd, SEEK_SET, 0);
    if (read(perf_fd, result_start.data(), result_start.size() * 8) == -1) {
      report_error("linux-perf-events::start");
    }
#else
    for (uint32_t i = 0; i < result_start.size(); ++i) {
      result_start[i] = mmap_addr[i];
    }
#endif

    for (uint32_t i = 0; i < result_start.size(); ++i) {
      result_total[i] -= result_stop[i] - result_start[i]; // substract the distance last stop
    }
  }

  inline void stop(std::vector<uint64_t> &results) {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    if (perf_fd<0)
      return;

#if 0
    lseek(perf_fd, SEEK_SET, 0);
    if (read(perf_fd, result_stop.data(), result_stop.size() * 8) == -1) {
      report_error("linux-perf-events::stop");
    }
#else
    for (uint32_t i = 0; i < result_start.size(); ++i) {
      result_stop[i] = mmap_addr[i];
    }
#endif

    for (uint32_t i = 1; i < result_start.size(); ++i) {
      result_total[i] += result_stop[i] - result_start[i];
    }

    // our actual results are in slots 1,3,5, ... of this structure
    // we really should be checking our ids obtained earlier to be safe
    for (uint32_t i = 1; i < result_total.size(); i += 2) {
      results[i / 2] = result_total[i];
    }
  }

  inline void sample(std::vector<uint64_t> &results) {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    if (perf_fd<0)
      return;

#if 0
    lseek(perf_fd, SEEK_SET, 0);
    if (read(perf_fd, result_stop.data(), result_stop.size() * 8) == -1) {
      report_error("linux-perf-events::stop");
    }
#else
    for (uint32_t i = 0; i < result_start.size(); ++i) {
      result_stop[i] = mmap_addr[i];
    }
#endif

    for (uint32_t i = 1; i < result_start.size(); ++i) {
      result_total[i] += result_stop[i] - result_start[i];
    }

    // our actual results are in slots 1,3,5, ... of this structure
    // we really should be checking our ids obtained earlier to be safe
    for (uint32_t i = 1; i < result_total.size(); i += 2) {
      results[i / 2] = result_total[i];
    }
  }

  bool is_working() const { return working; }

private:
  void report_error(const std::string &context) {
    if (working)
      std::cerr << (context + ": " + std::string(strerror(errno))) << std::endl;
    working = false;
    perf_fd = -1;
  }
};
#else
#define PERF_TYPE_HARDWARE 0
template <int TYPE = PERF_TYPE_HARDWARE>
class LinuxEvents {
public:
  void        setup(const std::vector<int>& /* config_vec */) {}
  inline void start() {}

  inline void sample(std::vector<uint64_t>& /* results */) {}

  inline void stop(std::vector<uint64_t>& /* results */) {}

  bool is_working() const { return false; }

  inline void close() {}
};
#endif
