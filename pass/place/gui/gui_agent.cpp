// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// The agent channel of the placement GUI (.claude/skills/livehd/agent_socket.md): a local process (an assistant, a script) asks
// questions and paints on the page. Highlights and the selection live in the session, so the page and the agent see the
// same thing; "critical" is answered by the flow thread through the timing provider.
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
#include <thread>
#include <unordered_set>

#include "core/hash_util.hpp"
#include "core/job_manager.hpp"
#include "gui_geometry.hpp"
#include "gui_scan.hpp"
#include "gui_tracks.hpp"
#include "gui_session.hpp"
#include "gui_worker.hpp"
#include "gui_wire.hpp"
#include "pass/place/macro_state.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "re2/re2.h"
#include "rapidjson/writer.h"

namespace livehd::placement::gui {

namespace {
std::string esc(const std::string& s) {
  std::string out;
  for (const unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          out += std::format("\\u{:04x}", unsigned(c));
        } else {
          out += char(c);
        }
    }
  }
  return out;
}
std::string num(double v) { return std::isfinite(v) ? std::format("{}", v) : "null"; }
std::string err_json(const std::string& why) { return std::format(R"({{"ok":false,"error":"{}"}})", esc(why)); }
std::string metric_delta(const rapidjson::Value& reference, const rapidjson::Value& candidate, const char* key) {
  const auto& a = reference[key];
  const auto& b = candidate[key];
  return a.IsNumber() && b.IsNumber() ? num(b.GetDouble() - a.GetDouble()) : "null";
}
bool same_endpoints(const rapidjson::Value& a, const rapidjson::Value& b) {
  return a["endpoints"] == b["endpoints"] && a["population"] == b["population"] && a["clock_population"] == b["clock_population"];
}
bool compatible_score(const rapidjson::Value& x, const rapidjson::Value& y, bool matched, bool same_routing) {
  bool        same_score = x["quality"]["estimator"] == y["quality"]["estimator"];
  const auto& left       = x["quality"]["components"];
  const auto& right      = y["quality"]["components"];
  if (left.Size() != right.Size()) {
    same_score = false;
  }
  for (unsigned i = 0; same_score && i < left.Size(); ++i) {
    for (const auto* key : {"loss", "weight", "half"}) {
      same_score &= left[i][key] == right[i][key];
    }
    if (left[i]["weight"].GetDouble() > 0) {
      const std::string_view loss(left[i]["loss"].GetString());
      if (loss == "negative_wns_ps" || loss == "negative_tns_ps") {
        same_score &= matched;
      } else if (loss == "routing_overflow" || loss == "overflow_bin_share") {
        same_score &= same_routing;
      }
    }
  }
  return same_score;
}
std::string timing_groups(const rapidjson::Value& a, const rapidjson::Value& b, const char* key) {
  if (!a.IsObject() || !b.IsObject() || !a.HasMember(key) || !b.HasMember(key)) {
    return "null";
  }
  const auto& left = a[key];
  const auto& right = b[key];
  std::set<std::string> names;
  for (const auto* groups : {&left, &right}) {
    for (const auto& item : groups->GetObject()) {
      names.insert(item.name.GetString());
    }
  }
  std::string rows;
  for (const auto& name : names) {
    const auto x = left.FindMember(name.c_str()), y = right.FindMember(name.c_str());
    const bool has_left = x != left.MemberEnd(), has_right = y != right.MemberEnd();
    const bool matched = has_left && has_right && same_endpoints(x->value, y->value);
    auto delta = [&](const char* metric) { return matched ? metric_delta(x->value, y->value, metric) : "null"; };
    rows += std::format(R"({}"{}":{{"coverage":"{}","reference_endpoints":{},"candidate_endpoints":{},"wns_ps":{},"tns_ps":{},"failing":{}}})",
                        rows.empty() ? "" : ",", esc(name), matched ? "matched" : "changed",
                        has_left ? x->value["endpoints"].GetUint() : 0, has_right ? y->value["endpoints"].GetUint() : 0,
                        delta("wns_ps"), delta("tns_ps"), delta("failing"));
  }
  return "{" + rows + "}";
}
// Only physical recipe inputs can be overridden by an exploration. Runtime,
// admission and output paths belong to the session. Values use the CLI spelling.
std::string replay_flags(const rapidjson::Value& request, Restart_request& checkpoint) {
  if (!request.HasMember("set")) {
    return {};
  }
  const auto& values = request["set"];
  if (!values.IsObject()) {
    return "set requires a physical option map";
  }
  static const std::set<std::string> allowed{"lhd.seed",
                                             "synth.place.aspect",
                                             "synth.place.utilization",
                                             "synth.place.area",
                                             "synth.place.die",
                                             "synth.place.core",
                                             "synth.place.density",
                                             "synth.place.halo",
                                             "synth.place.iterations"};
  for (const auto& field : values.GetObject()) {
    const std::string key(field.name.GetString(), field.name.GetStringLength());
    if (!allowed.contains(key)) {
      return "unsupported replay override " + key;
    }
    if (!field.value.IsString() || field.value.GetStringLength() != std::strlen(field.value.GetString())) {
      return "replay override values must be CLI strings";
    }
    if (!checkpoint.set.emplace(key, field.value.GetString()).second) {
      return "repeated replay override " + key;
    }
  }
  return {};
}
const char* kind_name(Kind k) {
  switch (k) {
    case Kind::cell: return "cell";
    case Kind::macro: return "macro";
    case Kind::fixed: return "fixed";
    case Kind::blockage: return "blockage";
    case Kind::io: return "io";
  }
  return "?";
}
// #rgb .. #rrggbbaa, or a CSS colour name; nothing else reaches the page's style.
bool valid_color(const std::string& c) {
  if (c.size() >= 4 && c.size() <= 9 && c[0] == '#') {
    return std::all_of(c.begin() + 1, c.end(), [](unsigned char ch) { return std::isxdigit(ch) != 0; });
  }
  return c.size() >= 3 && c.size() <= 20 && std::all_of(c.begin(), c.end(), [](unsigned char ch) { return std::isalpha(ch) != 0; });
}
// Full result details have a byte budget. Every closed experiment keeps its
// identifying fields, final status, measured metrics and cost for the life of the session.
std::string compact_history(const std::string& record) {
  rapidjson::Document d;
  d.Parse<rapidjson::kParseFullPrecisionFlag>(record.c_str());
  auto& a = d.GetAllocator();
  for (const std::string name : {"patch", "segment"}) {
    if (!d.HasMember(name.c_str()) || d[name.c_str()].IsNull()) {
      continue;
    }
    const auto& patch = d[name.c_str()];
    const auto commands = patch.IsArray() ? patch.Size() : 1u;
    rapidjson::StringBuffer serialized;
    rapidjson::Writer<rapidjson::StringBuffer> writer(serialized);
    patch.Accept(writer);
    d.AddMember(rapidjson::Value((name + "_commands").c_str(), a).Move(), commands, a);
    const auto hash = std::format("{:016x}", hash_util::fnv1a64(std::string_view(serialized.GetString(), serialized.GetSize())));
    d.AddMember(rapidjson::Value((name + "_hash").c_str(), a), rapidjson::Value(hash.c_str(), a), a);
    d.RemoveMember(name.c_str());
  }
  for (auto key : {"signature", "geometry", "set"}) {
    d.RemoveMember(key);
  }
  if (d.HasMember("annotation") && d["annotation"].IsString() && d["annotation"].GetStringLength() > 256) {
    const auto original = d["annotation"].GetStringLength();
    const auto* message = d["annotation"].GetString();
    size_t length = 256;
    while ((static_cast<unsigned char>(message[length]) & 0xc0) == 0x80) {
      --length;
    }
    const std::string prefix(message, length);
    d["annotation"].SetString(prefix.c_str(), prefix.size(), a);
    d.AddMember("annotation_bytes", original, a);
  }
  for (auto field : {"result", "last_measurement"}) {
    if (!d.HasMember(field) || !d[field].IsObject()) {
      continue;
    }
    auto& result = d[field];
    result.RemoveMember("signature");
    if (result.HasMember("summary") && result["summary"].IsObject()) {
      auto& timing = result["summary"]["timing"];
      if (timing.HasMember("endpoints") && timing["endpoints"].IsObject()) {
        // The number of clocks can grow with the design. Keep totals and the bounded path families.
        timing["endpoints"].RemoveMember("capture_clocks");
        timing["endpoints"].RemoveMember("clock_periods_ps");
        timing["endpoints"].RemoveMember("clock_periods_ns");
        timing["endpoints"].RemoveMember("constraint_issues");
      }
    }
    if (result.HasMember("validation") && result["validation"].IsObject()) {
      auto& validation = result["validation"];
      validation.RemoveMember("inputs");
      validation.RemoveMember("sha256");
      if (validation.HasMember("setup_timing") && validation["setup_timing"].IsObject()) {
        auto& timing = validation["setup_timing"];
        if (timing.HasMember("endpoints") && timing["endpoints"].IsObject()) {
          timing["endpoints"].RemoveMember("capture_clocks");
          timing["endpoints"].RemoveMember("clock_periods_ns");
        }
      }
    }
  }
  d.AddMember("details_evicted", true, a);
  rapidjson::StringBuffer output;
  rapidjson::Writer<rapidjson::StringBuffer> writer(output);
  d.Accept(writer);
  return {output.GetString(), output.GetSize()};
}
constexpr size_t kMaxNames = 200000;
}  // namespace

std::string Session::node_details_locked(size_t i) const {
  if (!scene_ || i >= scene_->nodes.size()) {
    return "null";
  }
  const auto&  n   = scene_->nodes[i];
  const double um  = scene_->dbu_per_um > 0 ? scene_->dbu_per_um : 1;
  const double x   = frame_ && i < frame_->x.size() ? double(frame_->x[i]) : 0, y = frame_ && i < frame_->y.size() ? double(frame_->y[i]) : 0;
  std::string  orientations;
  for (const auto& value : n.allowed_orientations) {
    orientations += std::format(R"({}"{}")", orientations.empty() ? "" : ",", esc(value));
  }
  std::string             locations;
  std::array<unsigned, 4> sides{};
  for (const auto& pin : n.signal_pins) {
    const std::array distance{pin.x, n.w - pin.x, pin.y, n.h - pin.y};
    ++sides[std::ranges::min_element(distance) - distance.begin()];
    locations += std::format(R"({}{{"pin":"{}","x":{},"y":{}}})",
                             locations.empty() ? "" : ",",
                             esc(pin.name),
                             num(x + pin.x),
                             num(y + pin.y));
  }
  auto out = std::format(
      R"({{"name":"{}","kind":"{}","register":{},"master":"{}","class":"{}","x_um":{},"y_um":{},"w_um":{},"h_um":{},"area_um2":{},"movable":{},"pinned":{},"pins":{},"orient":"{}","allowed_orientations":[{}],"signal_pins_dbu":[{}],"pin_sides":{{"left":{},"right":{},"bottom":{},"top":{}}}}})",
      esc(n.name),
      kind_name(n.kind),
      n.seq ? "true" : "false",
      esc(n.master),
      esc(n.klass),
      num(x / um),
      num(y / um),
      num(n.w / um),
      num(n.h / um),
      num(n.w * n.h / (um * um)),
      n.movable ? "true" : "false",
      pins_.contains(n.name) ? "true" : "false",
      n.pins,
      esc(n.orient),
      orientations,
      locations,
      sides[0],
      sides[1],
      sides[2],
      sides[3]);
  if (n.kind == Kind::macro || n.kind == Kind::fixed) {
    out.pop_back();  // the routing-track constraint a pinned or fixed pose must meet
    out += std::format(R"(,"track_check":"{}"}})", esc(track_describe(*scene_, i, n.orient)));
  }
  return out;
}

void Session::set_selection_locked(std::vector<std::string> names) {
  selection_count_ = names.size();
  if (names.size() > 5000) {
    names.resize(5000);
  }
  selection_ = std::move(names);
  ++sel_version_;
  bump_locked();
}

int Session::add_highlight_locked(const std::string& label, const std::string& color, const std::string& regex, const std::string& scope,
                                  std::vector<std::string> names, bool truncated) {
  std::erase_if(highlights_, [&](const Highlight& h) { return h.label == label; });  // the same label repaints
  Highlight h;
  h.id        = next_highlight_++;
  h.label     = label;
  h.color     = color;
  h.regex     = regex;
  h.scope     = scope;
  h.names     = std::move(names);
  h.truncated = truncated;
  highlights_.push_back(std::move(h));
  ++hl_version_;
  bump_locked();
  return highlights_.back().id;
}

std::string Session::highlights_json() const {
  std::lock_guard lk(m_);
  std::string     out = std::format(R"({{"v":{},"h":[)", hl_version_);
  for (size_t k = 0; k < highlights_.size(); ++k) {
    const auto& h = highlights_[k];
    std::string idx;
    size_t      count = 0;
    if (scene_) {
      for (const auto& name : h.names) {
        if (const auto it = scene_->index.find(name); it != scene_->index.end()) {
          idx += (count ? "," : "") + std::to_string(it->second);
          ++count;
        }
      }
    }
    out += std::format(R"({}{{"id":{},"label":"{}","color":"{}","regex":"{}","scope":"{}","total":{},"count":{},"truncated":{},"idx":[{}]}})", k ? "," : "",
                       h.id, esc(h.label), esc(h.color), esc(h.regex), esc(h.scope), h.names.size(), count, h.truncated ? "true" : "false", idx);
  }
  return out + "]}";
}

std::string Session::selection_json() const {
  std::lock_guard lk(m_);
  std::string     idx;
  size_t          count = 0;
  if (scene_) {
    for (const auto& name : selection_) {
      if (const auto it = scene_->index.find(name); it != scene_->index.end()) {
        idx += (count ? "," : "") + std::to_string(it->second);
        ++count;
      }
    }
  }
  return std::format(R"({{"v":{},"n":{},"idx":[{}]}})", sel_version_, selection_count_, idx);
}

std::string Session::Background::status() const {
  auto                row = worker ? worker->status()
                                   : std::format(R"({{"id":"{}","tier":"{}","state":"{}","reason":"{}"}})", id, tier, state, esc(reason));
  rapidjson::Document details;
  details.Parse(row.c_str());
  const double worker_s = details.HasMember("worker_s") ? details["worker_s"].GetDouble()
                          : admitted != decltype(admitted){} && finished != decltype(finished){}
                              ? std::chrono::duration<double>(finished - admitted).count() : 0;
  const auto   now       = std::chrono::steady_clock::now();
  const auto   queue_end = admitted != decltype(admitted){} ? admitted : finished != decltype(finished){} ? finished : now;
  const auto   queue_s   = std::chrono::duration<double>(queue_end - submitted).count();
  row.pop_back();
  row        += R"(,"set":{)";
  bool first  = true;
  for (const auto& [key, value] : checkpoint.set) {
    row   += std::format(R"({}"{}":"{}")", first ? "" : ",", esc(key), esc(value));
    first  = false;
  }
  row += "}";
  if (worker && !reason.empty()) {
    row += std::format(R"(,"worker_selection_reason":"{}")", esc(reason));
  }
  if (timing_call_limit >= 0) {
    row += std::format(R"(,"timing_call_limit":{})", timing_call_limit);
  }
  if (deadline_s) {
    row += std::format(R"(,"deadline_s":{})", deadline_s);
  }
  if (!until.empty()) {
    row += std::format(R"(,"until":"{}")", esc(until));
  }
  if (tier == "T4") {
    row += std::format(R"(,"placement_seconds":{},"placement_time_source":"{}")", placement_seconds, placement_time_source);
  }
  if (tier == "T3") {
    row += std::format(R"(,"repair_policy":{{"sizing":{},"buffering":{},"resynthesis":{}}},"budget":{{"repair_trials":{}}})",
                       repair_mode.contains("sizing"),
                       repair_mode.contains("buffering"),
                       repair_mode.contains("resynthesis"),
                       repair_limit);
  }
  row += std::format(
      R"(,"mode":"background","parent":"{}","label":"{}","client":"{}","annotation":"{}","baseline":"{}","baseline_frame":{},"gate":"{}","trace_seq":{},"queue_s":{},"elapsed_s":{},"active_cells":{},"scope_recipe":"{}","estimator":"{}","patch":{}}})",
      esc(parent),
      esc(label),
      esc(client),
      esc(annotation),
      baseline,
      frame,
      esc(checkpoint.label),
      checkpoint.seq,
      queue_s,
      queue_s + worker_s,
      active_cells,
      scope_key,
      esc(estimator),
      patch.empty() ? "[]" : patch);
  return row;
}

std::string Session::foreground_status_locked() const {
  auto status = foreground_->status();
  status.pop_back();
  std::string flags;
  for (const auto& [key, value] : foreground_flags_) {
    flags += std::format(R"({}"{}":"{}")", flags.empty() ? "" : ",", esc(key), esc(value));
  }
  return status + std::format(R"(,"baseline_frame":{},"label":"{}","annotation":"{}","client":"{}","set":{{{}}}}})",
                              foreground_frame_, esc(foreground_label_), esc(foreground_annotation_), esc(foreground_client_), flags);
}

