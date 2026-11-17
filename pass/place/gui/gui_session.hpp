// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "gui_policy.hpp"
#include "rapidjson/fwd.h"
// Interactive placement session (`lhd synth --gui`; agent channel: .claude/skills/livehd/agent_socket.md).
//
// The flow thread publishes frames and blocks inside poll()/gate() while the
// user has paused; one server thread (gui_server.cpp) turns HTTP requests into
// commands. Nothing in here knows about DREAMPlace: the placement adapter in
// place.cpp translates between the solver's node indices and the node *names*
// this session uses, so pins and the action trace survive a change of stage.
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace livehd::placement::gui {

std::string default_agent_socket();

enum class Kind : std::uint8_t { cell = 0, macro = 1, fixed = 2, blockage = 3, io = 4 };

struct Macro_pin {
  std::string      name;
  double           x = 0, y = 0;
  std::vector<int> logic;
};  // oriented offsets in DBU
struct Node {
  std::string name;
  Kind        kind    = Kind::cell;
  bool        movable = true;
  double      w = 0, h = 0;  // DBU
  bool        seq = false;   // a register: a cell with a clock pin (named in the design document)
  std::string master, klass; // library cell / macro name and LEF class (CORE, BLOCK, ...), for the agent's questions
  std::string              orient    = "N";
  bool                     exclusion = false;
  std::uint32_t pins = 0;    // connected pin count
  std::vector<Macro_pin>   signal_pins;
  std::vector<std::string> allowed_orientations;
  std::string              group;          // logical placement block of a macro
  std::string              external_name;  // exact component spelling in the imported DEF
};
struct Box {
  double xl = 0, yl = 0, xh = 0, yh = 0;
};
struct Centroid {
  std::string name;
  double      x = 0, y = 0, radius = 0;
  bool        movable = true;
};
// The lower-left origins at which a macro's signal pins land on routing tracks, for one orientation: along each axis
// an origin is allowed when it is phases[axis][i] + k * period[axis]. Empty phases on an axis: not constrained there.
// This is what the flow demands of a FIXED macro; a pose pinned in the session is held exactly through legalization,
// so one that misses it ends the run later with "fixed macro pin misses routing tracks".
struct Macro_tracks {
  std::string          orient;
  std::int64_t         period_x = 1, period_y = 1;
  std::vector<std::int64_t> phases_x, phases_y;
  std::uint32_t        pins = 0;
};
// Everything that is fixed for the duration of one placement phase.
struct Scene {
  // Macro master -> its track lattice per orientation. A master that is absent has no track constraint. Serialized
  // with the scene (the flow and the session may be different processes), so it is data, not a callback.
  std::map<std::string, std::vector<Macro_tracks>> track_lattices;
  std::string                                  design;
  std::string                                  chunk_plan, chunk_results;  // fp_plan record, DBU boundaries and scoped targets
  std::string                                  phase;  // "fp", "macro", "final", ...
  Box                                          die;    // the placement area (the core: the rows' hull), DBU
  Box                                          frame_die;  // the floorplan's die (the core plus its margin), DBU; zero: not known
  double                                       cell_area = 0;  // DBU^2 of every cell and macro body, fixed ones too (the `area` command's utilization)
  uint64_t topology_hash = 0, masters_hash = 0;
  double   halo_x = 0, halo_y = 0, channel_x = 0, channel_y = 0;
  double   row_height = 0, site_width = 0;  // DBU; zero when the placement lattice is unknown
  double                                       macro_area = 0;  // DBU^2 the macros and memory proxies take WITH their macro-to-macro spacing: what packing needs
  static constexpr double                      kMacroFillLimit = 0.70;  // the largest share of a core the macro footprints may take (first-fit packing)
  double                                       dbu_per_um = 1;
  std::vector<Node>                            nodes;  // solver node order
  std::vector<Box>                             regions;
  std::vector<Box> exclusions;  // physical routing and placement exclusions, before occupancy subtraction
  double           dead_pitch = 1, dead_narrow_x = 0, dead_narrow_y = 0, dead_min_area = 0, dead_pocket_fraction = .25;
  using Point = std::array<double, 2>;
  std::vector<std::vector<Point>>              outline;  // derived rings, usable space on the left
  double                                       usable_area = 0;
  std::map<std::string, std::vector<int>>      groups;  // exact centroid memberships at this phase
  std::vector<Centroid>                        centroids;
  std::unordered_map<std::string, int>         index;  // name -> node
  std::unordered_map<std::string, int>         centroid_index;  // centroid group name -> group
  void                                         build_index();
  int                                          node_index(const std::string& name) const;
};
struct Stats {
  int    iteration = 0;
  double hpwl_um   = -1;  // negative: unknown
  double overflow  = -1;
  double wns_ps = 0, tns_ps = 0;
  bool   has_timing = false;
  int    timing_iteration = 0;
  int    gp_steps = 0;  // completed iterations since the preceding solver callback
  std::vector<std::array<double, 3>> centroids;  // x, y, radius of each scene centroid, in scene order
  bool native_gp = false;
  std::string native_gp_reason{};
  uint64_t optimizer_identity = 0;  // active native GP callback only; a geometry frame is not a continuation
};
// Work observed at placement timing requests and solver callbacks.
struct Evaluation_work {
  uint64_t full_calls = 0, ranking_calls = 0, gp_iterations = 0;
  std::string json() const;
};
// What the solver must apply at the next poll.
struct Edit_pin {
  int    node = -1;
  double x = 0, y = 0;
};
struct Edit_centroid {
  int    group = -1;
  double x = 0, y = 0;
};
struct Repair_scope {
  std::vector<std::string> nodes;
  std::vector<double> x, y;
  std::optional<Box> window;
  unsigned trials = 0;
};
struct Edits {
  std::optional<Repair_scope> repair;
  std::vector<Edit_pin>      moves, pins;
  std::vector<int>           releases;
  std::vector<Edit_centroid> centroids;  // targets to apply (all that are still held when a release happens)
  bool                       release_centroids = false;
  bool empty() const { return !repair && moves.empty() && pins.empty() && releases.empty() && centroids.empty() && !release_centroids; }
};
struct Frame {
  std::uint64_t      id = 0;
  std::string        phase, state;
  int                iteration = 0;
  Stats              stats;
  std::vector<double> x, y;        // DBU, solver node order of the scene it belongs to
  std::vector<float> cx, cy, cr;  // centroid groups (scene order)
};
// A congestion-style map over the die: v[x * by + y], utilization (1 = full), `peak` its maximum.
struct Grid {
  int                bx = 0, by = 0;
  std::vector<float> v;
  double             peak = 0, overflow = 0;
  std::string        note;  // a line shown with the map, or why it is empty
  // kind "regions" (the region mesh at the current positions): rectangles (xl, yl, xh, yh, cells) and the estimated
  // places where nets cross their boundaries (x, y, axis: 0 a vertical edge, 1 a horizontal one), DBU
  std::vector<float> rects, crossings;
  std::size_t        crossing_total = 0;  // crossings found (the list is thinned to a cap)
};
// Only the evaluator can invoke its physical legalizer. An empty scope with
// scoped=false selects every movable object. Pins and authored FIXED remain held.
struct Legalize_request {
  std::vector<double> x, y;
  std::vector<int>    active, held;
  bool                scoped         = false;
  int                 iterations     = 0;
  bool                bounded_window = false;
  Box                 window{};
  bool                full_reference = false;
};
struct Legalize_result {
  std::string           error;
  std::vector<Edit_pin> moved;
  int                   iterations = 0;
};
using Orientation_edits    = std::vector<std::pair<int, std::string>>;
using Orientation_provider = std::function<void(const Orientation_edits&)>;
using Intent_provider      = std::function<std::string(const std::string&, const Frame&, bool)>;
using Legalizer            = std::function<Legalize_result(const Legalize_request&)>;
// Computed on the flow thread from the solver's current positions ("rudy", "rsmt").
using Map_provider         = std::function<Grid(const std::string& kind, const Frame&)>;
// WNS/TNS (ps) of the solver's current positions through the flow's own timer; false when unavailable.
struct Timing_trial {
  std::vector<Edit_pin> moves;
  unsigned              max_nets = 128, touched = 0, failing = 0;
  double                wns_ps = 0, tns_ps = 0;
  std::string           error;
};
struct Timing_query {
  bool                                        request_checkpoint = false;
  std::string                                 checkpoint, checkpoint_error, native_endpoint;
  std::vector<Timing_trial>                   trials;
  bool                                        rollback_verified = false;
  unsigned                                    baseline_failing  = 0;
  double                                      rc_s = -1, sta_s = -1;
  double                                      hpwl_um = -1, hold_wns_ps = std::numeric_limits<double>::quiet_NaN();
  double wns_ps = 0, tns_ps = 0;                 // out (tns is NaN when unknown)
  bool                                        full                  = false;
  int                                         electrical_violations = -1;
  std::vector<std::string>                    path;
  std::string                                 endpoints     = "null";
  const Frame*                                frame         = nullptr;  // immutable measurement geometry, valid during the callback
  bool   want_critical = false;                  // in: also list the most critical cells
  int    n = 25;                                 // in: how many cells
  double margin_ps = -1;                         // in: only cells within this slack of the worst (negative: no limit)
  std::vector<std::pair<std::string, double>> critical;  // out: instance name and its worst pin slack (ps), ascending
};
using Timing_provider = std::function<bool(Timing_query&)>;
struct Stage_switch {
  std::string name;
  bool        on        = true;
  bool        available = true;  // false: the options never run it
};
// "Replay from a checkpoint with other flags": the flow is re-run with the recorded actions up to the checkpoint.
struct Restart_request {
  std::string                        label;          // checkpoint label ("final", "gp_end", "legalize", "edit", ...)
  int                                nth       = 0;  // which occurrence of that label (1-based)
  std::uint64_t                      seq       = 0;  // trace lines recorded up to that checkpoint
  bool                               from_start = false;
  int                                listen_port = 0;
  std::string                        token;
  bool                               recipe_variant = false;
  std::string                        trace_source;  // session-private trace when a drag is still in progress
  std::optional<std::string>         captured_trace; // private foreground segment at submission
  std::map<std::string, std::string> set;            // flag overrides, full names (synth.place.iterations -> 60)
  // `area` moved the origin: the recorded actions' coordinates (DBU) must move with the frame (shift_trace).
  double                             shift_x = 0, shift_y = 0;
};
// "Fork a region as a new chip": the cells inside a box, extracted as a design of their own (cut nets become boundary
// pins fixed where the nets crossed the box) and run in a new session with its own page and options.
struct Frame;
struct Fork_request {
  std::string                  dir;    // an empty directory for the fork's files
  Box                          box;    // DBU, the requested region
  std::vector<std::string>     cells;  // instance names inside it
  std::shared_ptr<const Scene> scene;  // the parent's current scene and frame (positions of everything outside)
  std::shared_ptr<const Frame> frame;
};
struct Fork_files {
  std::string top, net_dir, floorplan, seed;  // the design's name, its lg: netlist, the DEF with rows and fixed pins, cell positions
  std::size_t cells = 0, inputs = 0, outputs = 0;  // cells taken, boundary pins created for the nets that entered / left
  Box         core;                                 // DBU, the box snapped to the site/row grid
  std::string error;                                // not empty: nothing was built
};
struct Fork_launch {
  std::string url;    // the new session's page
  std::string error;  // not empty: it did not start
};
// Adds (dx, dy) DBU to every position in a JSONL action trace (move and centroid actions). False when unreadable.
bool shift_trace(const std::string& path, double dx, double dy);
using Score_components = std::map<std::string, std::pair<double, double>>;  // loss -> (weight, half-score loss)
// Only metrics actually measured at this stage enter the common summary and quality score.
struct Summary_metrics {
  std::string           geometry, timing_model, endpoints = "null", advice = "null";
  bool                  timing_estimate = false;
  uint64_t              timing_frame = 0, routing_frame = 0;
  std::optional<double> wns, tns, hold, over_share, overflow, peak, hpwl;
  std::optional<int>    electrical;
};
std::string measurement_summary(const Summary_metrics&, const Score_components&, const std::string& estimator);
struct Config {
  std::string                      intent             = "[]", input_identity, source_identity;
  unsigned                         repair_trials      = 0;
  double                           invocation_start_s = 0;
  Score_components                 score;
  std::string                      score_estimator = "rudy";
  Mutation_policy                  policy;
  std::shared_ptr<Mutation_policy> evaluator_policy;
  std::string                      controller_executable;  // production sessions spawn the same binary in controller mode
  std::string                      hello = "{}";
  std::vector<std::string>         worker_args, region_args, validation_args;
  std::string                      target                = "main";
  bool                             snapshot_available    = false;
  std::string                      explore_worker        = "auto";
  bool                             repair_available      = false, scoped_repair_worker = false;
  bool                             batch_worker = false;
  int                              timing_call_limit = -1;
  bool                             resynthesis_available = false;
  bool                             terminal_sizing = false, terminal_buffering = false, terminal_resynthesis = false;
  unsigned                         repair_limit = 0;
  unsigned                         worker_memory_mb = 0, worker_disk_mb = 4096, history_result_mb = 16;
  int                              session_root_pid = 0;
  uint64_t                         session_memory_bytes = 0;
  std::map<std::string, int>       tier_timeout;
  // Controller-only deadline inherited from the submission, including its time in queue.
  std::chrono::steady_clock::time_point worker_deadline{};
  int                              timeout_for(const std::string& tier) const {
    const auto found = tier_timeout.find(tier);
    return found == tier_timeout.end() ? explore_timeout : found->second;
  }
  unsigned      explore_threads = 1;
  int           heavy_jobs      = 1;
  int           explore_timeout = 600, explore_open = 12, explore_jobs = 2, explore_queue = 8;
  int         port         = 0;  // 0: any free port
  bool        open_browser = true;
  int         poll         = 10;  // GP iterations between polls
  int         min_iters    = 20;  // extra iterations when run follows a finished GP
  int         history_every = 50;   // GP iterations between timeline snapshots (gates and checkpoints always record)
  int         history_mb    = 256;  // memory for the timeline's per-poll snapshots (16-bit positions); older ones are thinned
  int         timing_budget = 10;  // percent of wall time the page may spend on timing refreshes (0: only on request)
  std::string trace_path;         // JSONL action trace, empty: none
  std::string token;              // random when empty
  bool        start_paused = true;
  std::string shot_dir;     // where the `screenshot` command writes (file names are reduced to their basename)
  std::string video_dir;    // replay only: one numbered PNG per published frame
  std::string agent_socket; // Unix-domain socket (mode 0600) for a local agent: the same routes without a token, plus /agent
  std::string replay_path;  // JSONL action trace to replay without a browser (no server, never blocks)
  // Resume: replay the trace (lines up to replay_limit_seq) without blocking, then become interactive and paused at
  // the nth occurrence of resume_label. The server runs during the replay.
  std::string   resume_label;
  int           resume_nth = 0;
  std::uint64_t replay_limit_seq = ~std::uint64_t{0};
  // Called (on the flow thread) for a `restart` command; it normally re-executes the process and does not return.
  // A returned string is shown in the panel as the reason it could not restart.
  std::function<std::string(const Restart_request&)> restart;
  // Called (on the flow thread) for a `fork` command once the placer has written the region's files: starts the new
  // session as a child process and returns its page.
  std::string                                         fork_dir;  // where fork_<n>/ directories go (empty: forking is off)
  std::function<Fork_launch(const Fork_files&, const Fork_request&)> fork;
};

class Controller_link;
class Replay_worker;
class Snapshot_owner;
class Session {
public:
  explicit Session(Config);
  ~Session();
  Session(const Session&)            = delete;
  Session& operator=(const Session&) = delete;

  // Binds 127.0.0.1 and starts the server thread. Returns false (and a reason) on failure.
  bool               start(std::string* error = nullptr);
  Mutation_policy                 policy() const;
  // Stops the server: no new connection is taken, and the requests in flight finish their answers. A restart
  // calls it before it re-executes the process, so that a request does not arrive (or an answer end) in the
  // middle of the exec. `start` serves again, on the same port.
  void               stop_serving();
  void                            suspend_listeners();
  void                            shutdown();
  std::string        url() const;
  const std::string& token() const { return token_; }
  const std::string& agent_socket() const { return agent_socket_; }
  // The agent channel (a local process, e.g. an assistant): JSON in, JSON out. Highlights and selection are shown in the
  // page; "critical" asks the flow thread (answered at its next poll or gate, `timeout_ms` at most).
  std::string agent_call(const std::string& json, int timeout_ms = 15000);
  std::string                     result_snapshot() const;
  std::string                     capture_baseline() const;
  static std::unique_ptr<Session> read_result_snapshot(std::string_view, std::string routed_summary = {});
  Edits prepare_frozen(const std::string& id,const std::string& patch,const std::string& score);
  std::string finish_native_gp(const std::vector<double>& x,const std::vector<double>& y,const Stats&,const Map_provider&,const Timing_provider&);
  std::string finish_frozen(std::unique_lock<std::mutex>&,const Map_provider&,const Timing_provider&);
  std::string run_frozen_timing(const std::string& id, const std::string& patch, const std::string& score, const Map_provider&,
                                const Timing_provider&, const std::string& relax, const Legalizer&);
  std::string result_snapshot_locked() const;
  std::string exploration_get(const std::string& id, const std::string& route) const;
  std::string highlights_json() const;  // for the current scene: [{id,label,color,count,idx:[...]}]
  std::string selection_json() const;   // {n, idx:[...]} for the current scene
  const Config&      config() const { return cfg_; }
  bool               replaying() const { return replay_active_.load(); }
  int                port() const { return port_; }
  const std::string& epoch() const { return epoch_; }
  // Flags the page can edit (JSON array of {name, value, default, kind, help}), supplied by the host.
  void               set_options_json(std::string json);
  std::string        options_json() const;
  // The region mesh and boundary-pin crossings at the current positions: {v, on, note, rects, pins, pins_total}.
  std::string        regions_json() const;
  // The placer's part of a fork: given the cells of a region, writes the region's netlist, floorplan and seed positions.
  // Installed by the placement for the phase it is in (it reads that phase's data).
  using Fork_builder = std::function<Fork_files(const Fork_request&)>;
  void               solver_timing_call(bool full);
  std::string        evaluation_work_json() const;
  void               set_repair_trials(unsigned, const std::vector<std::string>& changed = {});
  void               begin_terminal_repair(Mutation_policy);
  void               set_legalizer(Legalizer);
  void               set_orientation_provider(Orientation_provider);
  void               set_intent_provider(Intent_provider);
  std::string        intent() const;
  void               set_fork_builder(Fork_builder);

  // ---- flow thread -----------------------------------------------------------------------
  void set_stages(std::vector<Stage_switch>);
  // A new placement phase: publishes the scene and the starting positions.
  // mark_pins_dirty = false for the edit phase: surviving pins are already part of the placement.
  void begin_phase(std::shared_ptr<const Scene>, const double* x, const double* y, size_t n, bool mark_pins_dirty = true);
  // From inside the GP loop. Publishes, applies queued commands, blocks while paused.
  Edits poll(int iteration, const double* x, const double* y, size_t n, const Stats&, const Map_provider* maps = nullptr, const Timing_provider* timing = nullptr);
  // The GP is about to stop. Blocks when paused, or when the legalize switch is off. `run` there asks for at least
  // config().min_iters more iterations (the session pauses again at the next end); `continue` goes on to the next stage.
  struct End_result {
    Edits edits;
    int   extend = 0;
  };
  End_result gp_end(const double* x, const double* y, size_t n, const Stats&, const Map_provider* maps = nullptr,
                    const Timing_provider* timing = nullptr);
  // After the final legalization (post-legalization edit mode): blocks while paused or while the `edit` switch is off.
  // `run` returns the edits accumulated since the last call (again = true: apply them and call again); `continue`
  // or an empty `run` leaves edit mode. Replays return the recorded edits of phase "edit" and never block.
  struct Edit_result {
    Edits edits;
    bool  again = false;
  };
  Edit_result edit_wait(const double* x, const double* y, size_t n, const Stats&, const Map_provider* maps = nullptr,
                        const Timing_provider* timing = nullptr);
  // Pins by name with their drop positions (the edit mode holds all of them while it re-places a region).
  std::map<std::string, std::pair<double, double>> pin_positions() const;
  // The legal placement the edit mode ended with, per cell name: x, y, orientation. Later final passes (restarts from
  // an earlier seed) start from it, so an edit survives them.
  void set_placement(std::map<std::string, std::array<std::int64_t, 3>>);
  std::map<std::string, std::array<std::int64_t, 3>> placement_override() const;
  // Fixed macros (memory proxies packed as obstacles) the user dragged or aligned in the page: name -> lower-left (DBU).
  // They cannot move inside a running solver, so the flow restarts and the placer starts them here (place.cpp, before the
  // proxy packing). Read back from the action trace on every restart.
  // The third number orders them: the larger, the more recent (the placer settles the newest first). The fourth is the
  // requested orientation as an Ann_place::Orientation value (0 = N; macro_move accepts N, S, FN and FS).
  std::map<std::string, std::array<double, 4>> macro_moves() const;
  // What the user changed in edit mode (name, drop position), in order; later final passes re-apply it.
  void record_edits(const std::vector<std::pair<std::string, std::pair<double, double>>>&);
  std::vector<std::pair<std::string, std::pair<double, double>>> recorded_edits() const;
  bool edit_left() const;
  // A short message for the page (e.g. an edit the legalizer could not honor); empty clears it.
  void set_note(const std::string&);
  // Between stages. Publishes and blocks while paused or while the switch for `stage` is off.
  Edits gate(const std::string& stage, const double* x, const double* y, size_t n, const Stats&, const Map_provider* maps = nullptr,
             const Timing_provider* timing = nullptr);
  // Read-only timing from the flow's own checkpoints (no extra STA call): shown with the iteration it came from.
  void set_timing(double wns_ps, double tns_ps, int iteration);
  // Shortest configured SDC period in ps, including before the first GUI timing evaluation.
  void set_clock_period_ps(double period_ps);
  // The placement the flow ended with, by node name: becomes the last published frame.
  void set_result(const std::vector<std::tuple<std::string, double, double>>&);
  // The run failed (an error ended the flow): show it in the page and keep serving until `finish`. A `restart` command
  // (from the start, with edited flags) still works. Returns when the user exits.
  void fail(const std::string& title, const std::string& detail);
  // The flow is done: keep serving until `finish` (or a shutdown).
  void wait_finish();
  bool exit_requested() const { return exit_.load(); }
  // Every surviving pin of the current scene (a new place() call starts with none held).
  Edits all_pins(bool legalizing = false) const;
  // Frame published most recently (for screenshots).
  std::shared_ptr<const Frame> frame() const;
  std::shared_ptr<const Scene> scene() const;

  // ---- server thread ---------------------------------------------------------------------
  // JSON documents / binary for the endpoints.
  // cp >= 0: the scene/frame recorded at that checkpoint (read-only review).
  std::string design_json(int cp = -1, int tl = -1) const;
  // The timeline: one snapshot per published frame (per GP poll, gate, ...) so the page can scrub back and forth.
  // JSON {"v":version,"phases":[...],"e":[[iteration, checkpoint index or -1, scene id, phase index], ...]}.
  std::string timeline_json() const;
  std::string state_json() const;
  // q16: "LHQ1" header (n, id, die box as 4 floats) then n uint16 x and n uint16 y over the die (4 bytes per node, for
  // large designs); otherwise "LHF1": n float32 x then n float32 y.
  // since != 0 and still in the history: "LHD1" delta (n, id, base id, m, m indices, m x, m y) when cheaper.
  std::string frame_binary(std::uint64_t* id, bool q16 = false, std::uint64_t since = 0, int cp = -1, int tl = -1) const;
  std::string   map_binary(const std::string& kind = {}, uint64_t expected_frame = 0) const;
  // Blocks until the published version differs from `since` (or timeout). Returns the version.
  std::uint64_t wait_version(std::uint64_t since, int timeout_ms) const;
  // Queues one JSON command; returns an error text or "" on success.
  std::string   submit(const std::string& json, uint64_t* sequence = nullptr);
  std::string   submit_reply(const std::string& json, bool* accepted = nullptr);
  std::string   read_document(const std::string& route) const;
  std::string node_json(int index) const;
  // Agent query: one node by name, or the nodes inside a box (DBU) with their positions in the current frame.
  std::string query_json(const std::string& name, const Box* box, int limit) const;
  // PNG of the current frame (server-side rasterizer), `layers` as in parse_layers.
  std::string screenshot_png(int w, int h, const std::string& layers, const Box* view) const;
  int         video_frames() const { return video_frames_; }

private:
  struct Command {
    uint64_t                           sequence = 0;
    std::vector<Command>               batch;
    std::optional<uint64_t>            expected_frame, expected_intent, expected_evaluation;
    std::optional<std::string>         expected_baseline, expected_target;
    std::optional<bool>                no_resynthesis, no_sizing, no_buffer_edits;
    bool                               relative = false, halo_edges = false, scoped = false, bound_only = false, full_reference = false;
    int                                iterations = 0;
    std::vector<std::string>           scope_nodes;
    std::vector<std::string>           orientations;
    std::string                        align, reference, relationship;
    double                             gap = 0;
    std::string                cmd;
    std::vector<std::string>   nodes;
    std::vector<double>        x, y;
    std::string                name, file, layers;
    int                        w = 0, h = 0;
    bool                       has_view = false;
    Box                        view;
    int                        cp = -1;                   // restart: checkpoint index (-1: from the start)
    std::map<std::string, std::string> set;               // restart: flag overrides
    bool                       on = true, final_ = true;
    int                        count = 0;
    std::string                edge;                      // area: "left", "right", "bottom" or "top"
    double                     delta = 0;                 // area: um, positive grows the area
    bool                       has_box = false;           // area: the new core in um (xl, yl, xh, yh) instead of an edge
    Box                        box;                       // area / fork: um (xl, yl, xh, yh)
                                                          // fork: nodes[] are the cells to take instead of a box when has_box is false
    std::string                move_fields_json() const;
  };
  template <class Archive>
  void                                         result_fields(Archive&);
  std::string                                  summary_locked() const;
  std::string                                  repair_advice_locked() const;
  void                                         save_foreground_measurement_locked();
  std::string                                  exploration_limits_locked() const;
  std::string                                  snapshot_reason_locked() const;
  bool                                         queue_full_locked() const;
  std::string                                  bind_nodes_locked(Command&) const;
  std::string                                  resolve_move_locked(Command&, const Frame* basis = nullptr, bool captured_orientation = false) const;
  static std::string                           parse_expectation(const rapidjson::Value&, Command&);
  std::string                                  check_expectation_locked(const Command&) const;
  std::string    parse_command(const std::string& json, Command&) const;
  void                                         replay_locked(int iteration, Edits&, std::unique_lock<std::mutex>&);
  void           macro_restart_locked();
  std::map<std::string, std::array<double, 4>> macro_moves_;  // name -> x, y (DBU), recency, orientation
  double                                       macro_order_ = 0;
  struct Inline_request {
    uint64_t                  frame = 0;
    std::vector<Timing_trial> trials;
    double                    wns_ps = 0, tns_ps = 0;
    unsigned                  failing = 0;
    std::string               error;
    bool                      done = false;
  };
  std::deque<std::shared_ptr<Inline_request>> inline_requests_;
  void                                        service_inline(std::unique_lock<std::mutex>&, const Timing_provider*);
  void           service_timing(std::unique_lock<std::mutex>&, const Timing_provider*, bool running);
  std::string    node_details_locked(size_t index) const;
  void           set_selection_locked(std::vector<std::string> names);
  int            add_highlight_locked(const std::string& label, const std::string& color, const std::string& regex, const std::string& scope,
                                      std::vector<std::string> names, bool truncated);
  bool           admit_timing_locked(bool full);
  void           set_timing_locked(double wns_ps, double tns_ps, int iteration);
  Edits          pins_locked(bool legalizing) const;
  void           push_frame_locked(std::shared_ptr<const Frame>);
  void           checkpoint_locked(const std::string& label);
  void           record_timeline_locked(bool force);
  void           service_map(std::unique_lock<std::mutex>&, const Map_provider*, bool every);
  void           publish_locked(const double* x, const double* y, size_t n, const Stats&, const std::string& state);
  void           drain_locked(Edits&, std::unique_lock<std::mutex>&);
  void           apply_locked(const Command&, Edits&);
  void           area_locked(const Command&);
  void           fork_unlocked(const Command&, std::unique_lock<std::mutex>&);
  void           trace_locked(const Command&);
  Edits          take_edits_locked();
  bool           switch_on_locked(const std::string& stage) const;
  void           bump_locked();

  std::shared_ptr<Controller_link> controller_;
  bool discard_exploration(const std::string& id);
  struct Geometry_result {
    std::shared_ptr<Session> document;
    std::string              summary;
    bool                     failed = false;
    std::string              label;
    std::map<std::string, std::string> recipe_flags;
  };
  std::map<std::string, Geometry_result> geometry_results_;
  struct Background {
    std::string                           id, label, client, tier, baseline, state = "queued", reason;
    uint64_t                              frame = 0;
    size_t                                baseline_nodes = 0;
    uint64_t                              reserved_bytes = 0;
    std::string                           parent = "main";
    Restart_request                       checkpoint;
    std::string                           worker_kind, patch, relax, until, annotation, estimator, scope_key;
    Score_components                      score;
    size_t                                active_cells = 0;
    unsigned                              repair_limit = 0;
    int                                   timing_call_limit = -1;
    int                                   deadline_s = 0;
    std::string                           repair_mode;
    std::shared_ptr<Session>              validation_document;
    double                                placement_seconds = 0;
    std::string                           placement_time_source;
    bool                                  waiting_for_memory = false;
    std::shared_ptr<Replay_worker>        worker;
    std::chrono::steady_clock::time_point submitted, admitted, finished;
    std::string                           status() const;
  };
  std::map<std::string, Background>        background_;
  std::jthread                             dispatcher_;
  std::condition_variable                  jobs_changed_;
  std::string                              last_client_;
  void                                     dispatch_explorations(std::stop_token);
  uint64_t                                 memory_prediction(size_t nodes, bool full_replay) const;
  bool                                     memory_fits_locked(uint64_t additional) const;
  uint64_t                                 foreground_reserved_bytes_ = 0, checkpoint_reserved_bytes_ = 0;
  std::shared_ptr<Replay_worker>           foreground_;
  std::string                              foreground_id_, foreground_label_, foreground_annotation_, foreground_client_;
  uint64_t                                 foreground_frame_ = 0;
  std::string                              foreground_status_locked() const;
  std::map<std::string, std::string>       foreground_flags_;
  std::deque<std::pair<std::string, std::string>> closed_workers_;  // ID, detailed row in close order
  std::set<std::string> closing_workers_;
  std::map<std::string, std::string>       closed_summaries_;
  size_t                                   closed_bytes_ = 0, history_evicted_ = 0;
  uint64_t                                 next_worker_ = 1, design_hash_ = 0;
  std::string                              placement_identity_locked() const;
  std::string                              baseline_identity_locked() const;
  uint64_t                                 pose_hash_ = 0, hint_hash_ = 0;
  Config                         cfg_;
  std::string                    routed_summary_;  // Read-only T4 view: the scene is the submitted macro proposal.
  mutable std::mutex             m_;
  mutable std::condition_variable cv_;       // wakes the flow thread (commands)
  mutable std::condition_variable updates_;  // wakes long polls (new frame/state)
  std::shared_ptr<const Scene>   scene_;
  std::shared_ptr<const Frame>   frame_;
  // Geometry is a pure function of these published objects. Keep at most one
  // report and don't retain the previous design or its coordinates.
  std::weak_ptr<const Scene>              findings_scene_;
  std::weak_ptr<const Frame>              findings_frame_;
  std::shared_ptr<const std::string>      findings_json_;
  std::deque<std::shared_ptr<const Frame>> history_;
  struct Checkpoint {
    std::string                  label, phase;
    int                          nth = 0, iteration = 0;
    std::uint64_t                seq = 0;
    std::shared_ptr<const Scene> scene;
    std::shared_ptr<const Frame> frame;  // released for old checkpoints (the restart point itself stays)
  };
  std::vector<Checkpoint>        checkpoints_;
  struct Tl_entry {
    std::shared_ptr<const Scene>                       scene;
    std::uint64_t                                      scene_id = 0;
    std::string                                        phase;
    int                                                iteration = 0, cp = -1;
    std::shared_ptr<const std::vector<std::uint16_t>>  q;     // x then y, 16 bits over the scene's die
    std::shared_ptr<const std::vector<float>>          cent;  // centroids: x, y, r each
  };
  std::vector<Tl_entry>          timeline_;
  std::size_t                    tl_bytes_ = 0;
  std::uint64_t                  tl_version_ = 0;
  bool                           record_timeline_ = true;
  std::map<std::string, int>     label_count_;
  std::atomic<bool>              replay_active_{false};
  bool                           resumed_ = false;
  std::string                    options_json_ = "[]", epoch_;
  // The kind and read-only bit of every editable flag (from options_json_), so that a restart naming a flag that does
  // not exist, is read-only or cannot hold the value is refused up front: the re-executed process would otherwise
  // exit on the bad flag and take the whole session with it. Its own mutex: parse_command runs with m_ held.
  struct Flag_spec {
    std::string kind;
    bool        ro = false;
  };
  mutable std::mutex               flags_m_;
  std::map<std::string, Flag_spec> flag_specs_;
  std::string                      validate_flag(const std::string& name, const std::string& value) const;
  int                            port_ = 0;
  std::string                    wanted_map_, map_kind_, map_note_;
  bool                           regions_wanted_ = false, regions_dirty_ = false;
  std::shared_ptr<const Grid>    regions_;
  std::uint64_t                  regions_version_ = 0, regions_frame_ = 0;
  bool                           map_dirty_ = false, all_maps_wanted_ = false, score_map_wanted_ = false;
  std::map<std::string, Grid>    measured_maps_;
  uint64_t                       measured_maps_frame_ = 0;
  std::shared_ptr<const Grid>    map_;
  std::uint64_t                  map_version_ = 0, map_frame_ = 0;
  struct Highlight {
    int                      id = 0;
    std::string              label, color, regex, scope;
    std::vector<std::string> names;  // by name: a highlight survives a change of phase
    bool                     truncated = false;
  };
  std::vector<Highlight>         highlights_;
  int                            next_highlight_ = 1;
  std::uint64_t                  hl_version_ = 0, sel_version_ = 0;
  std::vector<std::string>       selection_;
  std::size_t                    selection_count_ = 0;
  bool                           crit_pending_ = false;
  Timing_query                   crit_request_;
  std::uint64_t                  crit_seq_ = 0;
  std::string                    crit_error_;
  std::vector<std::pair<std::string, double>> crit_result_;
  mutable std::condition_variable crit_cv_;
  std::string                    agent_socket_;
  bool                                        full_timing_wanted_ = false;
  uint64_t                                    evaluated_frame_ = 0, evaluation_seq_ = 0;
  bool                           timing_wanted_ = false, t_start_set_ = false;
  Evaluation_work                work_, replay_work_;
  bool                           timing_budget_exhausted_ = false;
  std::string                    timing_operation_ = "idle";
  double                         last_rc_s_ = -1, last_sta_s_ = -1, last_timing_s_ = -1;
  double                         timing_spent_s_ = 0, snapshot_setup_s_ = 0;
  int                            polls_since_timing_ = 0;
  std::chrono::steady_clock::time_point t_start_;
  bool                           has_timing_ = false, timing_est_ = false;
  double                         period_ps_ = 0;
  double                         wns_ps_ = 0, tns_ps_ = 0;
  int                            timing_iter_ = 0;
  std::uint64_t                  version_ = 0, frame_id_ = 0, scene_version_ = 0;
  std::vector<Stage_switch>      stages_;
  bool                           paused_        = true;
  bool                           finished_      = false;
  bool                           finishing_     = false;  // `finish`: never block again
  bool                           gate_released_ = false;  // run pressed at a switched-off gate
  std::string                                 exploration_stop_;
  bool                           repause_       = false;  // the next GP end blocks even if running
  bool                           at_gp_end_ = false, gp_decided_ = false;
  bool                           at_edit_ = false, edit_decided_ = false, edit_left_ = false;  // left: later passes do not block again
  std::string                    note_, error_;
  Fork_builder                   fork_builder_;
  Mutation_policy                 initial_policy_;
  Legalizer                       legalizer_;
  Intent_provider                 intent_provider_;
  Orientation_provider            orientation_provider_;
  std::string                     intent_report_   = "{}", intent_status_;
  uint64_t                        intent_revision_ = 0, intent_report_frame_ = 0, intent_report_sequence_ = 0;
  void                            intent_unlocked(const Command&, std::unique_lock<std::mutex>&);
  std::map<std::string, Edit_pin> pending_moves_;
  std::vector<Edit_pin>           legalized_moves_;
  double                          hold_wns_ps_           = std::numeric_limits<double>::quiet_NaN();
  int                             electrical_violations_ = -1;
  std::vector<std::string>        worst_path_;
  std::map<std::string, int>      poll_counts_;
  bool native_pause_locked() const;
  std::string native_gp_reason_locked() const;
  uint64_t native_poll_frame_ = 0;
  std::string native_poll_baseline_;
  std::shared_ptr<Snapshot_owner> snapshot_owner_;
  std::string                     snapshot_baseline_, snapshot_attempt_, snapshot_requested_, snapshot_reason_;
  std::string                     endpoint_report_       = "null";
  bool                            legalize_available_    = false;
  int                             relaxation_iterations_ = 0;
  std::string                     legalization_status_, legalization_error_;
  void                            legalize_unlocked(const Command&, std::unique_lock<std::mutex>&);
  struct Forked {
    std::string url, top, dir;
    std::size_t cells = 0, inputs = 0, outputs = 0;
  };
  std::vector<std::string>         scoped_resize_changes_;
  std::optional<Repair_scope>      pending_repair_;
  std::vector<Forked>            forks_;
  std::string                                                    scratch_root_locked();
  std::string                                                    region_root_;
  int                                                            scratch_lease_ = -1;
  uint64_t                                                       next_region_   = 1;
  bool                           failed_ = false;
  std::map<std::string, std::array<std::int64_t, 3>> placement_;
  std::vector<std::pair<std::string, std::pair<double, double>>> edits_log_;
  int                            gp_extend_ = 0;
  std::string                    stage_name_, gate_name_, state_name_ = "starting";
  int                            iteration_ = 0;
  Stats                          stats_;
  std::map<std::string, Edit_pin> pins_;  // by node name, persistent across phases
  std::map<std::string, Edit_pin> cpins_;  // centroid targets by group name
  std::set<std::string>          dirty_cpins_;
  bool                           release_c_ = false;
  std::map<std::string, std::vector<std::string>>                geometry_conflicts_;
  std::set<std::string>                                          suspended_pins_;
  std::set<std::string>          dirty_pins_, released_;
  std::deque<Command>            queue_;
  uint64_t                                                       queued_sequence_ = 0, completed_sequence_ = 0;
  std::map<uint64_t, std::string> command_results_;
  size_t                          command_result_bytes_ = 0;
  struct Replay_item {
    std::string phase;
    int         iter = 0;
    Command     cmd;
  };
  std::deque<Replay_item>        replay_;
  std::vector<std::string>       relationships_;  // applied relative commands authored on this target, in order
  std::uint64_t                  seq_ = 0;
  std::ofstream                  trace_, private_trace_, foreground_segment_;
  std::string                    private_trace_path_;
  int                            video_frames_ = 0;
  std::atomic<bool>              exit_{false};

  struct Server;
  std::vector<std::shared_ptr<Server>> retired_servers_;
  std::shared_ptr<Server> server_;
  std::string             url_;
  std::string             token_;
};

// frames_dir/frame_%05d.png -> out (mp4) with ffmpeg when it is on the PATH. False when it is not (the frames remain).
bool make_video(const std::string& frames_dir, const std::string& out);

// The session of this process, kept alive after the flow unwinds so a failure can still be shown (never freed).
void                     set_active_session(std::shared_ptr<Session>);
std::shared_ptr<Session> active_session();

// Opens `url` in the default browser (macOS `open`, Linux `xdg-open`); best effort.
void open_browser(const std::string& url);

}  // namespace livehd::placement::gui
