## Tracing

### Build

Add `--define=profiling=1` to build arguments

```sh
bazel build --define=profiling=1 //...
```

### Profile

Perform the operations to trace, and the trace file is generated

> Trace file: `livehd.trace` in current working directory

### View trace

- Open [Perfetto Trace Viewer - ui.perfetto.dev](https://ui.perfetto.dev)
- Drag trace file into the viewer

### Add new events

Include `perf_tracing.hpp`

```cpp
#include "perf_tracing.hpp"
```

RAII

```cpp
{
    TRACE_EVENT(category, event);
    ...
}
```

Events that don't follow function scoping

```
TRACE_EVENT_BEGIN(category, event);
...
TRACE_EVENT_END(category);
```

Events with a runtime name

```cpp
(category, nullptr, [&name](perfetto::EventContext ctx) {
                        ctx.event()->set_name(name); 
                    }
);
```

### Add new categories

In `core/perf_tracing.hpp`, add to `PERFETTO_DEFINE_CATEGORIES`

```cpp
perfetto::Category(category).SetDescription(description)
```

### System mode

- Follow [Record traces on Linux - Building from source](https://perfetto.dev/docs/quickstart/linux-tracing#building-from-source)
- Add `data_sources {config {name: "track_event"}}` to `test/configs/scheduling.cfg`
- Run `tools/tmux -c test/configs/scheduling.cfg -C out/linux -n`

> Inside the tmux window, there will be a command to launch the tracing session

Perform the operations to trace while session is on, and the trace file is generated

> Trace file: `trace` in temporary directory

### References

- [https://perfetto.dev/docs/instrumentation/track-events](https://perfetto.dev/docs/instrumentation/track-events)
