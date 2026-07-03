#ifdef LIVEHD_PROFILING

#include "perf_tracing.hpp"

#include <fcntl.h>

#include <csignal>
#include <exception>

PERFETTO_TRACK_EVENT_STATIC_STORAGE();

static const char*                               trace_path = "livehd.trace";
static std::unique_ptr<perfetto::TracingSession> tracing_session;
static int                                       tracing_fd;

// A break (^C / kill) must still yield a readable trace: finalize the session,
// then re-raise with the default action so the exit status stays
// signal-accurate. Perfetto's Stop is not strictly async-signal-safe, but the
// alternative is losing the whole trace; the periodic write_into_file flush
// below bounds the loss to the last flush period even on SIGKILL.
static void tracing_signal_handler(int sig) {
  stop_tracing();
  std::signal(sig, SIG_DFL);
  std::raise(sig);
}

void start_tracing() {
  perfetto::TracingInitArgs args;
  perfetto::TraceConfig     cfg;

  args.backends |= perfetto::kInProcessBackend;

  perfetto::Tracing::Initialize(args);
  perfetto::TrackEvent::Register();

  cfg.add_buffers()->set_size_kb(32768);
  // Drain the in-memory buffer into the trace file every 2s so a mid-run
  // break/stop keeps everything gathered so far (a >1h compile killed at the
  // 59-minute mark used to write NOTHING — Stop only ran at process exit).
  cfg.set_write_into_file(true);
  cfg.set_file_write_period_ms(2000);
  // Matching flush period: without it the UI/trace_processor must load the
  // whole file into memory (config_write_into_file_no_flush health warning).
  cfg.set_flush_period_ms(2000);
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

  std::signal(SIGINT, tracing_signal_handler);
  std::signal(SIGTERM, tracing_signal_handler);
}

void stop_tracing() {
  if (tracing_session != nullptr) {
    perfetto::TrackEvent::Flush();
    tracing_session->StopBlocking();
    close(tracing_fd);
    tracing_session.reset();  // idempotent: the signal path and Trace_guard may both call
  }
}

#endif
