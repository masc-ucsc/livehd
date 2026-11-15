// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gui_session.hpp"

#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <random>
#include <string_view>
#include <utility>

#include "core/hash_util.hpp"
#include "core/metadata.hpp"
#include "gui_geometry.hpp"
#include "gui_ipc.hpp"
#include "gui_render.hpp"
#include "gui_wire.hpp"
#include "gui_worker.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::placement::gui {

namespace {
constexpr std::array<std::string_view, 1> hint_class{"livehd.placement.hints"};
// These inputs may change the graph, the legal sites, or the initial macro poses.
constexpr std::array<std::string_view, 4> synthesis_placement_classes{
    "livehd.synth.tuning", "livehd.physical.row_access", "livehd.placement.hints", "livehd.placement.state"};

std::string esc(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
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
std::string num(double v) {
  if (!std::isfinite(v)) {
    return "null";
  }
  return std::format("{}", v);
}
std::string make_token() {
  std::random_device rd;
  std::string        t;
  for (int i = 0; i < 4; ++i) {
    t += std::format("{:08x}", rd());
  }
  return t;
}
}  // namespace

std::string Evaluation_work::json() const {
  return std::format(R"({{"timing_calls":{},"full_calls":{},"ranking_calls":{},"gp_iterations":{}}})",
                     full_calls + ranking_calls, full_calls, ranking_calls, gp_iterations);
}
std::string Session::evaluation_work_json() const {
  std::lock_guard lock(m_);
  return work_.json();
}
bool Session::admit_timing_locked(bool full) {
  if (cfg_.batch_worker && !replaying() && cfg_.timing_call_limit >= 0
      && work_.full_calls + work_.ranking_calls >= static_cast<uint64_t>(cfg_.timing_call_limit)) {
    timing_budget_exhausted_ = true;
    bump_locked();
    return false;
  }
  ++(full ? work_.full_calls : work_.ranking_calls);
  return true;
}
void Session::solver_timing_call(bool full) {
  if (controller_) {
    controller_->call("solver_timing_call", wire::pack(full));
    return;
  }
  std::lock_guard lock(m_);
  if (!admit_timing_locked(full)) {
    throw std::runtime_error("candidate timing call budget exhausted");
  }
}

void Scene::build_index() {
  build_floorplan_outline(*this);
  index.clear();
  index.reserve(nodes.size());
  for (size_t i = 0; i < nodes.size(); ++i) {
    index.emplace(nodes[i].name, int(i));
  }
  centroid_index.clear();
  for (size_t i = 0; i < centroids.size(); ++i) {
    centroid_index.emplace(centroids[i].name, int(i));
  }
}

// The browser uses #N for unnamed cells. Prefer an exact instance name if one exists.
int Scene::node_index(const std::string& name) const {
  if (const auto it = index.find(name); it != index.end()) {
    return it->second;
  }
  if (!name.starts_with('#')) {
    return -1;
  }
  unsigned i;
  const auto* end = name.data() + name.size();
  const auto [next, error] = std::from_chars(name.data() + 1, end, i);
  return error == std::errc{} && next == end && i < nodes.size() ? int(i) : -1;
}

Session::Session(Config c) : cfg_(std::move(c)) {
  initial_policy_ = cfg_.policy;
  if (!cfg_.controller_executable.empty()) {
    return;  // the controller owns the replay, trace and public state
  }
  token_ = cfg_.token.empty() ? make_token() : cfg_.token;
  paused_ = cfg_.start_paused;
  epoch_ = make_token().substr(0, 8);
  record_timeline_ = cfg_.replay_path.empty() || !cfg_.resume_label.empty();  // a batch replay (video) keeps no history
  if (!cfg_.replay_path.empty()) {
    paused_        = false;
    replay_active_ = true;
    std::ifstream in(cfg_.replay_path);
    std::string   line;
    while (std::getline(in, line)) {
      rapidjson::Document d;
      d.Parse<rapidjson::kParseFullPrecisionFlag>(line.c_str());
      if (d.HasParseError() || !d.IsObject() || !d.HasMember("cmd") || !d["cmd"].IsString()) {
        continue;
      }
      if (d.HasMember("seq") && d["seq"].IsUint64() && d["seq"].GetUint64() > cfg_.replay_limit_seq) {
        continue;  // after the checkpoint being resumed: not part of this run
      }
      if (std::string_view{d["cmd"].GetString()} == "macro_move") {
        // A fixed macro put somewhere by the user stays there for the whole flow, whatever checkpoint is resumed.
        Command m;
        if (parse_command(line, m).empty()) {
          for (size_t i = 0; i < m.nodes.size(); ++i) {
            macro_moves_[m.nodes[i]] = {m.x[i], m.y[i], ++macro_order_};  // file order is move order
          }
        }
        continue;
      }
      Replay_item item;
      item.phase = d.HasMember("phase") && d["phase"].IsString() ? d["phase"].GetString() : "";
      item.iter  = d.HasMember("iter") && d["iter"].IsInt() ? d["iter"].GetInt() : 0;
      const std::string cmd = d["cmd"].GetString();
      if (cmd == "extend") {
        item.cmd.cmd   = cmd;
        item.cmd.count = d.HasMember("count") && d["count"].IsInt() ? d["count"].GetInt() : 0;
        replay_.push_back(std::move(item));
      } else if (cmd == "move" || cmd == "place_positions" || cmd == "pin" || cmd == "stage" || cmd == "policy" || cmd == "intent_apply"
                 || cmd == "unpin" || cmd == "centroid" || cmd == "centroid_release"
                 || cmd == "timing") {  // run/pause/stage/finish/screenshot have no effect without a user
        if (parse_command(line, item.cmd).empty()) {
          // A main restart keeps its own authored moves. Scratch replay starts a fresh segment.
          if (cfg_.target == "main" && cmd == "move" && d.HasMember("relationship") && d["relationship"].IsObject()) {
            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            d["relationship"].Accept(writer);
            item.cmd.relationship = buffer.GetString();
          }
          replay_.push_back(std::move(item));
        }
      }
    }
  }
  if (!cfg_.trace_path.empty()) {
    trace_.open(cfg_.trace_path, std::ios::trunc);
    // The new trace keeps the macro moves this run started from, so the next restart finds them too.
    std::vector<std::pair<std::string, std::array<double, 3>>> by_age(macro_moves_.begin(), macro_moves_.end());
    std::sort(by_age.begin(), by_age.end(), [](const auto& a, const auto& b) { return a.second[2] < b.second[2]; });
    for (const auto& [name, p] : by_age) {  // oldest first, so the next parse reads the same order
      trace_ << std::format(R"({{"seq":{},"phase":"fp","iter":0,"cmd":"macro_move","nodes":["{}"],"x":[{}],"y":[{}]}})", ++seq_, esc(name),
                            num(p[0]), num(p[1]))
             << "\n";
    }
    trace_.flush();
  }
}

// Replay: apply the recorded commands of this phase whose iteration has been reached, in file order.
void Session::replay_locked(int iteration, Edits& edits, std::unique_lock<std::mutex>& lock) {
  while (!replay_.empty() && replay_.front().phase == stage_name_ && replay_.front().iter <= iteration
         && replay_.front().cmd.cmd != "extend") {
    auto command = std::move(replay_.front().cmd);
    replay_.pop_front();
    if (command.cmd == "intent_apply") {
      intent_unlocked(command, lock);
      if (intent_status_ != "applied") {
        throw std::runtime_error("replay could not bind placement intent: " + intent_status_);
      }
    } else {
      apply_locked(command, edits);
    }
  }
}

Session::~Session() {
  shutdown();
  stop_serving();
  if (dispatcher_.joinable()) {
    dispatcher_.request_stop();
    jobs_changed_.notify_all();
    dispatcher_.join();
  }
  background_.clear();
  foreground_.reset();
  if (!region_root_.empty()) {
    std::error_code ec;
    std::filesystem::remove_all(region_root_, ec);
  }
  if (scratch_lease_ >= 0) {
    ::close(scratch_lease_);
  }
}

std::string Session::url() const { return url_; }

// A restart point: the state at a gate (before a stage), after a GP, or in edit mode. Counting (label, nth) is
// deterministic for the same flags, which is how a resumed run finds the same point again.
void Session::checkpoint_locked(const std::string& label) {
  const bool pure_replay = !cfg_.replay_path.empty() && cfg_.resume_label.empty();
  const int  nth         = ++label_count_[label];
  if (label == exploration_stop_) {
    paused_ = true;
    exploration_stop_.clear();
  }
  if (!pure_replay) {
    Checkpoint cp;
    cp.label     = label;
    cp.phase     = stage_name_;
    cp.nth       = nth;
    cp.iteration = iteration_;
    cp.seq       = seq_;
    cp.scene     = scene_;
    cp.frame     = frame_;
    checkpoints_.push_back(std::move(cp));
    if (checkpoints_.size() > 40) {  // older snapshots are released; their restart keys stay
      checkpoints_[checkpoints_.size() - 41].frame.reset();
    }
  }
  record_timeline_locked(true);  // a gate, the end of a GP, edit mode: always a snapshot (replaces the same iteration)
  if (!timeline_.empty() && !pure_replay) {
    timeline_.back().cp = int(checkpoints_.size()) - 1;
  }
  if (replay_active_ && !cfg_.resume_label.empty() && label == cfg_.resume_label && nth == cfg_.resume_nth) {
    if (cfg_.target != "main") {
      replay_work_ = std::exchange(work_, {});
      if (cfg_.batch_worker) {
        timing_wanted_ = full_timing_wanted_ = false;
      }
    }
    replay_active_ = false;  // the replay reached the checkpoint: from here the user drives
    resumed_       = true;
    paused_        = true;
    note_          = std::format("replayed to checkpoint {} #{}", label, nth);
  }
  bump_locked();
}

void Session::set_options_json(std::string json) {
  if (controller_) {
    controller_->call("options", json);
    return;
  }
  std::lock_guard lk(m_);
  options_json_ = std::move(json);
}

std::string Session::options_json() const {
  std::lock_guard lk(m_);
  return options_json_;
}

// Keep a 16-bit snapshot of the published frame. Within the memory budget every poll is kept; beyond it the older
// part is thinned (every other snapshot) but checkpoints always stay.
void Session::record_timeline_locked(bool force) {
  if (!record_timeline_ || !scene_ || !frame_ || frame_->x.empty()) {
    return;
  }
  if (!force && !timeline_.empty()) {  // polls are thinned to one snapshot per history_every GP iterations
    const auto& last = timeline_.back();
    if (last.scene_id == scene_version_ && frame_->iteration >= last.iteration && frame_->iteration - last.iteration < std::max(1, cfg_.history_every)) {
      return;
    }
  }
  Tl_entry e;
  e.scene     = scene_;
  e.scene_id  = scene_version_;
  e.phase     = stage_name_;
  e.iteration = frame_->iteration;
  const size_t n  = frame_->x.size();
  const auto&  die = scene_->die;
  const double kx = 65535.0 / std::max(1e-9, die.xh - die.xl), ky = 65535.0 / std::max(1e-9, die.yh - die.yl);
  auto         q = std::make_shared<std::vector<std::uint16_t>>(2 * n);
  for (size_t i = 0; i < n; ++i) {
    (*q)[i]     = std::uint16_t(std::clamp(std::lround((frame_->x[i] - die.xl) * kx), 0L, 65535L));
    (*q)[n + i] = std::uint16_t(std::clamp(std::lround((frame_->y[i] - die.yl) * ky), 0L, 65535L));
  }
  e.q = std::move(q);
  if (!frame_->cx.empty()) {
    auto c = std::make_shared<std::vector<float>>();
    c->insert(c->end(), frame_->cx.begin(), frame_->cx.end());
    c->insert(c->end(), frame_->cy.begin(), frame_->cy.end());
    c->insert(c->end(), frame_->cr.begin(), frame_->cr.end());
    e.cent = std::move(c);
  }
  if (!timeline_.empty()) {  // the same poll republished (gate after poll, finished): replace instead of repeating
    auto& last = timeline_.back();
    if (last.scene_id == e.scene_id && last.iteration == e.iteration && last.cp < 0) {
      tl_bytes_ -= last.q->size() * 2;
      timeline_.pop_back();
    }
  }
  tl_bytes_ += e.q->size() * 2;
  timeline_.push_back(std::move(e));
  const size_t budget = size_t(std::max(1, cfg_.history_mb)) << 20;
  while (tl_bytes_ > budget && timeline_.size() > 16) {
    std::vector<Tl_entry> kept;
    kept.reserve(timeline_.size());
    for (size_t i = 0; i < timeline_.size(); ++i) {
      const bool recent = i + 8 >= timeline_.size();
      if (timeline_[i].cp >= 0 || recent || i % 2 == 0) {
        kept.push_back(std::move(timeline_[i]));
      } else {
        tl_bytes_ -= timeline_[i].q->size() * 2;
      }
    }
    if (kept.size() == timeline_.size()) {
      timeline_ = std::move(kept);
      break;
    }  // only protected snapshots remain
    timeline_ = std::move(kept);
  }
  ++tl_version_;
}

std::string Session::timeline_json() const {
  std::lock_guard          lk(m_);
  std::vector<std::string> phases;
  std::string              out = std::format(R"({{"v":{},"phases":[)", tl_version_);
  std::string              entries;
  for (size_t i = 0; i < timeline_.size(); ++i) {
    const auto& e   = timeline_[i];
    size_t      idx = 0;
    while (idx < phases.size() && phases[idx] != e.phase) {
      ++idx;
    }
    if (idx == phases.size()) {
      phases.push_back(e.phase);
    }
    entries += std::format("{}[{},{},{},{}]", i ? "," : "", e.iteration, e.cp, e.scene_id, idx);
  }
  for (size_t i = 0; i < phases.size(); ++i) {
    out += std::format("{}\"{}\"", i ? "," : "", esc(phases[i]));
  }
  return out + "],\"e\":[" + entries + "]}";
}

std::string Session::placement_identity_locked() const {
  if (!scene_ || !frame_) {
    return {};
  }
  return std::format("{:016x}", pose_hash_);
}

bool Session::native_pause_locked() const {
  return frame_ && frame_->stats.native_gp && state_name_ == "paused" && native_poll_frame_ == frame_->id && !pending_repair_
         && pending_moves_.empty() && dirty_pins_.empty() && released_.empty() && dirty_cpins_.empty() && !release_c_
         && native_poll_baseline_ == baseline_identity_locked();
}

std::string Session::native_gp_reason_locked() const {
  if (!frame_ || state_name_ != "paused" || !frame_->stats.optimizer_identity) {
    return "requires a paused global placement iteration";
  }
  if (cfg_.explore_worker == "replay") {
    return "main uses replay by configuration";
  }
  if (!cfg_.snapshot_available) {
    return "native continuation requires the fast timing checkpoint";
  }
  if (!frame_->stats.native_gp_reason.empty()) {
    return frame_->stats.native_gp_reason;
  }
  if (!frame_->stats.native_gp) {
    return "the current solver callback cannot retain an independent optimizer";
  }
  if (!native_pause_locked()) {
    return "main edits have not reached the paused optimizer";
  }
  if (snapshot_owner_ && snapshot_baseline_ == baseline_identity_locked() && !snapshot_owner_->carries_gp()) {
    return "the current checkpoint cannot retain native GP";
  }
  return {};
}

std::string Session::baseline_identity_locked() const {
  if (!frame_ || !scene_) {
    return {};
  }
  return std::format("{:016x}",
                     hash_util::fnv1a64(wire::pack(cfg_.input_identity,
                                                   hint_hash_,
                                                   cfg_.policy,
                                                   pins_,
                                                   cpins_,
                                                   stages_,
                                                   frame_->iteration,
                                                   frame_->stats.optimizer_identity,
                                                   frame_->cx,
                                                   frame_->cy,
                                                   frame_->cr),
                                        pose_hash_));
}

void Session::push_frame_locked(std::shared_ptr<const Frame> f) {
  pose_hash_ = design_hash_;
  for (size_t i = 0; i < f->x.size(); ++i) {
    pose_hash_ = hash_util::fnv1a64_u64(uint64_t(std::llround(f->x[i])), pose_hash_);
    pose_hash_ = hash_util::fnv1a64_u64(uint64_t(std::llround(f->y[i])), pose_hash_);
  }
  geometry_conflicts_.clear();
  if (scene_) {
    for (const auto& issue : macro_geometry(*scene_, *f)) {
      const auto& node = scene_->nodes[issue.node].name;
      if (issue.other < 0) {
        geometry_conflicts_[node].push_back(issue.cause);
      } else {
        const auto& other = scene_->nodes[issue.other];
        geometry_conflicts_[node].push_back(issue.cause + " with " + other.name);
        if (!other.exclusion) {
          geometry_conflicts_[other.name].push_back(issue.cause + " with " + node);
        }
      }
    }
  }
  for (const auto& [name, pin] : pins_) {
    if (geometry_conflicts_.contains(name)) {
      if (suspended_pins_.insert(name).second) {
        released_.insert(name);
      }
      dirty_pins_.erase(name);
    } else if (suspended_pins_.erase(name)) {
      dirty_pins_.insert(name);
      released_.erase(name);
    }
  }
  frame_ = f;
  history_.push_back(std::move(f));
  while (history_.size() > 8) {
    history_.pop_front();
  }
}

void Session::bump_locked() {
  if (snapshot_owner_ && snapshot_baseline_ != baseline_identity_locked()) {
    snapshot_owner_.reset();  // active children retain their own reference until reaped
    snapshot_baseline_.clear();
    snapshot_attempt_.clear();
    snapshot_reason_.clear();
  }
  ++version_;
  updates_.notify_all();
}

bool Session::switch_on_locked(const std::string& stage) const {
  for (const auto& s : stages_) {
    if (s.name == stage) {
      return s.on;
    }
  }
  return true;
}

Mutation_policy Session::policy() const {
  if (controller_) {
    return wire::read<Mutation_policy>(controller_->call("policy"));
  }
  std::lock_guard lock(m_);
  return cfg_.policy;
}

void Session::set_stages(std::vector<Stage_switch> st) {
  if (controller_) {
    controller_->call("stages", wire::pack(st));
    return;
  }
  std::lock_guard lk(m_);
  stages_ = std::move(st);
  bump_locked();
}

void Session::set_clock_period_ps(double period_ps) {
  if (controller_) {
    controller_->call("period", wire::pack(period_ps));
    return;
  }
  std::lock_guard lk(m_);
  period_ps_ = period_ps;
  bump_locked();
}

void Session::set_timing(double wns_ps, double tns_ps, int iteration) {
  if (controller_) {
    controller_->call("set_timing", wire::pack(wns_ps, tns_ps, iteration));
    return;
  }
  std::lock_guard lk(m_);
  set_timing_locked(wns_ps, tns_ps, iteration);
}

// Answer everyone waiting at this gate from one full baseline build. Timing
// samples belong to their requests and never alter main's last measurement.
void Session::service_inline(std::unique_lock<std::mutex>& lock, const Timing_provider* provider) {
  if (inline_requests_.empty()) {
    return;
  }
  auto         pending = std::exchange(inline_requests_, {});
  auto         frame   = frame_;
  Timing_query query;
  query.frame = frame.get();
  query.full  = true;
  for (auto& request : pending) {
    if (!frame || request->frame != frame->id || (state_name_ != "gate" && state_name_ != "paused")) {
      request->error = "stale placement gate";
    } else {
      query.trials.insert(query.trials.end(), request->trials.begin(), request->trials.end());
    }
  }
  const bool admitted = query.trials.empty() || !provider || admit_timing_locked(true);
  lock.unlock();
  std::string error;
  try {
    if (!admitted) {
      error = "candidate timing call budget exhausted";
    } else if (!query.trials.empty() && (!provider || !(*provider)(query) || !query.rollback_verified)) {
      error = "journal timing unavailable at this gate";
    }
  } catch (const std::exception& ex) {
    error = ex.what();
  }
  lock.lock();
  size_t offset = 0;
  for (auto& request : pending) {
    if (request->error.empty()) {
      request->error = error;
      if (error.empty()) {
        std::copy_n(query.trials.begin() + offset, request->trials.size(), request->trials.begin());
        request->wns_ps  = query.wns_ps;
        request->tns_ps  = query.tns_ps;
        request->failing = query.baseline_failing;
      }
      offset += request->trials.size();
    }
    request->done = true;
  }
  cv_.notify_all();
}

// Explicit requests (the `timing` command, a released edit) always run; otherwise a running poll refreshes while the
// time spent stays under `timing_budget` percent of the wall time since the session began, at most every 5 polls.
void Session::service_timing(std::unique_lock<std::mutex>& lk, const Timing_provider* tp, bool running) {
  service_inline(lk, tp);
  if (!tp) {
    if (!snapshot_requested_.empty()) {
      snapshot_attempt_ = std::exchange(snapshot_requested_, {});
      snapshot_reason_  = "no timing provider at this gate";
      jobs_changed_.notify_all();
    }
    if (timing_wanted_ || crit_pending_) {
      if (full_timing_wanted_) {
        note_             = "timing unavailable at this gate";
        evaluated_frame_  = frame_ ? frame_->id : 0;
        stats_.has_timing = has_timing_ = timing_est_ = false;
        ++evaluation_seq_;
      }
      timing_wanted_ = full_timing_wanted_ = false;
      if (crit_pending_) {
        crit_pending_ = false;
        crit_error_   = "timing unavailable at this gate";
        ++crit_seq_;
        crit_cv_.notify_all();
      }
      bump_locked();
    }
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (!t_start_set_) {
    t_start_set_ = true;
    t_start_     = now;
  }
  bool automatic = false;
  if (running && !cfg_.batch_worker && !replaying() && cfg_.timing_budget > 0) {
    ++polls_since_timing_;
    const double elapsed = std::chrono::duration<double>(now - t_start_).count();
    automatic            = polls_since_timing_ >= 5 && timing_spent_s_ <= cfg_.timing_budget / 100.0 * elapsed;
  }
  auto checkpoint_baseline = std::exchange(snapshot_requested_, {});
  if (!checkpoint_baseline.empty() && (checkpoint_baseline != baseline_identity_locked() || checkpoint_baseline == snapshot_attempt_)) {
    checkpoint_baseline.clear();
  }
  if (!checkpoint_baseline.empty()) {
    const auto prediction = memory_prediction(scene_->nodes.size(), false);
    if (memory_fits_locked(prediction)) {
      checkpoint_reserved_bytes_ = prediction;
    } else {
      snapshot_attempt_ = std::exchange(checkpoint_baseline, {});
      snapshot_reason_ = "session memory budget cannot accommodate a snapshot owner";
      jobs_changed_.notify_all();
    }
  }
  const bool checkpoint_only = !checkpoint_baseline.empty() && !timing_wanted_ && !automatic && !crit_pending_;
  if (!timing_wanted_ && !automatic && !crit_pending_ && checkpoint_baseline.empty()) {
    return;
  }
  const bool   critical = crit_pending_;
  Timing_query q;
  if (critical) {
    q = crit_request_;
  }
  q.full               = std::exchange(full_timing_wanted_, false) || !checkpoint_baseline.empty();
  q.request_checkpoint = !checkpoint_baseline.empty();
  if (q.request_checkpoint && native_pause_locked()) {
    q.native_endpoint = (std::filesystem::path(scratch_root_locked()) / std::format("g{:x}", frame_->id)).string();
  }
  q.want_critical      = critical || (q.full && !checkpoint_only);
  if (q.full && !critical) {
    q.n = 500;
  }
  timing_wanted_       = false;
  if (!checkpoint_only) {
    polls_since_timing_ = 0;
  }
  const auto started   = std::chrono::steady_clock::now();
  const auto evaluated_frame = frame_;
  q.frame                    = evaluated_frame.get();
  timing_operation_          = q.request_checkpoint ? "snapshot_setup" : q.full ? "full_timing" : "ranking_timing";
  const bool admitted = admit_timing_locked(q.full);
  bump_locked();
  lk.unlock();
  bool ok = false;
  try {
    if (admitted) {
      ok = (*tp)(q);
    } else {
      q.checkpoint_error = "candidate timing call budget exhausted";
    }
  } catch (const std::exception& ex) {
    q.checkpoint_error = ex.what();
  }
  lk.lock();
  if (!checkpoint_only) {
    last_timing_s_   = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    last_rc_s_       = q.rc_s;
    last_sta_s_      = q.sta_s;
    timing_spent_s_ += last_timing_s_;
  }
  if (q.request_checkpoint && frame_ == evaluated_frame) {
    snapshot_owner_.reset();
    snapshot_baseline_.clear();
    snapshot_reason_
        = q.checkpoint_error.empty() ? "no constrained fast timing checkpoint for the selected geometry" : q.checkpoint_error;
    if (ok && !q.checkpoint.empty()) {
      const auto document = result_snapshot_locked();
      lk.unlock();
      std::shared_ptr<Snapshot_owner> owner;
      std::string                     error;
      try {
        owner = std::make_shared<Snapshot_owner>(cfg_, q.checkpoint, document,q.native_endpoint);
      } catch (const std::exception& ex) {
        error = ex.what();
      }
      lk.lock();
      if (checkpoint_baseline == baseline_identity_locked()) {
        snapshot_reason_ = error;
        if (owner) {
          snapshot_owner_    = std::move(owner);
          snapshot_baseline_ = checkpoint_baseline;
        }
      }
    }
    snapshot_attempt_ = checkpoint_baseline;
    snapshot_setup_s_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    jobs_changed_.notify_all();
  }
  checkpoint_reserved_bytes_ = 0;
  timing_operation_ = "idle";
  bump_locked();
  if (checkpoint_only) {
    return;
  }
  if (ok) {
    if (q.hpwl_um >= 0) {
      stats_.hpwl_um = q.hpwl_um;
    }
    set_timing_locked(q.wns_ps, q.tns_ps, iteration_);
    evaluated_frame_ = evaluated_frame ? evaluated_frame->id : 0;
    timing_est_      = !q.full;  // a ranking refresh is an estimate: the worst pin slack, no TNS
  }
  if (q.full) {
    ++evaluation_seq_;
    hold_wns_ps_           = q.hold_wns_ps;
    electrical_violations_ = q.electrical_violations;
    worst_path_            = std::move(q.path);
    endpoint_report_       = std::move(q.endpoints);
    if (ok && frame_ == evaluated_frame && resumed_ && cfg_.target != "main" && !cfg_.batch_worker && !cfg_.agent_socket.empty()) {
      save_foreground_measurement_locked();
    }
  }
  if (!ok && q.full) {
    timing_est_       = false;
    evaluated_frame_  = evaluated_frame ? evaluated_frame->id : 0;
    stats_.has_timing = has_timing_ = false;
    note_                           = q.checkpoint_error.empty() ? "full timing unavailable for this placement" : q.checkpoint_error;
    bump_locked();
  }
  if (critical || q.full) {
    crit_pending_ = false;
    crit_error_   = ok ? "" : "timing is not available at this point of the flow";
    crit_result_  = std::move(q.critical);
    ++crit_seq_;
    crit_cv_.notify_all();
  }
}

void Session::set_timing_locked(double wns_ps, double tns_ps, int iteration) {
  evaluated_frame_        = 0;  // A solver sample has no GUI frame identity. A frame callback sets its exact id.
  timing_est_  = false;
  has_timing_  = true;
  wns_ps_      = wns_ps;
  tns_ps_      = tns_ps;
  timing_iter_ = iteration < 0 ? iteration_ : iteration;
  iteration    = timing_iter_;
  stats_.has_timing       = true;
  stats_.wns_ps           = wns_ps;
  stats_.tns_ps           = tns_ps;
  stats_.timing_iteration = iteration;
  bump_locked();
}

void Session::publish_locked(const double* x, const double* y, size_t n, const Stats& st_in, const std::string& state) {
  Stats st = st_in;
  if (has_timing_) {  // timing is sampled by the flow's checkpoints, not by every poll
    st.has_timing       = true;
    st.wns_ps           = wns_ps_;
    st.tns_ps           = tns_ps_;
    st.timing_iteration = timing_iter_;
  }
  auto f       = std::make_shared<Frame>();
  f->id        = ++frame_id_;
  f->phase     = stage_name_;
  f->state     = state;
  f->iteration = st.iteration;
  f->stats     = st;
  if (x && y) {
    const size_t m = scene_ ? std::min(n, scene_->nodes.size()) : n;
    f->x.resize(m);
    f->y.resize(m);
    for (size_t i = 0; i < m; ++i) {
      f->x[i] = x[i];
      f->y[i] = y[i];
    }
  } else if (frame_) {
    f->x = frame_->x;
    f->y = frame_->y;
  }
  if (scene_ && !scene_->centroids.empty()) {
    const size_t m = scene_->centroids.size();
    f->cx.resize(m);
    f->cy.resize(m);
    f->cr.resize(m);
    for (size_t i = 0; i < m; ++i) {
      const bool live = i < st.centroids.size();
      f->cx[i]        = float(live ? st.centroids[i][0] : (frame_ && i < frame_->cx.size() ? frame_->cx[i] : scene_->centroids[i].x));
      f->cy[i]        = float(live ? st.centroids[i][1] : (frame_ && i < frame_->cy.size() ? frame_->cy[i] : scene_->centroids[i].y));
      f->cr[i]        = float(live ? st.centroids[i][2] : scene_->centroids[i].radius);
    }
    for (const auto& [name, pin] : cpins_) {  // a dragged centroid shows at its target until the solver reports it
      if (const auto it = scene_->centroid_index.find(name); it != scene_->centroid_index.end() && st.centroids.empty()) {
        f->cx[size_t(it->second)] = float(pin.x);
        f->cy[size_t(it->second)] = float(pin.y);
      }
    }
  }
  for (const auto& [name, move] : pending_moves_) {
    const auto it = scene_->index.find(name);
    if (it != scene_->index.end() && size_t(it->second) < f->x.size()) {
      f->x[it->second] = move.x;
      f->y[it->second] = move.y;
    }
  }
  push_frame_locked(std::move(f));
  record_timeline_locked(false);
  stats_      = st;
  iteration_  = st.iteration;
  state_name_ = state;
  if (replaying() && !cfg_.video_dir.empty() && scene_ && frame_ && !frame_->x.empty()) {
    std::set<int> pinned;
    for (const auto& [name, pin] : pins_) {
      if (const auto it = scene_->index.find(name); it != scene_->index.end()) {
        pinned.insert(it->second);
      }
    }
    std::error_code ec;
    std::filesystem::create_directories(cfg_.video_dir, ec);
    std::ofstream(std::filesystem::path(cfg_.video_dir) / std::format("frame_{:05d}.png", video_frames_++), std::ios::binary)
        << render_png(*scene_, *frame_, pinned, Render_options{});
  }
  bump_locked();
}

std::string Session::screenshot_png(int w, int h, const std::string& layers, const Box* view) const {
  std::shared_ptr<const Scene> scene;
  std::shared_ptr<const Frame> frame;
  std::shared_ptr<const Grid>  routing;
  std::set<int>                pinned;
  {
    std::lock_guard lk(m_);
    scene = scene_;
    frame = frame_;
    if (frame && map_frame_ == frame->id) {
      routing = map_;
    }
    if (scene) {
      for (const auto& [name, pin] : pins_) {
        if (const auto it = scene->index.find(name); it != scene->index.end()) {
          pinned.insert(it->second);
        }
      }
    }
  }
  if (!scene || !frame) {
    return "";
  }
  auto ro   = parse_layers(layers);
  ro.width  = w > 0 ? w : ro.width;
  ro.height = h > 0 ? h : ro.height;
  if (view) {
    ro.has_view = true;
    ro.view     = *view;
  }
  return render_png(*scene, *frame, pinned, ro, routing.get());
}

void Session::begin_phase(std::shared_ptr<const Scene> scene, const double* x, const double* y, size_t n, bool mark_pins_dirty) {
  if (controller_) {
    fork_builder_         = {};
    legalizer_            = {};
    intent_provider_      = {};
    orientation_provider_ = {};
    controller_->scene    = scene;
    controller_->call("scene", wire::pack(*scene, std::span(x, n), std::span(y, n), mark_pins_dirty));
    return;
  }
  {
    std::lock_guard lk(m_);
    legalizer_            = {};
    intent_provider_      = {};
    orientation_provider_ = {};
    legalize_available_   = false;
    fork_builder_ = nullptr;  // belongs to the previous phase's data; the placement installs this phase's
  }
  std::unique_lock lk(m_);
  if (!scene_) {
    // The driver knows the technology and sources before synthesis; the resolved
    // design first arrives here. A replay with edited input records is stale even
    // for a flag variant; joining identical records or adding observations is fine.
    const metadata::Bundle bundle(cfg_.intent);
    const auto input = bundle.identity(synthesis_placement_classes, scene->design);
    if (input != "[]") {
      const auto digest = hash_util::fnv1a64(input);
      for (auto* id : {&cfg_.input_identity, &cfg_.source_identity}) {
        *id = std::format("{:016x}", hash_util::fnv1a64_u64(digest, hash_util::fnv1a64(*id)));
      }
    }
    hint_hash_ = hash_util::fnv1a64(bundle.identity(hint_class, scene->design));
  }
  scene_      = std::move(scene);
  design_hash_ = hash_util::fnv1a64(wire::pack(*scene_));
  stage_name_ = scene_->phase;
  pending_moves_.clear();
  ++scene_version_;
  dirty_pins_.clear();
  dirty_cpins_.clear();
  if (mark_pins_dirty) {
    for (auto& [name, pin] : pins_) {
      dirty_pins_.insert(name);
    }
    for (const auto& [name, pin] : cpins_) {
      dirty_cpins_.insert(name);
    }
  }
  release_c_ = false;
  released_.clear();
  Stats st;
  publish_locked(x, y, n, st, "ready");
}

Edits Session::take_edits_locked() {
  Edits out;
  out.repair = std::exchange(pending_repair_, {});
  if (!scene_) {
    return out;
  }
  // A pin holds a node only while that node is movable in this phase: a macro pinned in the macro phase is fixed in the
  // final one, and the solver rejects a pin on a fixed node.
  const auto movable_index = [&](const std::string& name) -> std::optional<int> {
    const auto found = scene_->index.find(name);
    if (found == scene_->index.end() || !scene_->nodes[size_t(found->second)].movable) {
      return std::nullopt;
    }
    return found->second;
  };
  for (const auto& [name, move] : pending_moves_) {
    if (auto index = movable_index(name)) {
      out.moves.push_back({*index, move.x, move.y});
    }
  }
  pending_moves_.clear();
  for (auto it = dirty_pins_.begin(); it != dirty_pins_.end();) {
    const auto found = movable_index(*it);
    if (!found) {
      it = dirty_pins_.erase(it);  // not movable in this phase; applied if a later phase has it
      continue;
    }
    const auto& pin = pins_.at(*it);
    out.pins.push_back({*found, pin.x, pin.y});
    it = dirty_pins_.erase(it);
  }
  for (const auto& name : released_) {
    if (const auto found = movable_index(name)) {
      out.releases.push_back(*found);
    }
  }
  released_.clear();
  for (auto it = dirty_cpins_.begin(); it != dirty_cpins_.end();) {
    const auto found = scene_->centroid_index.find(*it);
    if (found != scene_->centroid_index.end()) {
      const auto& p = cpins_.at(*it);
      out.centroids.push_back({found->second, p.x, p.y});
    }
    it = dirty_cpins_.erase(it);
  }
  if (release_c_) {  // the solver restores its own targets, then takes the ones still held
    out.release_centroids = true;
    release_c_            = false;
    out.centroids.clear();
    for (const auto& [name, p] : cpins_) {
      if (const auto found = scene_->centroid_index.find(name); found != scene_->centroid_index.end()) {
        out.centroids.push_back({found->second, p.x, p.y});
      }
    }
  }
  return out;
}

// Shared by exact replay traces and reusable relative commands. Names have
// already been bound to instances in the source scene.
std::string Session::Command::move_fields_json() const {
  std::string out;
  const auto strings = [&](const char* key, const std::vector<std::string>& values) {
    out += std::format(R"(,"{}":[)", key);
    for (size_t i = 0; i < values.size(); ++i) {
      out += std::format(R"({}"{}")", i ? "," : "", esc(values[i]));
    }
    out += "]";
  };
  const auto numbers = [&](const char* key, const std::vector<double>& values) {
    out += std::format(R"(,"{}":[)", key);
    for (size_t i = 0; i < values.size(); ++i) {
      out += (i ? "," : "") + num(values[i]);
    }
    out += "]";
  };
  strings("nodes", nodes);
  if (!orientations.empty()) {
    strings("orientation", orientations);
  }
  if (!align.empty()) {
    out += std::format(R"(,"align":"{}","reference":"{}","gap":{})", esc(align), esc(reference), num(gap));
    if (halo_edges) {
      out += R"(,"edges":"halo")";
    }
  } else if (cmd == "move" || cmd == "place_positions" || cmd == "macro_move") {
    numbers(relative ? "dx" : "x", x);
    numbers(relative ? "dy" : "y", y);
  }
  return out;
}

void Session::trace_locked(const Command& c) {
  if (c.cmd == "map" || c.cmd == "regions") {
    return;
  }
  const bool drag = (c.cmd == "move" || c.cmd == "centroid") && !c.final_;
  const bool public_action = cfg_.target != "main" || !drag || !c.relationship.empty();
  // A worker may branch in the middle of a drag. Preserve its exact replay
  // history in the session directory while keeping mouse samples out of main's action trace.
  if (!private_trace_.is_open() && !cfg_.worker_args.empty() && (drag || !trace_.is_open())) {
    private_trace_path_ = (std::filesystem::path(scratch_root_locked()) / "replay.jsonl").string();
    private_trace_.open(private_trace_path_, std::ios::trunc);
    if (!private_trace_) {
      throw std::runtime_error("cannot create private placement replay trace");
    }
    if (seq_ && trace_.is_open()) {
      std::ifstream prior(cfg_.trace_path);
      private_trace_ << prior.rdbuf();
    }
  }
  if (!trace_.is_open() && !private_trace_.is_open()) {
    return;
  }
  if (public_action) {
    ++seq_;
  }
  std::string line = std::format(R"({{"seq":{},"phase":"{}","iter":{},"cmd":"{}")", seq_, esc(stage_name_), iteration_, esc(c.cmd));
  if (drag) {
    line += R"(,"final":false)";
  }
  if (c.cmd == "move" || c.cmd == "place_positions" || c.cmd == "unpin" || c.cmd == "pin" || c.cmd == "macro_move") {
    line += c.move_fields_json();
    if (!c.relationship.empty()) {
      line += R"(,"relationship":)" + c.relationship;
    }
  } else if (c.cmd == "intent_apply") {
    line += ",\"records\":" + c.file;
  } else if (c.cmd == "policy") {
    if (c.no_resynthesis) {
      line += std::format(R"(,"no_resynthesis":{})", *c.no_resynthesis);
    }
    if (c.no_sizing) {
      line += std::format(R"(,"no_sizing":{})", *c.no_sizing);
    }
    if (c.no_buffer_edits) {
      line += std::format(R"(,"no_buffer_edits":{})", *c.no_buffer_edits);
    }
  } else if (c.cmd == "centroid") {
    line += std::format(R"(,"name":"{}","x":{},"y":{})", esc(c.name), num(c.x.empty() ? 0 : c.x[0]), num(c.y.empty() ? 0 : c.y[0]));
  } else if (c.cmd == "centroid_release") {
    line += std::format(R"(,"name":"{}")", esc(c.name));
  } else if (c.cmd == "run_until") {
    line += std::format(R"(,"gate":"{}")", esc(c.name));
  } else if (c.cmd == "extend") {
    line += std::format(R"(,"count":{})", c.count);
  } else if (c.cmd == "map") {
    line += std::format(R"(,"kind":"{}")", esc(c.name));
  } else if (c.cmd == "stage") {
    line += std::format(R"(,"name":"{}","on":{})", esc(c.name), c.on ? "true" : "false");
  } else if (c.cmd == "screenshot") {
    line += std::format(R"(,"file":"{}","layers":"{}","w":{},"h":{})", esc(c.file), esc(c.layers), c.w, c.h);
  }
  line += "}\n";
  if (private_trace_.is_open()) {
    private_trace_ << line << std::flush;
  }
  if (public_action && trace_.is_open()) {
    trace_ << line << std::flush;
  }
  if (!replaying() && cfg_.target != "main" && !cfg_.batch_worker && !cfg_.agent_socket.empty()) {
    // A restart replays and replaces the flow trace. The supervisor's private
    // segment spans those processes and includes each authored action only once.
    if (!foreground_segment_.is_open()) {
      const auto dir = std::filesystem::path(cfg_.agent_socket).parent_path();
      foreground_segment_.open(dir / "foreground-segment.jsonl", std::ios::app);
      foreground_segment_ << std::format(R"({{"cmd":"checkpoint","epoch":"{}","gate":"{}","nth":{},"input_identity":"{}"}})",
                                         esc(epoch_), esc(cfg_.resume_label), cfg_.resume_nth, esc(cfg_.input_identity)) << '\n';
    }
    foreground_segment_ << line << std::flush;
  }
}


// `area`: move one edge of the placement area (or set the whole core). The frame is a flag (synth.place.die/core, um),
// so this is a restart at the latest checkpoint with the new frame; recorded pins are replayed (moved along when the
// origin moves). Nothing happens, and a note says why, when the new area cannot hold the design or a pinned node.
void Session::area_locked(const Command& c) {
  if (!scene_ || scene_->die.xh <= scene_->die.xl || scene_->die.yh <= scene_->die.yl) {
    note_ = "area: no placement area yet";
    return;
  }
  const auto&  sc  = *scene_;
  const double dbu = sc.dbu_per_um > 0 ? sc.dbu_per_um : 1;
  const Box    old = sc.die;
  const bool   has_die = sc.frame_die.xh > sc.frame_die.xl && sc.frame_die.yh > sc.frame_die.yl;
  const Box    fd      = has_die ? sc.frame_die : old;  // the die keeps its margin around the core
  Box          nc      = old;
  if (c.has_box) {
    nc = {c.box.xl * dbu, c.box.yl * dbu, c.box.xh * dbu, c.box.yh * dbu};
  } else {
    const double d = c.delta * dbu;
    if (c.edge == "left") {
      nc.xl -= d;
    } else if (c.edge == "right") {
      nc.xh += d;
    } else if (c.edge == "bottom") {
      nc.yl -= d;
    } else {
      nc.yh += d;
    }
  }
  Box nd = {fd.xl + (nc.xl - old.xl), fd.yl + (nc.yl - old.yl), fd.xh + (nc.xh - old.xh), fd.yh + (nc.yh - old.yh)};
  // The flow takes nonnegative frames: a frame that would start below the origin moves up instead, with its content.
  const double sx = std::max(0.0, -std::min(nd.xl, nc.xl)), sy = std::max(0.0, -std::min(nd.yl, nc.yl));
  double       max_w = 0, max_h = 0;
  for (const auto& n : sc.nodes) {
    if (n.movable) {
      max_w = std::max(max_w, n.w);
      max_h = std::max(max_h, n.h);
    }
  }
  const double w = nc.xh - nc.xl, h = nc.yh - nc.yl;
  if (w < max_w || h < max_h || w <= 0 || h <= 0) {
    note_ = std::format("area: the new core {:.1f} x {:.1f} um cannot hold the largest cell or macro ({:.1f} x {:.1f} um)", w / dbu, h / dbu,
                        max_w / dbu, max_h / dbu);
    return;
  }
  const auto capacity = usable_area(nc, sc.exclusions);
  if (capacity <= 0) {
    note_ = "area: the requested core has no usable placement area";
    return;
  }
  if (sc.cell_area > 0) {
    const double util = sc.cell_area / capacity;
    if (util > 0.98) {
      note_ = std::format("area: the new core {:.1f} x {:.1f} um would be {:.0f}% full of cells; it must stay below 98%", w / dbu, h / dbu,
                          100 * util);
      return;
    }
  }
  if (sc.macro_area > 0 && sc.macro_area / capacity > Scene::kMacroFillLimit) {
    note_ = std::format(
        "area: the macros and memories need {:.0f}% of the new core {:.1f} x {:.1f} um with their spacing; they cannot be packed "
        "above {:.0f}%",
        100 * sc.macro_area / capacity,
        w / dbu,
        h / dbu,
        100 * Scene::kMacroFillLimit);
    return;
  }
  for (const auto& [name, pin] : pins_) {
    const auto found = sc.index.find(name);
    if (found == sc.index.end()) {
      continue;
    }
    const auto& n = sc.nodes[size_t(found->second)];
    if (pin.x < nc.xl - 0.5 || pin.y < nc.yl - 0.5 || pin.x + n.w > nc.xh + 0.5 || pin.y + n.h > nc.yh + 0.5) {
      note_ = std::format("area: pinned {} would lie outside the new core; move it first or unpin it", name);
      return;
    }
  }
  const auto um = [&](const Box& b, double ox, double oy) {
    return std::format("{:.6f},{:.6f},{:.6f},{:.6f}", (b.xl + ox) / dbu, (b.yl + oy) / dbu, (b.xh + ox) / dbu, (b.yh + oy) / dbu);
  };
  Restart_request r;
  r.set["synth.place.core"] = um(nc, sx, sy);
  r.set["synth.place.die"]  = um(nd, sx, sy);
  r.shift_x                 = sx;
  r.shift_y                 = sy;
  if (checkpoints_.empty()) {
    r.from_start = true;
  } else {
    const auto& cp = checkpoints_.back();
    r.label        = cp.label;
    r.nth          = cp.nth;
    // Paused at that checkpoint's gate: what was done there (pins, switches) is part of the state to come back to.
    r.seq = gate_name_ == cp.label ? seq_ : cp.seq;
  }
  if (!cfg_.restart) {
    note_ = "area: restart is not available in this session";
    return;
  }
  note_ = std::format("area: core {:.1f} x {:.1f} um{}",
                      w / dbu,
                      h / dbu,
                      sc.cell_area > 0 ? std::format(", {:.0f}% full of cells", 100 * sc.cell_area / capacity) : std::string{});
  if (trace_.is_open()) {
    trace_.flush();
  }
  const auto why = cfg_.restart(r);  // normally re-executes the process and does not return
  if (!why.empty()) {
    note_ = "area: " + why;
  }
}

// A fixed macro was moved: re-run the flow to the latest checkpoint (where the user is) with the macro where it was put.
void Session::macro_restart_locked() {
  if (!cfg_.restart) {
    note_ = "macro move: restart is not available in this session";
    return;
  }
  if (checkpoints_.empty()) {
    note_ = "macro move: wait for the first gate, then move the macro again";
    return;
  }
  Restart_request r;
  const auto&     cp = checkpoints_.back();
  r.label            = cp.label;
  r.nth              = cp.nth;
  r.seq              = gate_name_ == cp.label ? seq_ : cp.seq;
  note_              = "macro moved: re-running the flow with it there";
  if (trace_.is_open()) {
    trace_.flush();
  }
  const auto why = cfg_.restart(r);  // normally re-executes the process and does not return
  if (!why.empty()) {
    note_ = "macro move: " + why;
  }
}

std::map<std::string, std::array<double, 3>> Session::macro_moves() const {
  if (controller_) {
    return wire::read<decltype(macro_moves())>(controller_->call("macro_moves"));
  }
  std::lock_guard lk(m_);
  return macro_moves_;
}

void Session::set_orientation_provider(Orientation_provider provider) {
  std::lock_guard lock(m_);
  orientation_provider_ = std::move(provider);
}
void Session::set_intent_provider(Intent_provider provider) {
  std::lock_guard lock(m_);
  intent_provider_ = std::move(provider);
}
std::string Session::intent() const {
  if (controller_) {
    return controller_->call("get_intent");
  }
  std::lock_guard lock(m_);
  return cfg_.intent;
}
void Session::intent_unlocked(const Command& cmd, std::unique_lock<std::mutex>& lock) {
  intent_report_frame_ = frame_ ? frame_->id : 0;
  intent_report_sequence_ = cmd.sequence;
  intent_report_ = "{}";
  if (!intent_provider_ || !frame_) {
    intent_status_ = "intent unavailable at this gate";
    bump_locked();
    return;
  }
  auto       provider = intent_provider_;
  auto       frame    = frame_;
  auto       current  = cfg_.intent;
  const auto design   = scene_->design;
  const bool apply    = cmd.cmd == "intent_apply";
  lock.unlock();
  std::string report, error;
  uint64_t hint_hash = 0;
  try {
    metadata::Bundle bundle(current);
    bundle.replace_selected("livehd.placement.hints", design, metadata::Bundle(cmd.file));
    if (apply) {
      hint_hash = hash_util::fnv1a64(bundle.identity(hint_class, design));
    }
    current = bundle.json();
    report = provider(current, *frame, apply);
  } catch (const std::exception& ex) {
    error = ex.what();
  }
  lock.lock();
  if (!error.empty()) {
    intent_status_ = error;
    bump_locked();
    return;
  }
  intent_report_ = std::move(report);
  intent_status_ = apply ? "applied" : "preview";
  if (apply) {
    cfg_.intent = std::move(current);
    hint_hash_ = hint_hash;
    ++intent_revision_;
    trace_locked(cmd);
  }
  bump_locked();
}
void Session::begin_terminal_repair(Mutation_policy requested) {
  if (controller_) {
    controller_->call("begin_repair", wire::pack(requested));
  }
  std::lock_guard lock(m_);
  cfg_.policy = requested;
  if (cfg_.evaluator_policy) {
    *cfg_.evaluator_policy = requested;
  }
  bump_locked();
}
void Session::set_repair_trials(unsigned n, const std::vector<std::string>& changed) {
  if (controller_) {
    controller_->call("repair_trials", wire::pack(n, changed));
    return;
  }
  std::lock_guard lk(m_);
  cfg_.repair_trials = n;
  scoped_resize_changes_ = changed;
}
void Session::set_legalizer(Legalizer legalizer) {
  std::lock_guard lock(m_);
  legalizer_ = std::move(legalizer);
}
void Session::legalize_unlocked(const Command& c, std::unique_lock<std::mutex>& lock) {
  if (!legalize_available_ || !legalizer_ || !scene_ || !frame_) {
    legalization_status_ = "failed";
    legalization_error_ = note_ = "legalizer unavailable";
    bump_locked();
    return;
  }
  const auto       frame = frame_;
  Legalize_request req{frame->x, frame->y, {}, {}, c.scoped, c.iterations};
  const bool restore_anchors = !c.scoped && !c.has_box && c.cmd == "legalize";
  const auto held = pins_locked(restore_anchors);
  for (const auto& pin : held.pins) {
    req.held.push_back(pin.node);
    if (restore_anchors) {
      req.x.at(pin.node) = pin.x;
      req.y.at(pin.node) = pin.y;
    }
  }
  for (const auto& name : c.scope_nodes) {
    if (!scene_->index.contains(name)) {
      legalization_status_ = "failed";
      legalization_error_  = "unknown scope member " + name;
      bump_locked();
      return;
    }
    req.active.push_back(scene_->index.at(name));
  }
  req.bounded_window = c.has_box;
  req.window         = c.box;
  req.full_reference = c.full_reference;
  if (c.has_box && !c.bound_only) {
    for (size_t i = 0; i < frame->x.size(); ++i) {
      const auto& node = scene_->nodes[i];
      if (frame->x[i] >= c.box.xl && frame->y[i] >= c.box.yl && frame->x[i] + node.w <= c.box.xh
          && frame->y[i] + node.h <= c.box.yh) {
        req.active.push_back(int(i));
      }
    }
  }
  relaxation_iterations_ = 0;
  legalization_status_   = "running";
  legalization_error_.clear();
  legalized_moves_.clear();
  bump_locked();
  auto solve = legalizer_;
  lock.unlock();
  Legalize_result r;
  try {
    r = solve(req);
    if (restore_anchors && r.error.empty()) {
      for (const auto& p : held.pins) {
        if (p.x != frame->x.at(p.node) || p.y != frame->y.at(p.node)) {
          r.moved.push_back(p);
        }
      }
    }
  } catch (const std::exception& e) {
    r.error = e.what();
  }
  lock.lock();
  relaxation_iterations_ = r.iterations;
  work_.gp_iterations += r.iterations;
  if (frame_ != frame || exit_.load()) {
    r.error = "placement changed during legalization";
  }
  if (r.error.empty()) {
    auto next = std::make_shared<Frame>(*frame);
    for (const auto& move : r.moved) {
      next->x.at(move.node) = move.x;
      next->y.at(move.node) = move.y;
    }
    if (!macro_geometry(*scene_, *next).empty()) {
      r.error = "macro clearance or exclusion remains infeasible";
    } else {
      for (const auto& move : r.moved) {
        pending_moves_[scene_->nodes.at(move.node).name] = move;
      }
      next->id = ++frame_id_;
      push_frame_locked(std::move(next));
      legalized_moves_  = std::move(r.moved);
      stats_.has_timing = has_timing_ = false;
      stats_.hpwl_um = stats_.overflow = -1;
      map_dirty_                       = !wanted_map_.empty();
      regions_dirty_                   = regions_wanted_;
      legalization_status_             = "applied";
      // Record realized moves as one-shot positions, including the legalizer's displacements.
      Command realized;
      realized.cmd = "place_positions";
      for (const auto& move : legalized_moves_) {
        realized.nodes.push_back(scene_->nodes[move.node].name);
        realized.x.push_back(move.x);
        realized.y.push_back(move.y);
      }
      trace_locked(realized);
    }
  }
  if (!r.error.empty()) {
    legalization_status_ = "failed";
    legalization_error_  = r.error;
  }
  note_
      = r.error.empty() ? std::format("legalization moved {} objects", legalized_moves_.size()) : "legalization failed: " + r.error;
  bump_locked();
}
void Session::set_fork_builder(Fork_builder b) {
  std::lock_guard lk(m_);
  fork_builder_ = std::move(b);
}

// `fork`: the cells inside a box (or the named ones) become a design of their own, run in a new session. The placer
// writes the region's files from the data of the phase it is in; the host starts the child and gives its page.
void Session::fork_unlocked(const Command& c, std::unique_lock<std::mutex>& lk) {
  const bool supervised = !cfg_.region_args.empty();
  if (!fork_builder_ || (!supervised && (!cfg_.fork || cfg_.fork_dir.empty()))) {
    note_ = "fork: not available here (it needs --workdir and a placement phase)";
    return;
  }
  if (!scene_ || !frame_ || frame_->x.size() < scene_->nodes.size()) {
    note_ = "fork: no placement yet";
    return;
  }
  const auto   scene = scene_;
  const auto   frame = frame_;
  const double dbu   = scene->dbu_per_um > 0 ? scene->dbu_per_um : 1;
  Fork_request req;
  req.scene = scene;
  req.frame = frame;
  const auto center = [&](size_t i) { return std::pair{frame->x[i] + scene->nodes[i].w / 2, frame->y[i] + scene->nodes[i].h / 2}; };
  const auto takes  = [&](size_t i) { return scene->nodes[i].kind == Kind::cell || scene->nodes[i].kind == Kind::macro || scene->nodes[i].kind == Kind::fixed; };
  if (c.has_box) {
    req.box = {c.box.xl * dbu, c.box.yl * dbu, c.box.xh * dbu, c.box.yh * dbu};
    for (size_t i = 0; i < scene->nodes.size(); ++i) {
      const auto [cx, cy] = center(i);
      if (takes(i) && scene->nodes[i].kind != Kind::fixed && cx >= req.box.xl && cx < req.box.xh && cy >= req.box.yl && cy < req.box.yh) {
        req.cells.push_back(scene->nodes[i].name);
      }
    }
  } else {
    Box hull{1e300, 1e300, -1e300, -1e300};
    for (const auto& name : c.nodes) {
      const auto f = scene->index.find(name);
      if (f == scene->index.end() || !takes(size_t(f->second)) || scene->nodes[size_t(f->second)].kind == Kind::fixed) {
        note_ = "fork: " + name + " is not a placeable cell";
        return;
      }
      const auto i = size_t(f->second);
      req.cells.push_back(name);
      hull.xl = std::min<double>(hull.xl, frame->x[i]);
      hull.yl = std::min<double>(hull.yl, frame->y[i]);
      hull.xh = std::max<double>(hull.xh, frame->x[i] + scene->nodes[i].w);
      hull.yh = std::max<double>(hull.yh, frame->y[i] + scene->nodes[i].h);
    }
    req.box = hull;
  }
  if (req.cells.empty()) {
    note_ = "fork: no cells inside the region";
    return;
  }
  const auto id = std::format("fork_{}", next_region_++);
  Job_slot   slot;
  if (supervised) {
    size_t running = 0;
    for (const auto& [unused, j] : background_) {
      running += j.worker && !j.worker->terminal();
    }
    if (running >= size_t(cfg_.explore_jobs)
        || background_.size() + geometry_results_.size() + bool(foreground_) >= size_t(cfg_.explore_open)) {
      note_ = "fork: exploration limit reached";
      return;
    }
    const auto prediction = memory_prediction(req.cells.size(), false);
    if (!memory_fits_locked(prediction)) {
      note_ = "fork: session memory budget cannot accommodate the region evaluator";
      return;
    }
    slot = Job_manager::global().try_acquire(Job_class::place_explore, req.cells.size(), prediction, false, 1, cfg_.explore_threads);
    if (!slot) {
      note_ = "fork: no host capacity for a region";
      return;
    }
    try {
      scratch_root_locked();
    } catch (const std::exception& error) {
      note_ = std::string("fork: ") + error.what();
      return;
    }
    Background job;
    job.id        = id;
    job.label     = id;
    job.tier      = "region";
    job.state     = "starting";
    job.frame     = frame->id;
    job.baseline  = placement_identity_locked();
    job.submitted = job.admitted = std::chrono::steady_clock::now();
    job.active_cells             = req.cells.size();
    job.reserved_bytes           = prediction;
    background_.emplace(id, std::move(job));
  }
  req.dir = std::filesystem::path(supervised ? region_root_ : cfg_.fork_dir) / id;
  std::error_code ec;
  std::filesystem::create_directories(req.dir, ec);
  if (ec) {
    note_ = "fork: cannot create " + req.dir + ": " + ec.message();
    if (supervised) {
      background_.at(id).state  = "failed";
      background_.at(id).reason = note_;
    }
    return;
  }
  note_ = std::format("fork: extracting {} cells...", req.cells.size());
  bump_locked();
  const auto builder = fork_builder_;
  const auto launch  = cfg_.fork;
  lk.unlock();
  Fork_files  files;
  Fork_launch started;
  std::shared_ptr<Replay_worker> worker;
  try {
    files = builder(req);
    if (files.error.empty()) {
      if (supervised) {
        worker      = std::make_shared<Replay_worker>(cfg_, id, files, req, std::move(slot));
        started.url = worker->await_region_page();
      } else {
        started = launch(files, req);
      }
    }
  } catch (const std::exception& e) {
    (files.error.empty() ? started.error : files.error) = e.what();
  }
  lk.lock();
  if (supervised && !background_.contains(id)) {
    std::filesystem::remove_all(req.dir, ec);
    return;
  }
  if (!files.error.empty() || !started.error.empty()) {
    if (supervised) {
      std::filesystem::remove_all(req.dir, ec);
      auto& job    = background_.at(id);
      job.finished = std::chrono::steady_clock::now();
      job.state    = "failed";
      job.reason   = files.error.empty() ? started.error : files.error;
    }
    note_ = "fork: " + (files.error.empty() ? started.error : files.error);
    return;
  }
  if (supervised) {
    background_.at(id).worker = std::move(worker);
  }
  forks_.push_back({started.url, files.top, req.dir, files.cells, files.inputs, files.outputs});
  note_ = std::format("fork {}: {} cells, {} pins in, {} pins out", forks_.size(), files.cells, files.inputs, files.outputs);
}

bool shift_trace(const std::string& path, double dx, double dy) {
  std::ifstream in(path);
  if (!in) {
    return false;
  }
  std::string out, line;
  while (std::getline(in, line)) {
    rapidjson::Document d;
    d.Parse<rapidjson::kParseFullPrecisionFlag>(line.c_str());
    if (d.HasParseError() || !d.IsObject() || !d.HasMember("cmd") || !d["cmd"].IsString()) {
      out += line + "\n";
      continue;
    }
    const std::string cmd = d["cmd"].GetString();
    const auto        rewrite = [&](const char* key, double by) {
      if (d.HasMember(key) && d[key].IsArray()) {
        for (auto& v : d[key].GetArray()) {
          if (v.IsNumber()) {
            v.SetDouble(v.GetDouble() + by);
          }
        }
      } else if (d.HasMember(key) && d[key].IsNumber()) {
        d[key].SetDouble(d[key].GetDouble() + by);
      }
    };
    if (cmd == "move" || cmd == "place_positions" || cmd == "centroid" || cmd == "macro_move") {
      rewrite("x", dx);
      rewrite("y", dy);
      rapidjson::StringBuffer                    buf;
      rapidjson::Writer<rapidjson::StringBuffer> w(buf);
      d.Accept(w);
      out += std::string(buf.GetString(), buf.GetSize()) + "\n";
    } else {
      out += line + "\n";
    }
  }
  in.close();
  std::ofstream(path, std::ios::trunc) << out;
  return true;
}

void Session::apply_locked(const Command& c, Edits&) {
  if (c.cmd == "area") {  // the process re-executes with the new frame (a flag): not an action of the trace
    area_locked(c);
    bump_locked();
    return;
  }
  if (!c.orientations.empty()) {
    Orientation_edits flips;
    auto              updated = std::make_shared<Scene>(*scene_);
    for (size_t i = 0; i < c.nodes.size(); ++i) {
      const auto  index  = updated->index.at(c.nodes[i]);
      auto&       node   = updated->nodes[index];
      const auto& orient = c.orientations[i];
      if (orient == node.orient) {
        continue;
      }
      const auto mirrors_x = [](const std::string& s) { return s == "FN" || s == "S"; };
      const auto mirrors_y = [](const std::string& s) { return s == "FS" || s == "S"; };
      for (auto& pin : node.signal_pins) {
        if (mirrors_x(node.orient) != mirrors_x(orient)) {
          pin.x = node.w - pin.x;
        }
        if (mirrors_y(node.orient) != mirrors_y(orient)) {
          pin.y = node.h - pin.y;
        }
      }
      node.orient = orient;
      flips.emplace_back(index, orient);
    }
    if (!flips.empty()) {
      if (orientation_provider_) {
        orientation_provider_(flips);
      } else if (state_name_ != "done") {
        throw std::runtime_error("orientation provider unavailable");
      }
      scene_ = std::move(updated);
      ++scene_version_;
      design_hash_ = hash_util::fnv1a64(wire::pack(*scene_));
    }
  }
  if (!c.relationship.empty()) {
    relationships_.push_back(c.relationship);
  }
  trace_locked(c);
  if (c.cmd == "policy") {
    if (c.no_resynthesis) {
      cfg_.policy.no_resynthesis = *c.no_resynthesis;
    }
    if (c.no_sizing) {
      cfg_.policy.no_sizing = *c.no_sizing;
    }
    if (c.no_buffer_edits) {
      cfg_.policy.no_buffer_edits = *c.no_buffer_edits;
    }
    if (cfg_.evaluator_policy) {
      *cfg_.evaluator_policy = cfg_.policy;
    }
  } else if (c.cmd == "run" || c.cmd == "continue" || c.cmd == "run_until" || c.cmd == "repair_scope") {
    if (c.cmd == "repair_scope") {
      pending_repair_.emplace(Repair_scope{c.scope_nodes, frame_->x, frame_->y, c.has_box ? std::optional(c.box) : std::nullopt,
                                          unsigned(c.count)});
    }
    if (c.cmd == "run_until") {
      exploration_stop_ = c.name;
      for (auto& stage : stages_) {
        if (!stage.on && stage.available) {
          stage.on = true;
          Command enabled;
          enabled.cmd = "stage";
          enabled.name = stage.name;
          enabled.on = true;
          trace_locked(enabled);
        }
      }
    }
    paused_        = false;
    gate_released_ = true;
    if (at_edit_) {
      edit_decided_ = true;
    }
    if (at_gp_end_) {
      gp_decided_ = true;
      gp_extend_  = c.cmd == "run" ? cfg_.min_iters : 0;
      repause_    = false;
      if (gp_extend_ > 0) {  // the replay needs the decision itself, not the key that made it
        Command e;
        e.cmd   = "extend";
        e.count = gp_extend_;
        trace_locked(e);
      }
    }
  } else if (c.cmd == "pause") {
    paused_ = true;
  } else if (c.cmd == "stage") {
    for (auto& s : stages_) {
      if (s.name == c.name && s.available) {
        s.on = c.on;
        if (at_gp_end_ && s.name == "legalize" && c.on) {
          gp_decided_ = true;
          gp_extend_  = 0;
        }
      }
    }
  } else if (c.cmd == "restart") {
    // Re-run the flow with the recorded actions up to the checkpoint, under other flags. The host normally
    // re-executes the process (same pid, port and token), so this returns only when it could not.
    Restart_request r;
    r.set = c.set;
    if (c.cp < 0 || size_t(c.cp) >= checkpoints_.size()) {
      r.from_start = true;
    } else {
      const auto& cp = checkpoints_[size_t(c.cp)];
      r.label        = cp.label;
      r.nth          = cp.nth;
      r.seq          = cp.seq;
    }
    if (!cfg_.restart) {
      note_ = "restart is not available in this session";
    } else {
      if (trace_.is_open()) {
        trace_.flush();
      }
      note_ = cfg_.restart(r);
    }
  } else if (c.cmd == "timing" || c.cmd == "evaluate") {
    full_timing_wanted_ |= c.cmd == "evaluate";
    timing_wanted_ = true;
  } else if (c.cmd == "centroid") {
    if (scene_ && scene_->centroid_index.contains(c.name) && !c.x.empty() && !c.y.empty()) {
      cpins_[c.name] = {-1, c.x[0], c.y[0]};
      dirty_cpins_.insert(c.name);
      auto       next = frame_ ? std::make_shared<Frame>(*frame_) : std::make_shared<Frame>();
      const auto i    = size_t(scene_->centroid_index.at(c.name));
      if (i < next->cx.size()) {
        next->cx[i] = float(c.x[0]);
        next->cy[i] = float(c.y[0]);
      }
      next->id = ++frame_id_;
      push_frame_locked(std::move(next));
    }
  } else if (c.cmd == "centroid_release") {
    bool any = false;
    if (c.name.empty()) {
      any = !cpins_.empty();
      cpins_.clear();
    } else {
      any = cpins_.erase(c.name) > 0;
    }
    release_c_ |= any;
    dirty_cpins_.clear();
  } else if (c.cmd == "regions") {
    regions_wanted_ = c.on;
    regions_dirty_  = c.on;
    if (!c.on) {
      regions_.reset();
      ++regions_version_;
    }
  } else if (c.cmd == "map") {
    all_maps_wanted_ = c.name == "all";
    wanted_map_      = all_maps_wanted_ ? cfg_.score_estimator : c.name == "none" ? "" : c.name;
    map_dirty_  = true;
    if (wanted_map_.empty()) {
      map_.reset();
      map_frame_ = 0;
      ++map_version_;
    }
  } else if (c.cmd == "move" || c.cmd == "place_positions" || c.cmd == "proposal_move") {
    auto next = frame_ ? std::make_shared<Frame>(*frame_) : std::make_shared<Frame>();
    std::vector<std::string> new_pins;
    for (size_t i = 0; i < c.nodes.size() && i < c.x.size() && i < c.y.size(); ++i) {
      if (scene_) {
        const auto found = scene_->index.find(c.nodes[i]);
        if (found == scene_->index.end()) {
          continue;
        }
        const auto& node = scene_->nodes[size_t(found->second)];
        if (!node.movable && (c.cmd != "proposal_move" || node.kind != Kind::fixed)) {
          continue;
        }
        if (size_t(found->second) < next->x.size()) {
          next->x[size_t(found->second)] = c.x[i];
          next->y[size_t(found->second)] = c.y[i];
        }
        if (!node.movable) {
          continue;
        }
      }
      pending_moves_[c.nodes[i]] = {-1, c.x[i], c.y[i]};
      if (c.cmd == "place_positions") {
        continue;
      }
      if (!pins_.contains(c.nodes[i])) {
        new_pins.push_back(c.nodes[i]);
      }
      pins_[c.nodes[i]] = {-1, c.x[i], c.y[i]};
      dirty_pins_.insert(c.nodes[i]);
      released_.erase(c.nodes[i]);
    }
    next->id = ++frame_id_;
    push_frame_locked(std::move(next));
    for (const auto& name : new_pins) {
      if (suspended_pins_.erase(name)) {
        pins_.erase(name);
        released_.erase(name);
        note_ = "pin refused on illegal macro " + name;
      }
    }
    stats_.hpwl_um = stats_.overflow = -1;
    map_dirty_                       = !wanted_map_.empty();
    regions_dirty_                   = regions_wanted_;
    stats_.has_timing = has_timing_ = false;
    if (c.final_ && !cfg_.batch_worker) {
      timing_wanted_ = true;  // show what a released edit did to the timing
    }
  } else if (c.cmd == "macro_move") {
    // A FIXED macro cannot move inside a running solver: remember where it goes and re-run the flow with it there.
    auto next = frame_ ? std::make_shared<Frame>(*frame_) : std::make_shared<Frame>();
    bool any  = false;
    for (size_t i = 0; scene_ && i < c.nodes.size() && i < c.x.size() && i < c.y.size(); ++i) {
      const auto found = scene_->index.find(c.nodes[i]);
      if (found == scene_->index.end()) {
        continue;
      }
      const auto& n = scene_->nodes[size_t(found->second)];
      if (n.movable || n.kind != Kind::fixed) {
        continue;
      }
      macro_moves_[c.nodes[i]] = {c.x[i], c.y[i], ++macro_order_};
      if (size_t(found->second) < next->x.size()) {
        next->x[size_t(found->second)] = c.x[i];
        next->y[size_t(found->second)] = c.y[i];
      }
      any = true;
    }
    if (any) {
      next->id = ++frame_id_;
      push_frame_locked(std::move(next));
      if (cfg_.target == "main") {
        macro_restart_locked();
      }
    }
  } else if (c.cmd == "pin") {
    for (const auto& name : c.nodes) {
      if (!scene_ || !frame_ || !scene_->index.contains(name) || geometry_conflicts_.contains(name)) {
        note_ = "pin refused for " + name;
        return;
      }
    }
    for (const auto& name : c.nodes) {
      const auto i = scene_->index.at(name);
      if (!scene_->nodes[i].movable) {
        continue;
      }
      pins_[name] = {i, frame_->x.at(i), frame_->y.at(i)};
      dirty_pins_.insert(name);
      released_.erase(name);
    }
  } else if (c.cmd == "unpin") {
    for (const auto& name : c.nodes) {
      suspended_pins_.erase(name);
      if (pins_.erase(name)) {
        released_.insert(name);
        dirty_pins_.erase(name);
      }
    }
  } else if (c.cmd == "screenshot") {
    if (!cfg_.shot_dir.empty() && scene_ && frame_ && !c.file.empty()) {
      std::set<int> pinned;
      for (const auto& [name, pin] : pins_) {
        if (const auto f = scene_->index.find(name); f != scene_->index.end()) {
          pinned.insert(f->second);
        }
      }
      auto ro   = parse_layers(c.layers);
      ro.width  = c.w > 0 ? c.w : ro.width;
      ro.height = c.h > 0 ? c.h : ro.height;
      if (c.has_view) {
        ro.has_view = true;
        ro.view     = c.view;
      }
      std::error_code ec;
      std::filesystem::create_directories(cfg_.shot_dir, ec);
      const auto png = render_png(*scene_, *frame_, pinned, ro, map_frame_ == frame_->id ? map_.get() : nullptr);
      std::ofstream(std::filesystem::path(cfg_.shot_dir) / std::filesystem::path(c.file).filename(), std::ios::binary) << png;
    }
  } else if (c.cmd == "finish") {
    finishing_ = true;
    paused_    = false;
    for (auto& s : stages_) {
      s.on = true;
    }
  }
  bump_locked();
}

void Session::drain_locked(Edits& edits, std::unique_lock<std::mutex>& lk) {
  while (!queue_.empty()) {
    auto c = std::move(queue_.front());
    queue_.pop_front();
    std::string status = "processed", reason;
    const auto execute = [&] {
      if (auto why = check_expectation_locked(c); !why.empty()) {
        status = "stale";
        reason = why;
      } else if (auto invalid = resolve_move_locked(c); !invalid.empty()) {
        status = "rejected";
        reason = invalid;
      }
      if (!reason.empty()) {
        note_ = reason + " for queued " + c.cmd;
        bump_locked();
        return;
      }
      if (c.cmd == "intent_apply" || c.cmd == "intent_preview") {
        intent_unlocked(c, lk);
        const bool ok = intent_status_ == "applied" || intent_status_ == "preview";
        status = ok ? "applied" : "rejected";
        if (!ok) {
          reason = intent_status_;
        }
        return;
      }
      if (c.cmd == "batch") {
        for (const auto& move : c.batch) {
          apply_locked(move, edits);
        }
        status = "applied";
        return;
      }
      if (c.cmd == "legalize" || c.cmd == "relax") {
        legalize_unlocked(c, lk);
        status = legalization_status_ == "applied" ? "applied" : "rejected";
        reason = legalization_error_;
        return;
      }
      if (c.cmd == "fork") {  // seconds of work for the placer and a child process: the page keeps being served meanwhile
        fork_unlocked(c, lk);
        bump_locked();
        return;
      }
      if (c.cmd == "pin") {
        for (const auto& name : c.nodes) {
          const auto i = scene_ ? scene_->node_index(name) : -1;
          if (i < 0 || !scene_->nodes[i].movable || geometry_conflicts_.contains(name)) {
            status = "rejected";
            note_ = reason = "pin refused for " + name;
            bump_locked();
            return;
          }
        }
      }
      apply_locked(c, edits);
      if (c.cmd == "move" || c.cmd == "place_positions" || c.cmd == "policy" || c.cmd == "unpin" || c.cmd == "pin") {
        status = "applied";
      }
    };
    execute();
    completed_sequence_ = c.sequence;
    std::string result = std::format(R"({{"ok":true,"sequence":{},"status":"{}","frame":{},"reason":"{}")",
                                     c.sequence, status, frame_ ? frame_->id : 0, esc(reason));
    if (status == "applied" && (c.cmd == "move" || c.cmd == "place_positions" || c.cmd == "batch")) {
      std::string resolved;
      std::set<std::string> reported;
      const auto append = [&](const Command& move) {
        for (const auto& name : move.nodes) {
          if (!reported.insert(name).second) {
            continue;
          }
          const auto i = scene_->index.at(name);
          if (!resolved.empty()) {
            resolved += ',';
          }
          resolved += std::format(R"({{"name":"{}","x":{},"y":{},"pinned":{},"conflicts":[)",
                                  esc(name), num(frame_->x[i]), num(frame_->y[i]), pins_.contains(name) && !suspended_pins_.contains(name));
          if (const auto found = geometry_conflicts_.find(name); found != geometry_conflicts_.end()) {
            for (size_t k = 0; k < found->second.size(); ++k) {
              resolved += std::format(R"({}"{}")", k ? "," : "", esc(found->second[k]));
            }
          }
          resolved += "]}";
        }
      };
      if (c.cmd == "batch") {
        for (const auto& move : c.batch) {
          append(move);
        }
      } else {
        append(c);
      }
      result += R"(,"units":"dbu","resolved":[)" + resolved + "]";
    }
    result += "}";
    command_result_bytes_ += result.size();
    command_results_.emplace(c.sequence, std::move(result));
    while (command_results_.size() > 1 && (command_results_.size() > 128 || command_result_bytes_ > (2u << 20))) {
      command_result_bytes_ -= command_results_.begin()->second.size();
      command_results_.erase(command_results_.begin());
    }
  }
}

// Congestion maps are the only thing the flow thread computes for the page; the provider reads the solver's own
// arrays, so it runs here, outside the lock.
void Session::service_map(std::unique_lock<std::mutex>& lk, const Map_provider* maps, bool every) {
  const auto compute = [&](const std::string& kind) {
    const auto frame = frame_;
    lk.unlock();
    Grid grid;
    try {
      if (maps && frame) {
        grid = (*maps)(kind, *frame);
      } else {
        grid.note = "map unavailable at this gate";
      }
    } catch (const std::exception& e) {
      grid      = {};
      grid.note = e.what();
    }
    lk.lock();
    return grid;
  };
  if (regions_wanted_ && (every || regions_dirty_)) {
    regions_dirty_ = false;
    regions_frame_ = frame_ ? frame_->id : 0;
    auto grid      = compute("regions");
    regions_       = std::make_shared<const Grid>(std::move(grid));
    ++regions_version_;
    bump_locked();
  }
  const auto frame_id = frame_ ? frame_->id : 0;
  if (measured_maps_frame_ != frame_id) {
    measured_maps_.clear();
    measured_maps_frame_ = frame_id;
  }
  const bool display = !wanted_map_.empty() && (every || map_dirty_);
  const auto estimator = cfg_.score_estimator;
  if (frame_id && score_map_wanted_ && !measured_maps_.contains(estimator)
      && !(display && (all_maps_wanted_ || wanted_map_ == estimator))) {
    auto grid = compute(estimator);
    if (frame_ && frame_->id == frame_id) {
      measured_maps_[estimator] = std::move(grid);
      bump_locked();
    }
  }
  if (!display) {
    return;
  }
  const std::string kind = wanted_map_;
  map_dirty_             = false;
  map_frame_             = frame_id;
  if (all_maps_wanted_ && scene_ && frame_) {
    for (const std::string tag : {"rudy", "rsmt"}) {
      if (tag != kind) {
        measured_maps_[tag] = compute(tag);
      }
    }
    measured_maps_["density"] = occupancy_grid(*scene_, *frame_, 32);
  }
  auto grid            = compute(kind);
  measured_maps_[kind] = grid;
  map_kind_ = kind;
  map_note_ = grid.note;
  map_      = std::make_shared<const Grid>(std::move(grid));
  ++map_version_;
  bump_locked();
}

std::string Session::regions_json() const {
  std::shared_ptr<const Grid> g;
  std::uint64_t               v  = 0;
  bool                        on = false;
  {
    std::lock_guard lk(m_);
    g  = regions_;
    v  = regions_version_;
    on = regions_wanted_;
  }
  std::string out = std::format(R"({{"v":{},"on":{},"note":"{}","rects":[)", v, on ? "true" : "false", esc(g ? g->note : std::string{}));
  if (g) {
    for (size_t i = 0; i + 4 < g->rects.size(); i += 5) {
      out += std::format("{}[{},{},{},{},{}]", i ? "," : "", num(g->rects[i]), num(g->rects[i + 1]), num(g->rects[i + 2]), num(g->rects[i + 3]), num(g->rects[i + 4]));
    }
  }
  out += R"(],"pins":[)";
  if (g) {
    for (size_t i = 0; i + 2 < g->crossings.size(); i += 3) {
      out += std::format("{}[{},{},{}]", i ? "," : "", num(g->crossings[i]), num(g->crossings[i + 1]), num(g->crossings[i + 2]));
    }
  }
  out += std::format(R"(],"pins_total":{}}})", g ? g->crossing_total : 0);
  return out;
}

std::string Session::map_binary(const std::string& kind, uint64_t expected_frame) const {
  std::shared_ptr<const Grid> g;
  std::shared_ptr<const Scene> scene;
  std::shared_ptr<const Frame> frame;
  {
    std::lock_guard lk(m_);
    if (expected_frame && (!frame_ || frame_->id != expected_frame || (kind.empty() && map_frame_ != expected_frame))) {
      throw std::runtime_error("placement map frame changed");
    }
    if (kind.empty()) {
      g = map_;
    } else if (frame_ && measured_maps_frame_ == frame_->id && measured_maps_.contains(kind)) {
      g = std::make_shared<Grid>(measured_maps_.at(kind));
    } else if (kind == "density") {
      scene = scene_;
      frame = frame_;
    }
  }
  if (scene && frame) {
    g = std::make_shared<Grid>(occupancy_grid(*scene, *frame, 32));
  }
  std::string out(24, '\0');
  std::memcpy(out.data(), "LHM1", 4);
  const std::uint32_t bx = g ? std::uint32_t(g->bx) : 0, by = g ? std::uint32_t(g->by) : 0;
  const float         peak = g ? float(g->peak) : 0, ovf = g ? float(g->overflow) : 0;
  std::memcpy(out.data() + 4, &bx, 4);
  std::memcpy(out.data() + 8, &by, 4);
  std::memcpy(out.data() + 12, &peak, 4);
  std::memcpy(out.data() + 16, &ovf, 4);
  if (g && g->v.size() == size_t(bx) * by) {
    out.append(reinterpret_cast<const char*>(g->v.data()), g->v.size() * 4);
  }
  return out;
}

Edits Session::poll(int iteration, const double* x, const double* y, size_t n, const Stats& st, const Map_provider* maps,
                    const Timing_provider* timing) {
  if (controller_) {
    auto stats         = st;
    stats.iteration    = iteration;
    const auto payload = wire::pack(std::string{},
                                    std::span(x, n),
                                    std::span(y, n),
                                    stats,
                                    bool(maps),
                                    bool(timing),
                                    bool(fork_builder_),
                                    bool(legalizer_),
                                    bool(intent_provider_),
                                    bool(orientation_provider_));
    const auto data
        = controller_->call("poll", payload, maps, timing, &fork_builder_, &legalizer_, &intent_provider_, &orientation_provider_);
    if (cfg_.evaluator_policy) {
      *cfg_.evaluator_policy = policy();
    }
    return wire::read<Edits>(data);
  }
  std::unique_lock lk(m_);
  work_.gp_iterations += st.gp_steps;
  Stats            s = st;
  s.iteration        = iteration;
  Edits discard;
  drain_locked(discard, lk);
  replay_locked(iteration, discard, lk);
  // A mid-GP pause can be reproduced without a timing-dependent GUI pause command.
  const auto pause_label  = std::format("pause_{}_{}", stage_name_, iteration);
  const bool pause_point  = paused_ && !finishing_ && !replaying();
  const bool resume_point = replaying() && cfg_.resume_label == pause_label;
  const int  poll_nth     = ++poll_counts_[pause_label];
  if (resume_point && cfg_.resume_nth == poll_nth) {
    paused_ = true;
  }
  publish_locked(x, y, n, s, paused_ && !finishing_ ? "paused" : "running");
  if (pause_point || (resume_point && cfg_.resume_nth == poll_nth)) {
    native_poll_frame_        = frame_->id;
    native_poll_baseline_     = s.native_gp ? baseline_identity_locked() : std::string{};
    gate_name_                = pause_label;
    label_count_[pause_label] = poll_nth - 1;
    checkpoint_locked(pause_label);
    legalize_available_ = bool(legalizer_);
  }
  service_map(lk, maps, true);
  service_timing(lk, timing, true);
  while (paused_ && !finishing_ && !exit_.load()) {
    cv_.wait_for(lk, std::chrono::milliseconds(200), [&] {
      return !queue_.empty() || !inline_requests_.empty() || !snapshot_requested_.empty() || exit_.load();
    });
    const bool had = !queue_.empty();
    drain_locked(discard, lk);
    service_map(lk, maps, false);
    service_timing(lk, timing, false);
    if (had && paused_) {
      // a move while paused only accumulates; the published frame already shows it
    }
  }
  if (gate_name_ == pause_label) {
    gate_name_.clear();
  }
  if (frame_ && !finishing_) {
    state_name_ = "running";
    bump_locked();
  }
  return take_edits_locked();
}

Session::End_result Session::gp_end(const double* x, const double* y, size_t n, const Stats& st, const Map_provider* maps,
                                    const Timing_provider* timing) {
  if (controller_) {
    auto stats = st;

    const auto payload = wire::pack(std::string{},
                                    std::span(x, n),
                                    std::span(y, n),
                                    stats,
                                    bool(maps),
                                    bool(timing),
                                    bool(fork_builder_),
                                    bool(legalizer_),
                                    bool(intent_provider_),
                                    bool(orientation_provider_));
    const auto data
        = controller_
              ->call("gp_end", payload, maps, timing, &fork_builder_, &legalizer_, &intent_provider_, &orientation_provider_);
    if (cfg_.evaluator_policy) {
      *cfg_.evaluator_policy = policy();
    }
    End_result r;
    wire::unpack(data, r.edits, r.extend);
    return r;
  }
  std::unique_lock lk(m_);
  Edits            discard;
  drain_locked(discard, lk);
  End_result out;
  work_.gp_iterations += st.gp_steps;
  {
    Stats s = st;
    publish_locked(x, y, n, s, "running");
    checkpoint_locked("gp_end");
  }
  if (replaying()) {  // the recorded decision: extend by the same count at the same phase and iteration
    replay_locked(st.iteration, discard, lk);
    if (!replay_.empty() && replay_.front().cmd.cmd == "extend" && replay_.front().phase == stage_name_
        && replay_.front().iter <= st.iteration) {
      out.extend = replay_.front().cmd.count;
      replay_.pop_front();
    }
    out.edits = take_edits_locked();
    return out;
  }
  const bool block = !finishing_ && !replaying() && (paused_ || repause_ || !switch_on_locked("legalize"));
  if (!block) {
    out.edits = take_edits_locked();
    return out;
  }
  at_gp_end_  = true;
  gp_decided_ = false;
  gp_extend_  = 0;
  gate_name_  = "gp_end";
  legalize_available_ = bool(legalizer_);
  publish_locked(x, y, n, st, "gate");
  service_map(lk, maps, true);
  service_timing(lk, timing, false);
  while (!gp_decided_ && !finishing_ && !exit_.load()) {
    cv_.wait_for(lk, std::chrono::milliseconds(200), [&] {
      return !queue_.empty() || !inline_requests_.empty() || !snapshot_requested_.empty() || exit_.load();
    });
    drain_locked(discard, lk);
    service_map(lk, maps, false);
    service_timing(lk, timing, false);
  }
  at_gp_end_ = false;
  gate_name_.clear();
  legalize_available_ = false;
  if (gp_decided_ && gp_extend_ > 0) {
    paused_  = false;  // the extra iterations run free ...
    repause_ = true;   // ... and the session stops again at their end
    out.extend = gp_extend_;
  }
  state_name_ = "running";
  bump_locked();
  out.edits = take_edits_locked();
  return out;
}

std::map<std::string, std::pair<double, double>> Session::pin_positions() const {
  if (controller_) {
    return wire::read<decltype(pin_positions())>(controller_->call("anchors"));
  }
  std::lock_guard                                  lk(m_);
  std::map<std::string, std::pair<double, double>> out;
  for (const auto& [name, p] : pins_) {
    if (suspended_pins_.contains(name)) {
      continue;
    }
    out.emplace(name, std::pair{p.x, p.y});
  }
  return out;
}

void Session::set_placement(std::map<std::string, std::array<std::int64_t, 3>> placement) {
  if (controller_) {
    controller_->call("set_placement", wire::pack(placement));
    return;
  }
  std::lock_guard lk(m_);
  placement_ = std::move(placement);
}

std::map<std::string, std::array<std::int64_t, 3>> Session::placement_override() const {
  if (controller_) {
    return wire::read<decltype(placement_override())>(controller_->call("placement"));
  }
  std::lock_guard lk(m_);
  return placement_;
}

void Session::record_edits(const std::vector<std::pair<std::string, std::pair<double, double>>>& edits) {
  if (controller_) {
    controller_->call("record_edits", wire::pack(edits));
    return;
  }
  std::lock_guard lk(m_);
  edits_log_.insert(edits_log_.end(), edits.begin(), edits.end());
}

std::vector<std::pair<std::string, std::pair<double, double>>> Session::recorded_edits() const {
  if (controller_) {
    return wire::read<decltype(recorded_edits())>(controller_->call("recorded_edits"));
  }
  std::lock_guard lk(m_);
  return edits_log_;
}

bool Session::edit_left() const {
  if (controller_) {
    return wire::read<decltype(edit_left())>(controller_->call("edit_left"));
  }
  std::lock_guard lk(m_);
  return edit_left_;
}

void Session::set_note(const std::string& note) {
  if (controller_) {
    controller_->call("note", note);
    return;
  }
  std::lock_guard lk(m_);
  note_ = note;
  bump_locked();
}

Session::Edit_result Session::edit_wait(const double* x, const double* y, size_t n, const Stats& st, const Map_provider* maps,
                                        const Timing_provider* timing) {
  if (controller_) {
    auto stats = st;

    const auto payload = wire::pack(std::string{},
                                    std::span(x, n),
                                    std::span(y, n),
                                    stats,
                                    bool(maps),
                                    bool(timing),
                                    bool(fork_builder_),
                                    bool(legalizer_),
                                    bool(intent_provider_),
                                    bool(orientation_provider_));
    const auto data
        = controller_->call("edit", payload, maps, timing, &fork_builder_, &legalizer_, &intent_provider_, &orientation_provider_);
    if (cfg_.evaluator_policy) {
      *cfg_.evaluator_policy = policy();
    }
    Edit_result r;
    wire::unpack(data, r.edits, r.again);
    return r;
  }
  std::unique_lock lk(m_);
  Edits            discard;
  drain_locked(discard, lk);
  Edit_result out;
  if (edit_left_) {  // the flow re-runs final passes (retries, region experiments): edit once
    return out;
  }
  publish_locked(x, y, n, st, "running");
  checkpoint_locked("edit");
  if (replaying()) {
    replay_locked(0, discard, lk);
    out.edits = take_edits_locked();
    out.again  = !out.edits.moves.empty() || !out.edits.pins.empty();
    edit_left_ = !out.again;
    return out;
  }
  const bool block = !finishing_ && (paused_ || !switch_on_locked("edit"));
  if (!block) {
    publish_locked(x, y, n, st, "running");
    edit_left_ = true;
    return out;
  }
  at_edit_      = true;
  edit_decided_ = false;
  gate_name_    = "edit";
  legalize_available_ = bool(legalizer_);
  publish_locked(x, y, n, st, "gate");
  service_map(lk, maps, true);
  service_timing(lk, timing, false);
  while (!edit_decided_ && !finishing_ && !exit_.load()) {
    cv_.wait_for(lk, std::chrono::milliseconds(200), [&] {
      return !queue_.empty() || !inline_requests_.empty() || !snapshot_requested_.empty() || exit_.load();
    });
    drain_locked(discard, lk);
    service_map(lk, maps, false);
    service_timing(lk, timing, false);
  }
  at_edit_ = false;
  gate_name_.clear();
  legalize_available_ = false;
  state_name_ = "running";
  if (edit_decided_ && !finishing_) {
    out.edits = take_edits_locked();
    out.again = !out.edits.moves.empty() || !out.edits.pins.empty();
  }
  edit_left_ = !out.again;
  note_.clear();
  bump_locked();
  return out;
}

Edits Session::gate(const std::string& stage, const double* x, const double* y, size_t n, const Stats& st, const Map_provider* maps,
                    const Timing_provider* timing) {
  if (controller_) {
    auto stats = st;

    const auto payload = wire::pack(stage,
                                    std::span(x, n),
                                    std::span(y, n),
                                    stats,
                                    bool(maps),
                                    bool(timing),
                                    bool(fork_builder_),
                                    bool(legalizer_),
                                    bool(intent_provider_),
                                    bool(orientation_provider_));
    const auto data
        = controller_->call("gate", payload, maps, timing, &fork_builder_, &legalizer_, &intent_provider_, &orientation_provider_);
    if (cfg_.evaluator_policy) {
      *cfg_.evaluator_policy = policy();
    }
    return wire::read<Edits>(data);
  }
  std::unique_lock lk(m_);
  gate_released_ = false;
  Edits discard;
  drain_locked(discard, lk);
  replay_locked(0, discard, lk);
  const auto blocked
      = [&] { return stage == "repair_result" || (!finishing_ && (paused_ || (!switch_on_locked(stage) && !gate_released_))); };
  gate_name_         = stage;
  legalize_available_ = bool(legalizer_);
  publish_locked(x, y, n, st, "running");
  checkpoint_locked(stage);
  if (!dirty_pins_.empty() && scene_ && frame_) {
    // Pins that reach the solver only when this gate lets go (replayed ones, or macros pinned in an earlier phase) are
    // already where the user put them: the page shows them there.
    auto next = std::make_shared<Frame>(*frame_);
    bool any  = false;
    for (const auto& name : dirty_pins_) {
      const auto pin   = pins_.find(name);
      const auto found = scene_->index.find(name);
      if (pin == pins_.end() || found == scene_->index.end() || !scene_->nodes[size_t(found->second)].movable
          || size_t(found->second) >= next->x.size()) {
        continue;
      }
      next->x[size_t(found->second)] = pin->second.x;
      next->y[size_t(found->second)] = pin->second.y;
      any                            = true;
    }
    if (any) {
      next->id = ++frame_id_;
      push_frame_locked(std::move(next));
    }
  }
  if (blocked()) {
    state_name_ = "gate";
    bump_locked();
  }
  service_map(lk, maps, true);
  service_timing(lk, timing, false);
  while (blocked() && !exit_.load()) {
    cv_.wait_for(lk, std::chrono::milliseconds(200), [&] {
      return !queue_.empty() || !inline_requests_.empty() || !snapshot_requested_.empty() || exit_.load();
    });
    drain_locked(discard, lk);
    service_map(lk, maps, false);
    service_timing(lk, timing, false);
  }
  gate_name_.clear();
  legalize_available_ = false;
  state_name_ = "running";
  bump_locked();
  return take_edits_locked();
}

void Session::set_result(const std::vector<std::tuple<std::string, double, double>>& placed) {
  if (controller_) {
    controller_->call("result", wire::pack(placed));
    return;
  }
  {
    std::lock_guard clear(m_);
    fork_builder_ = nullptr;  // the placement data it reads is gone or going
  }
  std::lock_guard lk(m_);
  if (!scene_ || !frame_) {
    return;
  }
  auto next = std::make_shared<Frame>(*frame_);
  for (const auto& [name, x, y] : placed) {
    const auto f = scene_->index.find(name);
    if (f != scene_->index.end() && size_t(f->second) < next->x.size()) {
      next->x[size_t(f->second)] = x;
      next->y[size_t(f->second)] = y;
    }
  }
  next->id    = ++frame_id_;
  next->state = "result";
  push_frame_locked(std::move(next));
  bump_locked();
}

void Session::fail(const std::string& title, const std::string& detail) {
  if (controller_) {
    controller_->call("fail", wire::pack(title, detail));
    return;
  }
  {
    std::lock_guard clear(m_);
    fork_builder_ = nullptr;  // the placement data it reads is gone or going
  }
  std::unique_lock lk(m_);
  failed_      = true;
  error_       = detail.empty() ? title : title + "\n" + detail;
  gate_name_.clear();
  legalize_available_ = false;
  replay_active_ = false;
  state_name_  = "failed";
  paused_      = false;
  bump_locked();
  Edits discard;
  while (!finishing_ && !exit_.load()) {
    cv_.wait_for(lk, std::chrono::milliseconds(200), [&] {
      return !queue_.empty() || !inline_requests_.empty() || !snapshot_requested_.empty() || exit_.load();
    });
    drain_locked(discard, lk);
  }
}

namespace {
std::shared_ptr<Session>& active_slot() {
  static auto* slot = new std::shared_ptr<Session>();  // intentionally never freed: a failure may be reported at exit
  return *slot;
}
}  // namespace
void set_active_session(std::shared_ptr<Session> s) { active_slot() = std::move(s); }
std::shared_ptr<Session> active_session() { return active_slot(); }

void Session::wait_finish() {
  if (controller_) {
    controller_->call("finish", {});
    return;
  }
  {
    std::lock_guard clear(m_);
    fork_builder_ = nullptr;  // the placement data it reads is gone or going
  }
  if (replaying()) {
    if (cfg_.resume_label.empty()) {
      return;  // a batch replay: nobody to press finish
    }
    std::lock_guard lk(m_);  // a resumed run that never reached its checkpoint: hand control to the user
    replay_active_ = false;
    note_          = std::format("checkpoint {} #{} was not reached with these flags", cfg_.resume_label, cfg_.resume_nth);
  }
  std::unique_lock lk(m_);
  finished_ = true;
  Edits discard;
  publish_locked(nullptr, nullptr, 0, stats_, "finished");
  checkpoint_locked("finished");
  while (!finishing_ && !exit_.load()) {
    cv_.wait_for(lk, std::chrono::milliseconds(200), [&] {
      return !queue_.empty() || !inline_requests_.empty() || !snapshot_requested_.empty() || exit_.load();
    });
    drain_locked(discard, lk);
  }
}

Edits Session::all_pins(bool legalizing) const {
  if (controller_) {
    return wire::read<Edits>(controller_->call(legalizing ? "legalization_pins" : "pins"));
  }
  std::lock_guard lk(m_);
  return pins_locked(legalizing);
}

Edits Session::pins_locked(bool legalizing) const {
  Edits out;
  if (scene_) {
    // A free neighbor can overlap a held SRAM during GP. Legalization can
    // move that neighbor away while restoring the requested anchor. Check
    // the proposed anchors together against each other and fixed geometry.
    std::set<std::string> blocked;
    const bool try_anchors = legalizing && frame_ && !suspended_pins_.empty();
    if (try_anchors) {
      Frame proposed;
      proposed.x = frame_->x;
      proposed.y = frame_->y;
      for (const auto& [name, pin] : pins_) {
        const int i = scene_->node_index(name);
        if (i >= 0 && scene_->nodes[i].movable) {
          proposed.x.at(i) = pin.x;
          proposed.y.at(i) = pin.y;
        }
      }
      const auto free = [&](int i) {
        const auto& n = scene_->nodes[i];
        return n.movable && !n.exclusion && !pins_.contains(n.name);
      };
      for (const auto& issue : macro_geometry(*scene_, proposed)) {
        if (free(issue.node) || (issue.other >= 0 && free(issue.other))) {
          continue;
        }
        blocked.insert(scene_->nodes[issue.node].name);
        if (issue.other >= 0) {
          blocked.insert(scene_->nodes[issue.other].name);
        }
      }
    }
    for (const auto& [name, pin] : pins_) {
      const auto f = scene_->index.find(name);
      if (f != scene_->index.end() && !(try_anchors ? blocked : suspended_pins_).contains(name)
          && scene_->nodes[size_t(f->second)].movable) {  // a macro pinned earlier is fixed now
        out.pins.push_back({f->second, pin.x, pin.y});
      }
    }
    for (const auto& [name, pin] : cpins_) {
      if (const auto f = scene_->centroid_index.find(name); f != scene_->centroid_index.end()) {
        out.centroids.push_back({f->second, pin.x, pin.y});
      }
    }
  }
  return out;
}

std::shared_ptr<const Frame> Session::frame() const {
  std::lock_guard lk(m_);
  return frame_;
}
std::shared_ptr<const Scene> Session::scene() const {
  std::lock_guard lk(m_);
  return controller_ ? controller_->scene : scene_;
}

std::uint64_t Session::wait_version(std::uint64_t since, int timeout_ms) const {
  std::unique_lock lk(m_);
  updates_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&] { return version_ != since || exit_.load(); });
  return version_;
}

