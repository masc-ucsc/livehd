// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include "core/job_manager.hpp"
#include "gui_session.hpp"

namespace livehd::placement::gui {

const rapidjson::Value* routed_endpoints(const rapidjson::Value& receipt);
// Reap crash-abandoned private directories owned by this uid. A live
// controller keeps both its PID and an exclusive directory lease.
void clean_exploration_roots(const std::filesystem::path& parent);
// A replay evaluator runs the same executable with a private workdir, socket
// and action trace. Its process group is owned and reaped by the controller.
// A fresh serial owner restores frozen timing; a retained native owner also has the live
// optimizer stack. Both take requests on a private channel and fork one directory per candidate.
class Snapshot_owner {
public:
  Snapshot_owner(const Config&, const std::string& checkpoint, const std::string& document, const std::string& native_endpoint = {});
  ~Snapshot_owner();
  bool carries_gp() const { return native_; }
  pid_t launch(const std::filesystem::path&, const std::string& id, const std::string& patch, const std::string& score,
               const std::string& relax);
  void  release(pid_t pid, double& cpu_s, uint64_t& rss);
  bool  finished(pid_t pid);

private:
  bool native_ = false;
  std::mutex mutex_;
  int        socket_ = -1;
  pid_t      pid_    = -1;
};
class Replay_worker {
public:
  Replay_worker(const Config&, const std::filesystem::path&, std::string id, const std::string& identity, std::string patch,
                Job_slot, std::shared_ptr<Snapshot_owner>, std::string relax);
  Replay_worker(const Config&, const std::filesystem::path& root, std::string id, const Restart_request&,
                const std::string& identity, Job_slot slot = {}, std::string tier = {}, std::string patch = {},
                std::string relax = {}, std::string repair_mode = {}, unsigned repair_limit = 0, std::string until = {});
  Replay_worker(const Config&, const std::filesystem::path& root, std::string id, const std::shared_ptr<Session>& proposal,
                double placement_seconds, Job_slot);
  // Extracted regions run a normal restartable page under the same supervisor.
  Replay_worker(const Config&, std::string id, const Fork_files&, const Fork_request&, Job_slot);
  std::string await_region_page();
  ~Replay_worker();
  Replay_worker(const Replay_worker&)            = delete;
  Replay_worker& operator=(const Replay_worker&) = delete;
  std::string    agent(const std::string& request) const;
  std::string    get(const std::string& route) const;
  std::string    status() const;
  void           collect_foreground_history(size_t byte_limit);
  bool           ready() const;
  bool           terminal() const;
  uint64_t       unallocated_memory(uint64_t reservation) const;
  void           cancel();

private:
  std::shared_ptr<Session>              saved_result() const;
  mutable std::weak_ptr<Session>        document_;
  std::string                           request(std::string_view method, std::string_view path, std::string_view body) const;
  std::filesystem::path                 dir_;
  std::string                           region_url_;
  std::condition_variable               region_started_;
  std::string                           socket_, id_, status_ = "starting", reason_, tier_, result_, replayed_baseline_, baseline_, gate_;
  std::string                           routed_summary_;
  std::string                           foreground_segment_, foreground_measurement_;
  uint64_t                              foreground_segment_hash_ = 0, foreground_segment_count_ = 0;
  bool                                  foreground_segment_evicted_ = false;
  uint64_t                              foreground_main_seq_ = 0;
  int                                   foreground_gate_nth_ = 0;
  std::shared_ptr<Snapshot_owner>       owner_;
  std::string                           work_ = "null", replay_work_ = "null";
  bool                                  snapshot_ = false;
  Job_slot                              slot_;
  mutable std::mutex                    mutex_;
  std::mutex                            close_mutex_;
  pid_t                                 pid_ = -1;
  std::chrono::steady_clock::time_point start_, end_;
  unsigned                              threads_ = 1;
  double                                startup_s_ = -1, cpu_s_ = -1;
  uint64_t                              peak_rss_bytes_ = 0;
  pid_t                                 wait_worker(int* status, int flags);
  std::jthread                          monitor_, resource_watch_;
  uint64_t                              peak_tree_rss_bytes_ = 0, peak_disk_bytes_ = 0, peak_session_rss_bytes_ = 0;
  void                                  watch_resources(const Config&);
  void                                  read_foreground_measurement_locked();
};
}  // namespace livehd::placement::gui
