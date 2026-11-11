// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
// Interactive placement session (`lhd synth --gui`, todo/livehd/2i-interact.html).
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
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace livehd::placement::gui {

enum class Kind : std::uint8_t { cell = 0, macro = 1, fixed = 2, blockage = 3, io = 4 };

struct Node {
  std::string name;
  Kind        kind    = Kind::cell;
  bool        movable = true;
  double      w = 0, h = 0;  // DBU
  bool        seq = false;   // a register: a cell with a clock pin (named in the design document)
  std::string master, klass; // library cell / macro name and LEF class (CORE, BLOCK, ...), for the agent's questions
  std::uint32_t pins = 0;    // connected pin count
};
struct Box {
  double xl = 0, yl = 0, xh = 0, yh = 0;
};
struct Centroid {
  std::string name;
  double      x = 0, y = 0, radius = 0;
  bool        movable = true;
};
// Everything that is fixed for the duration of one placement phase.
struct Scene {
  std::string                                  phase;  // "fp", "macro", "final", ...
  Box                                          die;    // the placement area (the core: the rows' hull), DBU
  Box                                          frame_die;  // the floorplan's die (the core plus its margin), DBU; zero: not known
  double                                       cell_area = 0;  // DBU^2 of every movable node (the `area` command's utilization)
  double                                       dbu_per_um = 1;
  std::vector<Node>                            nodes;  // solver node order
  std::vector<Box>                             regions;
  std::vector<Centroid>                        centroids;
  std::unordered_map<std::string, int>         index;  // name -> node
  std::unordered_map<std::string, int>         centroid_index;  // centroid group name -> group
  void                                         build_index();
};
struct Stats {
  int    iteration = 0;
  double hpwl_um   = -1;  // negative: unknown
  double overflow  = -1;
  double wns_ps = 0, tns_ps = 0;
  bool   has_timing = false;
  int    timing_iteration = 0;
  std::vector<std::array<double, 3>> centroids;  // x, y, radius of each scene centroid, in scene order
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
struct Edits {
  std::vector<Edit_pin>      pins;
  std::vector<int>           releases;
  std::vector<Edit_centroid> centroids;  // targets to apply (all that are still held when a release happens)
  bool                       release_centroids = false;
  bool                       empty() const { return pins.empty() && releases.empty() && centroids.empty() && !release_centroids; }
};
struct Frame {
  std::uint64_t      id = 0;
  std::string        phase, state;
  int                iteration = 0;
  Stats              stats;
  std::vector<float> x, y;  // DBU, solver node order of the scene it belongs to
  std::vector<float> cx, cy, cr;  // centroid groups (scene order)
};
// A congestion-style map over the die: v[x * by + y], utilization (1 = full), `peak` its maximum.
struct Grid {
  int                bx = 0, by = 0;
  std::vector<float> v;
  double             peak = 0, overflow = 0;
  std::string        note;  // why the map is empty (e.g. the URoute model is not loaded)
  // kind "regions" (the region mesh at the current positions): rectangles (xl, yl, xh, yh, cells) and the estimated
  // places where nets cross their boundaries (x, y, axis: 0 a vertical edge, 1 a horizontal one), DBU
  std::vector<float> rects, crossings;
  std::size_t        crossing_total = 0;  // crossings found (the list is thinned to a cap)
};
// Computed on the flow thread from the solver's current positions ("rudy", "rsmt").
using Map_provider = std::function<Grid(const std::string& kind)>;
// WNS/TNS (ps) of the solver's current positions through the flow's own timer; false when unavailable.
struct Timing_query {
  double wns_ps = 0, tns_ps = 0;                 // out (tns is NaN when unknown)
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
struct Config {
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

class Session {
public:
  explicit Session(Config);
  ~Session();
  Session(const Session&)            = delete;
  Session& operator=(const Session&) = delete;

  // Binds 127.0.0.1 and starts the server thread. Returns false (and a reason) on failure.
  bool               start(std::string* error = nullptr);
  std::string        url() const;
  const std::string& token() const { return token_; }
  const std::string& agent_socket() const { return agent_socket_; }
  // The agent channel (a local process, e.g. an assistant): JSON in, JSON out. Highlights and selection are shown in the
  // page; "critical" asks the flow thread (answered at its next poll or gate, `timeout_ms` at most).
  std::string agent_call(const std::string& json, int timeout_ms = 15000);
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
  // The SDC clock period (ps): the page shows the achieved frequency 1/(period - WNS).
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
  Edits all_pins() const;
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
  std::string map_binary() const;
  // Blocks until the published version differs from `since` (or timeout). Returns the version.
  std::uint64_t wait_version(std::uint64_t since, int timeout_ms) const;
  // Queues one JSON command; returns an error text or "" on success.
  std::string submit(const std::string& json);
  std::string node_json(int index) const;
  // Agent query: one node by name, or the nodes inside a box (DBU) with their positions in the current frame.
  std::string query_json(const std::string& name, const Box* box, int limit) const;
  // PNG of the current frame (server-side rasterizer), `layers` as in parse_layers.
  std::string screenshot_png(int w, int h, const std::string& layers, const Box* view) const;
  int         video_frames() const { return video_frames_; }

private:
  struct Command {
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
  };
  std::string    parse_command(const std::string& json, Command&) const;
  void           replay_locked(int iteration, Edits&);
  void           service_timing(std::unique_lock<std::mutex>&, const Timing_provider*, bool running);
  std::string    node_details_locked(size_t index) const;
  void           set_selection_locked(std::vector<std::string> names);
  int            add_highlight_locked(const std::string& label, const std::string& color, const std::string& regex, const std::string& scope,
                                      std::vector<std::string> names, bool truncated);
  void           set_timing_locked(double wns_ps, double tns_ps, int iteration);
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

  Config                         cfg_;
  mutable std::mutex             m_;
  mutable std::condition_variable cv_;       // wakes the flow thread (commands)
  mutable std::condition_variable updates_;  // wakes long polls (new frame/state)
  std::shared_ptr<const Scene>   scene_;
  std::shared_ptr<const Frame>   frame_;
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
  int                            port_ = 0;
  std::string                    wanted_map_, map_kind_, map_note_;
  bool                           regions_wanted_ = false, regions_dirty_ = false;
  std::shared_ptr<const Grid>    regions_;
  std::uint64_t                  regions_version_ = 0;
  bool                           map_dirty_ = false;
  std::shared_ptr<const Grid>    map_;
  std::uint64_t                  map_version_ = 0;
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
  bool                           timing_wanted_ = false, t_start_set_ = false;
  double                         timing_spent_s_ = 0;
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
  bool                           repause_       = false;  // the next GP end blocks even if running
  bool                           at_gp_end_ = false, gp_decided_ = false;
  bool                           at_edit_ = false, edit_decided_ = false, edit_left_ = false;  // left: later passes do not block again
  std::string                    note_, error_;
  Fork_builder                   fork_builder_;
  struct Forked {
    std::string url, top, dir;
    std::size_t cells = 0, inputs = 0, outputs = 0;
  };
  std::vector<Forked>            forks_;
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
  std::set<std::string>          dirty_pins_, released_;
  std::deque<Command>            queue_;
  struct Replay_item {
    std::string phase;
    int         iter = 0;
    Command     cmd;
  };
  std::deque<Replay_item>        replay_;
  std::uint64_t                  seq_ = 0;
  std::ofstream                  trace_;
  int                            video_frames_ = 0;
  std::atomic<bool>              exit_{false};

  struct Server;
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