std::string Session::design_json(int cp, int tl) const {
  std::lock_guard lk(m_);
  std::string     out;
  const auto      sc = tl >= 0 ? (size_t(tl) < timeline_.size() ? timeline_[size_t(tl)].scene : nullptr)
                       : cp >= 0 ? (size_t(cp) < checkpoints_.size() ? checkpoints_[size_t(cp)].scene : nullptr) : scene_;
  if (!sc) {
    return R"({"scene":0,"n":0})";
  }
  const auto& s = *sc;
  out += std::format(R"({{"scene":{},"phase":"{}","dbu":{},"die":[{},{},{},{}],"cell_area":{},"macro_area":{},"macro_limit":{},"n":{},"kinds":")",
                     tl >= 0 ? timeline_[size_t(tl)].scene_id : cp >= 0 ? 0 : scene_version_,
                     esc(s.phase),
                     num(s.dbu_per_um),
                     num(s.die.xl),
                     num(s.die.yl),
                     num(s.die.xh),
                     num(s.die.yh),
                     num(s.cell_area),
                     num(s.macro_area),
                     num(Scene::kMacroFillLimit),
                     s.nodes.size());
  for (const auto& n : s.nodes) {
    out += char('0' + int(n.kind) + (n.movable ? 0 : 5));
  }
  const bool any_seq = std::any_of(s.nodes.begin(), s.nodes.end(), [](const Node& n) { return n.seq; });
  if (any_seq) {
    out += R"(","seq":")";
    for (const auto& n : s.nodes) {
      out += n.seq ? '1' : '0';
    }
  }
  out += R"(","w":[)";
  for (size_t i = 0; i < s.nodes.size(); ++i) {
    out += (i ? "," : "") + num(s.nodes[i].w);
  }
  out += R"(],"h":[)";
  for (size_t i = 0; i < s.nodes.size(); ++i) {
    out += (i ? "," : "") + num(s.nodes[i].h);
  }
  out += R"(],"names":{)";
  bool first = true;
  for (size_t i = 0; i < s.nodes.size(); ++i) {
    if (s.nodes[i].kind != Kind::cell || s.nodes[i].seq) {
      out += std::format(R"({}"{}":"{}")", first ? "" : ",", i, esc(s.nodes[i].name));
      first = false;
    }
  }
  out += std::format(R"(}},"halo":[{},{}],"channel":[{},{}],"regions":[)", s.halo_x, s.halo_y, s.channel_x, s.channel_y);
  for (size_t i = 0; i < s.regions.size(); ++i) {
    const auto& r = s.regions[i];
    out += std::format("{}[{},{},{},{}]", i ? "," : "", num(r.xl), num(r.yl), num(r.xh), num(r.yh));
  }
  out += std::format(R"(],"usable_area":{},"outline":[)", s.usable_area);
  for (size_t i = 0; i < s.outline.size(); ++i) {
    out += i ? ",[" : "[";
    for (size_t j = 0; j < s.outline[i].size(); ++j) {
      out += std::format("{}[{},{}]", j ? "," : "", s.outline[i][j][0], s.outline[i][j][1]);
    }
    out += "]";
  }
  out += R"(],"exclusions":[)";
  for (size_t i = 0; i < s.exclusions.size(); ++i) {
    const auto& b  = s.exclusions[i];
    out           += std::format("{}[{},{},{},{}]", i ? "," : "", b.xl, b.yl, b.xh, b.yh);
  }
  out            += R"(],"macro_pins":[)";
  bool first_pin  = true;
  for (size_t i = 0; i < s.nodes.size(); ++i) {
    for (const auto& pin : s.nodes[i].signal_pins) {
      out       += std::format("{}[{},{},{}]", first_pin ? "" : ",", i, num(pin.x), num(pin.y));
      first_pin  = false;
    }
  }
  out += R"(],"centroids":[)";
  for (size_t i = 0; i < s.centroids.size(); ++i) {
    const auto& c = s.centroids[i];
    out += std::format(R"({}{{"name":"{}","x":{},"y":{},"r":{},"movable":{}}})",
                       i ? "," : "",
                       esc(c.name),
                       num(c.x),
                       num(c.y),
                       num(c.radius),
                       c.movable ? "true" : "false");
  }
  out += "]}";
  return out;
}

