#ifdef LIVEHD_PROFILING

#include "perf_tracing.hpp"

#include <fcntl.h>

PERFETTO_TRACK_EVENT_STATIC_STORAGE();

static const char*                               trace_path = "livehd.trace";
static std::unique_ptr<perfetto::TracingSession> tracing_session;
static int                                       tracing_fd;

void start_tracing() {
  perfetto::TracingInitArgs args;
  perfetto::TraceConfig     cfg;

  args.backends |= perfetto::kInProcessBackend;

  perfetto::Tracing::Initialize(args);
  perfetto::TrackEvent::Register();

  cfg.add_buffers()->set_size_kb(32768);
#if 0
  // FIXME: Does not seem to make a difference.
  // How to capture frequency??

  //auto* ds_cfg3 = cfg.add_data_sources()->mutable_config();
  //ds_cfg3->set_name("linux.sys_stats");

  auto* ds_cfg2 = cfg.add_data_sources()->mutable_config();
  ds_cfg2->set_name("linux.ftrace");

  perfetto::protos::gen::FtraceConfig ftrace_config;
  ftrace_config.add_ftrace_events("sched/sched_switch");
  ftrace_config.add_ftrace_events("power/suspend_resume");
  ftrace_config.add_ftrace_events("sched/sched_wakeup");
  ftrace_config.add_ftrace_events("sched/sched_wakeup_new");
  ftrace_config.add_ftrace_events("sched/sched_waking");
  ftrace_config.add_ftrace_events("power/cpu_frequency");
  ftrace_config.add_ftrace_events("power/cpu_idle");
  ftrace_config.add_ftrace_events("sched/sched_process_exit");
  ftrace_config.add_ftrace_events("sched/sched_process_free");
  ftrace_config.add_ftrace_events("task/task_newtask");
  ftrace_config.add_ftrace_events("task/task_rename");

  ds_cfg2->set_ftrace_config_raw(ftrace_config.SerializeAsString());

  //auto* ds_cfg4 = cfg.add_data_sources()->mutable_config();
  //ds_cfg4->set_name("linux.system_info");
  //ds_cfg4->set_target_buffer(0);
#endif
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("track_event");

  tracing_session = perfetto::Tracing::NewTrace();
  tracing_fd      = open(trace_path, O_RDWR | O_CREAT | O_TRUNC, 0600);
  if (tracing_fd == -1) {
    std::terminate();
  }

  tracing_session->Setup(cfg, tracing_fd);
  tracing_session->StartBlocking();
}

void stop_tracing() {
  if (tracing_session != nullptr) {
    perfetto::TrackEvent::Flush();
    tracing_session->StopBlocking();
    close(tracing_fd);
  }
}

#endif
