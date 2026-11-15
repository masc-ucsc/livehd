// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstring>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>

#include "core/binary_message.hpp"
#include "gui_session.hpp"

namespace livehd::placement {
template <class A>
void field(A& a, Mutation_policy& v) {
  binary_message::fields(a, v.no_resynthesis, v.no_sizing, v.no_buffer_edits);
}
}  // namespace livehd::placement
namespace livehd::placement::gui {
using namespace livehd::binary_message;
template <class A>
void field(A& a, Box& v) {
  fields(a, v.xl, v.yl, v.xh, v.yh);
}
template <class A>
void field(A& a, Macro_pin& v) {
  fields(a, v.name, v.x, v.y, v.logic);
}
template <class A>
void field(A& a, Node& v) {
  fields(a,
         v.name,
         v.kind,
         v.movable,
         v.w,
         v.h,
         v.seq,
         v.master,
         v.klass,
         v.pins,
         v.orient,
         v.exclusion,
         v.external_name,
         v.signal_pins,
         v.allowed_orientations,
         v.group);
}
template <class A>
void field(A& a, Centroid& v) {
  fields(a, v.name, v.x, v.y, v.radius, v.movable);
}
template <class A>
void field(A& a, Scene& v) {
  fields(a,
         v.design,
         v.phase,
         v.die,
         v.frame_die,
         v.halo_x,
         v.halo_y,
         v.channel_x,
         v.channel_y,
         v.row_height,
         v.site_width,
         v.cell_area,
         v.macro_area,
         v.dead_pitch,
         v.dead_narrow_x,
         v.dead_narrow_y,
         v.dead_min_area,
         v.dead_pocket_fraction,
         v.dbu_per_um,
         v.nodes,
         v.regions,
         v.exclusions,
         v.groups,
         v.centroids,
         v.topology_hash,
         v.masters_hash);
  if constexpr (std::is_same_v<A, Decoder>) {
    v.build_index();
  }
}
template <class A>
void field(A& a, Stage_switch& v) {
  fields(a, v.name, v.on, v.available);
}
template <class A>
void field(A& a, Stats& v) {
  fields(a,
         v.iteration,
         v.hpwl_um,
         v.overflow,
         v.wns_ps,
         v.tns_ps,
         v.has_timing,
         v.timing_iteration,
         v.centroids,
         v.gp_steps,
         v.optimizer_identity,
         v.native_gp,
         v.native_gp_reason);
}
template <class A>
void field(A& a, Frame& v) {
  fields(a, v.id, v.phase, v.state, v.iteration, v.stats, v.x, v.y, v.cx, v.cy, v.cr);
}
template <class A>
void field(A& a, Edit_pin& v) {
  fields(a, v.node, v.x, v.y);
}
template <class A>
void field(A& a, Edit_centroid& v) {
  fields(a, v.group, v.x, v.y);
}
template <class A>
void field(A& a, Repair_scope& v) {
  fields(a, v.nodes, v.x, v.y, v.window, v.trials);
}
template <class A>
void field(A& a, Edits& v) {
  fields(a, v.moves, v.pins, v.releases, v.centroids, v.release_centroids, v.repair);
}
template <class A>
void field(A& a, Legalize_request& v) {
  fields(a, v.x, v.y, v.active, v.held, v.scoped, v.iterations, v.bounded_window, v.window, v.full_reference);
}
template <class A>
void field(A& a, Legalize_result& v) {
  fields(a, v.error, v.moved, v.iterations);
}
template <class A>
void field(A& a, Grid& v) {
  fields(a, v.bx, v.by, v.v, v.peak, v.overflow, v.note, v.rects, v.crossings, v.crossing_total);
}
template <class A>
void field(A& a, Timing_trial& v) {
  fields(a, v.moves, v.max_nets, v.touched, v.failing, v.wns_ps, v.tns_ps, v.error);
}
template <class A>
void field(A& a, Timing_query& v) {
  fields(a,
         v.request_checkpoint,
         v.checkpoint,
         v.checkpoint_error,
         v.native_endpoint,
         v.rc_s,
         v.sta_s,
         v.hpwl_um,
         v.hold_wns_ps,
         v.trials,
         v.rollback_verified,
         v.baseline_failing,
         v.wns_ps,
         v.tns_ps,
         v.want_critical,
         v.n,
         v.margin_ps,
         v.critical,
         v.full,
         v.electrical_violations,
         v.path,
         v.endpoints);
}
template <class A>
void field(A& a, Restart_request& v) {
  fields(a, v.label, v.nth, v.seq, v.from_start, v.listen_port, v.token, v.set, v.shift_x, v.shift_y);
}
template <class A>
void field(A& a, Fork_files& v) {
  fields(a, v.top, v.net_dir, v.floorplan, v.seed, v.cells, v.inputs, v.outputs, v.core, v.error);
}
template <class A>
void field(A& a, Fork_launch& v) {
  fields(a, v.url, v.error);
}
template <class A>
void field(A& a, Config& v) {
  fields(a,
         v.intent,
         v.input_identity,
         v.source_identity,
         v.score,
         v.score_estimator,
         v.policy,
         v.port,
         v.poll,
         v.min_iters,
         v.history_every,
         v.history_mb,
         v.timing_budget,
         v.trace_path,
         v.token,
         v.start_paused,
         v.shot_dir,
         v.video_dir,
         v.agent_socket,
         v.replay_path,
         v.resume_label,
         v.resume_nth,
         v.replay_limit_seq,
         v.fork_dir,
         v.hello,
         v.worker_args,
         v.region_args,
         v.validation_args,
         v.invocation_start_s,
         v.target,
         v.explore_timeout,
         v.tier_timeout,
         v.worker_memory_mb,
         v.session_root_pid,
         v.session_memory_bytes,
         v.worker_disk_mb,
         v.history_result_mb,
         v.explore_open,
         v.explore_queue,
         v.explore_jobs,
         v.explore_threads,
         v.heavy_jobs,
         v.snapshot_available,
         v.explore_worker,
         v.repair_available,
         v.scoped_repair_worker,
         v.batch_worker,
         v.timing_call_limit,
         v.resynthesis_available,
         v.terminal_sizing,
         v.terminal_buffering,
         v.terminal_resynthesis,
         v.repair_limit);
}
namespace wire {
using livehd::binary_message::field;
using livehd::placement::field;
using livehd::placement::gui::field;
using namespace livehd::binary_message;
// Encoding is read-only. The common schema takes nonconst references to
// support decoding into the very same field list.
template <class... T>
std::string pack(const T&... v) {
  Encoder a;
  fields(a, const_cast<T&>(v)...);
  return std::move(a.bytes);
}
template <class... T>
void unpack(std::string_view bytes, T&... v) {
  Decoder a{bytes};
  fields(a, v...);
  if (!a.bytes.empty()) {
    throw std::runtime_error("trailing GUI controller data");
  }
}
template <class T>
T read(std::string_view bytes) {
  T v{};
  unpack(bytes, v);
  return v;
}
}  // namespace wire
}  // namespace livehd::placement::gui
