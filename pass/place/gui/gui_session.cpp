// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gui_session.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <cstring>
#include <format>
#include <fstream>
#include <optional>
#include <random>
#include <string_view>

#include "gui_render.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::placement::gui {

namespace {
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

void Scene::build_index() {
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

Session::Session(Config c) : cfg_(std::move(c)) {
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
      d.Parse(line.c_str());
      if (d.HasParseError() || !d.IsObject() || !d["cmd"].IsString()) {
        continue;
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
      if (d.HasMember("seq") && d["seq"].IsUint64() && d["seq"].GetUint64() > cfg_.replay_limit_seq) {
        continue;  // after the checkpoint being resumed: not part of this run
      }
      Replay_item item;
      item.phase = d.HasMember("phase") && d["phase"].IsString() ? d["phase"].GetString() : "";
      item.iter  = d.HasMember("iter") && d["iter"].IsInt() ? d["iter"].GetInt() : 0;
      const std::string cmd = d["cmd"].GetString();
      if (cmd == "extend") {
        item.cmd.cmd   = cmd;
        item.cmd.count = d.HasMember("count") && d["count"].IsInt() ? d["count"].GetInt() : 0;
        replay_.push_back(std::move(item));
      } else if (cmd == "move" || cmd == "unpin" || cmd == "centroid" || cmd == "centroid_release" || cmd == "timing") {  // run/pause/stage/finish/screenshot have no effect without a user
        if (parse_command(line, item.cmd).empty()) {
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
void Session::replay_locked(int iteration, Edits& edits) {
  while (!replay_.empty() && replay_.front().phase == stage_name_ && replay_.front().iter <= iteration
         && replay_.front().cmd.cmd != "extend") {
    apply_locked(replay_.front().cmd, edits);
    replay_.pop_front();
  }
}

Session::~Session() = default;  // Server's destructor (gui_server.cpp) stops the thread

std::string Session::url() const { return url_; }

// A restart point: the state at a gate (before a stage), after a GP, or in edit mode. Counting (label, nth) is
// deterministic for the same flags, which is how a resumed run finds the same point again.
void Session::checkpoint_locked(const std::string& label) {
  const bool pure_replay = !cfg_.replay_path.empty() && cfg_.resume_label.empty();
  const int  nth         = ++label_count_[label];
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
    replay_active_ = false;  // the replay reached the checkpoint: from here the user drives
    resumed_       = true;
    paused_        = true;
    note_          = std::format("replayed to checkpoint {} #{}", label, nth);
  }
  bump_locked();
}

void Session::set_options_json(std::string json) {
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

void Session::push_frame_locked(std::shared_ptr<const Frame> f) {
  frame_ = f;
  history_.push_back(std::move(f));
  while (history_.size() > 8) {
    history_.pop_front();
  }
}

void Session::bump_locked() {
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

void Session::set_stages(std::vector<Stage_switch> st) {
  std::lock_guard lk(m_);
  stages_ = std::move(st);
  bump_locked();
}

void Session::set_clock_period_ps(double period_ps) {
  std::lock_guard lk(m_);
  period_ps_ = period_ps;
  bump_locked();
}

void Session::set_timing(double wns_ps, double tns_ps, int iteration) {
  std::lock_guard lk(m_);
  set_timing_locked(wns_ps, tns_ps, iteration);
}

// Explicit requests (the `timing` command, a released edit) always run; otherwise a running poll refreshes while the
// time spent stays under `timing_budget` percent of the wall time since the session began, at most every 5 polls.
void Session::service_timing(std::unique_lock<std::mutex>& lk, const Timing_provider* tp, bool running) {
  if (!tp) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (!t_start_set_) {
    t_start_set_ = true;
    t_start_     = now;
  }
  bool automatic = false;
  if (running && !replaying() && cfg_.timing_budget > 0) {
    ++polls_since_timing_;
    const double elapsed = std::chrono::duration<double>(now - t_start_).count();
    automatic            = polls_since_timing_ >= 5 && timing_spent_s_ <= cfg_.timing_budget / 100.0 * elapsed;
  }
  if (!timing_wanted_ && !automatic && !crit_pending_) {
    return;
  }
  const bool critical  = crit_pending_;
  Timing_query q;
  if (critical) {
    q = crit_request_;
  }
  q.want_critical      = critical;
  timing_wanted_       = false;
  polls_since_timing_  = 0;
  const auto started   = std::chrono::steady_clock::now();
  lk.unlock();
  bool ok = false;
  try {
    ok = (*tp)(q);
  } catch (const std::exception&) {
    ok = false;
  }
  lk.lock();
  timing_spent_s_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  if (ok) {
    set_timing_locked(q.wns_ps, q.tns_ps, iteration_);
    timing_est_ = true;  // a ranking refresh is an estimate: the worst pin slack, no TNS
  }
  if (critical) {
    crit_pending_ = false;
    crit_error_   = ok ? "" : "timing is not available at this point of the flow";
    crit_result_  = std::move(q.critical);
    ++crit_seq_;
    crit_cv_.notify_all();
  }
}

void Session::set_timing_locked(double wns_ps, double tns_ps, int iteration) {
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
      f->x[i] = float(x[i]);
      f->y[i] = float(y[i]);
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
  std::set<int>                pinned;
  {
    std::lock_guard lk(m_);
    scene = scene_;
    frame = frame_;
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
  return render_png(*scene, *frame, pinned, ro);
}

void Session::begin_phase(std::shared_ptr<const Scene> scene, const double* x, const double* y, size_t n, bool mark_pins_dirty) {
  {
    std::lock_guard lk(m_);
    fork_builder_ = nullptr;  // belongs to the previous phase's data; the placement installs this phase's
  }
  std::unique_lock lk(m_);
  scene_      = std::move(scene);
  stage_name_ = scene_->phase;
  ++scene_version_;
  dirty_pins_.clear();
  dirty_cpins_.clear();
  if (mark_pins_dirty) {
    for (auto& [name, pin] : pins_) {
      // After an edit-mode round the legalized position supersedes the raw drop position: later passes start from it.
      if (const auto legal = placement_.find(name); legal != placement_.end()) {
        pin.x = double(legal->second[0]);
        pin.y = double(legal->second[1]);
      }
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

void Session::trace_locked(const Command& c) {
  if (!trace_.is_open()) {
    return;
  }
  if (((c.cmd == "move" || c.cmd == "centroid") && !c.final_) || c.cmd == "map" || c.cmd == "regions") {
    return;  // intermediate drag positions and view toggles are not part of the trace
  }
  std::string line = std::format(R"({{"seq":{},"phase":"{}","iter":{},"cmd":"{}")", ++seq_, esc(stage_name_), iteration_, esc(c.cmd));
  if (c.cmd == "move" || c.cmd == "unpin" || c.cmd == "macro_move") {
    line += R"(,"nodes":[)";
    for (size_t i = 0; i < c.nodes.size(); ++i) {
      line += std::format("{}\"{}\"", i ? "," : "", esc(c.nodes[i]));
    }
    line += "]";
    if (c.cmd == "move" || c.cmd == "macro_move") {
      line += R"(,"x":[)";
      for (size_t i = 0; i < c.x.size(); ++i) {
        line += (i ? "," : "") + num(c.x[i]);
      }
      line += R"(],"y":[)";
      for (size_t i = 0; i < c.y.size(); ++i) {
        line += (i ? "," : "") + num(c.y[i]);
      }
      line += "]";
    }
  } else if (c.cmd == "centroid") {
    line += std::format(R"(,"name":"{}","x":{},"y":{})", esc(c.name), num(c.x.empty() ? 0 : c.x[0]), num(c.y.empty() ? 0 : c.y[0]));
  } else if (c.cmd == "centroid_release") {
    line += std::format(R"(,"name":"{}")", esc(c.name));
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
  trace_ << line;
  trace_.flush();
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
  if (sc.cell_area > 0) {
    const double util = sc.cell_area / (w * h);
    if (util > 0.98) {
      note_ = std::format("area: the new core {:.1f} x {:.1f} um would be {:.0f}% full of cells; it must stay below 98%", w / dbu, h / dbu,
                          100 * util);
      return;
    }
  }
  if (sc.macro_area > 0 && sc.macro_area / (w * h) > Scene::kMacroFillLimit) {
    note_ = std::format("area: the macros and memories need {:.0f}% of the new core {:.1f} x {:.1f} um with their spacing; they cannot be packed above {:.0f}%",
                        100 * sc.macro_area / (w * h), w / dbu, h / dbu, 100 * Scene::kMacroFillLimit);
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
  note_ = std::format("area: core {:.1f} x {:.1f} um{}", w / dbu, h / dbu,
                      sc.cell_area > 0 ? std::format(", {:.0f}% full of cells", 100 * sc.cell_area / (w * h)) : std::string{});
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
  std::lock_guard lk(m_);
  return macro_moves_;
}

void Session::set_fork_builder(Fork_builder b) {
  std::lock_guard lk(m_);
  fork_builder_ = std::move(b);
}

// `fork`: the cells inside a box (or the named ones) become a design of their own, run in a new session. The placer
// writes the region's files from the data of the phase it is in; the host starts the child and gives its page.
void Session::fork_unlocked(const Command& c, std::unique_lock<std::mutex>& lk) {
  if (!fork_builder_ || !cfg_.fork || cfg_.fork_dir.empty()) {
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
  req.dir = std::format("{}/fork_{}", cfg_.fork_dir, forks_.size() + 1);
  std::error_code ec;
  std::filesystem::create_directories(req.dir, ec);
  if (ec) {
    note_ = "fork: cannot create " + req.dir + ": " + ec.message();
    return;
  }
  note_ = std::format("fork: extracting {} cells...", req.cells.size());
  bump_locked();
  const auto builder = fork_builder_;
  const auto launch  = cfg_.fork;
  lk.unlock();
  Fork_files  files;
  Fork_launch started;
  try {
    files = builder(req);
    if (files.error.empty()) {
      started = launch(files, req);
    }
  } catch (const std::exception& e) {
    (files.error.empty() ? started.error : files.error) = e.what();
  }
  lk.lock();
  if (!files.error.empty() || !started.error.empty()) {
    note_ = "fork: " + (files.error.empty() ? started.error : files.error);
    return;
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
    d.Parse(line.c_str());
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
    if (cmd == "move" || cmd == "centroid" || cmd == "macro_move") {
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
  trace_locked(c);
  if (c.cmd == "run" || c.cmd == "continue") {
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
  } else if (c.cmd == "timing") {
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
    wanted_map_ = c.name == "none" ? "" : c.name;
    map_dirty_  = true;
    if (wanted_map_.empty()) {
      map_.reset();
      ++map_version_;
    }
  } else if (c.cmd == "move") {
    auto next = frame_ ? std::make_shared<Frame>(*frame_) : std::make_shared<Frame>();
    for (size_t i = 0; i < c.nodes.size() && i < c.x.size() && i < c.y.size(); ++i) {
      if (scene_) {
        const auto found = scene_->index.find(c.nodes[i]);
        if (found == scene_->index.end() || !scene_->nodes[size_t(found->second)].movable) {
          continue;
        }
        if (size_t(found->second) < next->x.size()) {
          next->x[size_t(found->second)] = float(c.x[i]);
          next->y[size_t(found->second)] = float(c.y[i]);
        }
      }
      pins_[c.nodes[i]] = {-1, c.x[i], c.y[i]};
      dirty_pins_.insert(c.nodes[i]);
      released_.erase(c.nodes[i]);
    }
    next->id = ++frame_id_;
    push_frame_locked(std::move(next));
    if (c.final_) {
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
        next->x[size_t(found->second)] = float(c.x[i]);
        next->y[size_t(found->second)] = float(c.y[i]);
      }
      any = true;
    }
    if (any) {
      next->id = ++frame_id_;
      push_frame_locked(std::move(next));
      macro_restart_locked();
    }
  } else if (c.cmd == "unpin") {
    for (const auto& name : c.nodes) {
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
      const auto png = render_png(*scene_, *frame_, pinned, ro);
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
    if (c.cmd == "fork") {  // seconds of work for the placer and a child process: the page keeps being served meanwhile
      fork_unlocked(c, lk);
      bump_locked();
      continue;
    }
    apply_locked(c, edits);
  }
}

// Congestion maps are the only thing the flow thread computes for the page; the provider reads the solver's own
// arrays, so it runs here, outside the lock.
void Session::service_map(std::unique_lock<std::mutex>& lk, const Map_provider* maps, bool every) {
  if (!maps) {
    return;
  }
  const auto compute = [&](const std::string& kind) {
    lk.unlock();
    Grid grid;
    try {
      grid = (*maps)(kind);
    } catch (const std::exception& e) {
      grid      = {};
      grid.note = e.what();
    }
    lk.lock();
    return grid;
  };
  if (regions_wanted_ && (every || regions_dirty_)) {
    regions_dirty_ = false;
    auto grid      = compute("regions");
    regions_       = std::make_shared<const Grid>(std::move(grid));
    ++regions_version_;
    bump_locked();
  }
  if (wanted_map_.empty() || (!every && !map_dirty_)) {
    return;
  }
  const std::string kind = wanted_map_;
  map_dirty_             = false;
  auto grid              = compute(kind);
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

std::string Session::map_binary() const {
  std::shared_ptr<const Grid> g;
  {
    std::lock_guard lk(m_);
    g = map_;
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
  std::unique_lock lk(m_);
  Stats            s = st;
  s.iteration        = iteration;
  Edits discard;
  drain_locked(discard, lk);
  replay_locked(iteration, discard);
  publish_locked(x, y, n, s, paused_ && !finishing_ ? "paused" : "running");
  service_map(lk, maps, true);
  service_timing(lk, timing, true);
  while (paused_ && !finishing_ && !exit_.load()) {
    cv_.wait_for(lk, std::chrono::milliseconds(200), [&] { return !queue_.empty() || exit_.load(); });
    const bool had = !queue_.empty();
    drain_locked(discard, lk);
    service_map(lk, maps, false);
    service_timing(lk, timing, false);
    if (had && paused_) {
      // a move while paused only accumulates; the published frame already shows it
    }
  }
  if (frame_ && !finishing_) {
    state_name_ = "running";
    bump_locked();
  }
  return take_edits_locked();
}

Session::End_result Session::gp_end(const double* x, const double* y, size_t n, const Stats& st, const Map_provider* maps,
                                    const Timing_provider* timing) {
  std::unique_lock lk(m_);
  Edits            discard;
  drain_locked(discard, lk);
  End_result out;
  {
    Stats s = st;
    publish_locked(x, y, n, s, "running");
    checkpoint_locked("gp_end");
  }
  if (replaying()) {  // the recorded decision: extend by the same count at the same phase and iteration
    replay_locked(st.iteration, discard);
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
  publish_locked(x, y, n, st, "gate");
  service_map(lk, maps, true);
  service_timing(lk, timing, false);
  while (!gp_decided_ && !finishing_ && !exit_.load()) {
    cv_.wait_for(lk, std::chrono::milliseconds(200), [&] { return !queue_.empty() || exit_.load(); });
    drain_locked(discard, lk);
    service_map(lk, maps, false);
    service_timing(lk, timing, false);
  }
  at_gp_end_ = false;
  gate_name_.clear();
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
  std::lock_guard                                  lk(m_);
  std::map<std::string, std::pair<double, double>> out;
  for (const auto& [name, p] : pins_) {
    out.emplace(name, std::pair{p.x, p.y});
  }
  return out;
}

void Session::set_placement(std::map<std::string, std::array<std::int64_t, 3>> placement) {
  std::lock_guard lk(m_);
  placement_ = std::move(placement);
}

std::map<std::string, std::array<std::int64_t, 3>> Session::placement_override() const {
  std::lock_guard lk(m_);
  return placement_;
}

void Session::record_edits(const std::vector<std::pair<std::string, std::pair<double, double>>>& edits) {
  std::lock_guard lk(m_);
  edits_log_.insert(edits_log_.end(), edits.begin(), edits.end());
}

std::vector<std::pair<std::string, std::pair<double, double>>> Session::recorded_edits() const {
  std::lock_guard lk(m_);
  return edits_log_;
}

bool Session::edit_left() const {
  std::lock_guard lk(m_);
  return edit_left_;
}

void Session::set_note(const std::string& note) {
  std::lock_guard lk(m_);
  note_ = note;
  bump_locked();
}

Session::Edit_result Session::edit_wait(const double* x, const double* y, size_t n, const Stats& st, const Map_provider* maps,
                                        const Timing_provider* timing) {
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
    replay_locked(0, discard);
    out.edits = take_edits_locked();
    out.again = !out.edits.pins.empty();
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
  publish_locked(x, y, n, st, "gate");
  service_map(lk, maps, true);
  service_timing(lk, timing, false);
  while (!edit_decided_ && !finishing_ && !exit_.load()) {
    cv_.wait_for(lk, std::chrono::milliseconds(200), [&] { return !queue_.empty() || exit_.load(); });
    drain_locked(discard, lk);
    service_map(lk, maps, false);
    service_timing(lk, timing, false);
  }
  at_edit_ = false;
  gate_name_.clear();
  state_name_ = "running";
  if (edit_decided_ && !finishing_) {
    out.edits = take_edits_locked();
    out.again = !out.edits.pins.empty();
  }
  edit_left_ = !out.again;
  note_.clear();
  bump_locked();
  return out;
}

Edits Session::gate(const std::string& stage, const double* x, const double* y, size_t n, const Stats& st, const Map_provider* maps,
                    const Timing_provider* timing) {
  std::unique_lock lk(m_);
  gate_released_ = false;
  Edits discard;
  drain_locked(discard, lk);
  replay_locked(0, discard);
  const auto blocked = [&] { return !finishing_ && (paused_ || (!switch_on_locked(stage) && !gate_released_)); };
  gate_name_         = stage;
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
      next->x[size_t(found->second)] = float(pin->second.x);
      next->y[size_t(found->second)] = float(pin->second.y);
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
    cv_.wait_for(lk, std::chrono::milliseconds(200), [&] { return !queue_.empty() || exit_.load(); });
    drain_locked(discard, lk);
    service_map(lk, maps, false);
    service_timing(lk, timing, false);
  }
  gate_name_.clear();
  state_name_ = "running";
  bump_locked();
  return take_edits_locked();
}

void Session::set_result(const std::vector<std::tuple<std::string, double, double>>& placed) {
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
      next->x[size_t(f->second)] = float(x);
      next->y[size_t(f->second)] = float(y);
    }
  }
  next->id    = ++frame_id_;
  next->state = "result";
  push_frame_locked(std::move(next));
  bump_locked();
}

void Session::fail(const std::string& title, const std::string& detail) {
  {
    std::lock_guard clear(m_);
    fork_builder_ = nullptr;  // the placement data it reads is gone or going
  }
  std::unique_lock lk(m_);
  failed_      = true;
  error_       = detail.empty() ? title : title + "\n" + detail;
  gate_name_.clear();
  replay_active_ = false;
  state_name_  = "failed";
  paused_      = false;
  bump_locked();
  Edits discard;
  while (!finishing_ && !exit_.load()) {
    cv_.wait_for(lk, std::chrono::milliseconds(200), [&] { return !queue_.empty() || exit_.load(); });
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
    cv_.wait_for(lk, std::chrono::milliseconds(200), [&] { return !queue_.empty() || exit_.load(); });
    drain_locked(discard, lk);
  }
}

Edits Session::all_pins() const {
  std::lock_guard lk(m_);
  Edits           out;
  if (scene_) {
    for (const auto& [name, pin] : pins_) {
      const auto f = scene_->index.find(name);
      if (f != scene_->index.end() && scene_->nodes[size_t(f->second)].movable) {  // a macro pinned earlier is fixed now
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
  return scene_;
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
  out += R"(},"regions":[)";
  for (size_t i = 0; i < s.regions.size(); ++i) {
    const auto& r = s.regions[i];
    out += std::format("{}[{},{},{},{}]", i ? "," : "", num(r.xl), num(r.yl), num(r.xh), num(r.yh));
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
      num(stats_.wns_ps),
      num(stats_.tns_ps),
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
  out += R"(],"forks":[)";
  for (size_t i = 0; i < forks_.size(); ++i) {
    out += std::format(R"({}{{"url":"{}","top":"{}","cells":{},"inputs":{},"outputs":{}}})", i ? "," : "", esc(forks_[i].url), esc(forks_[i].top),
                       forks_[i].cells, forks_[i].inputs, forks_[i].outputs);
  }
  out += R"(],"pins":[)";
  bool first = true;
  if (scene_) {
    for (const auto& [name, pin] : pins_) {
      const auto f = scene_->index.find(name);
      if (f != scene_->index.end()) {
        out += std::format("{}{}", first ? "" : ",", f->second);
        first = false;
      }
    }
  }
  out += std::format(R"(],"pending":{}}})", dirty_pins_.size() + released_.size());
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
    std::memcpy(out.data() + 16, f->x.data(), size_t(n) * 4);
    std::memcpy(out.data() + 16 + size_t(n) * 4, f->y.data(), size_t(n) * 4);
  }
  append_centroids(out, f.get());
  if (id) {
    *id = fid;
  }
  return out;
}

std::string Session::parse_command(const std::string& json, Command& c) const {
  rapidjson::Document d;
  d.Parse(json.c_str());
  if (d.HasParseError() || !d.IsObject() || !d.HasMember("cmd") || !d["cmd"].IsString()) {
    return "bad command json";
  }
  c.cmd = d["cmd"].GetString();
  static const std::set<std::string> known{"restart", "map", "centroid", "centroid_release", "run", "continue", "pause", "stage", "move", "macro_move", "unpin", "finish", "screenshot", "timing", "area", "fork", "regions"};
  if (!known.contains(c.cmd)) {
    return "unknown command: " + c.cmd;
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
  if (c.cmd == "move" || c.cmd == "macro_move") {
    if (!strings("nodes", c.nodes) || !numbers("x", c.x) || !numbers("y", c.y) || c.x.size() != c.nodes.size()
        || c.y.size() != c.nodes.size()) {
      return "move needs nodes[], x[], y[] of equal length";
    }
    if (d.HasMember("final") && d["final"].IsBool()) {
      c.final_ = d["final"].GetBool();
    }
  } else if (c.cmd == "unpin") {
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
    if (c.name != "none" && c.name != "rudy" && c.name != "rsmt" && c.name != "uroute") {
      return "map kind must be none, rudy, rsmt or uroute";
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

std::string Session::submit(const std::string& json) {
  {  // the page's selection and highlight removal are session state, not flow commands
    rapidjson::Document d;
    d.Parse(json.c_str());
    if (!d.HasParseError() && d.IsObject() && d.HasMember("cmd") && d["cmd"].IsString()) {
      const std::string name = d["cmd"].GetString();
      if (name == "select" || name == "clear_highlight") {
        const auto reply = agent_call(json);
        return reply.find("\"ok\":true") != std::string::npos ? "" : reply;
      }
    }
  }
  Command c;
  if (auto err = parse_command(json, c); !err.empty()) {
    return err;
  }
  {
    std::lock_guard lk(m_);
    if (queue_.size() > 4096) {
      return "command queue full";
    }
    // The page does not know cell names (the design document carries names for macros and IO only): "#N" is
    // node N of the current scene. The queue, the trace and the pins always use the real name.
    for (auto& name : c.nodes) {
      if (!name.empty() && name[0] == '#') {
        const long i = std::strtol(name.c_str() + 1, nullptr, 10);
        if (!scene_ || i < 0 || size_t(i) >= scene_->nodes.size()) {
          return "unknown node " + name;
        }
        name = scene_->nodes[size_t(i)].name;
      }
    }
    queue_.push_back(std::move(c));
  }
  cv_.notify_all();
  return "";
}

}  // namespace livehd::placement::gui