std::string Session::node_json(int index) const {
  std::lock_guard lk(m_);
  if (!scene_ || index < 0 || size_t(index) >= scene_->nodes.size()) {
    return "{}";
  }
  const auto& n = scene_->nodes[size_t(index)];
  return std::format(R"({{"i":{},"name":"{}","kind":{},"movable":{},"w":{},"h":{},"pinned":{}}})",
                     index,
                     esc(n.name),
                     int(n.kind),
                     n.movable ? "true" : "false",
                     num(n.w),
                     num(n.h),
                     pins_.contains(n.name) ? "true" : "false");
}

std::string Session::query_json(const std::string& name, const Box* box, int limit) const {
  std::lock_guard lk(m_);
  if (!scene_ || !frame_) {
    return R"({"error":"no frame yet"})";
  }
  const auto& s = *scene_;
  const auto& f = *frame_;
  const auto  one = [&](size_t i) {
    return std::format(R"({{"i":{},"name":"{}","kind":"{}","movable":{},"x":{},"y":{},"w":{},"h":{},"pinned":{}}})",
                       i,
                       esc(s.nodes[i].name),
                       s.nodes[i].kind == Kind::cell ? "cell" : s.nodes[i].kind == Kind::macro ? "macro" : s.nodes[i].kind == Kind::fixed ? "fixed" : s.nodes[i].kind == Kind::io ? "io" : "blockage",
                       s.nodes[i].movable ? "true" : "false",
                       num(f.x[i]),
                       num(f.y[i]),
                       num(s.nodes[i].w),
                       num(s.nodes[i].h),
                       pins_.contains(s.nodes[i].name) ? "true" : "false");
  };
  if (!name.empty()) {
    const auto it = s.index.find(name);
    if (it == s.index.end() || size_t(it->second) >= f.x.size()) {
      return R"({"error":"no such node"})";
    }
    return one(size_t(it->second));
  }
  if (!box) {
    return R"({"error":"give name= or x0,y0,x1,y1"})";
  }
  std::string out = R"({"nodes":[)";
  size_t      count = 0, shown = 0;
  for (size_t i = 0; i < std::min(s.nodes.size(), f.x.size()); ++i) {
    if (f.x[i] + s.nodes[i].w < box->xl || f.x[i] > box->xh || f.y[i] + s.nodes[i].h < box->yl || f.y[i] > box->yh) {
      continue;
    }
    ++count;
    if (int(shown) < limit) {
      out += (shown ? "," : "") + one(i);
      ++shown;
    }
  }
  return out + std::format(R"(],"count":{},"shown":{}}})", count, shown);
}