std::string Session::snapshot_reason_locked() const {
  const bool ready = snapshot_owner_ && snapshot_baseline_ == baseline_identity_locked();
  return !cfg_.snapshot_available          ? "the configured timing engine requires spawned replay"
               : cfg_.explore_worker == "replay" ? "replay selected by configuration"
               : ready                           ? ""
               : snapshot_attempt_ == baseline_identity_locked() && !snapshot_reason_.empty()
                   ? snapshot_reason_
                   : "a frozen timing checkpoint is created on first use";
}
bool Session::queue_full_locked() const {
  return std::ranges::count_if(background_, [](const auto& pair) { return !pair.second.worker && pair.second.state == "queued"; })
         >= cfg_.explore_queue;
}
uint64_t Session::memory_prediction(size_t nodes, bool full_replay) const {
  const auto from_nodes = Job_manager::global().stats()[static_cast<unsigned>(Job_class::place_explore)].predicted_bytes(nodes);
  // Replay reconstructs the entire evaluator. The retained main evaluator supplies a measured lower estimate even
  // for the first replay, before the admission table has seen a completed worker for this design.
  return full_replay && cfg_.session_root_pid > 0 ? std::max(from_nodes, process_rss(cfg_.session_root_pid)) : from_nodes;
}
bool Session::memory_fits_locked(uint64_t additional) const {
  if (!cfg_.session_memory_bytes) {
    return true;
  }
  uint64_t available = cfg_.session_memory_bytes;
  const auto charge = [&](uint64_t bytes) {
    if (bytes > available) {
      return false;
    }
    available -= bytes;
    return true;
  };
  if (!charge(process_tree_rss(cfg_.session_root_pid)) || !charge(additional) || !charge(checkpoint_reserved_bytes_)
      || (foreground_ && !charge(foreground_->unallocated_memory(foreground_reserved_bytes_)))) {
    return false;
  }
  for (const auto& [id, job] : background_) {
    // Newly admitted workers may still be small. Reserve their predicted growth as well as their current RSS.
    if (job.worker && !charge(job.worker->unallocated_memory(job.reserved_bytes))) {
      return false;
    }
    if (!job.worker && job.state == "starting" && !charge(job.reserved_bytes)) {
      return false;
    }
  }
  return true;
}
std::string Session::exploration_limits_locked() const {
  size_t queued = 0, running = 0;
  for (const auto& [id, job] : background_) {
    if (job.worker) {
      running += !job.worker->terminal();
    } else {
      queued  += job.state == "queued";
      running += job.state == "starting";
    }
  }
  return std::format(
      R"({{"lhd_jobs":{},"background_slots":{},"running":{},"queued":{},"max_queued":{},"open":{},"max_open":{},"timeout_s":{},"threads_per_worker":{},"memory_mb_per_worker":{},"session_memory_bytes":{},"disk_mb_per_worker":{},"history_limit_mb":{},"history_evicted":{}}})",
      Job_manager::global().ceiling(),
      cfg_.explore_jobs,
      running,
      queued,
      cfg_.explore_queue,
      background_.size() + geometry_results_.size() + bool(foreground_),
      cfg_.explore_open,
      cfg_.explore_timeout,
      cfg_.explore_threads,
      cfg_.worker_memory_mb,
      cfg_.session_memory_bytes,
      cfg_.worker_disk_mb,
      cfg_.history_result_mb,
      history_evicted_);
}
void Session::dispatch_explorations(std::stop_token stop) {
  std::unique_lock lock(m_);
  while (!stop.stop_requested()) {
    size_t running = 0, heavy = 0;
    for (const auto& [id, job] : background_) {
      if ((job.tier == "T3" || job.tier == "T4") && job.worker && !job.worker->terminal()) {
        ++heavy;
      }
    }
    std::map<std::string, Background*> clients;
    for (auto& [id, job] : background_) {
      if (job.worker) {
        running += !job.worker->terminal();
        continue;
      }
      if (job.state == "starting") {
        ++running;
      }
      if (job.state != "queued") {
        continue;
      }
      if (job.parent == "main" && (!frame_ || job.frame != frame_->id || job.baseline != baseline_identity_locked()
          || (state_name_ != "gate" && state_name_ != "paused") || gate_name_ != job.checkpoint.label)) {
        job.state    = "stale";
        job.finished = std::chrono::steady_clock::now();
        job.reason   = "baseline gate or frame changed";
        continue;
      }
      if (std::chrono::steady_clock::now() - job.submitted > std::chrono::seconds(job.deadline_s ? job.deadline_s : cfg_.timeout_for(job.tier))) {
        job.finished = std::chrono::steady_clock::now();
        job.state    = "timed_out";
        job.reason   = job.waiting_for_memory ? "queue admission deadline reached waiting for session memory" : "queue admission deadline reached";
        cv_.notify_all();
        continue;
      }
      if ((job.tier == "T3" || job.tier == "T4") && heavy >= size_t(cfg_.heavy_jobs)) {
        continue;
      }
      if (job.worker_kind != "replay" && (job.tier == "T1" || job.tier == "T2") && job.baseline != snapshot_baseline_) {
        if (snapshot_attempt_ != job.baseline) {
          continue;
        }
        if (job.worker_kind == "snapshot") {
          job.state    = snapshot_reason_.starts_with("session memory budget") ? "budget_exhausted" : "failed";
          job.reason   = snapshot_reason_;
          job.finished = std::chrono::steady_clock::now();
          continue;
        }
        job.worker_kind = "replay";
        job.reason      = snapshot_reason_;
      }
      if (job.worker_kind != "replay" && job.until == "gp_end" && snapshot_owner_ && !snapshot_owner_->carries_gp()) {
        job.reason = "the current checkpoint cannot retain native GP";
        if (job.worker_kind == "snapshot") {
          job.state    = "failed";
          job.finished = std::chrono::steady_clock::now();
          continue;
        }
        job.worker_kind = "replay";
      }
      // Select the oldest pending request of each client first.
      auto& first = clients[job.client];
      if (!first || job.submitted < first->submitted) {
        first = &job;
      }
    }
    if (!clients.empty() && running < size_t(cfg_.explore_jobs)) {
      auto it = clients.upper_bound(last_client_);
      if (it == clients.end()) {
        it = clients.begin();
      }
      auto& job  = *it->second;
      const bool snapshot = (job.tier == "T1" || job.tier == "T2") && job.worker_kind != "replay" && snapshot_owner_
                            && job.baseline == snapshot_baseline_;
      const auto prediction = memory_prediction(job.baseline_nodes, !snapshot && job.tier != "T4");
      if (!memory_fits_locked(prediction)) {
        job.waiting_for_memory = true;
        last_client_ = job.client;
        if (!running && !checkpoint_reserved_bytes_) {
          job.reason = "session memory budget cannot accommodate the predicted worker";
          job.state = "budget_exhausted";
          job.finished = std::chrono::steady_clock::now();
          cv_.notify_all();
        } else {
          jobs_changed_.wait_for(lock, std::chrono::milliseconds(100));
        }
        continue;
      }
      job.waiting_for_memory = false;
      auto slot = Job_manager::global().try_acquire(Job_class::place_explore, job.baseline_nodes, prediction, false, 1,
                                                   snapshot ? 1 : cfg_.explore_threads);
      if (slot) {
        try {
          job.admitted = std::chrono::steady_clock::now();
          job.reserved_bytes = prediction;
          Config worker_config = cfg_;
          worker_config.timing_call_limit = job.timing_call_limit;
          if (job.tier != "T4") {
            worker_config.score = job.score;
            worker_config.score_estimator = job.estimator;
          }
          const int duration = job.deadline_s ? job.deadline_s : cfg_.timeout_for(job.tier);
          worker_config.worker_deadline = job.submitted + std::chrono::seconds(duration);
          if (job.tier == "T4") {
            job.worker = std::make_shared<Replay_worker>(worker_config,
                                                         scratch_root_locked(),
                                                         job.id,
                                                         job.validation_document,
                                                         job.placement_seconds,
                                                         std::move(slot));
            job.validation_document.reset();
          } else if (snapshot) {
            job.worker = std::make_shared<Replay_worker>(worker_config,
                                                         scratch_root_locked(),
                                                         job.id,
                                                         job.baseline,
                                                         job.patch.empty() ? "[]" : job.patch,
                                                         std::move(slot),
                                                         snapshot_owner_,
                                                         job.until=="gp_end" ? "@gp_end" : job.relax);
          } else {
            job.worker = std::make_shared<Replay_worker>(worker_config,
                                                         scratch_root_locked(),
                                                         job.id,
                                                         job.checkpoint,
                                                         job.baseline,
                                                         std::move(slot),
                                                         job.tier,
                                                         job.patch,
                                                         job.relax,
                                                         job.repair_mode,
                                                         job.repair_limit,
                                                         job.until);
          }
          job.checkpoint.captured_trace.reset();
          job.state    = "running";
          last_client_ = job.client;
        } catch (const std::exception& e) {
          job.state    = "failed";
          job.reason   = e.what();
          job.finished = std::chrono::steady_clock::now();
        }
        cv_.notify_all();
        continue;
      }
    }
    jobs_changed_.wait_for(lock, std::chrono::milliseconds(100));
  }
}

std::string Session::exploration_get(const std::string& id, const std::string& route) const {
  std::shared_ptr<Replay_worker> worker;
  std::shared_ptr<Session>       geometry;
  {
    std::lock_guard lock(m_);
    if (const auto found = geometry_results_.find(id); found != geometry_results_.end()) {
      geometry = found->second.document;
    } else if (const auto job = background_.find(id); job != background_.end()) {
      worker = job->second.worker;
    } else if (!foreground_ || id != foreground_id_) {
      throw std::runtime_error("unknown exploration");
    } else {
      worker = foreground_;
    }
  }
  if (geometry) {
    return geometry->read_document(route);
  }
  if (!worker || (!worker->ready() && !route.starts_with("/validation/"))) {
    throw std::runtime_error("exploration not ready");
  }
  return worker->get(route);
}
bool Session::discard_exploration(const std::string& id) {
  std::unique_lock lk(m_);
  if (closing_workers_.contains(id)) {
    return false;
  }
  std::shared_ptr<Replay_worker> worker;
  if (auto job = background_.find(id); job != background_.end()) {
    worker = job->second.worker;
    if (!worker && job->second.state == "queued") {
      job->second.state = "cancelled";
    }
  } else if (foreground_ && foreground_id_ == id) {
    worker = foreground_;
  } else if (!geometry_results_.contains(id)) {
    return false;
  }
  if (worker) {
    // Keep the slot and ledger entry visible while the process group is stopping.
    const bool foreground = worker == foreground_;
    closing_workers_.insert(id);
    lk.unlock();
    worker->cancel();
    if (foreground) {
      worker->collect_foreground_history(uint64_t(cfg_.history_result_mb) << 20);
    }
    lk.lock();
    closing_workers_.erase(id);
  }
  std::string row;
  if (auto job = background_.find(id); job != background_.end()) {
    job->second.finished = std::chrono::steady_clock::now();
    row = job->second.status();
    background_.erase(job);
  } else if (auto geometry = geometry_results_.find(id); geometry != geometry_results_.end()) {
    row = geometry->second.summary;
    geometry_results_.erase(geometry);
  } else if (foreground_ && foreground_id_ == id) {
    row = foreground_status_locked();
    foreground_.reset();
    foreground_id_.clear();
    foreground_flags_.clear();
  } else {
    return false;
  }
  // Bound open jobs separately from the session's compact history.
  rapidjson::Document history;
  history.Parse<rapidjson::kParseFullPrecisionFlag>(row.c_str());
  history.AddMember("closed", true, history.GetAllocator());
  history.AddMember("close_reason", "discarded", history.GetAllocator());
  if (std::string_view(history["state"].GetString()) == "running"
      || std::string_view(history["state"].GetString()) == "starting") {
    history["state"].SetString("cancelled");
  }
  rapidjson::StringBuffer                    buf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
  history.Accept(writer);
  closed_bytes_ += buf.GetSize();
  closed_workers_.emplace_back(id, std::string(buf.GetString(), buf.GetSize()));
  while (closed_bytes_ > (uint64_t(cfg_.history_result_mb) << 20)) {
    const auto& [old_id, oldest] = closed_workers_.front();
    closed_summaries_[old_id] = compact_history(oldest);
    closed_bytes_ -= oldest.size();
    closed_workers_.pop_front();
    ++history_evicted_;
  }
  jobs_changed_.notify_one();
  cv_.notify_all();
  bump_locked();
  return true;
}

std::string Session::submit_reply(const std::string& json, bool* accepted) {
  uint64_t sequence = 0;
  const auto why = submit(json, &sequence);
  if (accepted) {
    *accepted = why.empty();
  }
  if (!why.empty()) {
    return err_json(why);
  }
  return sequence ? std::format(R"({{"ok":true,"status":"queued","sequence":{}}})", sequence) : R"({"ok":true,"status":"applied"})";
}

