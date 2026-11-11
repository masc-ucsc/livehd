// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Hand-written HTTP/1.1 server for the placement GUI: localhost only, a random
// per-run token in every request, small fixed request limits, no file serving.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <format>
#include <string_view>
#include <thread>

#include "gui_page.hpp"
#include "gui_session.hpp"

namespace livehd::placement::gui {

struct Session::Server {
  Session*                 session = nullptr;
  int                      fd      = -1;
  int                      ufd     = -1;  // the agent's Unix-domain socket (local processes of the same user only)
  std::string              upath;
  std::atomic<bool>        stop{false};
  std::atomic<int>         active{0};
  std::thread              accept_thread;
  ~Server() {
    stop = true;
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
    }
    for (int i = 0; i < 100 && active.load() > 0; ++i) {  // handlers poll `stop` every 200 ms at most
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }
};

namespace {
struct Request {
  std::string method, path, query, body;
  bool        ok = false;
};

std::string query_value(const std::string& query, const std::string& key) {
  size_t pos = 0;
  while (pos <= query.size()) {
    const auto amp  = query.find('&', pos);
    const auto part = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
    const auto eq   = part.find('=');
    if (eq != std::string::npos && part.substr(0, eq) == key) {
      return part.substr(eq + 1);
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
  if (req.method == "GET" && (req.path == "/" || req.path == "/index.html")) {
    respond(fd, 200, "text/html; charset=utf-8", page_html());
  } else if (req.method == "GET" && req.path == "/design") {
    const auto cp = query_value(req.query, "cp"), tl = query_value(req.query, "tl");
    respond(fd, 200, "application/json", s.design_json(cp.empty() ? -1 : std::atoi(cp.c_str()), tl.empty() ? -1 : std::atoi(tl.c_str())));
  } else if (req.method == "GET" && req.path == "/state") {
    const auto since = std::strtoull(query_value(req.query, "since").c_str(), nullptr, 10);
    if (since) {
      for (int waited = 0; waited < 20000 && !stop.load(); waited += 500) {
        if (s.wait_version(since, 500) != since) {
          break;
        }
      }
    }
    respond(fd, 200, "application/json", s.state_json());
  } else if (req.method == "GET" && req.path == "/frame") {
    respond(fd, 200, "application/octet-stream", s.frame_binary(nullptr, query_value(req.query, "q") == "16", std::strtoull(query_value(req.query, "since").c_str(), nullptr, 10),
                                                            query_value(req.query, "cp").empty() ? -1 : std::atoi(query_value(req.query, "cp").c_str()),
                                                            query_value(req.query, "tl").empty() ? -1 : std::atoi(query_value(req.query, "tl").c_str())));
  } else if (req.method == "GET" && req.path == "/screenshot.png") {
    Box        view;
    const Box* pv = nullptr;
    const auto x0 = query_value(req.query, "x0"), y0 = query_value(req.query, "y0"), x1 = query_value(req.query, "x1"),
               y1 = query_value(req.query, "y1");
    if (!x0.empty() && !y0.empty() && !x1.empty() && !y1.empty()) {
      view = {std::atof(x0.c_str()), std::atof(y0.c_str()), std::atof(x1.c_str()), std::atof(y1.c_str())};
      pv   = &view;
    }
    const auto png = s.screenshot_png(std::atoi(query_value(req.query, "w").c_str()),
                                      std::atoi(query_value(req.query, "h").c_str()),
                                      query_value(req.query, "layers"),
                                      pv);
    if (png.empty()) {
      respond(fd, 404, "text/plain", "no frame yet");
    } else {
      respond(fd, 200, "image/png", png);
    }
  } else if (req.method == "GET" && req.path == "/query") {
    Box        box;
    const Box* pb = nullptr;
    const auto x0 = query_value(req.query, "x0"), y0 = query_value(req.query, "y0"), x1 = query_value(req.query, "x1"),
               y1 = query_value(req.query, "y1");
    if (!x0.empty() && !y0.empty() && !x1.empty() && !y1.empty()) {
      box = {std::atof(x0.c_str()), std::atof(y0.c_str()), std::atof(x1.c_str()), std::atof(y1.c_str())};
      pb  = &box;
    }
    const int limit = std::clamp(std::atoi(query_value(req.query, "limit").c_str()), 1, 1000);
    respond(fd, 200, "application/json", s.query_json(query_value(req.query, "name"), pb, query_value(req.query, "limit").empty() ? 100 : limit));
  } else if (req.method == "GET" && req.path == "/regions") {
    respond(fd, 200, "application/json", s.regions_json());
  } else if (req.method == "GET" && req.path == "/map") {
    respond(fd, 200, "application/octet-stream", s.map_binary());
  } else if (req.method == "POST" && req.path == "/agent") {
    respond(fd, 200, "application/json", s.agent_call(req.body));
  } else if (req.method == "GET" && req.path == "/highlights") {
    respond(fd, 200, "application/json", s.highlights_json());
  } else if (req.method == "GET" && req.path == "/selection") {
    respond(fd, 200, "application/json", s.selection_json());
  } else if (req.method == "GET" && req.path == "/timeline") {
    respond(fd, 200, "application/json", s.timeline_json());
  } else if (req.method == "GET" && req.path == "/options") {
    respond(fd, 200, "application/json", s.options_json());
  } else if (req.method == "GET" && req.path == "/node") {
    respond(fd, 200, "application/json", s.node_json(std::atoi(query_value(req.query, "i").c_str())));
  } else if (req.method == "POST" && req.path == "/cmd") {
    const auto err = s.submit(req.body);
    if (err.empty()) {
      respond(fd, 200, "application/json", R"({"ok":true})");
    } else {
      respond(fd, 400, "application/json", std::format(R"({{"ok":false,"error":"{}"}})", err));
    }
  } else {
    respond(fd, 404, "text/plain", "not found");
  }
}
}  // namespace

bool Session::start(std::string* error) {
  const auto fail = [&](const std::string& why) {
    if (error) {
      *error = why;
    }
    return false;
  };
  auto server     = std::make_shared<Server>();
  server->session = this;
  server->fd      = ::socket(AF_INET, SOCK_STREAM, 0);
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
  addr.sin_port        = htons(uint16_t(cfg_.port));
  if (::bind(server->fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    return fail(std::format("cannot bind 127.0.0.1:{}", cfg_.port));
  }
  socklen_t len = sizeof(addr);
  ::getsockname(server->fd, reinterpret_cast<sockaddr*>(&addr), &len);
  if (::listen(server->fd, 16) != 0) {
    return fail("listen() failed");
  }
  port_ = ntohs(addr.sin_port);
  ::fcntl(server->fd, F_SETFD, FD_CLOEXEC);  // a restart re-executes the process and rebinds the same port
  url_ = std::format("http://127.0.0.1:{}/?t={}", ntohs(addr.sin_port), token_);
  if (!cfg_.agent_socket.empty()) {
    sockaddr_un ua{};
    ua.sun_family = AF_UNIX;
    if (cfg_.agent_socket.size() >= sizeof(ua.sun_path)) {
      return fail("agent socket path is too long: " + cfg_.agent_socket);
    }
    std::strncpy(ua.sun_path, cfg_.agent_socket.c_str(), sizeof(ua.sun_path) - 1);
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
      std::thread([raw, self, client, local] {
        handle(*self, self->token(), client, raw->stop, local);
        ::close(client);
        raw->active.fetch_sub(1);
      }).detach();
    }
  });
  server_ = std::move(server);
  return true;
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