// Keep one exact final scene and its measurements. Completed background jobs
// release their evaluator and load these private result bytes only for readers.
template <class Archive>
void Session::result_fields(Archive& archive) {
  wire::fields(archive,
               work_.full_calls,
               work_.ranking_calls,
               work_.gp_iterations,
               replay_work_.full_calls,
               replay_work_.ranking_calls,
               replay_work_.gp_iterations,
               timing_budget_exhausted_,
               cfg_.timing_call_limit,
               cfg_.repair_trials,
               cfg_.repair_limit,
               cfg_.terminal_sizing,
               cfg_.terminal_buffering,
               cfg_.terminal_resynthesis,
               last_rc_s_,
               last_sta_s_,
               last_timing_s_,
               scoped_resize_changes_,
               initial_policy_,
               cfg_.input_identity,
               cfg_.source_identity,
               cfg_.intent,
               intent_revision_,
               hint_hash_,
               intent_report_,
               intent_report_frame_,
               intent_report_sequence_,
               intent_status_,
               cfg_.target,
               cfg_.hello,
               cfg_.policy,
               cfg_.score,
               cfg_.score_estimator,
               options_json_,
               epoch_,
               version_,
               frame_id_,
               scene_version_,
               design_hash_,
               stage_name_,
               stats_,
               pins_,
               cpins_,
               queued_sequence_,
               completed_sequence_,
               command_results_,
               relationships_,
               stages_,
               seq_,
               period_ps_,
               has_timing_,
               timing_est_,
               wns_ps_,
               tns_ps_,
               timing_iter_,
               evaluated_frame_,
               evaluation_seq_,
               measured_maps_,
               measured_maps_frame_,
               wanted_map_,
               map_kind_,
               map_note_,
               map_frame_,
               map_version_,
               regions_wanted_,
               regions_version_,
               regions_frame_,
               crit_result_,
               crit_error_,
               hold_wns_ps_,
               electrical_violations_,
               worst_path_,
               endpoint_report_,
               legalization_status_,
               legalization_error_,
               legalized_moves_,
               relaxation_iterations_);
}
std::string Session::capture_baseline() const {
  std::lock_guard lock(m_);
  if (cfg_.target == "main" || !frame_ || (state_name_ != "gate" && state_name_ != "paused")
      || !queue_.empty() || checkpoints_.empty()) {
    throw std::runtime_error("foreground must be at a stable placement boundary");
  }
  std::ifstream segment(cfg_.trace_path);
  if (!segment) {
    throw std::runtime_error("foreground replay segment is unavailable");
  }
  const std::string trace{std::istreambuf_iterator<char>(segment), {}};
  return wire::pack(result_snapshot_locked(), trace, gate_name_, checkpoints_.back().nth,
                    legalize_available_, bool(intent_provider_), bool(orientation_provider_));
}
std::string Session::result_snapshot() const {
  std::lock_guard lock(m_);
  return result_snapshot_locked();
}
void Session::save_foreground_measurement_locked() {
  // The supervisor may discard or reap this worker before its next poll. Publish
  // only complete measurements beside the worker socket, in its private directory.
  const auto path = std::filesystem::path(cfg_.agent_socket).parent_path() / "foreground-measurement.json";
  const auto tmp  = path.string() + ".tmp";
  std::ofstream out(tmp);
  out << std::format(R"({{"frame":{},"epoch":"{}","trace_seq":{},"gate":"{}","iteration":{},"identity":"{}","input_identity":"{}","topology":"{:016x}","masters":"{:016x}","geometry_status":"{}","policy":{{"no_sizing":{},"no_buffer_edits":{},"no_resynthesis":{}}},"summary":{}}})",
                     frame_->id, esc(epoch_), seq_, esc(gate_name_), iteration_, placement_identity_locked(),
                     esc(cfg_.input_identity), scene_->topology_hash, scene_->masters_hash,
                     geometry_conflicts_.empty() ? "clear" : "illegal", cfg_.policy.no_sizing,
                     cfg_.policy.no_buffer_edits, cfg_.policy.no_resynthesis, summary_locked());
  out.close();
  std::error_code error;
  if (out) {
    std::filesystem::rename(tmp, path, error);
  }
  if (!out || error) {
    note_ = "could not retain the foreground timing measurement";
  }
}
std::string Session::result_snapshot_locked() const {
  if (!scene_ || !frame_) {
    throw std::runtime_error("no evaluated scene");
  }
  wire::Encoder encoder;
  const_cast<Session*>(this)->result_fields(encoder);
  encoder.bytes += wire::pack(*scene_, *frame_, map_ != nullptr);
  if (map_) {
    encoder.bytes += wire::pack(*map_);
  }
  encoder.bytes += wire::pack(regions_ != nullptr);
  if (regions_) {
    encoder.bytes += wire::pack(*regions_);
  }
  return std::move(encoder.bytes);
}
std::unique_ptr<Session> Session::read_result_snapshot(std::string_view bytes, std::string routed_summary) {
  auto          doc = std::make_unique<Session>(Config{});
  wire::Decoder decoder{bytes};
  doc->result_fields(decoder);
  auto scene  = std::make_shared<Scene>();
  auto frame  = std::make_shared<Frame>();
  bool mapped = false, regions = false;
  wire::fields(decoder, *scene, *frame, mapped);
  if (mapped) {
    auto grid = std::make_shared<Grid>();
    wire::field(decoder, *grid);
    doc->map_ = std::move(grid);
  }
  wire::field(decoder, regions);
  if (regions) {
    auto grid = std::make_shared<Grid>();
    wire::field(decoder, *grid);
    doc->regions_ = std::move(grid);
  }
  if (!decoder.bytes.empty()) {
    throw std::runtime_error("invalid placement result");
  }
  doc->scene_ = std::move(scene);
  doc->push_frame_locked(std::move(frame));
  doc->dirty_pins_.clear();
  doc->released_.clear();
  doc->state_name_ = "done";
  doc->finished_   = true;
  doc->paused_     = false;
  doc->routed_summary_ = std::move(routed_summary);
  return doc;
}

