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
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
#if 1
  ds_cfg->set_name("track_event");
#else
  ds_cfg->set_name("linux.ftrace");
  ds_cfg->set_target_buffer(0);

  perfetto::protos::gen::FtraceConfig ftrace_config;
  ftrace_config.add_ftrace_events("power/cpu_frequency");
  ftrace_config.add_ftrace_events("power/cpu_idle");
  ftrace_config.add_ftrace_events("power/suspend_resume");

  ds_cfg->set_ftrace_config_raw(ftrace_config.SerializeAsString());
#endif

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
