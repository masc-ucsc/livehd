//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lbench.hpp"
#include "thread_pool.hpp"

int Lbench::getValue() const {  // Note: this value is in KB!
#if 1
  return 0; // disable memory stats
#else
#ifdef __linux__
  FILE *file   = fopen("/proc/self/status", "r");
  int   result = -1;
  char  line[128];

  if (file == nullptr)
    return 0;

  while (fgets(line, 128, file) != nullptr) {
    if (strncmp(line, "VmRSS:", 6) == 0) {
      result = parseLine(line);
      break;
    }
  }
  fclose(file);
  return result;
#else
  task_vm_info_data_t    vmInfo;
  mach_msg_type_number_t count        = TASK_VM_INFO_COUNT;
  kern_return_t          kernelReturn = task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vmInfo, &count);
  if (kernelReturn == KERN_SUCCESS) {
    return (int)vmInfo.phys_footprint / (1024 * 1024);
  }
  return 0;
#endif
#endif
}

void Lbench::perf_start(const std::string &name) {
  if (unlikely(!perf_setup)) {
    const char *do_perf = getenv("LGBENCH_PERF");
    if (do_perf == nullptr) {
      perf_enabled = false;
    } else if (do_perf[0] == '0') {
      perf_enabled = false;
    } else {
      perf_enabled = true;
    }
    if (perf_enabled && access("/usr/bin/perf", X_OK) == -1) {
      std::cerr << "ERROR: lgbench could not find /usr/bin/perf in system\n";
      exit(-3);
    }

    perf_setup = true;
  }
  if (!perf_enabled)
    return;

  std::string filename = name.find(".data") == std::string::npos ? (name + ".data") : name;

  std::stringstream s;
  s << getpid();
  perf_pid = fork();
  if (perf_pid == 0) {
    auto fd = open("/dev/null", O_RDWR);
    dup2(fd, 1);
    dup2(fd, 2);
    exit(execl("/usr/bin/perf", "perf", "record", "-o", filename.c_str(), "-p", s.str().c_str(), nullptr));
  }
}

void Lbench::perf_stop() {
  if (!perf_enabled)
    return;
  // Kill profiler
  kill(perf_pid, SIGINT);
  waitpid(perf_pid, nullptr, 0);
}


void Lbench::start() {
  start_time = std::chrono::system_clock::now();
  start_mem  = getValue();
  linux.start();

  static bool first = true;
  if (first) {
    global_start_time = start_time;
    first = false;
  }
}

void Lbench::sample(const std::string &name) {
  std::vector<uint64_t> stats(4);
  linux.sample(stats);

  Time_Sample s;
  s.tp          = std::chrono::system_clock::now();
  s.mem         = getValue();
  s.ncycles     = stats[0];
  s.ninst       = stats[1];
  s.nbr_misses  = stats[2];
  s.nmem_misses = stats[3];
  s.name        = name;

  record.push_back(s);
}

double Lbench::get_secs() const {
  Time_Point tp = std::chrono::system_clock::now();

  std::chrono::duration<double> t    = tp - start_time;
  return t.count();
}

void Lbench::end() {
  if (end_called)
    return;
  end_called = true;

  Time_Point tp = std::chrono::system_clock::now();

  Time_Point prev     = start_time;
  //int        prev_mem = start_mem;

  for (const auto &s : record) {
    std::chrono::duration<double> t = s.tp - prev;

    if (s.name == "end" && t.count() < 0.01)
      continue;

#if 0
    int m;
    if (prev_mem > s.mem)
      m = prev_mem - s.mem;
    else
      m = s.mem - prev_mem;

    std::cerr << s.name << " secs=" << t.count();
    if (s.ncycles) {
      std::cerr
        << ":IPC=" << ((double)s.ninst) / (s.ncycles+1)
        << ":BR MPKI=" << ((double)s.nbr_misses*1000 ) / (s.ninst+1)
        << ":L2 MPKI=" << ((double)s.nmem_misses*1000) / (s.ninst+1);
    }
    std::cerr << m << ":KB delta " << s.mem << "KB abs\n";

    prev_mem = s.mem;
#endif

    prev     = s.tp;
  }
  std::vector<uint64_t> stats(4);
  linux.stop(stats);

  std::chrono::duration<double> t = tp - start_time;
  std::stringstream             sstr;

  std::chrono::duration<double> from_sec = start_time - global_start_time;
  std::chrono::duration<double> to_sec   = tp         - global_start_time;

  sstr << std::setw(20) << std::left << sample_name
    << " tid=" << std::setw(4) << Thread_pool::get_task_id()
    << " secs=" << std::setw(15) << t.count() << " IPC=" << std::setw(10)
    << ((double)stats[1]) / (stats[0] + 1) << " BR_MPKI=" << std::setw(10) << ((double)stats[2] * 1000) / (stats[1] + 1)
    << " L2_MPKI=" << std::setw(10) << ((double)stats[3] * 1000) / (stats[1] + 1)
    << " from=" << from_sec.count() << " to=" << to_sec.count()
    << "\n";
  // std::cerr << sstr.str();

  int tfd = ::open("lbench.trace", O_CREAT | O_RDWR | O_APPEND, 0644);

  if (tfd >= 0) {
    auto sz = write(tfd, sstr.str().data(), sstr.str().size());
    (void)sz;
    close(tfd);
  }
}