Edits Session::prepare_frozen(const std::string& id, const std::string& patch, const std::string& score) {
  cfg_.target            = id;
  cfg_.batch_worker      = true;
  cfg_.timing_call_limit = 1;
  work_ = replay_work_     = {};
  timing_budget_exhausted_ = false;
  relationships_.clear();
  command_results_.clear();
  command_result_bytes_ = queued_sequence_ = completed_sequence_ = 0;
  cfg_.policy                                                    = {true, true, true};
  if (agent_call(score).find(R"("ok":true)") == std::string::npos) {
    throw std::runtime_error("invalid snapshot score configuration");
  }
  rapidjson::Document inputs;
  inputs.Parse<rapidjson::kParseFullPrecisionFlag>(patch.c_str());
  std::unique_lock lock(m_);
  Edits            edits;
  const auto       apply = [&](const rapidjson::Value& entry) {
    rapidjson::StringBuffer                    buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    entry.Accept(writer);
    Command c;
    if (auto why = parse_command(buffer.GetString(), c); !why.empty()) {
      throw std::runtime_error(why);
    }
    if (auto why = resolve_move_locked(c); !why.empty()) {
      throw std::runtime_error(why);
    }
    if (c.cmd != "move" && c.cmd != "unpin" && c.cmd != "pin") {
      throw std::runtime_error("unsupported snapshot command");
    }
    for (const auto& name : c.nodes) {
      const auto it = scene_->index.find(name);
      if (it == scene_->index.end() || !scene_->nodes[it->second].movable
          || (c.cmd == "pin" && geometry_conflicts_.contains(name))) {
        throw std::runtime_error("invalid snapshot node: " + name);
      }
    }
    apply_locked(c, edits);
  };
  if (inputs.IsArray()) {
    for (const auto& cmd : inputs.GetArray()) {
      apply(cmd);
    }
  } else if (inputs.IsObject()) {
    apply(inputs);
  } else {
    throw std::runtime_error("invalid snapshot patch");
  }
  return take_edits_locked();
}
std::string Session::run_frozen_timing(const std::string& id, const std::string& patch, const std::string& score,
                                       const Map_provider& maps, const Timing_provider& timing, const std::string& relax,
                                       const Legalizer& legalize) {
  prepare_frozen(id, patch, score);
  std::unique_lock lock(m_);
  if (!relax.empty()) {
    Command command;
    if (auto why = parse_command(relax, command); !why.empty()) {
      throw std::runtime_error(why);
    }
    if (command.cmd != "relax") {
      throw std::runtime_error("snapshot relaxation requires an iteration budget");
    }
    legalizer_          = legalize;
    legalize_available_ = true;
    legalize_unlocked(command, lock);
    legalizer_ = {};
    if (legalization_status_ != "applied") {
      throw std::runtime_error(legalization_error_);
    }
  }
  return finish_frozen(lock, maps, timing);
}
std::string Session::finish_native_gp(const std::vector<double>& x, const std::vector<double>& y, const Stats& final,
                                      const Map_provider& maps, const Timing_provider& timing) {
  std::unique_lock lock(m_);
  if (!frame_ || x.size() != frame_->x.size() || y.size() != x.size() || final.iteration < frame_->iteration) {
    throw std::runtime_error("inconsistent native GP end frame");
  }
  work_.gp_iterations = final.iteration - frame_->iteration;
  gate_name_          = "gp_end";
  at_gp_end_          = true;
  publish_locked(x.data(), y.data(), x.size(), final, "gate");
  return finish_frozen(lock, maps, timing);
}
std::string Session::finish_frozen(std::unique_lock<std::mutex>& lock, const Map_provider& maps, const Timing_provider& timing) {
  regions_wanted_ = regions_dirty_ = true;
  all_maps_wanted_                 = true;
  wanted_map_                      = cfg_.score_estimator;
  map_dirty_                       = true;
  service_map(lock, &maps, true);
  full_timing_wanted_ = timing_wanted_ = true;
  service_timing(lock, &timing, false);
  if (!has_timing_ || !frame_ || evaluated_frame_ != frame_->id) {
    throw std::runtime_error("full frozen timing unavailable");
  }
  return result_snapshot_locked();
}