std::string Session::agent_call(const std::string& json, int timeout_ms) {
  rapidjson::Document d;
  d.Parse<rapidjson::kParseFullPrecisionFlag>(json.c_str());
  if (d.HasParseError() || !d.IsObject() || !d.HasMember("cmd") || !d["cmd"].IsString()) {
    return err_json("send a JSON object with a \"cmd\" (see cmd=help)");
  }
  // The length matters: a "\u0000" inside the text must not shorten it ("state\u0000x" is not "state").
  const std::string cmd(d["cmd"].GetString(), d["cmd"].GetStringLength());
  const auto        str = [&](const char* key, const std::string& def = "") {
    return d.HasMember(key) && d[key].IsString() ? std::string(d[key].GetString(), d[key].GetStringLength()) : def;
  };
  const auto integer = [&](const char* key, int def) { return d.HasMember(key) && d[key].IsInt() ? d[key].GetInt() : def; };
  // A bounded integer argument. Absent: the default. Present but not an integer in [lo, hi] (a string, 2.7, 0, -1,
  // 1e300): `why` says so, instead of the value being ignored or clamped to something the caller did not ask for.
  const auto bounded = [&](const char* key, int def, int lo, int hi, std::string& why) {
    if (!d.HasMember(key)) {
      return def;
    }
    if (!d[key].IsInt() || d[key].GetInt() < lo || d[key].GetInt() > hi) {
      why = std::format("{} must be an integer in [{}, {}]", key, lo, hi);
      return def;
    }
    return d[key].GetInt();
  };
  const auto number  = [&](const char* key, double def) { return d.HasMember(key) && d[key].IsNumber() ? d[key].GetDouble() : def; };
  // A browser cannot round-trip arbitrary 64-bit JSON integers through Number.
  // Offer the serialized array for downloads/editors; normal socket clients still get records.
  const auto record_field = [&](const std::string& records) {
    return str("encoding") == "json_text" ? std::format(R"("records_json":"{}")", esc(records))
                                         : std::format(R"("records":{})", records);
  };

  const auto expected_revision_locked = [&]() {
    Command expectation;
    if (auto why = parse_expectation(d, expectation); !why.empty()) {
      return why;
    }
    return check_expectation_locked(expectation);
  };

  // The main controller also admits jobs based on a foreground scratch session. Capture its
  // immutable frame and replay segment together while it is still at the requested gate.
  std::shared_ptr<Replay_worker> scratch;
  std::unique_ptr<Session> scratch_doc;
  Session* basis = this;
  std::string source_trace, source_gate;
  int source_nth = 0;
  bool source_intent = false, source_orientation = false;
  std::map<std::string, std::string> inherited_flags;
  const auto target_session = str("target", cfg_.target);
  if (cmd == "explore" && target_session != cfg_.target) {
    {
      std::lock_guard lock(m_);
      if (!foreground_ || foreground_id_ != target_session || !foreground_->ready()) {
        return err_json("requested exploration baseline is unavailable");
      }
      scratch = foreground_;
      inherited_flags = foreground_flags_;
    }
    try {
      std::string snapshot;
      bool can_legalize;
      wire::unpack(scratch->get("/baseline.snapshot"), snapshot, source_trace, source_gate, source_nth, can_legalize, source_intent, source_orientation);
      scratch_doc = read_result_snapshot(snapshot);
      basis = scratch_doc.get();
      basis->gate_name_ = source_gate;
      basis->state_name_ = "gate";
      basis->legalize_available_ = can_legalize;
    } catch (const std::exception& ex) {
      return err_json(ex.what());
    }
  }
  const auto baseline_expected = [&] {
    Command expected;
    if (auto why = parse_expectation(d, expected); !why.empty()) {
      return why;
    }
    return basis->check_expectation_locked(expected);
  };
  if (cmd == "explore" && (str("tier") == "T0" || str("tier") == "T4")) {
    const bool               validate = str("tier") == "T4";
    Config                   cfg;
    std::string              id, baseline;
    auto                     frame = std::make_shared<Frame>();
    std::shared_ptr<Session> doc;
    std::vector<Command>     commands;
    const auto               parse_patch = [&](const auto& object) -> std::string {
      if (!object.IsObject()) {
        return "patch must contain placement commands";
      }
      if (object.HasMember("expect") || object.HasMember("target")) {
        return "patch entries use a common submission target and revision";
      }
      rapidjson::StringBuffer                    buf;
      rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
      object.Accept(writer);
      Command c;
      if (auto why = parse_command(buf.GetString(), c); !why.empty()) {
        return why;
      }
      if (c.cmd != "move" && c.cmd != "pin" && c.cmd != "unpin") {
        return "unsupported geometry command " + c.cmd;
      }
      commands.push_back(std::move(c));
      return {};
    };
    if (!d.HasMember("patch")) {
      return err_json("explore needs patch");
    }
    if (d["patch"].IsArray()) {
      for (const auto& item : d["patch"].GetArray()) {
        if (auto why = parse_patch(item); !why.empty()) {
          return err_json(why);
        }
      }
    } else if (auto why = parse_patch(d["patch"]); !why.empty()) {
      return err_json(why);
    }
    std::unique_lock lock(m_);
    if (scratch && foreground_ != scratch) {
      return err_json("foreground exploration closed during submission");
    }
    if (!basis->scene_ || !basis->frame_) {
      return err_json("no placement available");
    }
    if (validate && d.HasMember("placement_seconds")
        && (!d["placement_seconds"].IsNumber() || !std::isfinite(d["placement_seconds"].GetDouble())
            || d["placement_seconds"].GetDouble() < 0)) {
      return err_json("placement_seconds must be a nonnegative elapsed time");
    }
    if (validate && (cfg_.validation_args.empty() || cfg_.worker_args.empty() || !cfg_.heavy_jobs || !cfg_.explore_jobs)) {
      return err_json("T4 needs a configured downstream flow and a heavy worker share");
    }
    if (validate && ((basis->state_name_ != "gate" && basis->state_name_ != "paused") || (!scratch && (checkpoints_.empty() || !queue_.empty())))) {
      return err_json("T4 requires a stable gate");
    }
    if (validate && (d.HasMember("policy") || d.HasMember("set") || d.HasMember("scope") || d.HasMember("budget"))) {
      return err_json("T4 uses the session's fixed full-design recipe");
    }
    if (target_session != basis->cfg_.target) {
      return err_json("unknown geometry baseline");
    }
    if (auto why = baseline_expected(); !why.empty()) {
      return err_json(why);
    }
    if (background_.size() + geometry_results_.size() + bool(foreground_) >= size_t(cfg_.explore_open)) {
      return err_json("open exploration limit reached");
    }
    id                    = std::format("x{}", next_worker_++);
    baseline              = basis->baseline_identity_locked();
    cfg.target            = id;
    cfg.input_identity    = basis->cfg_.input_identity;
    cfg.source_identity   = basis->cfg_.source_identity;
    cfg.intent            = basis->cfg_.intent;
    cfg.open_browser      = false;
    cfg.hello             = cfg_.hello;
    cfg.policy            = basis->cfg_.policy;
    cfg.score             = basis->cfg_.score;
    cfg.score_estimator   = basis->cfg_.score_estimator;
    doc                   = std::make_shared<Session>(std::move(cfg));
    doc->initial_policy_  = initial_policy_;
    doc->scene_           = basis->scene_;
    doc->scene_version_   = basis->scene_version_;
    doc->design_hash_     = basis->design_hash_;
    doc->hint_hash_       = basis->hint_hash_;
    doc->stage_name_      = basis->stage_name_;
    doc->gate_name_       = basis->gate_name_;
    doc->state_name_      = "done";
    doc->options_json_    = basis->options_json_;
    doc->period_ps_       = basis->period_ps_;
    doc->intent_revision_ = basis->intent_revision_;
    doc->intent_report_   = basis->intent_report_;
    doc->intent_report_frame_ = basis->intent_report_frame_;
    doc->intent_status_ = basis->intent_status_;
    doc->pins_            = basis->pins_;
    doc->suspended_pins_  = basis->suspended_pins_;
    doc->frame_id_        = basis->frame_->id;
    const auto baseframe  = basis->frame_;
    const auto started    = std::chrono::steady_clock::now();
    // Reserve the result under the lock; the candidate itself only uses the
    // immutable scene/frame and never needs the solver or an admission slot.
    geometry_results_.emplace(id, Geometry_result{nullptr, std::format(R"({{"id":"{}","tier":"T0","state":"running"}})", id), false, str("label"), inherited_flags});
    lock.unlock();
    std::string why;
    try {
      *frame = *baseframe;
      doc->push_frame_locked(frame);
      Edits unused;
      for (auto& c : commands) {
        if (!(why = doc->resolve_move_locked(c)).empty()) {
          break;
        }
        bool moves_fixed = false;
        for (const auto& name : c.nodes) {
          auto it = doc->scene_->index.find(name);
          if (it == doc->scene_->index.end()) {
            why = "unknown node " + name;
            break;
          }
          const auto& node = doc->scene_->nodes[it->second];
          if (!node.movable && (c.cmd == "pin" || (c.cmd == "move" && node.kind != Kind::fixed))) {
            why = "requires a movable node: " + name;
            break;
          }
          if (c.cmd == "pin" && doc->geometry_conflicts_.contains(name)) {
            why = "pin refused on illegal macro " + name;
            break;
          }
          moves_fixed |= c.cmd == "move" && node.kind == Kind::fixed;
        }
        if (!why.empty()) {
          break;
        }
        if (moves_fixed) {
          c.cmd = "proposal_move";
        }
        doc->apply_locked(c, unused);
      }
    } catch (const std::exception& e) {
      why = e.what();
    }
    lock.lock();
    const auto found = geometry_results_.find(id);
    if (found == geometry_results_.end()) {
      return err_json("exploration was discarded");
    }
    rapidjson::StringBuffer patch_buffer;
    rapidjson::Writer<rapidjson::StringBuffer> patch_writer(patch_buffer);
    d["patch"].Accept(patch_writer);
    const auto row_fields = std::format(R"(,"parent":"{}","client":"{}","label":"{}","annotation":"{}","baseline":"{}","baseline_frame":{},"patch":{})",
                                        esc(target_session), esc(str("client")), esc(str("label")), esc(str("annotation")),
                                        baseline, baseframe->id, patch_buffer.GetString());
    const auto controller_cost = [&] {
      const double run = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
      return std::format(R"(,"run_s":{},"elapsed_s":{},"work":{})", run, run, doc->work_.json());
    };
    if (!why.empty()) {
      found->second.failed = true;
      found->second.summary = std::format(R"({{"id":"{}","tier":"{}","worker":"controller","state":"failed","reason":"{}"{}}})",
                                          id, validate ? "T4" : "T0", esc(why), row_fields + controller_cost());
      cv_.notify_all();
      return std::format(R"({{"ok":false,"id":"{}","error":"{}"}})", id, esc(why));
    }
    doc->stats_.hpwl_um = doc->stats_.overflow = -1;
    if (validate) {
      geometry_results_.erase(found);
      if (!scratch && (baseline != baseline_identity_locked() || gate_name_ != doc->gate_name_)) {
        return err_json("stale validation baseline");
      }
      if (queue_full_locked()) {
        return err_json("background queue limit reached");
      }
      Background job;
      job.id                    = id;
      job.parent = target_session;
      job.baseline_nodes = basis->scene_->nodes.size();
      job.tier                  = "T4";
      job.checkpoint.set        = inherited_flags;
      job.frame                 = basis->frame_->id;
      job.baseline              = baseline;
      job.submitted             = started;
      job.checkpoint.label      = basis->gate_name_;
      job.checkpoint.seq        = basis->seq_;
      job.label                 = str("label");
      job.client                = str("client");
      job.annotation            = str("annotation");
      job.estimator             = "OpenROAD";
      job.validation_document   = doc;
      job.placement_time_source = d.HasMember("placement_seconds") ? "caller" : "session_wall";
      job.placement_seconds
          = d.HasMember("placement_seconds")
                ? d["placement_seconds"].GetDouble()
                : std::max(0.,
                           std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count()
                               - cfg_.invocation_start_s);
      job.patch = patch_buffer.GetString();
      background_.emplace(id, std::move(job));
      if (!dispatcher_.joinable()) {
        dispatcher_ = std::jthread([this](std::stop_token token) { dispatch_explorations(token); });
      }
      jobs_changed_.notify_one();
      return std::format(R"({{"ok":true,"id":"{}","state":"queued"}})", id);
    }
    const auto geometry = doc->geometry_conflicts_.empty() ? "clear" : "illegal";
    const auto signature = macro_signature(*doc->scene_, *doc->frame_);
    const auto row = std::format(
        R"({{"id":"{}","tier":"T0","worker":"controller","state":"done"{},"signature":{},"geometry_status":"{}"{},"result":{{"summary":{},"geometry_status":"{}"}}}})",
        id, row_fields, signature, geometry, controller_cost(),
        doc->summary_locked(), geometry);
    found->second.document = std::move(doc);
    found->second.summary  = row;
    bump_locked();
    cv_.notify_all();
    return std::format(R"({{"ok":true,"id":"{}","state":"done","result":{}}})", id, row);
  }
  if (cmd == "explore") {
    std::lock_guard lock(m_);
    const auto      tier = str("tier");
    if (tier != "T1" && tier != "T2" && tier != "T3") {
      return err_json("supported background tiers are T0, T1, T2 and T3");
    }
    if (queue_full_locked()) {
      return err_json("background queue limit reached");
    }
    if (cfg_.worker_args.empty() || !cfg_.explore_jobs || Job_manager::global().ceiling() < 2) {
      return err_json("no background replay capacity");
    }
    if ((scratch && foreground_ != scratch) || cfg_.target != "main") {
      return err_json("unknown background baseline");
    }
    if (!basis->frame_ || (!scratch && ((state_name_ != "gate" && state_name_ != "paused") || checkpoints_.empty() || !queue_.empty()))) {
      return err_json("a stable placement gate is required");
    }
    if (auto why = baseline_expected(); !why.empty()) {
      return err_json(why);
    }
    if (background_.size() + geometry_results_.size() + bool(foreground_) >= size_t(cfg_.explore_open)) {
      return err_json("open exploration limit reached");
    }
    for (const auto* option : {"policy", "until"}) {
      if (d.HasMember(option) && !(tier == "T3" && std::string_view(option) == "policy")
          && !(tier == "T2" && std::string_view(option) == "until")) {
        return err_json("unsupported " + tier + " option " + option);
      }
    }
    int deadline_s = cfg_.timeout_for(tier);
    if (d.HasMember("budget") && d["budget"].IsObject() && d["budget"].HasMember("elapsed_s")) {
      auto& budget = d["budget"];
      const auto& seconds = budget["elapsed_s"];
      if (!seconds.IsInt() || seconds.GetInt() < 1 || seconds.GetInt() > deadline_s) {
        return err_json("budget.elapsed_s must be positive and within the configured tier deadline");
      }
      deadline_s = seconds.GetInt();
      budget.RemoveMember("elapsed_s");
      if (budget.ObjectEmpty()) {
        d.RemoveMember("budget");
      }
    }
    // Frozen geometry and bounded relaxation need exactly one final measurement.
    // A named stop or repair can also invoke timing inside the ordinary flow.
    const bool one_measurement = tier == "T1" || (tier == "T2" && !d.HasMember("until"));
    int timing_call_limit = one_measurement ? 1 : -1;
    if (d.HasMember("budget") && d["budget"].IsObject() && d["budget"].HasMember("timing_calls")) {
      auto& budget = d["budget"];
      const auto& calls = budget["timing_calls"];
      if (!calls.IsInt() || calls.GetInt() < 1) {
        return err_json("budget.timing_calls must be a positive integer");
      }
      if (!one_measurement) {
        timing_call_limit = calls.GetInt();
      }
      budget.RemoveMember("timing_calls");
      if (budget.ObjectEmpty()) {
        d.RemoveMember("budget");
      }
    }
    if (tier == "T1" && (d.HasMember("scope") || d.HasMember("budget"))) {
      return err_json("T1 uses frozen geometry and one full timing call");
    }
    if (!d.HasMember("patch")) {
      return err_json("explore needs a patch");
    }
    const auto&                          patch = d["patch"];
    std::vector<const rapidjson::Value*> entries;
    if (patch.IsArray()) {
      if (patch.Size() > 4096) {
        return err_json("patch exceeds 4096 commands");
      }
      for (const auto& entry : patch.GetArray()) {
        entries.push_back(&entry);
      }
    } else {
      entries.push_back(&patch);
    }
    std::set<std::string> active;
    bool                  restricted = false;
    Background            job;
    const bool variant = d.HasMember("set") && d["set"].IsObject() && !d["set"].ObjectEmpty();
    if (auto why = replay_flags(d, job.checkpoint); !why.empty()) {
      return err_json(why);
    }
    if (tier == "T3") {
      if (!cfg_.heavy_jobs) {
        return err_json("repair workers disabled");
      }
      if (!cfg_.repair_available) {
        return err_json("terminal repair requires full placement and an enabled timing engine");
      }
      if (!d.HasMember("policy") || !d["policy"].IsObject()) {
        return err_json("T3 requires an explicit repair policy");
      }
      bool size = false, buffer = false, resynth = false;
      for (const auto& entry : d["policy"].GetObject()) {
        const std::string_view key = entry.name.GetString();
        if (!entry.value.IsBool() || (key != "sizing" && key != "buffering" && key != "resynthesis")) {
          return err_json("T3 policy supports sizing, buffering and resynthesis booleans");
        }
        (key == "sizing" ? size : key == "buffering" ? buffer : resynth) = entry.value.GetBool();
      }
      if (resynth && !cfg_.resynthesis_available) {
        return err_json("the ABC cone resynthesis provider is unavailable");
      }
      if (d.HasMember("scope") && (buffer || resynth)) {
        return err_json("scoped repair supports sizing; buffering and resynthesis require expanded implementation scope");
      }
      if (!size && !buffer && !resynth) {
        return err_json("T3 requires an enabled repair operation");
      }
      if (!d.HasMember("budget") || !d["budget"].IsObject()
          || d["budget"].MemberCount() != 1u + unsigned(d.HasMember("scope") && d["budget"].HasMember("active_cells"))
          || !d["budget"].HasMember("repair_trials") || !d["budget"]["repair_trials"].IsUint()
          || d["budget"]["repair_trials"].GetUint() < 1 || d["budget"]["repair_trials"].GetUint() > 64) {
        return err_json("T3 requires budget.repair_trials in [1,64]");
      }
      job.repair_limit = d["budget"]["repair_trials"].GetUint();
      job.repair_mode  = size ? "sizing" : "";
      if (buffer) {
        job.repair_mode += " buffering";
      }
      if (resynth) {
        job.repair_mode += " resynthesis";
      }
    }
    if (tier == "T2" && d.HasMember("until")) {
      if (d.HasMember("scope") || d.HasMember("budget")) {
        return err_json("a named stop runs the ordinary full placement flow; use iterations for a bounded movement scope");
      }
      if (!d["until"].IsString()) {
        return err_json("until needs a placement gate name or end");
      }
      job.until = d["until"].GetString();
      job.active_cells
          = std::count_if(basis->scene_->nodes.begin(), basis->scene_->nodes.end(), [](const Node& n) { return n.movable; });
      static const std::set<std::string> stops{"fp", "macro", "macro_legalize", "final", "gp_end", "legalize", "edit", "end"};
      if (!stops.contains(job.until)) {
        return err_json("unknown T2 stop");
      }
    }
    const bool scoped_repair = tier == "T3" && d.HasMember("scope");
    if ((tier == "T2" && job.until.empty()) || scoped_repair) {
      if (!scoped_repair && (!basis->legalize_available_ || !d.HasMember("budget") || !d["budget"].IsObject() || !d["budget"].HasMember("iterations")
          || !d["budget"]["iterations"].IsInt())) {
        return err_json("T2 requires a legalization gate and budget.iterations");
      }
      for (const auto& entry : d["budget"].GetObject()) {
        if (std::string_view(entry.name.GetString()) != (scoped_repair ? "repair_trials" : "iterations")
            && std::string_view(entry.name.GetString()) != "active_cells") {
          return err_json("unsupported T2 budget field");
        }
      }
      rapidjson::Document cmd_json;
      cmd_json.SetObject();
      auto& alloc = cmd_json.GetAllocator();
      if (scoped_repair) {
        cmd_json.AddMember("cmd", "repair_scope", alloc);
        cmd_json.AddMember("trials", job.repair_limit, alloc);
      } else {
        cmd_json.AddMember("cmd", "relax", alloc);
        cmd_json.AddMember("iterations", d["budget"]["iterations"].GetInt(), alloc);
      }
      if (d.HasMember("scope")) {
        restricted        = true;
        const auto& scope = d["scope"];
        if (!scope.IsObject()) {
          return err_json("invalid movement scope");
        }
        double halo = 0;
        if (scope.HasMember("halo_dbu")) {
          if (!scope["halo_dbu"].IsNumber() || (halo = scope["halo_dbu"].GetDouble()) < 0 || !std::isfinite(halo)) {
            return err_json("invalid relaxation halo");
          }
        }
        unsigned selectors = 0;
        for (auto key : {"nodes", "hierarchy", "cluster", "region", "box"}) {
          selectors += scope.HasMember(key);
        }
        if (selectors != 1) {
          return err_json("choose one movement scope: nodes, hierarchy, cluster, region or box");
        }
        std::optional<Box> window;
        if (scope.HasMember("box")) {
          const auto& box = scope["box"];
          if (!box.IsArray() || box.Size() != 4) {
            return err_json("scope box needs four DBU coordinates");
          }
          for (const auto& v : box.GetArray()) {
            if (!v.IsNumber() || !std::isfinite(v.GetDouble())) {
              return err_json("invalid scope box");
            }
          }
          window = Box{box[0].GetDouble(), box[1].GetDouble(), box[2].GetDouble(), box[3].GetDouble()};
          if (window->xl >= window->xh || window->yl >= window->yh) {
            return err_json("empty scope box");
          }
        } else if (scope.HasMember("region")) {
          if (!scope["region"].IsUint() || !basis->regions_ || basis->regions_frame_ != basis->frame_->id
              || scope["region"].GetUint() >= basis->regions_->rects.size() / 5) {
            return err_json("requested region unavailable at this frame; request regions first");
          }
          const auto k = size_t(scope["region"].GetUint()) * 5;
          window       = Box{basis->regions_->rects[k], basis->regions_->rects[k + 1], basis->regions_->rects[k + 2], basis->regions_->rects[k + 3]};
        } else if (scope.HasMember("cluster")) {
          if (!scope["cluster"].IsString() || !basis->scene_->groups.contains(scope["cluster"].GetString())) {
            return err_json("unknown placement cluster");
          }
          for (int i : basis->scene_->groups.at(scope["cluster"].GetString())) {
            if (i >= 0 && size_t(i) < basis->scene_->nodes.size() && basis->scene_->nodes[i].movable) {
              active.insert(basis->scene_->nodes[i].name);
            }
          }
        } else if (scope.HasMember("nodes")) {
          if (!scope["nodes"].IsArray()) {
            return err_json("scope nodes must be an array");
          }
          for (const auto& name : scope["nodes"].GetArray()) {
            if (!name.IsString()) {
              return err_json("scope nodes must be names or scene indices");
            }
            const std::string key = name.GetString();
            const auto index = basis->scene_->node_index(key);
            if (index < 0) {
              return err_json(key.starts_with('#') ? "unknown scene index " + key : "unknown scope node");
            }
            if (basis->scene_->nodes[index].movable) {
              active.insert(basis->scene_->nodes[index].name);
            }
          }
        } else {
          if (!scope["hierarchy"].IsString() || !scope["hierarchy"].GetStringLength()) {
            return err_json("hierarchy needs an instance path");
          }
          const std::string prefix = scope["hierarchy"].GetString();
          for (const auto& n : basis->scene_->nodes) {
            if (n.movable && (n.name == prefix || n.name.starts_with(prefix + ".") || n.name.starts_with(prefix + "/"))) {
              active.insert(n.name);
            }
          }
        }
        if (halo > 0 && !window) {
          for (const auto& name : active) {
            const auto i = basis->scene_->index.at(name);
            const Box  body{basis->frame_->x[i], basis->frame_->y[i], basis->frame_->x[i] + basis->scene_->nodes[i].w, basis->frame_->y[i] + basis->scene_->nodes[i].h};
            if (!window) {
              window = body;
            } else {
              window = Box{std::min(window->xl, body.xl),
                           std::min(window->yl, body.yl),
                           std::max(window->xh, body.xh),
                           std::max(window->yh, body.yh)};
            }
          }
        }
        if (window) {
          for (size_t i = 0; i < basis->scene_->nodes.size(); ++i) {
            const auto& n = basis->scene_->nodes[i];
            if (n.movable && basis->frame_->x[i] >= window->xl - halo && basis->frame_->x[i] + n.w <= window->xh + halo
                && basis->frame_->y[i] >= window->yl - halo && basis->frame_->y[i] + n.h <= window->yh + halo) {
              active.insert(n.name);
            }
          }
        }
        if (active.empty()) {
          return err_json("movement scope contains no movable objects at the baseline");
        }
        rapidjson::Value names(rapidjson::kArrayType), resolved(rapidjson::kObjectType);
        if (scope.HasMember("kernel")) {
          if (scoped_repair) {
            return err_json("a movement kernel is only used for T2 relaxation");
          }
          if (!scope["kernel"].IsString()
              || (std::string_view(scope["kernel"].GetString()) != "local"
                  && std::string_view(scope["kernel"].GetString()) != "full_reference")) {
            return err_json("unknown movement kernel");
          }
          resolved.AddMember("kernel", rapidjson::Value(scope["kernel"], alloc), alloc);
        }
        for (const auto& name : active) {
          names.PushBack(rapidjson::Value(name.c_str(), alloc), alloc);
        }
        if (window) {
          rapidjson::Value box(rapidjson::kArrayType);
          for (double edge : {window->xl - halo, window->yl - halo, window->xh + halo, window->yh + halo}) {
            box.PushBack(edge, alloc);
          }
          resolved.AddMember("box", box, alloc);
        }
        resolved.AddMember("nodes", names, alloc);
        cmd_json.AddMember("scope", resolved, alloc);
      }
      const auto active_count
          = restricted ? active.size()
                       : std::count_if(basis->scene_->nodes.begin(), basis->scene_->nodes.end(), [](const Node& n) { return n.movable; });
      job.active_cells = active_count;
      if (d["budget"].HasMember("active_cells")) {
        const auto& limit = d["budget"]["active_cells"];
        if (!limit.IsUint64() || limit.GetUint64() < active_count) {
          return err_json("movement scope exceeds active_cells budget");
        }
      }
      rapidjson::StringBuffer                    buffer;
      rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
      cmd_json.Accept(writer);
      job.relax = buffer.GetString();
      Command parsed;
      if (auto why = parse_command(job.relax, parsed); !why.empty()) {
        return err_json(why);
      }
    }
    auto requested = str("worker", cfg_.explore_worker);
    if (requested != "auto" && requested != "snapshot" && requested != "replay") {
      return err_json("unknown worker selection");
    }
    std::string fallback;
    if (scratch) {
      fallback = "the foreground replay segment requires a spawned evaluator";
    } else if (tier == "T2" && job.until == "gp_end") {
      fallback = basis->native_gp_reason_locked();
      if (fallback.empty() && !job.checkpoint.set.empty()) {
        fallback = "flag changes require a spawned flow";
      }
    } else if (tier != "T1" && !(tier == "T2" && !job.relax.empty())) {
      fallback = "this implementation tier requires a spawned flow";
    } else if (!cfg_.snapshot_available) {
      fallback = "the configured timing engine requires spawned replay";
    } else if (!job.checkpoint.set.empty()) {
      fallback = "flag changes require a spawned flow";
    }
    for (const auto* entry : entries) {
      if (!entry->IsObject() || !entry->HasMember("cmd") || !(*entry)["cmd"].IsString() || entry->HasMember("expect")
          || entry->HasMember("target")) {
        return err_json("patch entries use a common submission target and revision");
      }
      rapidjson::StringBuffer                    buffer;
      rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
      entry->Accept(writer);
      Command c;
      if (auto why = parse_command(buffer.GetString(), c); !why.empty()) {
        return err_json(why);
      }
      if (c.cmd != "move" && c.cmd != "pin" && c.cmd != "unpin" && c.cmd != "intent_apply") {
        return err_json("unsupported exploration patch command " + c.cmd);
      }
      if (c.cmd == "intent_apply" || !c.orientations.empty()) {
        fallback = "orientation and native intent edits require replay";
      }
      if (c.cmd == "intent_apply" && (restricted || !(scratch ? source_intent : bool(intent_provider_)))) {
        return err_json("intent patch needs a full-design native intent gate");
      }
      if (auto why = basis->resolve_move_locked(c, nullptr, source_orientation); !why.empty()) {
        return err_json(why);
      }
      for (const auto& name : c.nodes) {
        const auto it = basis->scene_->index.find(name);
        if (it == basis->scene_->index.end() || !basis->scene_->nodes[it->second].movable) {
          return err_json("requires a movable node at this gate: " + name);
        }
        if (restricted && !active.contains(name)) {
          return err_json("patch addresses a frozen exterior node: " + name);
        }
      }
    }
    if (requested == "snapshot" && !fallback.empty() && (tier == "T1" || d.HasMember("worker"))) {
      return err_json(fallback);
    }
    job.timing_call_limit = timing_call_limit;
    job.worker_kind = fallback.empty() ? requested : "replay";
    job.deadline_s = deadline_s;
    if (requested != "replay") {
      job.reason = fallback;
    }
    if (!entries.empty()) {
      rapidjson::StringBuffer                    buffer;
      rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
      patch.Accept(writer);
      job.patch = buffer.GetString();
    }
    job.checkpoint.trace_source = private_trace_path_;
    job.checkpoint.recipe_variant = variant;
    job.checkpoint.set.insert(inherited_flags.begin(), inherited_flags.end());
    job.parent = target_session;
    job.baseline_nodes = basis->scene_->nodes.size();
    if (scratch) {
      job.checkpoint.captured_trace = std::move(source_trace);
    }
    job.id             = std::format("x{}", next_worker_++);
    job.submitted      = std::chrono::steady_clock::now();
    job.estimator      = basis->cfg_.score_estimator;
    job.score          = basis->cfg_.score;
    job.annotation     = str("annotation");
    std::string recipe = job.until + job.relax + job.repair_mode + std::to_string(job.repair_limit);
    for (const auto& [key, setting] : job.checkpoint.set) {
      recipe += std::format("|{}={}#{}", key, setting.size(), setting);
    }
    job.scope_key        = std::format("{:016x}", hash_util::fnv1a64(recipe));
    job.label            = str("label");
    job.client           = str("client");
    job.tier             = tier;
    job.frame            = basis->frame_->id;
    job.baseline         = basis->baseline_identity_locked();
    job.checkpoint.label = scratch ? source_gate : checkpoints_.back().label;
    job.checkpoint.nth   = scratch ? source_nth : checkpoints_.back().nth;
    job.checkpoint.seq   = basis->seq_;
    if (job.worker_kind != "replay" && job.baseline != snapshot_baseline_ && job.baseline != snapshot_attempt_) {
      snapshot_requested_ = job.baseline;
      cv_.notify_all();
    }
    auto id = job.id;
    background_.emplace(id, std::move(job));
    if (!dispatcher_.joinable()) {
      dispatcher_ = std::jthread([this](std::stop_token token) { dispatch_explorations(token); });
    }
    jobs_changed_.notify_one();
    return std::format(R"({{"ok":true,"id":"{}","state":"queued"}})", id);
  }
  if (cmd == "similarity") {
    rapidjson::Document reference, candidate;
    for (const auto& [key, doc] : {
             std::pair{"reference", &reference},
             std::pair{"candidate", &candidate}
    }) {
      if (!d.HasMember(key) || !d[key].IsString()) {
        return err_json("similarity requires reference and candidate targets");
      }
      const auto  target = str(key);
      std::string signature;
      {
        std::lock_guard lock(m_);
        for (const auto& [id, record] : closed_workers_) {
          if (id != target) {
            continue;
          }
          rapidjson::Document row;
          row.Parse(record.c_str());
          const auto& value = row.HasMember("signature") || !row.HasMember("result") ? static_cast<const rapidjson::Value&>(row) : row["result"];
          if (!value.IsObject() || !value.HasMember("signature")) {
            return err_json("target has no measured signature");
          }
          rapidjson::StringBuffer                    text;
          rapidjson::Writer<rapidjson::StringBuffer> writer(text);
          value["signature"].Accept(writer);
          signature = text.GetString();
          break;
        }
      }
      if (signature.empty()) {
        signature = agent_call(std::format(R"({{"cmd":"signature","target":"{}"}})", esc(target)));
      }
      doc->Parse(signature.c_str());
      if (!doc->IsObject() || !doc->HasMember("macros") || !(*doc)["macros"].IsObject()) {
        return err_json("signature unavailable for " + target);
      }
    }
    const auto& a      = reference["macros"];
    const auto& b      = candidate["macros"];
    unsigned    common = 0, matches = 0;
    for (const auto& m : a.GetObject()) {
      if (!b.HasMember(m.name)) {
        continue;
      }
      ++common;
      for (auto key : {"group", "side", "order", "orient", "position"}) {
        matches += m.value[key] == b[m.name][key];
      }
    }
    const auto members = a.MemberCount() + b.MemberCount() - common;
    return std::format(
        R"({{"ok":true,"common_macros":{},"union_macros":{},"matched_fields":{},"fields_per_macro":5,"similarity":{}}})",
        common,
        members,
        matches,
        members ? num(double(matches) / (5. * members)) : "null");
  }
  if (cmd == "compare") {
    // The reference is another measured candidate, which may use an empty
    // patch. A later repaint or timing refresh on main is never a reference.
    std::lock_guard lock(m_);
    auto            lookup = [&](const std::string& id) {
      rapidjson::Document row;
      if (id == "main" && frame_ && has_timing_ && !timing_est_ && evaluated_frame_ == frame_->id) {
        const auto identity = baseline_identity_locked();
        const auto ref      = std::format(
            R"({{"state":"done","tier":"T1","baseline":"{}","replayed_baseline":"{}","scope_recipe":"{:016x}","estimator":"{}","result":{{"summary":{}}}}})",
            identity,
            identity,
            hash_util::fnv1a64("0"),
            esc(cfg_.score_estimator),
            summary_locked());
        row.Parse<rapidjson::kParseFullPrecisionFlag>(ref.c_str());
        return row;
      }
      if (auto it = background_.find(id); it != background_.end()) {
        row.Parse<rapidjson::kParseFullPrecisionFlag>(it->second.status().c_str());
        return row;
      }
      if (auto geometry = geometry_results_.find(id); geometry != geometry_results_.end()) {
        row.Parse<rapidjson::kParseFullPrecisionFlag>(geometry->second.summary.c_str());
        return row;
      }
      for (const auto& [closed_id, archived] : closed_workers_) {
        if (closed_id == id) {
          row.Parse<rapidjson::kParseFullPrecisionFlag>(archived.c_str());
          return row;
        }
      }
      if (auto found = closed_summaries_.find(id); found != closed_summaries_.end()) {
        row.Parse<rapidjson::kParseFullPrecisionFlag>(found->second.c_str());
        return row;
      }
      row.SetNull();
      return row;
    };
    const auto a = lookup(str("reference")), b = lookup(str("candidate"));
    if (!a.IsObject() || !b.IsObject()) {
      return err_json("comparison requires two measured exploration ids");
    }
    for (const auto* field : {"state", "tier", "baseline"}) {
      if (!a.HasMember(field) || !b.HasMember(field) || a[field] != b[field]) {
        return err_json(std::string("unmatched ") + field);
      }
    }
    if (std::string_view(a["state"].GetString()) != "done") {
      return err_json("comparison requires completed measurements");
    }
    if (std::string_view(a["tier"].GetString()) == "T0") {
      return std::format(R"({{"ok":true,"tier":"T0","baseline":"{}","geometry":{{"reference":"{}","candidate":"{}"}}}})",
                         a["baseline"].GetString(), a["geometry_status"].GetString(), b["geometry_status"].GetString());
    }
    for (const auto* field : {"scope_recipe", "estimator"}) {
      if (!a.HasMember(field) || !b.HasMember(field) || a[field] != b[field]) {
        return err_json(std::string("unmatched ") + field);
      }
    }
    if ((a.HasMember("details_evicted") || b.HasMember("details_evicted")) && std::string_view(a["tier"].GetString()) == "T4") {
      return err_json("detailed downstream comparison evidence expired");
    }
    if (a.HasMember("tier") && std::string_view(a["tier"].GetString()) == "T4") {
      for (const auto* row : {&a, &b}) {
        if (!row->HasMember("result") || !(*row)["result"].IsObject() || !(*row)["result"].HasMember("validation")) {
          return err_json("downstream validation unavailable");
        }
      }
      const auto& x = a["result"]["validation"];
      const auto& y = b["result"]["validation"];
      for (auto key : {"floorplan_sha256",
                       "templates_sha256",
                       "aspect",
                       "openroad_sha256",
                       "scorer_sha256",
                       "technology_constraints_sha256",
                       "threads"}) {
        if (!x["inputs"].HasMember(key) || !y["inputs"].HasMember(key) || x["inputs"][key] != y["inputs"][key]) {
          return err_json("unmatched validation recipe");
        }
      }
      if (std::string_view(x["row"]["status"].GetString()) != "routed"
          || std::string_view(y["row"]["status"].GetString()) != "routed") {
        return err_json("both validations must complete routing");
      }
      auto diff = [&](const char* metric) {
        if (!x["row"].HasMember(metric) || !y["row"].HasMember(metric)) {
          return std::string("null");
        }
        const auto& l = x["row"][metric];
        const auto& r = y["row"][metric];
        return l.IsNumber() && r.IsNumber() ? num(r.GetDouble() - l.GetDouble()) : "null";
      };
      const auto* left       = routed_endpoints(x);
      const auto* right      = routed_endpoints(y);
      const bool  compatible = left && right && (*left)["clock_periods_ns"] == (*right)["clock_periods_ns"];
      const bool  matched    = compatible && same_endpoints((*left)["total"], (*right)["total"]);
      const auto& lhs        = a["result"]["summary"];
      const auto& rhs        = b["result"]["summary"];
      const bool  same_score = compatible_score(lhs, rhs, matched, lhs["routing"]["estimator"] == rhs["routing"]["estimator"]);
      return std::format(
          R"({{"ok":true,"tier":"T4","baseline":"{}","geometry":{{"reference":"{}","candidate":"{}"}},"timing_coverage":"{}","delta":{{"wns_ns":{},"tns_ns":{},"failing":{},"capture_clocks":{},"path_families":{},"drc":{},"wl_um":{},"quality":{}}}}})",
          esc(a["baseline"].GetString()),
          a["result"]["geometry_status"].GetString(),
          b["result"]["geometry_status"].GetString(),
          matched         ? "matched"
          : left && right ? "changed"
                          : "unavailable",
          matched ? diff("wns") : "null",
          matched ? diff("tns") : "null",
          matched ? metric_delta((*left)["total"], (*right)["total"], "failing") : "null",
          compatible ? timing_groups(*left, *right, "capture_clocks") : "null",
          compatible ? timing_groups(*left, *right, "path_families") : "null",
          diff("drc"),
          diff("wl_um"),
          same_score ? metric_delta(lhs["quality"], rhs["quality"], "score") : "null");
    }
    if (!a.HasMember("result") || !b.HasMember("result") || !a["result"].IsObject() || !b["result"].IsObject()
        || !a["result"].HasMember("summary") || !b["result"].HasMember("summary")) {
      return err_json("candidate measurements unavailable");
    }
    if (!a.HasMember("replayed_baseline") || !b.HasMember("replayed_baseline")
        || a["replayed_baseline"] != b["replayed_baseline"]) {
      return err_json("unmatched replayed baseline");
    }
    const auto& x = a["result"]["summary"];
    const auto& y = b["result"]["summary"];
    const auto& xends = x["timing"]["endpoints"];
    const auto& yends = y["timing"]["endpoints"];
    const bool measured = xends.IsObject() && yends.IsObject() && x["timing"]["model"] == y["timing"]["model"]
                          && xends["engine"] == yends["engine"] && xends["setup_corners"] == yends["setup_corners"]
                          && xends["clock_periods_hash"] == yends["clock_periods_hash"]
                          && xends["constraint_issues_hash"] == yends["constraint_issues_hash"];
    const bool matched = measured && same_endpoints(xends["total"], yends["total"]) && xends["total"]["endpoints"].GetUint() > 0;
    const bool same_routing = x["routing"]["estimator"] == y["routing"]["estimator"];
    const bool same_score = compatible_score(x, y, matched, same_routing);
    return std::format(
        R"({{"ok":true,"tier":"{}","reference":"{}","candidate":"{}","reference_frame":{},"candidate_frame":{},"baseline":"{}","geometry":{{"reference":"{}","candidate":"{}"}},"timing_coverage":"{}","delta":{{"wns_ps":{},"tns_ps":{},"failing":{},"capture_clocks":{},"path_families":{},"overflow_bin_share":{},"quality":{}}}}})",
        a["tier"].GetString(),
        esc(str("reference")),
        esc(str("candidate")),
        x["timing"]["frame"].GetUint64(),
        y["timing"]["frame"].GetUint64(),
        esc(a["baseline"].GetString()),
        x["geometry_status"].GetString(),
        y["geometry_status"].GetString(),
        !measured ? "unavailable" : matched ? "matched" : "changed",
        matched ? metric_delta(x["timing"], y["timing"], "wns_ps") : "null",
        matched ? metric_delta(x["timing"], y["timing"], "tns_ps") : "null",
        matched ? metric_delta(xends["total"], yends["total"], "failing") : "null",
        measured ? timing_groups(xends, yends, "capture_clocks") : "null",
        measured ? timing_groups(xends, yends, "path_families") : "null",
        same_routing ? metric_delta(x["routing"], y["routing"], "overflow_bin_share") : "null",
        same_score ? metric_delta(x["quality"], y["quality"], "score") : "null");
  }
  if (cmd == "export" && str("what") == "scope") {
    std::lock_guard lock(m_);
    auto            found = background_.find(str("target"));
    if (found == background_.end() || (found->second.tier != "T2" && found->second.tier != "T3")) {
      return err_json("select an open T2 or T3 exploration");
    }
    const auto& job = found->second;
    if (job.relax.empty()) {
      return std::format(R"({{"ok":true,"resolved":{{"scope":"full_design","until":"{}"}}}})", esc(job.until));
    }
    return R"({"ok":true,"resolved":)" + job.relax + "}";
  }
  if (cmd == "explorations" || cmd == "explore_wait") {
    auto until
        = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::clamp(integer("timeout_ms", timeout_ms), 0, 60000));
    std::unique_lock      lk(m_);
    std::set<std::string> ids;
    if (d.HasMember("ids")) {
      if (!d["ids"].IsArray()) {
        return err_json("ids must be an array");
      }
      for (const auto& id : d["ids"].GetArray()) {
        if (!id.IsString()) {
          return err_json("invalid exploration id");
        }
        ids.insert(id.GetString());
        if (!background_.contains(id.GetString()) && !geometry_results_.contains(id.GetString())
            && foreground_id_ != id.GetString() && !closed_summaries_.contains(id.GetString())
            && std::ranges::none_of(closed_workers_, [&](const auto& entry) { return entry.first == id.GetString(); })) {
          return err_json("unknown exploration");
        }
      }
    }
    const auto matches  = [&](const std::string& id) { return ids.empty() || ids.contains(id); };
    const auto finished = [&] {
      size_t n = 0, done = 0;
      for (const auto& [id, j] : background_) {
        if (matches(id)) {
          ++n;
          done += j.worker ? j.worker->terminal() : (j.state != "queued" && j.state != "starting");
        }
      }
      for (const auto& [id, j] : geometry_results_) {
        if (matches(id)) {
          ++n;
          done += bool(j.document) || j.failed;
        }
      }
      if (foreground_ && matches(foreground_id_)) {
        ++n;
        done += foreground_->ready() || foreground_->terminal();
      }
      // Closed IDs named in a wait count as finished even when another client closed them mid-wait.
      if (!ids.empty()) {
        done += ids.size() - n;
        n = ids.size();
      }
      return n == 0 || (d.HasMember("any") && d["any"].IsBool() && d["any"].GetBool() ? done > 0 : done == n);
    };
    while (cmd == "explore_wait" && !finished() && !exit_.load() && std::chrono::steady_clock::now() < until) {
      cv_.wait_for(lk, std::chrono::milliseconds(50));
    }
    // A terminal background result is immutable until discard. Closing or eviction
    // changes its storage class. A new controller may reuse IDs, so scope the hint to its epoch.
    const bool incremental = cmd == "explorations" && d.HasMember("known");
    std::map<std::string, std::string> known;
    if (incremental) {
      if (!d["known"].IsObject()) {
        return err_json("known must map exploration IDs to record states");
      }
      if (str("epoch") == epoch_) {
        for (const auto& item : d["known"].GetObject()) {
          if (item.value.IsString()) {
            known.emplace(item.name.GetString(), item.value.GetString());
          }
        }
      }
    }
    std::string rows, unchanged;
    auto append = [](std::string& array, const std::string& element) {
      if (!array.empty()) {
        array += ",";
      }
      array += element;
    };
    const auto cached = [&](const std::string& id, std::string_view state) {
      auto it = known.find(id);
      if (it == known.end() || it->second != state) {
        return false;
      }
      append(unchanged, std::format(R"("{}")", esc(id)));
      return true;
    };
    for (const auto& [id, job] : background_) {
      if (matches(id)) {
        const bool active = job.worker ? !job.worker->terminal() : job.state == "queued" || job.state == "starting";
        if (active || !cached(id, "terminal")) {
          append(rows, job.status());
        }
      }
    }
    for (const auto& [id, result] : geometry_results_) {
      if (matches(id) && (!(result.document || result.failed) || !cached(id, "terminal"))) {
        append(rows, result.summary);
      }
    }
    for (const auto& [id, row] : closed_summaries_) {
      if (matches(id) && !cached(id, "evicted")) {
        append(rows, row);
      }
    }
    for (const auto& [id, row] : closed_workers_) {
      if (matches(id) && !cached(id, "closed")) {
        append(rows, row);
      }
    }
    if (foreground_ && matches(foreground_id_)) {
      append(rows, foreground_status_locked());
    }
    return std::format(R"({{"ok":true,"epoch":"{}","limits":{},"main":{},"explorations":[{}]{}}})",
                       esc(epoch_), exploration_limits_locked(), summary_locked(), rows,
                       incremental ? R"(,"unchanged":[)" + unchanged + "]" : "");
  }
  if (cmd == "explore_begin") {
    std::lock_guard lk(m_);
    if (cfg_.worker_args.empty()) {
      return err_json("replay evaluator unavailable for this session");
    }
    if (foreground_) {
      return err_json("foreground exploration already open");
    }
    if (background_.size() + geometry_results_.size() >= size_t(cfg_.explore_open)) {
      return err_json("open exploration limit reached");
    }
    if ((state_name_ != "gate" && state_name_ != "paused") || checkpoints_.empty() || !frame_ || !queue_.empty()) {
      return err_json("a stable placement gate is required");
    }
    if (str("target", "main") != "main") {
      return err_json("unknown exploration baseline");
    }
    if (auto why = expected_revision_locked(); !why.empty()) {
      return err_json(why);
    }
    const auto prediction = memory_prediction(scene_->nodes.size(), true);
    const auto host = host_resources();
    if (!memory_fits_locked(prediction)) {
      return err_json("session memory budget cannot accommodate foreground replay");
    }
    if (host.available && host.available < prediction) {
      return err_json("insufficient available memory for replay");
    }
    const auto&     cp = checkpoints_.back();
    Restart_request base;
    if (auto why = replay_flags(d, base); !why.empty()) {
      return err_json(why);
    }
    base.trace_source = private_trace_path_;
    base.recipe_variant = !base.set.empty();
    base.label    = cp.label;
    base.nth      = cp.nth;
    base.seq      = seq_;
    const auto id = std::format("x{}", next_worker_++);
    try {
      foreground_    = std::make_shared<Replay_worker>(cfg_, scratch_root_locked(), id, base, baseline_identity_locked());
      foreground_reserved_bytes_ = prediction;
      foreground_id_ = id;
      foreground_flags_ = base.set;
      foreground_frame_ = frame_->id;
      foreground_label_ = str("label");
      foreground_annotation_ = str("annotation");
      foreground_client_ = str("client");
      bump_locked();
      return std::format(R"({{"ok":true,"id":"{}","state":"starting","baseline_frame":{},"baseline":"{}"}})",
                         id,
                         frame_->id,
                         baseline_identity_locked());
    } catch (const std::exception& e) {
      return err_json(e.what());
    }
  }
  if (cmd == "explore_discard") {
    const bool all = d.HasMember("all");
    if ((all && (!d["all"].IsBool() || !d["all"].GetBool()))
        || (d.HasMember("label") && (!d["label"].IsString() || !d["label"].GetStringLength()))
        || (d.HasMember("id") && !d["id"].IsString())
        || int(all) + int(d.HasMember("label")) + int(d.HasMember("id")) > 1) {
      return err_json("choose one exploration id, a nonempty label or all=true");
    }
    std::vector<std::string> ids;
    {
      std::lock_guard lock(m_);
      if (all || d.HasMember("label")) {
        const auto label = str("label");
        for (const auto& [id, job] : background_) {
          if (all || job.label == label) {
            ids.push_back(id);
          }
        }
        for (const auto& [id, geometry] : geometry_results_) {
          if (all || geometry.label == label) {
            ids.push_back(id);
          }
        }
        if (foreground_ && (all || foreground_label_ == label)) {
          ids.push_back(foreground_id_);
        }
        if (!all && ids.size() != 1) {
          return err_json(ids.empty() ? "unknown exploration label" : "ambiguous exploration label; choose an id");
        }
      } else {
        ids.push_back(str("id", foreground_id_));
      }
    }
    size_t closed = 0;
    for (const auto& id : ids) {
      closed += discard_exploration(id);
    }
    return !all && !closed ? err_json("unknown exploration")
                           : std::format(R"({{"ok":true,"target":"main","closed":{}}})", closed);
  }
  std::shared_ptr<Replay_worker> worker;
  std::shared_ptr<Session>       geometry;
  const bool exporting_flags = cmd == "export" && str("what") == "flags";
  std::map<std::string, std::string> recipe_flags;
  {
    std::lock_guard lk(m_);
    if (d.HasMember("target") && !d["target"].IsString()) {
      return err_json("invalid target");
    }
    const auto target = str("target", foreground_id_.empty() || cmd == "hello" || cmd == "help" ? cfg_.target : foreground_id_);
    if (target != cfg_.target) {
      if (const auto found = geometry_results_.find(target); found != geometry_results_.end()) {
        geometry = found->second.document;
        if (exporting_flags) {
          recipe_flags = found->second.recipe_flags;
        }
        if (!geometry) {
          return err_json("geometry check still running");
        }
      } else if (auto j = background_.find(target); j != background_.end()) {
        worker = j->second.worker;
        if (exporting_flags) {
          recipe_flags = j->second.checkpoint.set;
        }
        if (!worker || !worker->ready()) {
          return err_json("background result unavailable");
        }
        if (cmd != "geometry" && cmd != "maps" && cmd != "clusters" && cmd != "signature" && cmd != "state" && cmd != "summary"
            && cmd != "export" && cmd != "command_status" && cmd != "describe" && cmd != "find" && cmd != "selection" && cmd != "critical" && cmd != "evidence"
            && cmd != "leverage") {
          return err_json("background results are read-only");
        }
      } else if (!foreground_ || target != foreground_id_) {
        return err_json("unknown or closed exploration");
      } else {
        worker = foreground_;
        if (exporting_flags) {
          recipe_flags = foreground_flags_;
        }
      }
    }
  }
  if (exporting_flags && !recipe_flags.empty()) {
    // The controller owns the submitted recipe. The live or archived target owns its current policy.
    if (worker && !worker->ready()) {
      return err_json("exploration is not ready");
    }
    try {
      const auto policy = geometry ? geometry->agent_call(json, timeout_ms) : worker->agent(json);
      rapidjson::Document result, options;
      result.Parse(policy.c_str());
      if (!result.IsObject() || !result.HasMember("set") || !result["set"].IsObject()) {
        return policy;
      }
      options.Parse(options_json().c_str());
      for (const auto& [key, value] : recipe_flags) {
        if (key == "lhd.seed") {
          const auto it = std::find(cfg_.worker_args.begin(), cfg_.worker_args.end(), "--seed");
          if (it != cfg_.worker_args.end() && it + 1 != cfg_.worker_args.end() && *(it + 1) == value) {
            continue;
          }
        } else if (options.IsArray() && std::ranges::any_of(options.GetArray(), [&](const auto& opt) {
                     return key == opt["name"].GetString() && value == opt["value"].GetString();
                   })) {
          continue;
        }
        auto& alloc = result.GetAllocator();
        result["set"].AddMember(rapidjson::Value(key.c_str(), alloc), rapidjson::Value(value.c_str(), alloc), alloc);
      }
      rapidjson::StringBuffer buffer;
      rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
      result.Accept(writer);
      return buffer.GetString();
    } catch (const std::exception& e) {
      return err_json(e.what());
    }
  }
  if (geometry) {
    if (cmd != "geometry" && cmd != "maps" && cmd != "clusters" && cmd != "signature" && cmd != "state" && cmd != "summary" && cmd != "export" && cmd != "describe"
        && cmd != "find" && cmd != "selection") {
      return err_json("completed geometry results are read-only");
    }
    return geometry->agent_call(json, timeout_ms);
  }
  if (worker) {
    if (!worker->ready()) {
      return err_json("exploration is not ready: " + worker->status());
    }
    try {
      return worker->agent(json);
    } catch (const std::exception& e) {
      return err_json(e.what());
    }
  }
  if (cmd == "whatif") {
    std::unique_lock lock(m_);
    if (!frame_ || (state_name_ != "gate" && state_name_ != "paused") || !queue_.empty()) {
      return err_json("whatif requires a stable placement gate");
    }
    if (auto why = expected_revision_locked(); !why.empty()) {
      return err_json(why);
    }
    if (!d.HasMember("candidates") || !d["candidates"].IsArray() || d["candidates"].Empty() || d["candidates"].Size() > 64) {
      return err_json("whatif requires 1 to 64 candidate moves or batches");
    }
    if (inline_requests_.size() >= 64) {
      return err_json("inline timing queue full");
    }
    const auto max_nets = integer("max_nets", 128);
    if (max_nets <= 0 || max_nets > 4096) {
      return err_json("max_nets must be in [1,4096]");
    }
    auto request        = std::make_shared<Inline_request>();
    request->frame      = frame_->id;
    const auto baseline = baseline_identity_locked();
    for (const auto& candidate : d["candidates"].GetArray()) {
      rapidjson::StringBuffer                    buf;
      rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
      rapidjson::Document                        copy;  // a candidate is a move unless it says it is a batch
      copy.CopyFrom(candidate, copy.GetAllocator());
      if (copy.IsObject() && !copy.HasMember("cmd")) {
        copy.AddMember("cmd", "move", copy.GetAllocator());
      }
      copy.Accept(writer);
      Command move;
      if (auto why = parse_command(buf.GetString(), move); !why.empty()) {
        return err_json("candidate " + std::to_string(request->trials.size()) + ": " + why);
      }
      if (move.cmd != "move" && move.cmd != "batch") {
        return err_json("inline candidates require move or batch");
      }
      if (auto why = resolve_move_locked(move); !why.empty()) {
        return err_json(why);
      }
      if (!move.orientations.empty()
          || std::ranges::any_of(move.batch, [](const Command& entry) { return !entry.orientations.empty(); })) {
        return err_json("journal trials support translations; use T1 for orientation");
      }
      Timing_trial trial;
      trial.max_nets = max_nets;
      auto collect   = [&](const Command& command) {
        for (size_t i = 0; i < command.nodes.size(); ++i) {
          const auto node = scene_->index.at(command.nodes[i]);
          if (!scene_->nodes[node].movable) {
            return false;
          }
          trial.moves.push_back({node, command.x[i], command.y[i]});
        }
        return true;
      };
      if (!collect(move) || !std::ranges::all_of(move.batch, collect)) {
        return err_json("candidate requires a movable object");
      }
      request->trials.push_back(std::move(trial));
    }
    inline_requests_.push_back(request);
    cv_.notify_all();
    if (!cv_.wait_for(lock, std::chrono::milliseconds(std::clamp(integer("timeout_ms", timeout_ms), 1, 120000)), [&] {
          return request->done || exit_.load();
        })) {
      std::erase(inline_requests_, request);
      return err_json("inline timing request timed out");
    }
    if (!request->done || !request->error.empty()) {
      return err_json(request->done ? request->error : "session closed");
    }
    std::string rows;
    for (const auto& trial : request->trials) {
      const bool ok  = trial.error.empty();
      rows          += std::format(R"({}{{"ok":{},"error":"{}","touched_nets":{},"wns_ps":{},"tns_ps":{},"failing_endpoints":{}}})",
                          rows.empty() ? "" : ",",
                          ok,
                          esc(trial.error),
                          trial.touched,
                          ok ? num(trial.wns_ps) : "null",
                          ok ? num(trial.tns_ps) : "null",
                          ok ? std::to_string(trial.failing) : "null");
    }
    return std::format(
        R"({{"ok":true,"baseline":"{}","frame":{},"worker":"inline","model":"placement_rc","rollback_verified":true,"baseline_timing":{{"wns_ps":{},"tns_ps":{},"failing_endpoints":{}}},"candidates":[{}]}})",
        baseline,
        request->frame,
        num(request->wns_ps),
        num(request->tns_ps),
        request->failing,
        rows);
  }
  if (cmd == "score_config") {
    if (!d.HasMember("components") || !d["components"].IsArray()) {
      return err_json("score_config requires a components array");
    }
    const auto estimator = str("estimator", "rudy");
    if (estimator != "rudy" && estimator != "rsmt") {
      return err_json("unknown score estimator");
    }
    Score_components config;
    double           sum = 0;
    for (const auto& term : d["components"].GetArray()) {
      if (!term.IsObject() || !term.HasMember("loss") || !term["loss"].IsString() || !term.HasMember("weight")
          || !term["weight"].IsNumber() || !term.HasMember("half") || !term["half"].IsNumber()) {
        return err_json("each score component needs loss, weight and half");
      }
      const std::string name = term["loss"].GetString();
      if (name != "negative_wns_ps" && name != "negative_tns_ps" && name != "routing_overflow" && name != "overflow_bin_share"
          && name != "hpwl_um") {
        return err_json("unsupported score loss " + name);
      }
      const auto weight = term["weight"].GetDouble(), half = term["half"].GetDouble();
      if (!std::isfinite(weight) || weight < 0 || !std::isfinite(half) || half <= 0
          || !config.emplace(name, std::pair{weight, half}).second) {
        return err_json("invalid or repeated score component");
      }
      sum += weight;
    }
    if (!config.empty() && std::abs(sum - 1) > 1e-8) {
      return err_json("score weights must sum to one");
    }
    std::lock_guard lock(m_);
    if (!foreground_id_.empty()) {
      return err_json("discard the foreground exploration before changing main");
    }
    cfg_.score           = std::move(config);
    cfg_.score_estimator = estimator;
    score_map_wanted_    = true;
    if (all_maps_wanted_ && wanted_map_ != estimator) {
      wanted_map_ = estimator;
      map_dirty_ = true;
    }
    bump_locked();
    return R"({"ok":true,"summary":)" + summary_locked() + "}";
  }
  if (cmd == "signature" || cmd == "geometry") {
    std::shared_ptr<const Scene> scene;
    std::shared_ptr<const Frame> frame;
    std::shared_ptr<const std::string> report;
    {
      std::lock_guard lock(m_);
      scene = scene_;
      frame = frame_;
      if (cmd == "geometry" && findings_scene_.lock() == scene && findings_frame_.lock() == frame) {
        report = findings_json_;
      }
    }
    if (!scene || !frame) {
      return err_json("no published placement");
    }
    if (cmd == "signature") {
      return macro_signature(*scene, *frame);
    }
    if (!report) {
      report = std::make_shared<const std::string>(floorplan_findings(*scene, *frame));
      // A slow inspection of a previous frame must not evict a newer result.
      std::lock_guard lock(m_);
      if (scene == scene_ && frame == frame_ && report->size() <= 4 * 1024 * 1024) {
        findings_scene_ = scene;
        findings_frame_ = frame;
        findings_json_  = report;
      }
    }
    return *report;
  }
  if (cmd == "maps") {
    std::string maps;
    std::shared_ptr<const Scene> scene;
    std::shared_ptr<const Frame> frame;
    bool has_density = false;
    {
      std::lock_guard lock(m_);
      scene = scene_;
      frame = frame_;
      if (frame && scene && frame->id == measured_maps_frame_) {
        for (const auto& [kind, grid] : measured_maps_) {
          if (!maps.empty()) {
            maps += ",";
          }
          maps += grid_findings(*scene, *frame, kind, grid);
        }
        has_density = measured_maps_.contains("density");
      }
    }
    if (!has_density && frame && scene) {
      if (!maps.empty()) {
        maps += ",";
      }
      maps += grid_findings(*scene, *frame, "density", occupancy_grid(*scene, *frame, 32));
    }
    return std::format(R"({{"ok":true,"maps":[{}]}})", maps);
  }
  if (cmd == "macro_scan") {
    std::shared_ptr<const Scene> scene;
    std::shared_ptr<const Frame> frame;
    bool                         orientation_editable = false;
    std::shared_ptr<const Chunk_frame> chunks;
    {
      std::lock_guard lock(m_);
      if (auto why = expected_revision_locked(); !why.empty()) {
        return err_json(why);
      }
      scene = scene_;
      frame = frame_;
      // the same condition resolve_move_locked applies to an orientation change
      orientation_editable = bool(orientation_provider_) && state_name_ == "gate" && (gate_name_ == "fp" || gate_name_ == "macro");
    }
    chunks = chunk_frame_of(scene);
    if (!scene || !frame) {
      return err_json("no published placement");
    }
    return macro_scan(*scene, *frame, d, orientation_editable, chunks.get());
  }
  if (cmd == "clusters") {
    std::lock_guard lk(m_);
    if (!scene_) {
      return err_json("no published scene");
    }
    std::string rows;
    for (const auto& [name, members] : scene_->groups) {
      rows += std::format(R"({}{{"name":"{}","members":{}}})", rows.empty() ? "" : ",", esc(name), members.size());
    }
    return R"({"ok":true,"clusters":[)" + rows + "]}";
  }
  if (cmd == "summary") {
    std::lock_guard lock(m_);
    return summary_locked();
  }
  if (cmd == "hello") {
    bool        ready;
    std::string reason;
    {
      std::lock_guard lock(m_);
      ready  = snapshot_owner_ && snapshot_baseline_ == baseline_identity_locked();
      reason = snapshot_reason_locked();
    }
    return std::format(
        R"({{"ok":true,"session":{},"url":"{}","capabilities":{{"controller":"separate process","snapshot":{},"snapshot_ready":{},"snapshot_reason":"{}","geometry":true,"replay":{},"terminal_repair":{},"external_validation":{},"repair_operations":["sizing","buffering"{}],"local_repair":{},"local_repair_operations":["sizing"],"h2d_intent":false,"rigid_macro_magnets":false,"region_macro_buses":false}},"options":{},"state":{}}})",
        cfg_.hello,
        esc(url()),
        cfg_.snapshot_available && cfg_.explore_worker != "replay",
        ready,
        esc(reason),
        !cfg_.worker_args.empty(),
        cfg_.repair_available && cfg_.heavy_jobs > 0 && !cfg_.worker_args.empty(),
        !cfg_.validation_args.empty() && cfg_.heavy_jobs > 0,
        cfg_.resynthesis_available ? R"(,"resynthesis")" : "",
        cfg_.repair_available && cfg_.heavy_jobs > 0 && !cfg_.worker_args.empty(),
        options_json(),
        state_json());
  }
  if (cmd == "chunk_interface") {
    std::string design, intent;
    uint64_t    frame = 0, revision = 0;
    {
      std::lock_guard lock(m_);
      if (!scene_ || !frame_ || scene_->phase != "fp" || scene_->chunk_plan.empty() || state_name_ != "gate") {
        return err_json("interface edits require the live chunk floorplan gate");
      }
      if (auto why = expected_revision_locked(); !why.empty()) {
        return err_json(why);
      }
      design   = scene_->design;
      intent   = cfg_.intent;
      frame    = frame_->id;
      revision = intent_revision_;
    }
    auto dst = str("chunk"), src = str("source"), ori = str("orientation", "auto");
    if (dst.empty() || (ori != "clear" && src.empty()) || (d.HasMember("orientation") && !d["orientation"].IsString())
        || (d.HasMember("apply") && !d["apply"].IsBool())) {
      return err_json("supply chunk, source and orientation (or clear), and an optional boolean apply");
    }
    const bool       apply = d.HasMember("apply") && d["apply"].GetBool();
    metadata::Bundle prior(intent), edits;
    bool             removed = false;
    for (auto* record : prior.select("livehd.placement.chunk_guidance", design)) {
      rapidjson::Document r;
      r.CopyFrom(*record, r.GetAllocator());
      if (r.HasMember("interface_copies") && r["interface_copies"].IsArray()) {
        auto& a = r["interface_copies"];
        for (auto it = a.Begin(); it != a.End();) {
          if (it->IsObject() && it->HasMember("target") && (*it)["target"].IsString() && dst == (*it)["target"].GetString()) {
            if (!r.HasMember("design") || r["design"].IsNull()) {
              return err_json("this interface is set by design-wide default metadata; edit that input explicitly");
            }
            it      = a.Erase(it);
            removed = true;
          } else {
            ++it;
          }
        }
      }
      edits.replace(r);
    }
    if (ori == "clear" && !removed) {
      return err_json("this home has no authored interface copy");
    }
    if (ori != "clear") {
      const auto          id = "interface:" + dst;
      rapidjson::Document r(rapidjson::kObjectType);
      auto&               a = r.GetAllocator();
      for (auto* e : edits.select("livehd.placement.chunk_guidance", design)) {
        if (e->HasMember("id") && (*e)["id"].IsString() && id == (*e)["id"].GetString() && e->HasMember("design")
            && (*e)["design"].IsString()) {
          r.CopyFrom(*e, a);
        }
      }
      if (!r.HasMember("class")) {
        r.AddMember("class", "livehd.placement.chunk_guidance", a)
            .AddMember("design", rapidjson::Value(design.c_str(), a), a)
            .AddMember("id", rapidjson::Value(id.c_str(), a), a);
      }
      if (!r.HasMember("interface_copies")) {
        r.AddMember("interface_copies", rapidjson::Value(rapidjson::kArrayType), a);
      }
      rapidjson::Value op(rapidjson::kObjectType);
      op.AddMember("source", rapidjson::Value(src.c_str(), a), a)
          .AddMember("target", rapidjson::Value(dst.c_str(), a), a)
          .AddMember("orientation", rapidjson::Value(ori.c_str(), a), a);
      r["interface_copies"].PushBack(op, a);
      edits.replace(r);
    }
    return submit_reply(std::format(R"({{"cmd":"{}","expect":{{"frame":{},"intent":{}}},"records":{}}})",
                                    apply ? "intent_apply" : "intent_preview",
                                    frame,
                                    revision,
                                    edits.json()));
  }
  if (cmd == "export" && str("what") != "macros") {
    std::lock_guard lock(m_);
    const auto      what = str("what");
    if (what == "relationships") {
      if (!scene_ || !frame_) {
        return err_json("no placement available");
      }
      std::string moves;
      for (const auto& move : relationships_) {
        if (!moves.empty()) {
          moves += ',';
        }
        moves += move;
      }
      return std::format(R"({{"ok":true,"target":"{}","frame":{},"units":"dbu","dbu_per_um":{},"moves":[{}]}})",
                         esc(cfg_.target),
                         frame_->id,
                         scene_->dbu_per_um,
                         moves);
    }
    if (what == "chunks" || what == "chunk_pins" || what == "chunk_peers") {
      if (!scene_ || scene_->chunk_plan.empty()) {
        return err_json("no chunk floorplan at this frame");
      }
      auto view    = str("view", "live");
      bool preview = view == "preview";
      if (view != "live" && !preview) {
        return err_json("chunk view must be live or preview");
      }
      if (preview && (intent_status_ != "preview" || !frame_ || intent_report_frame_ != frame_->id)) {
        return err_json("no current chunk preview");
      }
      rapidjson::Document plan;
      plan.Parse<rapidjson::kParseFullPrecisionFlag>((preview ? intent_report_ : scene_->chunk_plan).c_str());
      if (!plan.IsObject() || !plan.HasMember("chunks")) {
        return err_json("no chunk plan in this result");
      }
      auto filter = str("chunk");
      int  start = integer("offset", 0), limit = integer("limit", 64);
      if (start < 0 || limit < 1 || limit > 256 || (d.HasMember("offset") && !d["offset"].IsInt())
          || (d.HasMember("limit") && !d["limit"].IsInt())) {
        return err_json("use a nonnegative offset and a limit between 1 and 256");
      }
      if (what == "chunk_peers") {
        auto homes = plan["chunks"].GetArray();
        auto src   = std::find_if(homes.Begin(), homes.End(), [&](const auto& c) { return filter == c["id"].GetString(); });
        if (src == homes.End()) {
          return err_json("chunk_peers requires a known physical chunk");
        }
        auto root = [](const auto& c) -> std::string_view {
          return (c.HasMember("interface_copy") ? c["interface_copy"]["source"] : c["id"]).GetString();
        };
        // The planner emits one boundary contact per (home, crossing net).
        std::unordered_set<uint64_t> incident;
        for (const auto& p : (*src)["ports"].GetArray()) {
          incident.insert(p["net_index"].GetUint64());
        }
        rapidjson::Document reply(rapidjson::kObjectType);
        auto&               a = reply.GetAllocator();
        rapidjson::Value    peers(rapidjson::kArrayType);
        unsigned            total = 0;
        for (const auto& c : homes) {
          if (&c == &*src) {
            continue;
          }
          const bool logical  = c["partition"] == (*src)["partition"];
          const bool physical = logical && c["implementation"] == (*src)["implementation"];
          const bool pins     = root(c) == root(*src);
          unsigned   shared   = 0;
          for (const auto& p : c["ports"].GetArray()) {
            shared += incident.contains(p["net_index"].GetUint64());
          }
          if (!shared && !logical && !pins) {
            continue;
          }
          if (total++ < unsigned(start) || peers.Size() >= unsigned(limit)) {
            continue;
          }
          rapidjson::Value row(rapidjson::kObjectType);
          row.AddMember("id", rapidjson::Value(c["id"], a), a)
              .AddMember("box_dbu", rapidjson::Value(c["box_dbu"], a), a)
              .AddMember("shared_nets", shared, a)
              .AddMember("same_partition", logical, a)
              .AddMember("same_implementation", physical, a)
              .AddMember("shared_interface", pins, a);
          peers.PushBack(row, a);
        }
        reply.AddMember("ok", true, a)
            .AddMember("view", rapidjson::Value(view.c_str(), a), a)
            .AddMember("chunk", rapidjson::Value(filter.c_str(), a), a)
            .AddMember("frame", frame_ ? frame_->id : 0, a)
            .AddMember("intent_revision", intent_revision_, a)
            .AddMember("sequence", preview ? intent_report_sequence_ : 0, a)
            .AddMember("total", total, a)
            .AddMember("peers", peers, a);
        return metadata::canonical(reply);
      }
      if (what == "chunk_pins") {
        auto match = str("match", "all");
        if (filter.empty() || (match != "all" && match != "copied" && match != "local")) {
          return err_json("supply a physical chunk and match=all, copied or local");
        }
        auto homes = plan["chunks"].GetArray();
        auto c     = std::find_if(homes.Begin(), homes.End(), [&](const auto& h) { return filter == h["id"].GetString(); });
        if (c == homes.End()) {
          return err_json("unknown physical chunk: " + filter);
        }
        rapidjson::Document          reply(rapidjson::kObjectType);
        auto&                        a = reply.GetAllocator();
        rapidjson::Value             rows(rapidjson::kArrayType);
        size_t                       total = 0;
        std::map<uint64_t, unsigned> page_sources;
        for (const auto& pin : (*c)["ports"].GetArray()) {
          bool copied = pin.HasMember("copy_source_net");
          if ((match == "copied" && !copied) || (match == "local" && copied)) {
            continue;
          }
          if (total++ < unsigned(start) || rows.Size() >= unsigned(limit)) {
            continue;
          }
          if (copied) {
            page_sources.emplace(pin["copy_source_net"].GetUint64(), rows.Size());
          }
          rows.PushBack(rapidjson::Value(pin, a), a);
        }
        if (c->HasMember("interface_copy")) {
          const auto& copy = (*c)["interface_copy"];
          reply.AddMember("interface_copy", rapidjson::Value(copy, a), a);
          for (const auto& h : homes) {
            if (std::string_view(h["id"].GetString()) != copy["source"].GetString() || page_sources.empty()) {
              continue;
            }
            for (const auto& pin : h["ports"].GetArray()) {
              auto found = page_sources.find(pin["net_index"].GetUint64());
              if (found != page_sources.end()) {
                rows[found->second]
                    .AddMember("source_net", rapidjson::Value(pin["net"], a), a)
                    .AddMember("source_xy_dbu", rapidjson::Value(pin["xy_dbu"], a), a);
              }
            }
            break;
          }
        }
        reply.AddMember("ok", true, a)
            .AddMember("view", rapidjson::Value(view.c_str(), a), a)
            .AddMember("chunk", rapidjson::Value(filter.c_str(), a), a)
            .AddMember("frame", frame_ ? frame_->id : 0, a)
            .AddMember("intent_revision", intent_revision_, a)
            .AddMember("sequence", preview ? intent_report_sequence_ : 0, a)
            .AddMember("total", uint64_t(total), a)
            .AddMember("pins", rows, a);
        return metadata::canonical(reply);
      }
      std::map<std::string_view, const rapidjson::Value*> measurements;
      if (plan.HasMember("local_placement")) {
        for (const auto& r : plan["local_placement"].GetArray()) {
          measurements.emplace(r["id"].GetString(), &r);
        }
      }
      rapidjson::Document reply(rapidjson::kObjectType);
      auto&               a = reply.GetAllocator();
      rapidjson::Value    rows(rapidjson::kArrayType);
      int                 seen = 0;
      for (const auto& c : plan["chunks"].GetArray()) {
        auto id = c["id"].GetString();
        if (!filter.empty() && filter != id) {
          continue;
        }
        if (seen++ < start || rows.Size() >= unsigned(limit)) {
          continue;
        }
        rapidjson::Value row(rapidjson::kObjectType);
        for (const auto* f : {"id",
                              "partition",
                              "implementation",
                              "box_dbu",
                              "density",
                              "estimated_bytes",
                              "incident_pin_hpwl_dbu",
                              "interface_copy"}) {
          if (c.HasMember(f)) {
            row.AddMember(rapidjson::StringRef(f), rapidjson::Value(c[f], a), a);
          }
        }
        row.AddMember("members", c["members"].Size(), a).AddMember("ports", c["ports"].Size(), a);
        if (auto it = measurements.find(id); it != measurements.end()) {
          row.AddMember("measurement", rapidjson::Value(*it->second, a), a);
        }
        rows.PushBack(row, a);
      }
      if (!filter.empty() && !seen) {
        return err_json("unknown physical chunk: " + filter);
      }
      reply.AddMember("ok", true, a)
          .AddMember("view", rapidjson::Value(view.c_str(), a), a)
          .AddMember("frame", frame_ ? frame_->id : 0, a)
          .AddMember("intent_revision", intent_revision_, a)
          .AddMember("sequence", preview ? intent_report_sequence_ : 0, a)
          .AddMember("dbu_per_um", scene_->dbu_per_um, a)
          .AddMember("crossing_pin_hpwl_dbu", plan["communication"]["pin_hpwl_dbu"].GetDouble(), a)
          .AddMember("total", seen, a)
          .AddMember("chunks", rows, a);
      return metadata::canonical(reply);
    }
    if (what == "floorplan") {
      if (!scene_ || scene_->chunk_plan.empty()) {
        return err_json("no floorplan chunks in this scene");
      }
      metadata::Bundle bundle(cfg_.intent);
      for (const auto* value : {&scene_->chunk_plan, &scene_->chunk_results}) {
        if (!value->empty()) {
          rapidjson::Document r;
          r.Parse<rapidjson::kParseFullPrecisionFlag>(value->c_str());
          r.RemoveMember("local_placement");
          bundle.replace(r);
        }
      }
      return std::format(R"({{"ok":true,{}}})", record_field(bundle.json()));
    }
    if (what == "intent") {
      return std::format(R"({{"ok":true,"revision":{},"frame":{},"sequence":{},{},"bindings":{},"status":"{}"}})",
                         intent_revision_, intent_report_frame_, intent_report_sequence_,
                         record_field(cfg_.intent),
                         intent_report_,
                         esc(intent_status_));
    }
    if (what == "summary") {
      return summary_locked();
    }
    if (what == "score") {
      return R"({"ok":true,"summary":)" + summary_locked() + "}";
    }
    if (what == "flags") {
      const auto& policy = cfg_.policy;
      std::string flags;
      for (const auto& [key, on, initial] : {
               std::tuple{      "no_sizing",       policy.no_sizing,       initial_policy_.no_sizing},
               std::tuple{ "no_resynthesis",  policy.no_resynthesis,  initial_policy_.no_resynthesis},
               std::tuple{"no_buffer_edits", policy.no_buffer_edits, initial_policy_.no_buffer_edits}
      }) {
        if (on != initial) {
          flags += std::format(R"({}"synth.place.policy.{}":{})", flags.empty() ? "" : ",", key, on);
        }
      }
      return R"({"ok":true,"set":{)" + flags + "}}";
    }
    return err_json("unknown export");
  }
  if (cmd == "export" && str("what") == "macros") {
    std::lock_guard lk(m_);
    if (!scene_ || !frame_) {
      return err_json("no placement available");
    }
    const auto format = str("format", "dbu");
    if (format != "dbu" && format != "placereasoner" && format != "def" && format != "metadata") {
      return err_json("unknown macro placement format");
    }
    std::vector<Macro_pose> poses;
    std::string rows, components;
    size_t      count = 0;
    const auto  chunks = format == "dbu" ? chunk_frame_of(scene_) : nullptr;  // each macro's home chunk and its place in it
    for (size_t i = 0; i < scene_->nodes.size(); ++i) {
      const auto& n = scene_->nodes[i];
      if ((n.kind != Kind::fixed && n.kind != Kind::macro) || n.name.starts_with("__lhd_frame_mask_")) {
        continue;
      }
      const auto                                      x = std::llround(frame_->x.at(i)), y = std::llround(frame_->y.at(i));
      const auto&                                     name = format == "dbu" || n.external_name.empty() ? n.name : n.external_name;
      static const std::map<std::string, std::string> odb_orient{
          { "N",    "R0"},
          { "W",   "R90"},
          { "S",  "R180"},
          { "E",  "R270"},
          {"FN",    "MY"},
          {"FE", "MYR90"},
          {"FS",    "MX"},
          {"FW", "MXR90"}
      };
      if (format == "metadata") {
        poses.push_back({n.name, n.master, n.orient, x / scene_->dbu_per_um, y / scene_->dbu_per_um});
      } else if (format == "def") {
        components += std::format("- {} {} + FIXED ( {} {} ) {} ;\n", name, n.master, x, y, n.orient);
      } else {
        const double scale  = format == "placereasoner" ? scene_->dbu_per_um : 1;
        std::string home;
        if (const auto* chunk = chunks ? chunks->of(n.name) : nullptr) {
          home = std::format(R"(,"chunk":"{}","chunk_x":{},"chunk_y":{})", esc(chunk->id), num(double(x) - chunk->box[0]), num(double(y) - chunk->box[1]));
        }
        rows               += std::format(R"({}{{"name":"{}","x":{},"y":{},"orient":"{}"{}}})",
                            count ? "," : "",
                            esc(name),
                            num(x / scale),
                            num(y / scale),
                            format == "placereasoner" ? odb_orient.at(n.orient) : n.orient,
                            home);
      }
      ++count;
    }
    if (format == "metadata") {
      if (scene_->design.empty()) {
        return err_json("no design name for placement metadata");
      }
      metadata::Bundle bundle(cfg_.intent);
      if (!scene_->chunk_plan.empty()) {
        if(scene_->phase=="fp")return err_json("coarse chunk objects have no final macro poses; export floorplan at this gate");
        rapidjson::Document r;r.Parse<rapidjson::kParseFullPrecisionFlag>(scene_->chunk_plan.c_str());r.RemoveMember("local_placement");bundle.replace(r);
      }
      bundle.erase_selected("livehd.placement.state", scene_->design);
      bundle.replace(macro_state(scene_->design, poses));
      return std::format(R"({{"ok":true,{}}})", record_field(bundle.json()));
    }
    if (format == "def") {
      const auto section = std::format("COMPONENTS {} ;\n{}END COMPONENTS\n", count, components);
      rapidjson::Document info;
      info.Parse(cfg_.hello.c_str());
      const std::string top = info.IsObject() && info.HasMember("top") && info["top"].IsString()
                                  && info["top"].GetStringLength() ? info["top"].GetString() : "placement";
      const auto& die = scene_->frame_die.xh > scene_->frame_die.xl ? scene_->frame_die : scene_->die;
      const auto file = std::format("VERSION 5.8 ;\nDIVIDERCHAR \"/\" ;\nBUSBITCHARS \"[]\" ;\nDESIGN {} ;\n"
                                    "UNITS DISTANCE MICRONS {} ;\nDIEAREA ( {} {} ) ( {} {} ) ;\n{}END DESIGN\n",
                                    top, std::llround(scene_->dbu_per_um), std::llround(die.xl), std::llround(die.yl),
                                    std::llround(die.xh), std::llround(die.yh), section);
      return std::format(R"({{"ok":true,"frame":{},"components":"{}","def":"{}"}})", frame_->id, esc(section), esc(file));
    }
    std::string outline;
    if (format == "dbu") {
      for (const auto& ring : scene_->outline) {
        std::string points;
        for (const auto& [x, y] : ring) {
          points += std::format("{}[{},{}]", points.empty() ? "" : ",", x, y);
        }
        outline += std::format("{}[{}]", outline.empty() ? "" : ",", points);
      }
    }
    const auto& core = scene_->die;
    const auto view = format == "dbu" ? std::format(R"(,"core_dbu":[{},{},{},{}],"outline":[{}])",
                                                     core.xl, core.yl, core.xh, core.yh, outline) : "";
    return std::format(R"({{"ok":true,"units":"{}","dbu_per_um":{},"frame":{}{},"macros":[{}]}})",
                       format == "placereasoner" ? "um" : "dbu",
                       scene_->dbu_per_um,
                       frame_->id,
                       view,
                       rows);
  }
  if (cmd == "evaluate" && d.HasMember("wait_ms")) {
    // Full timing is asynchronous: the command is queued and `state.evaluation_seq` moves when the flow has measured.
    // With wait_ms the call returns the measurement (the summary) instead of leaving the caller to poll for it.
    std::string bad;
    const int   wait = bounded("wait_ms", 0, 0, 600000, bad);
    if (!bad.empty()) {
      return err_json(bad);
    }
    uint64_t before = 0;
    {
      std::lock_guard lk(m_);
      before = evaluation_seq_;
    }
    bool       accepted = false;
    const auto reply    = submit_reply(json, &accepted);
    if (!accepted || wait == 0) {
      return reply;
    }
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait);
    for (;;) {
      {
        std::lock_guard lk(m_);
        if (evaluation_seq_ != before) {
          return std::format(R"({{"ok":true,"evaluation_seq":{},"evaluation_frame":{},"frame":{},"summary":{}}})", evaluation_seq_, evaluated_frame_,
                             frame_ ? frame_->id : 0, summary_locked());
        }
        if (exit_.load()) {
          return err_json("session closed");
        }
      }
      if (std::chrono::steady_clock::now() >= until) {
        return err_json("evaluation did not finish within wait_ms; it is still queued, poll state.evaluation_seq");
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
  }
  if (cmd == "repair_scope" || cmd == "intent_apply" || cmd == "intent_preview" || cmd == "batch" || cmd == "relax" || cmd == "legalize" || cmd == "policy"
      || cmd == "evaluate" || cmd == "move" || cmd == "macro_move" || cmd == "pin" || cmd == "unpin") {
    return submit_reply(json);
  }
  if (cmd == "help") {
    // "overview" is the short map an agent reads first; "commands" is the full argument reference.
    return R"HELP({"ok":true,
"client":"tools/lhd_gui.py --workdir W <command> key=value ... (or --socket PATH, or LHD_GUI_SOCKET); a JSON object as <command> is sent verbatim. Guide: .claude/skills/livehd/agent_socket.md",
"overview":{
"start":["hello","state","summary"],
"inspect":["find","describe","selection","critical","geometry","maps","clusters","signature","leverage","evidence"],
"paint":["highlight","highlight_names","select","clear_highlight","highlights"],
"edit":["move","batch","pin","unpin","relax","legalize","evaluate","command_status","cmd"],
"explore":["whatif","explore","explore_begin","explore_wait","explorations","compare","similarity","explore_discard","score_config"],
"collateral":["export","intent_preview","intent_apply"],
"loop":"hello -> state (wait for a gate) -> inspect -> whatif/explore candidates -> compare -> apply with move/batch + evaluate -> export what=macros|intent|flags"},
"commands":{
"explore":"{tier:T0|T1|T2|T3|T4, target:main, patch, expect?, label?, client?} -> isolated geometry or frozen-netlist evaluation",
"geometry":"{target?} -> outline, utilization, macro periphery, cluster bounds, facing strips with standard-cell occupancy and open entrances, and rasterized dead space in DBU",
"whatif":"{candidates:[move or batch,...], max_nets?, expect?, timeout_ms?} -> measured journal trials and rollback at one gate",
"macro_scan":"{node, positions?:[[x,y],...] | grid?:{nx,ny}, orientations?:[N|FN|FS|S,...], top?, refine?, frame?:die|chunk, chunk?, expect?} -> positions (DBU, lower-left; with frame chunk relative to the home chunk, whose box is the grid and the limit) of one macro ranked by its pin nets' half-perimeter, legal ones only, the current pose included, the best refined downhill; cells frozen, no timing, other macros' pins not counted: screen here, then whatif the best",
"maps":"{} -> frame, availability, peak, overflow, over-capacity share and hotspot boxes of each measured grid",
"clusters":"{} -> available placement cluster names and member counts",
"explore_begin":"{expect?:{frame},set?:{physical_option:CLI_value}} -> start a private foreground replay at this gate",
"explorations":"{ids?,epoch?,known?} -> limits, main measurements and matching records. Supply the previous epoch and known {id:terminal|closed|evicted} to receive unchanged IDs in place of cached rows.",
"compare":"{reference,candidate} -> candidate minus reference at matched baseline, tier and scope, with geometry on each side and timing coverage by clock and path family",
"explore_wait":"{timeout_ms?} -> wait for replay readiness or failure",
"explore_discard":"{id? | label? | all:true} -> discard scratch and return to main",
"score_config":"{estimator,components:[{loss,weight,half}]} -> set an informational score; empty components disables it",
"summary":"{} -> current timing, routing and configured quality with measurement frame IDs",
"hello":"{} -> invocation, files, capabilities, options and state",
"export":"{what:macros|relationships|intent|floorplan|chunks|chunk_pins|chunk_peers|flags|summary|score|scope, format?:metadata|dbu|placereasoner|def, encoding?:json_text} -> placement or session data",
"chunk_interface":"{chunk,source?,orientation:auto|N|FN|FS|S|clear,apply?:false,expect?} -> preview or apply a physical home interface; inspect export what=chunks or what=chunk_pins (chunk required, match=all|copied|local), view=preview or live, offset?, limit?",
"intent_apply":"{records or records_json,expect?} -> replace the applicable placement hints; preserve other design metadata",
"intent_preview":"{records or records_json,expect?} -> bind records and measure residuals; export intent returns the preview report",
"signature":"{} -> macro side, order, orientation and quantized position by logical group",
"similarity":"{reference,candidate} -> matched signature fields divided by all possible fields across both sets of macros",
"leverage":"{} -> per-clock and per-family path coverage, macro pins and channel intersection proxy",
"evidence":"{} -> full-frame electrical violations and worst path when the timer provides them",
"batch":"{moves:[move,...],expect?} -> atomic ordered moves resolved on one baseline",
"relax":"{iterations,scope?} -> bounded GP and legalization with frozen exterior and implementation",
"legalize":"{scope?:{nodes:[names] | box:[xl,yl,xh,yh]}} -> legalize the placement at a gate, preserving active pins; see state.legalization",
"evaluate":"{expect?:{frame,baseline?,target?,intent?,evaluation?}} -> request full timing; state.evaluation_seq identifies the completed attempt, evaluation_frame its placement",
"move":"{nodes, x/y (one number for every node, or one per node) or dx/dy or align+reference, gap?, edges?:body|halo, orientation?, frame?:die|chunk, chunk?, final?, expect?} -> queue a DBU edit; with frame chunk, x and y are measured from the lower-left corner of the chunk (the one named by chunk, else each node's home chunk) and the instruction stays recorded that way; command_status returns resolved coordinates",
"macro_move":"{nodes,x,y (DBU, lower-left),orientation?:N|S|FN|FS (one or one per node; memory proxies only),final?} -> place a PLACED (fixed) macro, which move cannot: remembers the pose and re-runs the flow to the latest gate with it there (the session restarts: tools/lhd_gui.py --retry rides it out)",
"pin":"{nodes} -> anchor legal objects at their published positions",
"unpin":"{nodes} -> release selected anchors",
"find":"{regex, scope?, field?, limit?} -> the matching nodes (nothing is painted). scope: all|cells|comb|regs|macros|io|movable|fixed|selected; field: name (default)|master|any",
"highlight":"{regex, scope?, field?, color?, label?} -> paint the matches in the page; the same label repaints",
"highlight_names":"{names:[...], color?, label?}",
"clear_highlight":"{id | label | all:true}",
"highlights":"{} -> what is painted",
"critical":"{n? (25), margin_ps?} -> the most critical cells (worst pin slack estimate through the flow's timer), painted as 'critical'",
"select":"{names:[...]} or {regex, scope?, field?} -> the page's selection",
"selection":"{} -> details of what the user selected in the page (name, kind, master, class, size/position in um, pins)",
"describe":"{name} -> the same details for one node",
"state":"{} -> phase, state, iteration, WNS/TNS, checkpoints",
"command_status":"{target?,sequence} -> queued, applied, rejected, stale, processed or expired; applied moves include DBU poses, pin state and conflicts",
"cmd":"{command: {...}, target?, expect?} -> any page command (run, pause, continue, stage, move, unpin, centroid, map, timing, screenshot, restart, area {edge,delta um | box:[xl,yl,xh,yh]}, fork {box:[xl,yl,xh,yh] um | nodes:[...]} -> a new page for the region (state.forks), ...)"}})HELP";
  }
  if (cmd == "state") {
    return state_json();
  }
  if (cmd == "command_status") {
    if (!d.HasMember("sequence") || !d["sequence"].IsUint64() || !d["sequence"].GetUint64()) {
      return err_json("command_status requires a positive command sequence");
    }
    const auto sequence = d["sequence"].GetUint64();
    std::lock_guard lock(m_);
    if (sequence > queued_sequence_) {
      return err_json("unknown command sequence");
    }
    if (const auto found = command_results_.find(sequence); found != command_results_.end()) {
      return found->second;
    }
    return std::format(R"({{"ok":true,"sequence":{},"status":"{}"}})", sequence, sequence > completed_sequence_ ? "queued" : "expired");
  }
  if (cmd == "cmd") {  // the page's own command vocabulary (move, run, stage, ...), through the same queue
    if (!d.HasMember("command") || !d["command"].IsObject()) {
      return err_json("cmd needs a \"command\" object");
    }
    rapidjson::StringBuffer                    buf;
    rapidjson::Writer<rapidjson::StringBuffer> w(buf);
    auto& inner = d["command"];
    if (d.HasMember("expect")) {
      if (inner.HasMember("expect") && inner["expect"] != d["expect"]) {
        return err_json("conflicting command expectations");
      }
      if (!inner.HasMember("expect")) {
        inner.AddMember("expect", d["expect"].Move(), d.GetAllocator());
      }
    }
    if (inner.HasMember("target") && (!inner["target"].IsString() || inner["target"].GetString() != cfg_.target)) {
      return err_json("conflicting command targets");
    }
    if (!inner.HasMember("target")) {
      inner.AddMember("target", rapidjson::Value(cfg_.target.c_str(), d.GetAllocator()), d.GetAllocator());
    }
    inner.Accept(w);
    return submit_reply(buf.GetString());
  }

  if (cmd == "clear_highlight") {
    std::lock_guard lk(m_);
    if (auto why = expected_revision_locked(); !why.empty()) {
      return err_json(why);
    }
    const size_t before = highlights_.size();
    if (d.HasMember("all") && d["all"].IsBool() && d["all"].GetBool()) {
      highlights_.clear();
    } else if (d.HasMember("id") && d["id"].IsInt()) {
      std::erase_if(highlights_, [&](const Highlight& h) { return h.id == d["id"].GetInt(); });
    } else if (d.HasMember("label") && d["label"].IsString()) {
      std::erase_if(highlights_, [&](const Highlight& h) { return h.label == d["label"].GetString(); });
    } else {
      return err_json("clear_highlight needs id, label or all:true");
    }
    ++hl_version_;
    bump_locked();
    return std::format(R"({{"ok":true,"removed":{}}})", before - highlights_.size());
  }
  if (cmd == "highlights") {
    std::lock_guard lk(m_);
    std::string     out = R"({"ok":true,"highlights":[)";
    for (size_t k = 0; k < highlights_.size(); ++k) {
      const auto& h = highlights_[k];
      out += std::format(R"({}{{"id":{},"label":"{}","color":"{}","regex":"{}","scope":"{}","count":{}}})", k ? "," : "", h.id, esc(h.label), esc(h.color),
                         esc(h.regex), esc(h.scope), h.names.size());
    }
    return out + "]}";
  }
  if (cmd == "selection") {
    std::lock_guard lk(m_);
    std::string     out = std::format(R"({{"ok":true,"count":{},"nodes":[)", selection_count_);
    size_t          shown = 0;
    if (scene_) {
      for (const auto& name : selection_) {
        const auto it = scene_->index.find(name);
        if (it == scene_->index.end() || shown >= 20) {
          continue;
        }
        out += (shown++ ? "," : "") + node_details_locked(size_t(it->second));
      }
    }
    return out + "]}";
  }
  if (cmd == "describe") {
    std::lock_guard lk(m_);
    if (!scene_) {
      return err_json("no design yet");
    }
    auto name = str("name");
    if (!name.empty() && name[0] == '#') {  // node N of the current scene, as move/select/highlight_names take them
      const auto i = scene_->node_index(name);
      if (i >= 0) {
        name = scene_->nodes[size_t(i)].name;
      }
    }
    const auto it = scene_->index.find(name);
    if (it == scene_->index.end()) {
      return err_json("no such node in the current phase: " + str("name"));
    }
    return R"({"ok":true,"node":)" + node_details_locked(size_t(it->second)) + "}";
  }

  if (cmd == "evidence" || cmd == "leverage") {
    std::lock_guard lock(m_);
    const bool      valid = frame_ && evaluated_frame_ == frame_->id && has_timing_ && !timing_est_;
    std::string     path;
    if (valid) {
      for (const auto& pin : worst_path_) {
        path += std::format("{}\"{}\"", path.empty() ? "" : ",", esc(pin));
      }
    }
    return std::format(
        R"({{"ok":true,"frame":{},"scope":"full_design","model":"placement_rc","electrical_violations":{},"repair_advice":{},"worst_path":[{}],"endpoints":{}}})",
        evaluated_frame_,
        valid && electrical_violations_ >= 0 ? std::to_string(electrical_violations_) : "null",
        repair_advice_locked(),
        path,
        valid ? endpoint_report_ : "null");
  }
  if (cmd == "critical") {
    std::string bad;
    const int   n = bounded("n", 25, 1, 500, bad);
    if (!bad.empty()) {
      return err_json(bad);
    }
    std::unique_lock lk(m_);
    const bool       archived = state_name_ == "done";
    if (!archived) {
      if (finished_ || failed_ || !scene_) {
        return err_json("timing is only available while the flow is running or paused inside it");
      }
      crit_cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&] { return !crit_pending_; });  // one query at a time
      if (crit_pending_) {
        return err_json("another critical query is still waiting for the flow");
      }
      crit_request_.n         = n;
      crit_request_.margin_ps = number("margin_ps", -1);
      crit_pending_           = true;
      const auto before       = crit_seq_;
      cv_.notify_all();
      if (!crit_cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&] { return crit_seq_ != before || exit_.load(); })) {
        crit_pending_ = false;
        return err_json("timed out: the flow is inside a stage that does not poll; pause it or retry");
      }
    } else if (!has_timing_) {
      return err_json("full timing unavailable for this result");
    }
    if (!crit_error_.empty()) {
      return err_json(crit_error_);
    }
    std::vector<std::string> names;
    std::string              cells;
    for (size_t k = 0; k < crit_result_.size() && k < size_t(n); ++k) {
      const auto& [name, slack] = crit_result_[k];
      if (archived && number("margin_ps", -1) >= 0 && slack > crit_result_.front().second + number("margin_ps", -1)) {
        break;
      }
      names.push_back(name);
      const auto it = scene_->index.find(name);
      cells += std::format(R"({}{{"name":"{}","slack_ps":{},"master":"{}","register":{}}})", k ? "," : "", esc(name), num(slack),
                           it == scene_->index.end() ? "" : esc(scene_->nodes[size_t(it->second)].master),
                           it != scene_->index.end() && scene_->nodes[size_t(it->second)].seq ? "true" : "false");
    }
    const int id
        = archived ? -1 : add_highlight_locked("critical", str("color", "#ff4d4d"), "", "critical", std::move(names), false);
    return std::format(R"({{"ok":true,"id":{},"estimate":{},"frame":{},"wns_ps":{},"cells":[{}]}})",
                       id,
                       timing_est_,
                       evaluated_frame_,
                       num(stats_.wns_ps),
                       cells);
  }

  // The rest select nodes by name/regex: find, highlight, highlight_names, select.
  // The pattern is RE2 (linear time, no backtracking): a nested quantifier such as (.*)*x cannot hold the session
  // lock, and with it the page and the flow, for minutes. It is compiled before the lock is taken. Backreferences
  // and lookaround do not exist in RE2 and are refused with its message.
  std::unique_ptr<RE2> re;
  std::string          limit_error;
  const int            limit = cmd == "find" ? bounded("limit", 50, 1, 1000, limit_error) : 15;
  if (!limit_error.empty()) {
    return err_json(limit_error);
  }
  if (!(cmd == "highlight_names" || (cmd == "select" && d.HasMember("names"))) && (cmd == "find" || cmd == "highlight" || cmd == "select")) {
    if (str("regex").empty()) {
      return err_json(cmd + " needs a regex");
    }
    RE2::Options options;
    options.set_log_errors(false);
    options.set_max_mem(16 << 20);
    options.set_never_capture(true);
    re = std::make_unique<RE2>(str("regex"), options);
    if (!re->ok()) {
      return err_json("bad regex (RE2 syntax, no backreferences or lookaround): " + re->error());
    }
  }
  std::unique_lock lk(m_);
  if (!scene_) {
    return err_json("no design yet");
  }
  if (auto why = expected_revision_locked(); !why.empty()) {
    return err_json(why);
  }
  // The scene is immutable: scan it without the session lock. On a four-million-node design a scan is 100 ms or more, and
  // with the lock held every other client, the page and the flow's own publish waited behind it.
  const auto scene_hold    = scene_;
  const auto selection_now = selection_;
  lk.unlock();
  const auto&              s = *scene_hold;
  std::vector<std::string> names;
  bool                     truncated = false;
  std::string              regex_text = str("regex"), scope = str("scope", "all");
  if (cmd == "highlight_names" || (cmd == "select" && d.HasMember("names"))) {
    if (!d.HasMember("names") || !d["names"].IsArray()) {
      return err_json("names must be an array of strings");
    }
    for (const auto& v : d["names"].GetArray()) {
      if (!v.IsString()) {
        return err_json("names must be strings");
      }
      std::string name = v.GetString();
      if (!name.empty() && name[0] == '#') {  // node N of the current scene, as the page sends them
        const auto i = s.node_index(name);
        if (i < 0) {
          return err_json("unknown node " + name);
        }
        name = s.nodes[size_t(i)].name;
      }
      if (names.size() >= kMaxNames) {
        truncated = true;
        break;
      }
      names.push_back(std::move(name));
    }
  } else if (cmd == "find" || cmd == "highlight" || cmd == "select") {
    static const std::unordered_set<std::string> scopes{"all", "cells", "comb", "regs", "macros", "io", "movable", "fixed", "selected"};
    if (!scopes.contains(scope)) {
      return err_json("scope must be one of all, cells, comb, regs, macros, io, movable, fixed, selected");
    }
    const std::string field = str("field", "name");
    if (field != "name" && field != "master" && field != "any") {
      return err_json("field must be name, master or any");
    }
    std::unordered_set<std::string> chosen;
    if (scope == "selected") {
      chosen.insert(selection_now.begin(), selection_now.end());
    }
    for (const auto& n : s.nodes) {
      bool in = false;
      if (scope == "all") {
        in = n.kind != Kind::blockage;
      } else if (scope == "cells") {
        in = n.kind == Kind::cell;
      } else if (scope == "comb") {
        in = n.kind == Kind::cell && !n.seq;
      } else if (scope == "regs") {
        in = n.kind == Kind::cell && n.seq;
      } else if (scope == "macros") {
        in = n.kind == Kind::macro || n.kind == Kind::fixed;
      } else if (scope == "io") {
        in = n.kind == Kind::io;
      } else if (scope == "movable") {
        in = n.movable;
      } else if (scope == "fixed") {
        in = !n.movable && n.kind != Kind::io && n.kind != Kind::blockage;
      } else {
        in = chosen.contains(n.name);
      }
      if (!in) {
        continue;
      }
      const bool hit = (field != "master" && RE2::PartialMatch(n.name, *re)) || (field != "name" && RE2::PartialMatch(n.master, *re));
      if (hit) {
        if (names.size() >= kMaxNames) {
          truncated = true;
          break;
        }
        names.push_back(n.name);
      }
    }
  } else {
    return err_json("unknown cmd '" + cmd + "' (see cmd=help)");
  }
  std::string sample;
  for (size_t k = 0; k < names.size() && k < size_t(limit); ++k) {
    sample += std::format("{}\"{}\"", k ? "," : "", esc(names[k]));
  }
  if (cmd == "find") {
    return std::format(R"({{"ok":true,"count":{},"truncated":{},"names":[{}]}})", names.size(), truncated ? "true" : "false", sample);
  }
  lk.lock();  // the page's selection and highlights are session state
  if (cmd == "select") {
    const size_t count = names.size();
    set_selection_locked(std::move(names));
    return std::format(R"({{"ok":true,"count":{},"sample":[{}]}})", count, sample);
  }
  std::string label = str("label", regex_text.empty() ? "highlight" : regex_text), color = str("color");
  if (color.empty()) {
    static const char* palette[] = {"#ffd23f", "#3ddc97", "#ff6b6b", "#4cc9f0", "#f78fb3", "#b794f6"};
    color                        = palette[size_t(next_highlight_) % 6];
  }
  if (!valid_color(color)) {
    return err_json("color must be #rgb, #rrggbb or a CSS colour name");
  }
  const size_t count = names.size();
  const int    id    = add_highlight_locked(label, color, regex_text, scope, std::move(names), truncated);
  return std::format(R"({{"ok":true,"id":{},"label":"{}","color":"{}","count":{},"truncated":{},"sample":[{}]}})", id, esc(label), esc(color), count,
                     truncated ? "true" : "false", sample);
}

}  // namespace livehd::placement::gui
