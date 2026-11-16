// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Hand-written HTTP/1.1 server for the placement GUI: localhost only, a random
// per-run token in every request, small fixed request limits, no file serving.
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <format>
#include <string_view>
#include <system_error>
#include <thread>

#include "gui_ipc.hpp"
#include "gui_page.hpp"
#include "gui_session.hpp"

namespace livehd::placement::gui {

namespace {
// A session killed (or crashed) leaves its lhd-gui-<pid>.sock behind: a long run of sessions fills /tmp with them.
// Remove the sockets in `dir` of this naming that belong to a process that no longer exists and to this user.
void remove_stale_agent_sockets(const std::filesystem::path& dir) {
  std::error_code ec;
  for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    const auto name = it->path().filename().string();
    if (!name.starts_with("lhd-gui-") || !name.ends_with(".sock")) {
      continue;
    }
    const auto digits = std::string_view(name).substr(8, name.size() - 8 - 5);
    long       pid    = 0;
    const auto [end_of, rc] = std::from_chars(digits.data(), digits.data() + digits.size(), pid);
    if (digits.empty() || rc != std::errc{} || end_of != digits.data() + digits.size() || pid <= 0) {
      continue;
    }
    struct stat st{};
    if (::lstat(it->path().c_str(), &st) != 0 || !S_ISSOCK(st.st_mode) || st.st_uid != ::getuid()) {
      continue;
    }
    if (::kill(pid_t(pid), 0) != 0 && errno == ESRCH) {
      ::unlink(it->path().c_str());
    }
  }
}
}  // namespace