std::string Session::repair_advice_locked() const {
  if (!frame_ || evaluated_frame_ != frame_->id || !has_timing_ || timing_est_) {
    return "null";
  }
  std::string reasons;
  const auto add = [&](const char* reason) {
    if (!reasons.empty()) {
      reasons += ',';
    }
    reasons += std::format("\"{}\"", reason);
  };
  if (std::isfinite(wns_ps_) && wns_ps_ < 0) {
    add("negative_setup_slack");
  }
  if (std::isfinite(hold_wns_ps_) && hold_wns_ps_ < 0) {
    add("negative_hold_slack");
  }
  if (electrical_violations_ > 0) {
    add("electrical_violations");
  }
  return std::format(R"({{"recommended":{},"reasons":[{}],"mutations_frozen":{}}})",
                     !reasons.empty(), reasons, cfg_.policy.no_sizing && cfg_.policy.no_buffer_edits && cfg_.policy.no_resynthesis);
}

std::string measurement_summary(const Summary_metrics& m, const Score_components& config, const std::string& estimator) {
  const auto finite   = [](const std::optional<double>& v) { return v && std::isfinite(*v); };
  const auto value    = [&](const std::optional<double>& v) { return finite(v) ? num(*v) : std::string{"null"}; };
  const auto validity = [&](bool present, bool estimate = false) {
    return !present ? "unavailable" : estimate || m.geometry == "illegal" ? "approximate" : "meaningful";
  };
  std::map<std::string, double> losses;
  if (!m.timing_estimate && finite(m.wns)) {
    losses["negative_wns_ps"] = std::max(0., -*m.wns);
  }
  if (!m.timing_estimate && finite(m.tns)) {
    losses["negative_tns_ps"] = std::max(0., -*m.tns);
  }
  if (finite(m.over_share)) {
    losses["overflow_bin_share"] = *m.over_share;
  }
  if (finite(m.overflow)) {
    losses["routing_overflow"] = *m.overflow;
  }
  if (finite(m.hpwl)) {
    losses["hpwl_um"] = *m.hpwl;
  }
  std::string terms;
  double      score = 100;
  bool        valid = !config.empty();
  for (const auto& [metric, params] : config) {
    const auto [weight, half] = params;
    const auto it             = losses.find(metric);
    if (it != losses.end()) {
      score *= std::pow(1 + it->second / half, -weight);
    } else if (weight > 0) {
      valid = false;
    }
    terms += std::format(R"({}{{"loss":"{}","value":{},"weight":{},"half":{},"validity":"{}"}})",
                         terms.empty() ? "" : ",",
                         metric,
                         it == losses.end() ? "null" : num(it->second),
                         weight,
                         half,
                         validity(it != losses.end(), metric == "overflow_bin_share" || metric == "routing_overflow"));
  }
  return std::format(
      R"({{"geometry_status":"{}","timing":{{"wns_ps":{},"tns_ps":{},"hold_wns_ps":{},"electrical_violations":{},"repair_advice":{},"model":"{}","frame":{},"endpoints":{}}},"routing":{{"estimator":"{}","overflow_bin_share":{},"overflow":{},"peak":{},"frame":{}}},"quality":{{"score":{},"status":"{}","estimator":"{}","components":[{}]}},"validity":{{"wns_ps":"{}","tns_ps":"{}","hold_wns_ps":"{}","electrical_violations":"{}","overflow_bin_share":"{}","overflow":"{}","peak":"{}","score":"{}"}}}})",
      m.geometry,
      value(m.wns),
      value(m.tns),
      value(m.hold),
      m.electrical ? std::to_string(*m.electrical) : "null",
      m.advice,
      m.timing_model,
      m.timing_frame,
      m.endpoints,
      esc(estimator),
      value(m.over_share),
      value(m.overflow),
      value(m.peak),
      m.routing_frame,
      valid ? num(score) : "null",
      config.empty() ? "not_configured"
      : valid        ? "available"
                     : "unavailable",
      esc(estimator),
      terms,
      validity(finite(m.wns), m.timing_estimate),
      validity(finite(m.tns), m.timing_estimate),
      validity(finite(m.hold)),
      validity(m.electrical.has_value()),
      validity(finite(m.over_share), true),
      validity(finite(m.overflow), true),
      validity(finite(m.peak), true),
      validity(valid, true));  // A composite score is informational, even when all inputs are measured.
}

std::string Session::summary_locked() const {
  if (!routed_summary_.empty()) {
    return routed_summary_;
  }
  const auto      frame = frame_ ? frame_->id : 0;
  Summary_metrics m;
  m.geometry        = !frame ? "unavailable" : geometry_conflicts_.empty() ? "clear" : "illegal";
  m.timing_estimate = timing_est_;
  m.timing_model    = timing_est_ ? "ranking" : "placement_rc";
  m.timing_frame    = evaluated_frame_;
  m.advice          = repair_advice_locked();
  if (frame && evaluated_frame_ == frame && has_timing_) {
    m.wns = wns_ps_;
    m.tns = tns_ps_;
    if (!timing_est_) {
      m.hold = hold_wns_ps_;
      if (electrical_violations_ >= 0) {
        m.electrical = electrical_violations_;
      }
      m.endpoints = endpoint_report_;
    }
  }
  if (const auto map = measured_maps_.find(cfg_.score_estimator);
      frame && measured_maps_frame_ == frame && map != measured_maps_.end() && !map->second.v.empty()) {
    const auto& grid = map->second;
    m.over_share     = double(std::ranges::count_if(grid.v, [](float v) { return v > 1; })) / grid.v.size();
    m.routing_frame  = frame;
    m.overflow       = grid.overflow;
    m.peak           = grid.peak;
  }
  if (frame && stats_.hpwl_um >= 0) {
    m.hpwl = stats_.hpwl_um;
  }
  return measurement_summary(m, cfg_.score, cfg_.score_estimator);
}

