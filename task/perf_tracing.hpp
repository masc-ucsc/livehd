#pragma once

#ifdef LIVEHD_PROFILING

#include "perfetto.h"

PERFETTO_DEFINE_CATEGORIES(perfetto::Category("core").SetDescription("Core functions"));

#else

#define TRACE_EVENT(category, name, ...) \
  do {                                   \
  } while (false)

#define TRACE_EVENT_BEGIN(category, name, ...) \
  do {                                         \
  } while (false)

#define TRACE_EVENT_END(category, ...) \
  do {                                 \
  } while (false)

#define TRACE_COUNTER(category, track, ...) \
  do {                                      \
  } while (false)

#endif