struct Session::Server {
  Session*                 session = nullptr;
  int                      fd      = -1;
  int                      ufd     = -1;  // the agent's Unix-domain socket (local processes of the same user only)
  std::string              upath;
  std::atomic<bool>        stop{false};
  std::atomic<int>         active{0};
  std::thread              accept_thread;
  void                     close_listeners() {
    if (stop.exchange(true)) {
      return;
    }
    if (fd >= 0) {
      ::shutdown(fd, SHUT_RDWR);
      ::close(fd);
    }
    if (ufd >= 0) {
      ::shutdown(ufd, SHUT_RDWR);
      ::close(ufd);
    }
    if (accept_thread.joinable()) {
      accept_thread.join();
    }
    if (!upath.empty()) {
      ::unlink(upath.c_str());
      upath.clear();
    }
  }
  ~Server() {
    close_listeners();
    while (active.load() > 0) {  // handlers poll `stop` every 200 ms at most
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }
};

namespace {
struct Request {
  std::string method, path, query, body;
  bool        ok = false;
};

std::string query_value(const std::string& query, const std::string& key, size_t* start = nullptr) {
  size_t pos = 0;
  while (pos <= query.size()) {
    const auto amp  = query.find('&', pos);
    const auto part = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
    const auto eq   = part.find('=');
    if (eq != std::string::npos && part.substr(0, eq) == key) {
      if (start) {
        *start = pos;
      }
      std::string value;
      for (size_t i = eq + 1; i < part.size(); ++i) {
        if (part[i] == '%' && i + 2 < part.size()) {
          unsigned byte = 0;
          const char* end = part.data() + i + 3;
          const auto parsed = std::from_chars(end - 2, end, byte, 16);
          if (parsed.ec == std::errc{} && parsed.ptr == end) {
            value += static_cast<char>(byte);
            i += 2;
            continue;
          }
        }
        value += part[i] == '+' ? ' ' : part[i];
      }
      return value;
    }
    if (amp == std::string::npos) {
      break;
    }
    pos = amp + 1;
  }
  return "";
}

bool wait_readable(int fd, std::atomic<bool>& stop, int total_ms) {
  for (int waited = 0; waited < total_ms && !stop.load(); waited += 200) {
    pollfd p{fd, POLLIN, 0};
    const int r = ::poll(&p, 1, 200);
    if (r > 0) {
      return true;
    }
    if (r < 0 && errno != EINTR) {
      return false;
    }
  }
  return false;
}

Request read_request(int fd, std::atomic<bool>& stop) {
  Request     req;
  std::string buf;
  char        chunk[4096];
  size_t      header_end = std::string::npos;
  while (header_end == std::string::npos) {
    if (buf.size() > 16384 || !wait_readable(fd, stop, 10000)) {
      return req;
    }
    const auto n = ::recv(fd, chunk, sizeof(chunk), 0);
    if (n <= 0) {
      return req;
    }
    buf.append(chunk, size_t(n));
    header_end = buf.find("\r\n\r\n");
  }
  const std::string head = buf.substr(0, header_end);
  size_t            content = 0;
  {
    std::string lower = head;
    for (auto& c : lower) {
      c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    const auto at = lower.find("content-length:");
    if (at != std::string::npos) {
      content = size_t(std::strtoull(lower.c_str() + at + 15, nullptr, 10));
    }
  }
  if (content > (1U << 20)) {
    return req;
  }
  req.body = buf.substr(header_end + 4);
  while (req.body.size() < content) {
    if (!wait_readable(fd, stop, 10000)) {
      return req;
    }
    const auto n = ::recv(fd, chunk, sizeof(chunk), 0);
    if (n <= 0) {
      return req;
    }
    req.body.append(chunk, size_t(n));
  }
  const auto line_end = head.find("\r\n");
  const auto line     = head.substr(0, line_end);
  const auto sp1      = line.find(' ');
  const auto sp2      = line.find(' ', sp1 == std::string::npos ? 0 : sp1 + 1);
  if (sp1 == std::string::npos || sp2 == std::string::npos) {
    return req;
  }
  req.method  = line.substr(0, sp1);
  auto target = line.substr(sp1 + 1, sp2 - sp1 - 1);
  const auto q = target.find('?');
  if (q != std::string::npos) {
    req.query = target.substr(q + 1);
    target.resize(q);
  }
  req.path = target;
  req.ok   = true;
  return req;
}

void send_all(int fd, std::string_view data) {
  size_t sent = 0;
  while (sent < data.size()) {
#ifdef MSG_NOSIGNAL
    const auto n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#else
    const auto n = ::send(fd, data.data() + sent, data.size() - sent, 0);
#endif
    if (n <= 0) {
      return;
    }
    sent += size_t(n);
  }
}

void respond(int fd, int code, std::string_view type, std::string_view body) {
  const char* text = code == 200 ? "OK" : code == 400 ? "Bad Request" : code == 403 ? "Forbidden" : code == 404 ? "Not Found" : "Error";
  send_all(fd,
           std::format("HTTP/1.1 {} {}\r\nContent-Type: {}\r\nContent-Length: {}\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
                       code,
                       text,
                       type,
                       body.size()));
  send_all(fd, body);
}

void handle(Session& s, const std::string& token, int fd, std::atomic<bool>& stop, bool trusted) {
  const auto req = read_request(fd, stop);
  if (!req.ok) {
    respond(fd, 400, "text/plain", "bad request");
    return;
  }
  if (!trusted && query_value(req.query, "t") != token) {  // the Unix socket is protected by its file permissions instead
    respond(fd, 403, "text/plain", "forbidden");
    return;
  }
  size_t target_pos = 0;
  const auto target = query_value(req.query, "x", &target_pos);
  if (req.method == "GET" && req.path != "/" && req.path != "/index.html" && !target.empty() && target != s.config().target) {
    try {
      // Private evaluators see the same document query without the parent's
      // target selector. The worker only listens on its private Unix socket.
      auto query = req.query;
      const auto end = query.find('&', target_pos);
      query.erase(target_pos, end == query.npos ? end : end + 1 - target_pos);
      const auto body = s.exploration_get(target, req.path + "?" + query);
      respond(fd,
              200,
              req.path.starts_with("/validation/")           ? "application/octet-stream"
              : req.path == "/screenshot.png"                ? "image/png"
              : (req.path == "/frame" || req.path == "/map") ? "application/octet-stream"
                                                             : "application/json",
              body);
    } catch (const std::exception& e) {
      respond(fd, 404, "text/plain", e.what());
    }
    return;
  }
  if (req.method == "GET" && (req.path == "/" || req.path == "/index.html")) {
    respond(fd, 200, "text/html; charset=utf-8", page_html());
  } else if (req.method == "GET" && req.path == "/baseline.snapshot" && trusted && s.config().target != "main") {
    try {
      respond(fd, 200, "application/octet-stream", s.capture_baseline());
    } catch (const std::exception& e) {
      respond(fd, 400, "text/plain", e.what());
    }
  } else if (req.method == "GET" && req.path == "/result.snapshot" && trusted && s.config().target != "main") {
    try {
      respond(fd, 200, "application/octet-stream", s.result_snapshot());
    } catch (const std::exception& e) {
      respond(fd, 404, "text/plain", e.what());
    }
  } else if (req.method == "GET") {
    const auto since = std::strtoull(query_value(req.query, "since").c_str(), nullptr, 10);
    if (req.path == "/state" && since) {
      for (int waited = 0; waited < 20000 && !stop.load() && s.wait_version(since, 500) == since; waited += 500) {
      }
    }
    try {
      const auto body = s.read_document(req.path + "?" + req.query);
      respond(fd,
              200,
              req.path == "/screenshot.png"                  ? "image/png"
              : (req.path == "/frame" || req.path == "/map") ? "application/octet-stream"
                                                             : "application/json",
              body);
    } catch (const std::exception& e) {
      respond(fd, 404, "text/plain", e.what());
    }
  } else if (req.method == "POST" && req.path == "/agent") {
    respond(fd, 200, "application/json", s.agent_call(req.body));
  } else if (req.method == "POST" && req.path == "/cmd") {
    bool accepted;
    const auto reply = s.submit_reply(req.body, &accepted);
    respond(fd, accepted ? 200 : 400, "application/json", accepted ? R"({"ok":true})" : reply);
  } else {
    respond(fd, 404, "text/plain", "not found");
  }
}
}  // namespace

std::string Session::read_document(const std::string& route) const {
  const auto at    = route.find('?');
  const auto path  = route.substr(0, at);
  const auto query = at == route.npos ? std::string{} : route.substr(at + 1);
  if (path == "/design") {
    const auto cp = query_value(query, "cp"), tl = query_value(query, "tl");
    return this->design_json(cp.empty() ? -1 : std::atoi(cp.c_str()), tl.empty() ? -1 : std::atoi(tl.c_str()));
  } else if (path == "/state") {
    return this->state_json();
  } else if (path == "/frame") {
    return this->frame_binary(nullptr,
                              query_value(query, "q") == "16",
                              std::strtoull(query_value(query, "since").c_str(), nullptr, 10),
                              query_value(query, "cp").empty() ? -1 : std::atoi(query_value(query, "cp").c_str()),
                              query_value(query, "tl").empty() ? -1 : std::atoi(query_value(query, "tl").c_str()));
  } else if (path == "/screenshot.png") {
    Box        view;
    const Box* pv = nullptr;
    const auto x0 = query_value(query, "x0"), y0 = query_value(query, "y0"), x1 = query_value(query, "x1"),
               y1 = query_value(query, "y1");
    if (!x0.empty() && !y0.empty() && !x1.empty() && !y1.empty()) {
      view = {std::atof(x0.c_str()), std::atof(y0.c_str()), std::atof(x1.c_str()), std::atof(y1.c_str())};
      pv   = &view;
    }
    const auto png = this->screenshot_png(std::atoi(query_value(query, "w").c_str()),
                                          std::atoi(query_value(query, "h").c_str()),
                                          query_value(query, "layers"),
                                          pv);
    if (png.empty()) {
      throw std::runtime_error("no frame yet");
    } else {
      return png;
    }
  } else if (path == "/query") {
    Box        box;
    const Box* pb = nullptr;
    const auto x0 = query_value(query, "x0"), y0 = query_value(query, "y0"), x1 = query_value(query, "x1"),
               y1 = query_value(query, "y1");
    if (!x0.empty() && !y0.empty() && !x1.empty() && !y1.empty()) {
      box = {std::atof(x0.c_str()), std::atof(y0.c_str()), std::atof(x1.c_str()), std::atof(y1.c_str())};
      pb  = &box;
    }
    const int limit = std::clamp(std::atoi(query_value(query, "limit").c_str()), 1, 1000);
    return this->query_json(query_value(query, "name"), pb, query_value(query, "limit").empty() ? 100 : limit);
  } else if (path == "/regions") {
    return this->regions_json();
  } else if (path == "/map") {
    return this->map_binary(query_value(query, "kind"), std::strtoull(query_value(query, "frame").c_str(), nullptr, 10));
  } else if (path == "/highlights") {
    return this->highlights_json();
  } else if (path == "/selection") {
    return this->selection_json();
  } else if (path == "/timeline") {
    return this->timeline_json();
  } else if (path == "/options") {
    return this->options_json();
  } else if (path == "/node") {
    return this->node_json(std::atoi(query_value(query, "i").c_str()));
  }
  throw std::runtime_error("unknown placement document");
}

bool Session::start(std::string* error) {
  const auto fail = [&](const std::string& why) {
    if (error) {
      *error = why;
    }
    return false;
  };
  if (!cfg_.controller_executable.empty()) {
    try {
      controller_   = std::make_shared<Controller_link>(cfg_);
      url_          = controller_->url;
      token_        = controller_->token;
      port_         = controller_->port;
      agent_socket_ = cfg_.agent_socket;
      return true;
    } catch (const std::exception& e) {
      return fail(e.what());
    }
  }
  std::erase_if(retired_servers_, [](const auto& s) { return !s->active.load(); });
  auto server     = std::make_shared<Server>();
  server->session = this;
  if (cfg_.target == "main") {
    server->fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server->fd < 0) {
      return fail("socket() failed");
    }
    const int one = 1;
    ::setsockopt(server->fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_NOSIGPIPE
  ::setsockopt(server->fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
  sockaddr_in addr{};
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  // The configured port, or the one this session was already serving on (a restart that could not re-execute).
  const int port       = port_ != 0 ? port_ : cfg_.port;
  addr.sin_port        = htons(uint16_t(port));
  if (::bind(server->fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    return fail(std::format("cannot bind 127.0.0.1:{}", port));
  }
  socklen_t len = sizeof(addr);
  ::getsockname(server->fd, reinterpret_cast<sockaddr*>(&addr), &len);
  if (::listen(server->fd, 16) != 0) {
    return fail("listen() failed");
  }
  port_ = ntohs(addr.sin_port);
  ::fcntl(server->fd, F_SETFD, FD_CLOEXEC);  // a restart re-executes the process and rebinds the same port
  url_ = std::format("http://127.0.0.1:{}/?t={}", ntohs(addr.sin_port), token_);
  }
  if (!cfg_.agent_socket.empty()) {
    sockaddr_un ua{};
    ua.sun_family = AF_UNIX;
    if (cfg_.agent_socket.size() >= sizeof(ua.sun_path)) {
      return fail("agent socket path is too long: " + cfg_.agent_socket);
    }
    std::strncpy(ua.sun_path, cfg_.agent_socket.c_str(), sizeof(ua.sun_path) - 1);
    remove_stale_agent_sockets(std::filesystem::path(cfg_.agent_socket).parent_path());
    ::unlink(cfg_.agent_socket.c_str());  // a stale socket from a previous run (or the process before a restart)
    server->ufd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (server->ufd < 0) {
      return fail("socket(AF_UNIX) failed");
    }
    const mode_t old = ::umask(0177);  // created 0600: only the user running lhd can talk to it
    const int    rc  = ::bind(server->ufd, reinterpret_cast<sockaddr*>(&ua), sizeof(ua));
    ::umask(old);
    if (rc != 0 || ::listen(server->ufd, 8) != 0) {
      return fail("cannot listen on the agent socket " + cfg_.agent_socket);
    }
    ::chmod(cfg_.agent_socket.c_str(), 0600);
    ::fcntl(server->ufd, F_SETFD, FD_CLOEXEC);
    server->upath = cfg_.agent_socket;
    agent_socket_ = cfg_.agent_socket;
  }
  auto* raw = server.get();
  Session* self = this;
  raw->accept_thread = std::thread([raw, self] {
    while (!raw->stop.load()) {
      pollfd p[2] = {{raw->fd, POLLIN, 0}, {raw->ufd, POLLIN, 0}};
      const int r = ::poll(p, raw->ufd >= 0 ? 2 : 1, 200);
      if (r <= 0) {
        continue;
      }
      const bool local  = raw->ufd >= 0 && (p[1].revents & POLLIN) && !(p[0].revents & POLLIN);
      const int  client = ::accept(local ? raw->ufd : raw->fd, nullptr, nullptr);
      if (client < 0) {
        continue;
      }
#ifdef SO_NOSIGPIPE
      const int on = 1;
      ::setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
      ::fcntl(client, F_SETFD, FD_CLOEXEC);
      raw->active.fetch_add(1);
      try {
        std::thread([raw, self, client, local] {
          handle(*self, self->token(), client, raw->stop, local);
          ::close(client);
          raw->active.fetch_sub(1);
        }).detach();
      } catch (const std::system_error&) {
        // No thread for this request (the system refused one): drop the connection, keep serving. The page and
        // the agent retry; an exception leaving this thread would end the whole run.
        ::close(client);
        raw->active.fetch_sub(1);
      }
    }
  });
  server_ = std::move(server);
  return true;
}

void Session::suspend_listeners() {
  // A restart callback holds the state lock. Existing handlers may need that
  // lock to finish, so keep their Server alive until the callback returns.
  if (server_) {
    server_->close_listeners();
    retired_servers_.push_back(std::move(server_));
  }
}
void Session::stop_serving() {
  controller_.reset();
  server_.reset();
}

void Session::shutdown() {
  exit_.store(true);
  cv_.notify_all();
  crit_cv_.notify_all();
  updates_.notify_all();
}

bool make_video(const std::string& frames_dir, const std::string& out) {
  if (std::system("command -v ffmpeg >/dev/null 2>&1") != 0) {
    return false;
  }
  const std::string cmd = "ffmpeg -y -loglevel error -framerate 15 -i '" + frames_dir + "/frame_%05d.png' -pix_fmt yuv420p '" + out + "' >/dev/null 2>&1";
  return std::system(cmd.c_str()) == 0;
}

void open_browser(const std::string& url) {
#if defined(__APPLE__)
  const std::string cmd = "open '" + url + "' >/dev/null 2>&1 &";
#else
  const std::string cmd = "xdg-open '" + url + "' >/dev/null 2>&1 &";
#endif
  if (std::system(cmd.c_str()) != 0) {
    // best effort: the URL was already printed
  }
}

}  // namespace livehd::placement::gui