std::string Session::state_json() const {
  std::lock_guard lk(m_);
  std::string     out = std::format(
      R"({{"v":{},"frame":{},"scene":{},"phase":"{}","state":"{}","gate":"{}","paused":{},"finished":{},"iter":{},"hpwl":{},"overflow":{},)"
          R"("has_timing":{},"timing_est":{},"period":{},"wns":{},"tns":{},"timing_iter":{},"poll":{},"min_iters":{},"map":"{}","map_v":{},"map_note":"{}","regions_on":{},"regions_v":{},"note":"{}","epoch":"{}","error":"{}","tl_n":{},"tl_v":{},"hl_v":{},"sel_v":{},"cps":[{}],"stages":[)",
      version_,
      frame_ ? frame_->id : 0,
      scene_version_,
      esc(stage_name_),
      esc(state_name_),
      esc(gate_name_),
      paused_ ? "true" : "false",
      finished_ ? "true" : "false",
      iteration_,
      num(stats_.hpwl_um),
      num(stats_.overflow),
      stats_.has_timing ? "true" : "false",
      timing_est_ ? "true" : "false",
      period_ps_ > 0 ? num(period_ps_) : "null",
      stats_.has_timing ? num(stats_.wns_ps) : "null",
      stats_.has_timing ? num(stats_.tns_ps) : "null",
      stats_.timing_iteration,
      cfg_.poll,
      cfg_.min_iters,
      esc(wanted_map_),
      map_version_,
      esc(map_note_),
      regions_wanted_ ? "true" : "false",
      regions_version_,
      esc(note_),
      epoch_,
      esc(error_),
      timeline_.size(),
      tl_version_,
      hl_version_,
      sel_version_,
      [&] {
        std::string cps;
        for (size_t i = 0; i < checkpoints_.size(); ++i) {
          const auto& c = checkpoints_[i];
          cps += std::format(R"({}{{"label":"{}","nth":{},"seq":{},"phase":"{}","iter":{},"snap":{}}})", i ? "," : "", esc(c.label), c.nth,
                             c.seq, esc(c.phase), c.iteration, c.frame ? "true" : "false");
        }
        return cps;
      }());

  for (size_t i = 0; i < stages_.size(); ++i) {
    out += std::format(R"({}{{"name":"{}","on":{},"available":{}}})",
                       i ? "," : "",
                       esc(stages_[i].name),
                       stages_[i].on ? "true" : "false",
                       stages_[i].available ? "true" : "false");
  }
  out += std::format(R"(],"policy":{{"no_resynthesis":{},"no_sizing":{},"no_buffer_edits":{}}})",
                     cfg_.policy.no_resynthesis,
                     cfg_.policy.no_sizing,
                     cfg_.policy.no_buffer_edits);
  out += std::format(R"(,"work":{},"replay_work":{},"timing_budget_exhausted":{})",
                     work_.json(), replay_work_.json(), timing_budget_exhausted_);
  const auto operation = [&](const char* name, bool configured, bool deferred) {
    return std::format(R"({{"operation":"{}","configured":{},"deferred":{},"trigger":"{}"}})", name, configured, deferred,
                       cfg_.scoped_repair_worker ? "requested_scope_at_gate" : "after_legal_placement");
  };
  const auto seconds = [](double value) { return value < 0 ? std::string("null") : num(value); };
  out += std::format(R"(,"slow_work":{{"timing":"{}","last_timing_s":{},"last_rc_s":{},"last_sta_s":{},"remaining_repair_trials":{},"operations":[{},{},{}]}})",
                     timing_operation_ != "idle" ? timing_operation_ : timing_wanted_ ? "timing_queued" : "idle",
                     seconds(last_timing_s_), seconds(last_rc_s_), seconds(last_sta_s_),
                     cfg_.repair_limit ? std::to_string(cfg_.repair_limit - std::min(cfg_.repair_limit, cfg_.repair_trials)) : "null",
                     operation("sizing", cfg_.terminal_sizing, cfg_.policy.no_sizing),
                     operation("buffering", cfg_.terminal_buffering, cfg_.policy.no_buffer_edits),
                     operation("resynthesis", cfg_.terminal_resynthesis, cfg_.policy.no_resynthesis));
  out += std::format(R"(,"repair_trials":{},"scoped_resize_changes":[)", cfg_.repair_trials);
  for (size_t i = 0; i < scoped_resize_changes_.size(); ++i) {
    out += std::format(R"({}"{}")", i ? "," : "", esc(scoped_resize_changes_[i]));
  }
  out += "]";
  out += std::format(R"(,"baseline_identity":"{}","input_identity":"{}","source_identity":"{}")",
                     baseline_identity_locked(),
                     cfg_.input_identity,
                     cfg_.source_identity);
  const auto gp_reason  = native_gp_reason_locked();
  out                  += std::format(R"(,"native_gp_snapshot":{},"native_gp_reason":"{}")", gp_reason.empty(), esc(gp_reason));
  out += R"(,"optimizer_identity":)" + (frame_ && frame_->stats.optimizer_identity
                                            ? std::format(R"("{:016x}")", frame_->stats.optimizer_identity)
                                            : std::string{"null"});
  out += std::format(R"(,"intent_revision":{},"intent_status":"{}","intent_available":{})",
                     intent_revision_,
                     esc(intent_status_),
                     bool(intent_provider_) && state_name_ == "gate");
  out += R"(,"summary":)" + summary_locked() + R"(,"exploration_limits":)" + exploration_limits_locked();
  out += std::format(R"(,"worker_selection":{{"configured":"{}","snapshot_available":{},"snapshot_ready":{},"setup_s":{},"reason":"{}"}})",
                     esc(cfg_.explore_worker),
                     cfg_.snapshot_available,
                     bool(snapshot_owner_) && snapshot_baseline_ == baseline_identity_locked(),
                     snapshot_attempt_ == baseline_identity_locked() ? snapshot_setup_s_ : 0,
                     esc(snapshot_reason_locked()));
  out += std::format(R"(,"topology":"{:016x}","masters":"{:016x}")",
                     scene_ ? scene_->topology_hash : 0,
                     scene_ ? scene_->masters_hash : 0);
  out += std::format(R"(,"relaxation_iterations":{})", relaxation_iterations_);
  out += std::format(R"(,"legalize_available":{},"legalization":{{"status":"{}","error":"{}","moved":[)",
                     legalize_available_,
                     esc(legalization_status_),
                     esc(legalization_error_));
  for (size_t k = 0; k < legalized_moves_.size(); ++k) {
    const auto& m  = legalized_moves_[k];
    out           += std::format("{}[{},{},{}]", k ? "," : "", m.node, m.x, m.y);
  }
  out += "]}";
  out += std::format(R"(,"evaluation_seq":{},"map_frame":{})", evaluation_seq_, map_frame_);
  out += std::format(
      R"(,"target":"{}","placement_identity":"{}","foreground":"{}","evaluation_frame":{},"trace_seq":{},"controller_pid":{},"forks":[)",
      esc(cfg_.target),
      placement_identity_locked(),
      esc(foreground_id_),
      evaluated_frame_,
      seq_,
      ::getpid());
  for (size_t i = 0; i < forks_.size(); ++i) {
    out += std::format(R"({}{{"url":"{}","top":"{}","dir":"{}","cells":{},"inputs":{},"outputs":{}}})",
                       i ? "," : "",
                       esc(forks_[i].url),
                       esc(forks_[i].top),
                       esc(forks_[i].dir),
                       forks_[i].cells,
                       forks_[i].inputs,
                       forks_[i].outputs);
  }
  out += R"(],"pins":[)";
  bool first = true;
  if (scene_) {
    for (const auto& [name, pin] : pins_) {
      const auto f = scene_->index.find(name);
      if (f != scene_->index.end() && !suspended_pins_.contains(name)) {
        out += std::format("{}{}", first ? "" : ",", f->second);
        first = false;
      }
    }
  }
  out   += std::format(R"(],"geometry_status":"{}","geometry_conflicts":{{)", geometry_conflicts_.empty() ? "clear" : "illegal");
  first  = true;
  for (const auto& [name, causes] : geometry_conflicts_) {
    out   += std::format(R"({}"{}":[)", first ? "" : ",", esc(name));
    first  = false;
    for (size_t i = 0; i < causes.size(); ++i) {
      out += std::format(R"({}"{}")", i ? "," : "", esc(causes[i]));
    }
    out += "]";
  }
  out   += R"(},"pin_intent":[)";
  first  = true;
  for (const auto& [name, pin] : pins_) {
    out   += std::format(R"({}{{"name":"{}","x":{},"y":{},"suspended":{}}})",
                       first ? "" : ",",
                       esc(name),
                       pin.x,
                       pin.y,
                       suspended_pins_.contains(name));
    first  = false;
  }
  out += std::format(R"(],"pending":{},"completed_command":{}}})", dirty_pins_.size() + released_.size(), completed_sequence_);
  return out;
}

namespace {
// After the node positions: u32 m, then m float32 x, m y, m radius (the centroid groups of the scene).
void append_centroids(std::string& out, const Frame* f) {
  const std::uint32_t m = f ? std::uint32_t(f->cx.size()) : 0;
  out.append(reinterpret_cast<const char*>(&m), 4);
  if (m) {
    out.append(reinterpret_cast<const char*>(f->cx.data()), size_t(m) * 4);
    out.append(reinterpret_cast<const char*>(f->cy.data()), size_t(m) * 4);
    out.append(reinterpret_cast<const char*>(f->cr.data()), size_t(m) * 4);
  }
}
}  // namespace

std::string Session::frame_binary(std::uint64_t* id, bool q16, std::uint64_t since, int cp, int tl) const {
  std::shared_ptr<const Frame> f, base;
  std::shared_ptr<const Scene> sc;
  {
    std::lock_guard lk(m_);
    if (tl >= 0 && q16 && size_t(tl) < timeline_.size()) {  // a recorded snapshot as it is stored: 16 bits, no expansion
      const auto&         e   = timeline_[size_t(tl)];
      const std::uint32_t n   = std::uint32_t(e.q->size() / 2);
      const auto&         die = e.scene->die;
      const float         box[4] = {float(die.xl), float(die.yl), float(die.xh), float(die.yh)};
      std::string         out(32 + size_t(n) * 4, '\0');
      std::memcpy(out.data(), "LHQ1", 4);
      std::memcpy(out.data() + 4, &n, 4);
      const std::uint64_t fid = std::uint64_t(tl) + 1;
      std::memcpy(out.data() + 8, &fid, 8);
      std::memcpy(out.data() + 16, box, 16);
      std::memcpy(out.data() + 32, e.q->data(), size_t(n) * 4);
      const std::uint32_t m = e.cent ? std::uint32_t(e.cent->size() / 3) : 0;
      out.append(reinterpret_cast<const char*>(&m), 4);
      if (m) {
        out.append(reinterpret_cast<const char*>(e.cent->data()), size_t(m) * 12);
      }
      if (id) {
        *id = fid;
      }
      return out;
    }
    if (tl >= 0 && size_t(tl) < timeline_.size()) {  // a recorded 16-bit snapshot, expanded to a normal frame
      const auto& e   = timeline_[size_t(tl)];
      auto        fr  = std::make_shared<Frame>();
      const size_t n  = e.q->size() / 2;
      const auto& die = e.scene->die;
      fr->id          = size_t(tl) + 1;
      fr->x.resize(n);
      fr->y.resize(n);
      for (size_t i = 0; i < n; ++i) {
        fr->x[i] = float(die.xl + double((*e.q)[i]) * (die.xh - die.xl) / 65535.0);
        fr->y[i] = float(die.yl + double((*e.q)[n + i]) * (die.yh - die.yl) / 65535.0);
      }
      if (e.cent) {
        const size_t m = e.cent->size() / 3;
        fr->cx.assign(e.cent->begin(), e.cent->begin() + long(m));
        fr->cy.assign(e.cent->begin() + long(m), e.cent->begin() + long(2 * m));
        fr->cr.assign(e.cent->begin() + long(2 * m), e.cent->end());
      }
      f     = fr;
      sc    = e.scene;
      since = 0;
    } else if (cp >= 0) {
      if (size_t(cp) < checkpoints_.size()) {
        f  = checkpoints_[size_t(cp)].frame;
        sc = checkpoints_[size_t(cp)].scene;
      }
      since = 0;
    } else {
      f  = frame_;
      sc = scene_;
    }
    if (since) {
      for (const auto& h : history_) {
        if (h->id == since) {
          base = h;
        }
      }
    }
  }
  // Delta: only the nodes whose position changed since frame `since` ("LHD1"), when that is cheaper than all of them.
  if (base && f && base->x.size() == f->x.size() && !f->x.empty()) {
    std::vector<std::uint32_t> changed;
    for (std::uint32_t i = 0; i < f->x.size(); ++i) {
      if (f->x[i] != base->x[i] || f->y[i] != base->y[i]) {
        changed.push_back(i);
      }
    }
    if (changed.size() * 12 < f->x.size() * 8) {
      const std::uint32_t n = std::uint32_t(f->x.size()), m = std::uint32_t(changed.size());
      std::string         out(32 + size_t(m) * 12, '\0');
      std::memcpy(out.data(), "LHD1", 4);
      std::memcpy(out.data() + 4, &n, 4);
      std::memcpy(out.data() + 8, &f->id, 8);
      std::memcpy(out.data() + 16, &base->id, 8);
      std::memcpy(out.data() + 24, &m, 4);
      auto* idx = reinterpret_cast<std::uint32_t*>(out.data() + 32);
      auto* px  = reinterpret_cast<float*>(out.data() + 32 + size_t(m) * 4);
      auto* py  = px + m;
      for (std::uint32_t k = 0; k < m; ++k) {
        idx[k] = changed[k];
        px[k]  = f->x[changed[k]];
        py[k]  = f->y[changed[k]];
      }
      append_centroids(out, f.get());
      if (id) {
        *id = f->id;
      }
      return out;
    }
  }
  if (q16 && f && sc) {
    const std::uint32_t n   = std::uint32_t(f->x.size());
    const float         box[4] = {float(sc->die.xl), float(sc->die.yl), float(sc->die.xh), float(sc->die.yh)};
    std::string         out(32 + size_t(n) * 4, '\0');
    std::memcpy(out.data(), "LHQ1", 4);
    std::memcpy(out.data() + 4, &n, 4);
    std::memcpy(out.data() + 8, &f->id, 8);
    std::memcpy(out.data() + 16, box, 16);
    const double sx = 65535.0 / std::max(1e-9, double(box[2]) - box[0]), sy = 65535.0 / std::max(1e-9, double(box[3]) - box[1]);
    auto* px = reinterpret_cast<std::uint16_t*>(out.data() + 32);
    auto* py = px + n;
    for (std::uint32_t i = 0; i < n; ++i) {
      px[i] = std::uint16_t(std::clamp(std::lround((f->x[i] - box[0]) * sx), 0L, 65535L));
      py[i] = std::uint16_t(std::clamp(std::lround((f->y[i] - box[1]) * sy), 0L, 65535L));
    }
    append_centroids(out, f.get());
    if (id) {
      *id = f->id;
    }
    return out;
  }
  std::string out;
  const std::uint32_t n = f ? std::uint32_t(f->x.size()) : 0;
  out.resize(16 + size_t(n) * 8);
  std::memcpy(out.data(), "LHF1", 4);
  std::memcpy(out.data() + 4, &n, 4);
  const std::uint64_t fid = f ? f->id : 0;
  std::memcpy(out.data() + 8, &fid, 8);
  if (n) {
    auto* px = reinterpret_cast<float*>(out.data() + 16);
    std::copy(f->x.begin(), f->x.end(), px);
    std::copy(f->y.begin(), f->y.end(), px + n);
  }
  append_centroids(out, f.get());
  if (id) {
    *id = fid;
  }
  return out;
}

std::string Session::parse_expectation(const rapidjson::Value& d, Command& c) {
  if (d.HasMember("expect")) {
    const auto& e = d["expect"];
    if (!e.IsObject() || !e.HasMember("frame") || !e["frame"].IsUint64()) {
      return "expect requires a frame revision";
    }
    for (const auto& field : e.GetObject()) {
      const std::string_view name(field.name.GetString(), field.name.GetStringLength());
      if (name != "frame" && name != "target" && name != "baseline" && name != "intent" && name != "evaluation") {
        return "unknown expected revision: " + std::string(name);
      }
    }
    if (e.HasMember("target")) {
      if (!e["target"].IsString()) {
        return "invalid expected target";
      }
      c.expected_target = e["target"].GetString();
    }
    c.expected_frame = e["frame"].GetUint64();
    if (e.HasMember("baseline")) {
      if (!e["baseline"].IsString()) {
        return "invalid baseline identity";
      }
      c.expected_baseline = e["baseline"].GetString();
    }
    if (e.HasMember("intent")) {
      if (!e["intent"].IsUint64()) {
        return "invalid intent revision";
      }
      c.expected_intent = e["intent"].GetUint64();
    }
    if (e.HasMember("evaluation")) {
      if (!e["evaluation"].IsUint64()) {
        return "invalid evaluation sequence";
      }
      c.expected_evaluation = e["evaluation"].GetUint64();
    }
  }
  return {};
}
std::string Session::check_expectation_locked(const Command& c) const {
  if (c.expected_target && *c.expected_target != cfg_.target) {
    return "stale placement target";
  }
  if (c.expected_frame && (!frame_ || frame_->id != *c.expected_frame)) {
    return "stale placement revision";
  }
  if (c.expected_baseline && *c.expected_baseline != baseline_identity_locked()) {
    return "stale baseline identity";
  }
  if (c.expected_intent && *c.expected_intent != intent_revision_) {
    return "stale placement intent";
  }
  if (c.expected_evaluation && *c.expected_evaluation != evaluation_seq_) {
    return "stale timing evaluation";
  }
  return {};
}

