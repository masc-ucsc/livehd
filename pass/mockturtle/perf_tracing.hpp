#pragma once

#include <string>

#define TRACE_EVENT(category, name, lambda) lambda(perfetto::EventContext())

namespace perfetto {
struct EventContext {
  struct Event {
    void set_name(const std::string& name) {}
  };
  Event* event() { return &evt; }
  Event  evt;
};
}  // namespace perfetto
