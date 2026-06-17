//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// LHD_LOG — developer trace logging that is compiled out entirely in `-c opt`.
//
// Channels mirror the LiveHD block/pass tree. A channel is enabled per run via
//   lhd --set <channel>.log=<level>        (default: off — nothing is emitted)
// where <level> is off|error|warn|info|debug|trace. A channel sets the level
// for its whole dotted subtree; a more specific child channel overrides it
// (`--set upass.log=info --set upass.verifier.log=debug`). The channel
// vocabulary lives in core/log.cpp (kChannels) and is the single source both
// `lhd list log-channels` and the --set validator read.
//
//   LHD_LOG("upass.verifier", info, "resolved {} regs", n);
//
// In a release build (`-c opt`: LIVEHD_LOGGING is undefined, see core/BUILD)
// the macro expands to a do-while(false) no-op — its arguments are NOT
// evaluated and no code is emitted (the core/perf_tracing.hpp pattern). The
// runtime helpers (is_channel/parse_level/configure) stay defined in EVERY
// build so `--set ...log=...` validates identically regardless of the
// compilation mode (an opt binary just records the level and never logs).

#include <cstdint>
#include <format>
#include <string_view>
#include <vector>

namespace livehd::log {

enum class Level : uint8_t { off = 0, error, warn, info, debug, trace };

// "off|error|warn|warning|info|debug|trace" -> Level; sets ok=false on a bad name.
Level            parse_level(std::string_view name, bool& ok);
std::string_view level_name(Level lvl);

// Is `channel` a registered logging channel (a node in kChannels)?
bool is_channel(std::string_view channel);

// Every registered channel, in declaration order (for `lhd list log-channels`).
const std::vector<std::string_view>& channels();

// Record `channel = lvl` (one --set ...log= entry). The channel sets the level
// for itself and every descendant channel unless a more specific channel
// overrides it. Cheap and startup-only; recomputes the effective table.
void configure(std::string_view channel, Level lvl);

// Clear every configured level (back to all-off). For tests and a fresh re-init.
void reset();

// Stable per-channel id for a call site (cached in a static local by LHD_LOG).
// In a debug build it I()-asserts when `channel` is not registered (a typo).
int id(std::string_view channel);

// Is the call site's channel id enabled at or above `lvl`?
bool enabled(int channel_id, Level lvl);

// Emit one already-formatted record to stderr (used only by the macro).
void emit(std::string_view channel, std::string_view level, std::string_view msg);

}  // namespace livehd::log

#ifdef LIVEHD_LOGGING
#define LHD_LOG(chan, lvl, ...)                                            \
  do {                                                                     \
    static const int _lhd_log_id = ::livehd::log::id(chan);                \
    if (::livehd::log::enabled(_lhd_log_id, ::livehd::log::Level::lvl)) {   \
      ::livehd::log::emit((chan), #lvl, std::format(__VA_ARGS__));          \
    }                                                                      \
  } while (false)
#else
#define LHD_LOG(chan, lvl, ...) \
  do {                          \
  } while (false)
#endif