std::string Session::parse_command(const std::string& json, Command& c) const {
  rapidjson::Document d;
  d.Parse<rapidjson::kParseFullPrecisionFlag>(json.c_str());
  if (d.HasParseError() || !d.IsObject() || !d.HasMember("cmd") || !d["cmd"].IsString()) {
    return "bad command json";
  }
  c.cmd = d["cmd"].GetString();
  if (d.HasMember("target") && (!d["target"].IsString() || d["target"].GetString() != cfg_.target)) {
    return "unknown placement target";
  }
  if (auto why = parse_expectation(d, c); !why.empty()) {
    return why;
  }
  static const std::set<std::string> known{
      "intent_apply", "intent_preview", "batch",      "relax",    "place_positions",  "legalize",   "policy",   "pin",
      "evaluate",     "restart",        "map",        "centroid", "centroid_release", "run",        "continue", "pause",
      "stage",        "move",           "macro_move", "unpin",    "finish",           "screenshot", "timing",   "area",
      "fork",         "regions",        "run_until", "repair_scope"};
  if (c.cmd == "run_until") {
    if (cfg_.target == "main" || !d.HasMember("gate") || !d["gate"].IsString()) {
      return "run_until requires a scratch evaluator and a gate";
    }
    c.name = d["gate"].GetString();
  }
  if (!known.contains(c.cmd)) {
    return "unknown command: " + c.cmd;
  }
  if (c.cmd == "intent_apply" || c.cmd == "intent_preview") {
    if (d.HasMember("records_json")) {
      if (d.HasMember("records") || !d["records_json"].IsString()) {
        return "supply either records or a JSON array as records_json text";
      }
      // intent_unlocked validates this array with the same Bundle reader as positional JSON inputs.
      c.file.assign(d["records_json"].GetString(), d["records_json"].GetStringLength());
      return {};
    }
    if (!d.HasMember("records") || !d["records"].IsArray()) {
      return "intent requires a complete array of authored records";
    }
    rapidjson::StringBuffer                    buf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
    d["records"].Accept(writer);
    c.file = buf.GetString();
    return {};
  }
  if (c.cmd == "batch") {
    if (!d.HasMember("moves") || !d["moves"].IsArray() || d["moves"].Empty() || d["moves"].Size() > 4096) {
      return "batch needs 1..4096 moves";
    }
    for (const auto& entry : d["moves"].GetArray()) {
      if (!entry.IsObject() || !entry.HasMember("cmd") || !entry["cmd"].IsString()
          || std::string_view(entry["cmd"].GetString()) != "move" || entry.HasMember("expect")) {
        return "batch entries must be moves with one common expected frame";
      }
      rapidjson::StringBuffer                    text;
      rapidjson::Writer<rapidjson::StringBuffer> writer(text);
      entry.Accept(writer);
      Command move;
      if (auto why = parse_command(text.GetString(), move); !why.empty()) {
        return why;
      }
      c.batch.push_back(std::move(move));
    }
    return {};
  }
  const auto strings = [&](const char* key, std::vector<std::string>& out) {
    if (!d.HasMember(key) || !d[key].IsArray()) {
      return false;
    }
    for (const auto& v : d[key].GetArray()) {
      if (!v.IsString()) {
        return false;
      }
      out.emplace_back(v.GetString());
    }
    return true;
  };
  const auto numbers = [&](const char* key, std::vector<double>& out) {
    if (!d.HasMember(key) || !d[key].IsArray()) {
      return false;
    }
    for (const auto& v : d[key].GetArray()) {
      if (!v.IsNumber() || !std::isfinite(v.GetDouble())) {
        return false;
      }
      out.push_back(v.GetDouble());
    }
    return true;
  };
  if (c.cmd == "repair_scope") {
    if (!d.HasMember("trials") || !d["trials"].IsUint() || d["trials"].GetUint() < 1 || d["trials"].GetUint() > 64
        || !d.HasMember("scope") || !d["scope"].IsObject() || !d["scope"].HasMember("nodes")) {
      return "a scoped repair needs bounded trials and exact membership";
    }
    c.count = int(d["trials"].GetUint());
  }
  if (c.cmd == "relax") {
    if (!d.HasMember("iterations") || !d["iterations"].IsInt() || d["iterations"].GetInt() < 1
        || d["iterations"].GetInt() > 10000) {
      return "relax requires iterations in [1,10000]";
    }
    c.iterations = d["iterations"].GetInt();
  }
  if ((c.cmd == "legalize" || c.cmd == "relax" || c.cmd == "repair_scope") && d.HasMember("scope")) {
    if (!d["scope"].IsObject()) {
      return "scope requires nodes or a DBU box";
    }
    const auto& scope = d["scope"];
    if (scope.HasMember("kernel")) {
      if (!scope["kernel"].IsString()) {
        return "invalid movement kernel";
      }
      const std::string_view kernel = scope["kernel"].GetString();
      if (kernel != "local" && kernel != "full_reference") {
        return "unknown movement kernel";
      }
      c.full_reference = kernel == "full_reference";
    }
    if (scope.HasMember("nodes")) {
      if (!scope["nodes"].IsArray()) {
        return "scope nodes must be an array";
      }
      for (const auto& name : scope["nodes"].GetArray()) {
        if (!name.IsString()) {
          return "invalid scope member";
        }
        c.scope_nodes.emplace_back(name.GetString());
      }
    }
    if (scope.HasMember("box")) {
      if (!scope["box"].IsArray() || scope["box"].Size() != 4) {
        return "scope box needs four DBU coordinates";
      }
      c.bound_only    = scope.HasMember("nodes");
      const auto& box = scope["box"];
      for (const auto& v : box.GetArray()) {
        if (!v.IsNumber() || !std::isfinite(v.GetDouble())) {
          return "invalid scope box";
        }
      }
      c.box = {box[0].GetDouble(), box[1].GetDouble(), box[2].GetDouble(), box[3].GetDouble()};
      if (c.box.xl >= c.box.xh || c.box.yl >= c.box.yh) {
        return "empty scope box";
      }
      c.has_box = true;
    } else if (!scope.HasMember("nodes")) {
      return "scope requires nodes or a DBU box";
    }
    c.scoped = true;
  }
  if (c.cmd == "policy") {
    if (d.HasMember("fast")) {
      if (!d["fast"].IsBool()) {
        return "fast must be boolean";
      }
      c.no_sizing = c.no_resynthesis = c.no_buffer_edits = d["fast"].GetBool();
    }
    for (auto [key, to] : {
             std::pair{ "no_resynthesis",  &c.no_resynthesis},
             {      "no_sizing",       &c.no_sizing},
             {"no_buffer_edits", &c.no_buffer_edits}
    }) {
      if (d.HasMember(key)) {
        if (!d[key].IsBool()) {
          return std::string(key) + " must be boolean";
        }
        *to = d[key].GetBool();
      }
    }
    if (!c.no_sizing && !c.no_resynthesis && !c.no_buffer_edits) {
      return "policy needs a mutation gate or fast preset";
    }
  }
  if (c.cmd == "move" || c.cmd == "place_positions" || c.cmd == "macro_move") {
    if (!strings("nodes", c.nodes) || c.nodes.empty()) {
      return "move needs nodes[]";
    }
    if (d.HasMember("orientation")) {
      if (c.cmd != "move") {
        return "orientation requires a movable macro";
      }
      if (d["orientation"].IsString()) {
        c.orientations.assign(c.nodes.size(), d["orientation"].GetString());
      } else if (!strings("orientation", c.orientations) || c.orientations.size() != c.nodes.size()) {
        return "provide one orientation per node";
      }
    }
    if (d.HasMember("edges")) {
      if (!d.HasMember("align") || !d["edges"].IsString()) {
        return "edges requires a named alignment and body or halo";
      }
      const std::string_view edges = d["edges"].GetString();
      if (edges != "body" && edges != "halo") {
        return "unknown alignment edges";
      }
      c.halo_edges = edges == "halo";
    }
    if (!c.orientations.empty() && !d.HasMember("align") && !d.HasMember("x") && !d.HasMember("y") && !d.HasMember("dx")
        && !d.HasMember("dy")) {
      c.relative = true;
      c.x.resize(c.nodes.size());
      c.y.resize(c.nodes.size());
    } else if (d.HasMember("align") && c.cmd == "move") {
      if (!d["align"].IsString() || !d.HasMember("reference") || !d["reference"].IsString()) {
        return "alignment needs align and reference";
      }
      c.align     = d["align"].GetString();
      c.reference = d["reference"].GetString();
      if (d.HasMember("gap")) {
        if (!d["gap"].IsNumber() || !std::isfinite(d["gap"].GetDouble())) {
          return "gap must be finite DBU";
        }
        c.gap = d["gap"].GetDouble();
      }
      if (d.HasMember("x") || d.HasMember("y") || d.HasMember("dx") || d.HasMember("dy")) {
        return "ambiguous move coordinates";
      }
      static const std::set<std::string>
          modes{"left", "right", "top", "bottom", "left_of", "right_of", "above", "below", "center_x", "center_y"};
      if (!modes.contains(c.align)) {
        return "unknown alignment";
      }
    } else {
      c.relative = c.cmd == "move" && (d.HasMember("dx") || d.HasMember("dy"));
      if (c.relative && (d.HasMember("x") || d.HasMember("y"))) {
        return "ambiguous move coordinates";
      }
      if (!numbers(c.relative ? "dx" : "x", c.x) || !numbers(c.relative ? "dy" : "y", c.y) || c.x.size() != c.nodes.size()
          || c.y.size() != c.nodes.size()) {
        return "move needs nodes[], x[], y[] or dx[], dy[] of equal length";
      }
    }
    if (d.HasMember("final") && d["final"].IsBool()) {
      c.final_ = d["final"].GetBool();
    }
  } else if (c.cmd == "unpin" || c.cmd == "pin") {
    if (!strings("nodes", c.nodes)) {
      return "unpin needs nodes[]";
    }
  } else if (c.cmd == "centroid") {
    if (!d.HasMember("name") || !d["name"].IsString() || !d.HasMember("x") || !d["x"].IsNumber() || !d.HasMember("y")
        || !d["y"].IsNumber()) {
      return "centroid needs name, x, y";
    }
    c.name = d["name"].GetString();
    c.x    = {d["x"].GetDouble()};
    c.y    = {d["y"].GetDouble()};
    if (d.HasMember("final") && d["final"].IsBool()) {
      c.final_ = d["final"].GetBool();
    }
  } else if (c.cmd == "regions") {
    c.on = !d.HasMember("on") || !d["on"].IsBool() || d["on"].GetBool();
  } else if (c.cmd == "fork") {
    std::vector<double> box;
    if (numbers("box", box)) {
      if (box.size() != 4 || box[2] <= box[0] || box[3] <= box[1]) {
        return "fork box needs [xl, yl, xh, yh] um with xh > xl and yh > yl";
      }
      c.has_box = true;
      c.box     = {box[0], box[1], box[2], box[3]};
    } else if (!strings("nodes", c.nodes) || c.nodes.empty()) {
      return "fork needs box [xl, yl, xh, yh] (um) or nodes[] (the cells to take)";
    }
  } else if (c.cmd == "area") {
    std::vector<double> box;
    if (numbers("box", box)) {
      if (box.size() != 4 || box[2] <= box[0] || box[3] <= box[1]) {
        return "area box needs [xl, yl, xh, yh] um with xh > xl and yh > yl";
      }
      c.has_box = true;
      c.box     = {box[0], box[1], box[2], box[3]};
    } else {
      c.edge = d.HasMember("edge") && d["edge"].IsString() ? d["edge"].GetString() : "";
      if (c.edge != "left" && c.edge != "right" && c.edge != "bottom" && c.edge != "top") {
        return "area needs edge (left, right, bottom or top) and delta (um, positive grows), or box";
      }
      if (!d.HasMember("delta") || !d["delta"].IsNumber() || !std::isfinite(d["delta"].GetDouble()) || d["delta"].GetDouble() == 0) {
        return "area needs a nonzero delta in um (positive grows the area, negative shrinks it)";
      }
      c.delta = d["delta"].GetDouble();
    }
  } else if (c.cmd == "restart") {
    c.cp = d.HasMember("checkpoint") && d["checkpoint"].IsInt() ? d["checkpoint"].GetInt() : -1;
    if (d.HasMember("set") && d["set"].IsObject()) {
      for (const auto& m : d["set"].GetObject()) {
        if (!m.value.IsString()) {
          return "restart set values must be strings";
        }
        c.set[m.name.GetString()] = m.value.GetString();
      }
    }
  } else if (c.cmd == "centroid_release") {
    c.name = d.HasMember("name") && d["name"].IsString() ? d["name"].GetString() : "";
  } else if (c.cmd == "map") {
    c.name = d.HasMember("kind") && d["kind"].IsString() ? d["kind"].GetString() : "none";
    if (c.name != "none" && c.name != "rudy" && c.name != "rsmt" && c.name != "all") {
      return "map kind must be none, rudy, rsmt or all";
    }
  } else if (c.cmd == "stage") {
    if (!d.HasMember("name") || !d["name"].IsString() || !d.HasMember("on") || !d["on"].IsBool()) {
      return "stage needs name and on";
    }
    c.name = d["name"].GetString();
    c.on   = d["on"].GetBool();
  } else if (c.cmd == "screenshot") {
    if (d.HasMember("file") && d["file"].IsString()) {
      c.file = d["file"].GetString();
    }
    if (d.HasMember("layers") && d["layers"].IsString()) {
      c.layers = d["layers"].GetString();
    }
    if (d.HasMember("w") && d["w"].IsInt()) {
      c.w = d["w"].GetInt();
    }
    if (d.HasMember("h") && d["h"].IsInt()) {
      c.h = d["h"].GetInt();
    }
    if (d.HasMember("view") && d["view"].IsArray() && d["view"].Size() == 4) {
      c.has_view = true;
      c.view     = {d["view"][0].GetDouble(), d["view"][1].GetDouble(), d["view"][2].GetDouble(), d["view"][3].GetDouble()};
    }
  }
  return "";
}

// Bind browser indices to stable names before a command can cross a queue or phase boundary.
std::string Session::bind_nodes_locked(Command& c) const {
  for (auto& name : c.nodes) {
    if (name.starts_with('#')) {
      const auto i = scene_ ? scene_->node_index(name) : -1;
      if (i < 0) {
        return "unknown node " + name;
      }
      name = scene_->nodes[i].name;
    }
  }
  if (c.reference.starts_with('#')) {
    const auto i = scene_ ? scene_->node_index(c.reference) : -1;
    if (i < 0) {
      return "unknown reference " + c.reference;
    }
    c.reference = scene_->nodes[i].name;
  }
  for (auto& move : c.batch) {
    if (auto why = bind_nodes_locked(move); !why.empty()) {
      return why;
    }
  }
  return {};
}

// Resolve a relationship once against one immutable frame. The trace carries
// the resolved DBU coordinates so batch replay does not depend on GUI polling.
std::string Session::resolve_move_locked(Command& c, const Frame* basis, bool captured_orientation) const {
  if (c.cmd == "batch") {
    if (!frame_ || !scene_) {
      return "no placement available";
    }
    Frame proposed = *frame_;
    for (auto& move : c.batch) {
      if (auto why = resolve_move_locked(move, &proposed, captured_orientation); !why.empty()) {
        return why;
      }
      for (size_t k = 0; k < move.nodes.size(); ++k) {
        const auto i = scene_->index.at(move.nodes[k]);
        if (!scene_->nodes[i].movable) {
          return "batch requires movable objects: " + move.nodes[k];
        }
        proposed.x[i] = move.x[k];
        proposed.y[i] = move.y[k];
      }
    }
    return {};
  }
  if (auto why = bind_nodes_locked(c); !why.empty()) {
    return why;
  }
  if (c.cmd != "move" && c.cmd != "macro_move") {
    return {};
  }
  if (!scene_ || !frame_) {
    return "no placement available";
  }
  const Frame& coordinates = basis ? *basis : *frame_;
  const int ref = c.align.empty() ? -1 : scene_->node_index(c.reference);
  if (!c.align.empty() && ref < 0) {
    return "unknown reference " + c.reference;
  }
  if (c.relative || !c.align.empty()) {
    // Keep the instruction before replacing its arguments with execution coordinates.
    c.relationship = R"({"cmd":"move")" + c.move_fields_json() + (c.final_ ? "}" : R"(,"final":false})");
  }
  if (!c.align.empty()) {
    c.x.resize(c.nodes.size());
    c.y.resize(c.nodes.size());
  }
  for (size_t k = 0; k < c.nodes.size(); ++k) {
    const auto it = scene_->index.find(c.nodes[k]);
    if (it == scene_->index.end()) {
      return "unknown node " + c.nodes[k];
    }
    const size_t i = it->second;
    if (!c.relationship.empty() && !scene_->nodes[i].movable && state_name_ != "done") {
      return "relative move requires a movable node at this gate: " + c.nodes[k];
    }
    if (!c.orientations.empty()) {
      const auto& node     = scene_->nodes[i];
      const bool  proposal = state_name_ == "done";
      if (!proposal && ((!orientation_provider_ && !captured_orientation) || state_name_ != "gate" || (gate_name_ != "fp" && gate_name_ != "macro"))) {
        return "macro orientation requires a floorplan or macro gate";
      }
      if ((!proposal && !node.movable)
          || std::ranges::find(node.allowed_orientations, c.orientations[k]) == node.allowed_orientations.end()) {
        return "orientation unsupported for " + node.name;
      }
    }
    if (c.relative) {
      c.x[k] += coordinates.x.at(i);
      c.y[k] += coordinates.y.at(i);
    }
    if (!c.align.empty()) {
      const size_t j  = ref;
      const auto& node = scene_->nodes[i];
      const auto& reference = scene_->nodes[j];
      const auto has_halo = [&](const Node& n) { return c.halo_edges && (n.kind == Kind::macro || n.kind == Kind::fixed) && !n.exclusion; };
      const double hx = has_halo(node) ? scene_->halo_x : 0, hy = has_halo(node) ? scene_->halo_y : 0;
      const double ref_hx = has_halo(reference) ? scene_->halo_x : 0, ref_hy = has_halo(reference) ? scene_->halo_y : 0;
      // Express the reference in the moved object's body-origin coordinates.
      const auto rx = coordinates.x.at(j) - ref_hx + hx, ry = coordinates.y.at(j) - ref_hy + hy;
      const auto w = node.w + 2 * hx, h = node.h + 2 * hy, rw = reference.w + 2 * ref_hx, rh = reference.h + 2 * ref_hy;
      c.x[k] = coordinates.x.at(i);
      c.y[k] = coordinates.y.at(i);
      if (c.align == "left") {
        c.x[k] = rx;
      } else if (c.align == "right") {
        c.x[k] = rx + rw - w;
      } else if (c.align == "bottom") {
        c.y[k] = ry;
      } else if (c.align == "top") {
        c.y[k] = ry + rh - h;
      } else if (c.align == "left_of") {
        c.x[k] = rx - w - c.gap;
      } else if (c.align == "right_of") {
        c.x[k] = rx + rw + c.gap;
      } else if (c.align == "above") {
        c.y[k] = ry + rh + c.gap;
      } else if (c.align == "below") {
        c.y[k] = ry - h - c.gap;
      } else if (c.align == "center_x") {
        c.x[k] = rx + (rw - w) / 2;
      } else {
        c.y[k] = ry + (rh - h) / 2;
      }
    }
    if (!std::isfinite(c.x[k]) || !std::isfinite(c.y[k]) || std::abs(c.x[k]) > INT32_MAX || std::abs(c.y[k]) > INT32_MAX) {
      return "move exceeds the design coordinate range";
    }
  }
  c.relative = false;
  c.align.clear();
  return {};
}

std::string Session::submit(const std::string& json, uint64_t* sequence) {
  if (controller_) {
    uint64_t queued = 0;
    std::string error;
    wire::unpack(controller_->call("submit", json), error, queued);
    if (sequence) {
      *sequence = queued;
    }
    return error;
  }
  {  // the page's selection and highlight removal are session state, not flow commands
    rapidjson::Document d;
    d.Parse(json.c_str());
    if (!d.HasParseError() && d.IsObject() && d.HasMember("cmd") && d["cmd"].IsString()) {
      const std::string name = d["cmd"].GetString();
      if (name == "select" || name == "clear_highlight") {
        rapidjson::Document reply;
        reply.Parse(agent_call(json).c_str());
        if (reply.IsObject() && reply.HasMember("ok") && reply["ok"].IsBool() && reply["ok"].GetBool()) {
          return {};
        }
        return reply.IsObject() && reply.HasMember("error") && reply["error"].IsString() ? reply["error"].GetString() : "invalid selection response";
      }
    }
  }
  Command c;
  if (auto err = parse_command(json, c); !err.empty()) {
    return err;
  }
  {
    std::lock_guard lk(m_);
    if (c.cmd == "repair_scope" && (!cfg_.scoped_repair_worker || !frame_ || (state_name_ != "gate" && state_name_ != "paused"))) {
      return "scoped repair requires its private evaluator at the submitted gate";
    }
    if (cfg_.target != "main" && (c.cmd == "restart" || c.cmd == "area" || c.cmd == "fork")) {
      return "this command starts a separate flow and requires the main session";
    }
    if (!foreground_id_.empty()) {
      return "discard the foreground exploration before changing main";
    }
    if ((c.cmd == "intent_apply" || c.cmd == "intent_preview") && (!intent_provider_ || state_name_ != "gate")) {
      return "intent edits require a native FP or macro placement gate";
    }
    if ((c.cmd == "legalize" || c.cmd == "relax") && !legalize_available_) {
      return "legalization requires a placement or edit gate";
    }
    if (auto why = check_expectation_locked(c); !why.empty()) {
      return why;
    }
    if (queue_.size() > 4096) {
      return "command queue full";
    }
    if (auto why = bind_nodes_locked(c); !why.empty()) {
      return why;
    }
    if (c.relative || !c.align.empty() || c.cmd == "batch") {
      // Preserve the authored delta for execution in queue order.
      Command checked = c;
      if (auto why = resolve_move_locked(checked); !why.empty()) {
        return why;
      }
    } else if (auto why = resolve_move_locked(c); !why.empty()) {
      return why;
    }
    if (c.cmd == "pin") {
      for (const auto& name : c.nodes) {
        if (!scene_ || !frame_ || !scene_->index.contains(name) || !scene_->nodes[scene_->index.at(name)].movable) {
          return "no movable node " + name;
        }
        if (geometry_conflicts_.contains(name)) {
          return "pin refused on illegal macro " + name;
        }
      }
    }
    c.sequence = ++queued_sequence_;
    if (sequence) {
      *sequence = c.sequence;
    }
    queue_.push_back(std::move(c));
  }
  cv_.notify_all();
  return "";
}

}  // namespace livehd::placement::gui
