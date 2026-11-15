// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <string>

#include "gui_session.hpp"
#include "job_manager.hpp"

namespace livehd::placement::gui {

// The evaluator owns this channel. Only its flow thread may call it. The
// controller runs its own Session, HTTP handlers and command queue. At a
// safe boundary it can call back over the same socket for timing/maps, while
// the evaluator's solver arrays and callbacks are still in scope.
class Controller_link {
public:
  explicit Controller_link(const Config&);
  ~Controller_link();
  Controller_link(const Controller_link&)            = delete;
  Controller_link& operator=(const Controller_link&) = delete;

  std::shared_ptr<const Scene> scene;
  std::string                  url, token;
  int                          port = 0;
  std::string                  call(const std::string& operation, std::string payload = {}, const Map_provider* = nullptr,
                                    const Timing_provider* = nullptr, const Session::Fork_builder* = nullptr, const Legalizer* = nullptr,
                                    const Intent_provider* = nullptr, const Orientation_provider* = nullptr) const;

private:
  const Config& config_;
  int           socket_ = -1, pid_ = -1;
};

// Private entry point of the same executable, before engine initialization.
std::pair<std::string, std::string> receive_message(int fd);
void                                send_message(int fd, const std::string& op, const std::string& data);
int                                 serve_controller(int fd, int admission_fd, Job_manager::Probe probe);
}  // namespace livehd::placement::gui
