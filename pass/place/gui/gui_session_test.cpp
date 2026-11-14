// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "gtest/gtest.h"
#include "gui_session.hpp"

using namespace livehd::placement::gui;

namespace {
int port_of(const std::string& url) { return std::stoi(url.substr(url.find("127.0.0.1:") + 10)); }

// One request, one response (the server closes the connection).
std::string http(int port, const std::string& method, const std::string& target, const std::string& body = "") {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{};
  a.sin_family      = AF_INET;
  a.sin_port        = htons(uint16_t(port));
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  EXPECT_EQ(::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)), 0);
  const std::string req = method + " " + target + " HTTP/1.1\r\nHost: x\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
  EXPECT_EQ(::send(fd, req.data(), req.size(), 0), ssize_t(req.size()));
  std::string out;
  char        buf[4096];
  for (;;) {
    const auto n = ::recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) {
      break;
    }
    out.append(buf, size_t(n));
  }
  ::close(fd);
  return out;
}
std::string body_of(const std::string& r) { return r.substr(r.find("\r\n\r\n") + 4); }

std::shared_ptr<Scene> scene() {
  auto s        = std::make_shared<Scene>();
  s->phase      = "final";
  s->die        = {0, 0, 1000, 1000};
  s->dbu_per_um = 1000;
  s->nodes      = {{"c0", Kind::cell, true, 10, 10}, {"c1", Kind::cell, true, 10, 10}, {"ram", Kind::macro, true, 100, 100}, {"a", Kind::io, false, 0, 0}};
  s->build_index();
  return s;
}
}  // namespace

TEST(GuiSession, ServesPageStateAndRejectsMissingToken) {
  Session s(Config{.open_browser = false});
  std::string err;
  ASSERT_TRUE(s.start(&err)) << err;
  const int port = port_of(s.url());
  const auto t   = "t=" + s.token();
  EXPECT_NE(http(port, "GET", "/", "").find("403"), std::string::npos);
  EXPECT_NE(http(port, "GET", "/?t=wrong", "").find("403"), std::string::npos);
  const auto page = http(port, "GET", "/?" + t, "");
  EXPECT_NE(page.find("200 OK"), std::string::npos);
  EXPECT_NE(page.find("LiveHD placement"), std::string::npos);
  EXPECT_NE(body_of(http(port, "GET", "/state?" + t)).find("\"paused\":true"), std::string::npos);
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"nope"})").find("400"), std::string::npos);
}

TEST(GuiSession, StopServingAnswersWhatIsInFlightAndStartServesAgainOnTheSamePort) {
  // A restart stops the server before it re-executes the process: a command sent just before gets its whole
  // answer, nothing is taken after, and the same port serves again (the process after the exec, or this one when
  // the exec fails).
  Session s(Config{.open_browser = false});
  ASSERT_TRUE(s.start());
  const int  port = port_of(s.url());
  const auto t    = "t=" + s.token();
  std::vector<std::string> answers(16);
  std::vector<std::thread> clients;
  for (auto& answer : answers) {
    clients.emplace_back([&] { answer = http(port, "POST", "/cmd?" + t, R"({"cmd":"pause"})"); });
  }
  for (auto& client : clients) {
    client.join();
  }
  s.stop_serving();
  for (const auto& answer : answers) {  // whole answers: the status line and all of the body
    ASSERT_NE(answer.find("200 OK"), std::string::npos);
    EXPECT_EQ(body_of(answer), R"({"ok":true})");
  }
  {  // stopped: the port refuses a connection
    const int   fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family      = AF_INET;
    a.sin_port        = htons(uint16_t(port));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    EXPECT_NE(::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)), 0);
    ::close(fd);
  }
  s.stop_serving();  // a second stop is nothing
  std::string err;
  ASSERT_TRUE(s.start(&err)) << err;
  EXPECT_EQ(port_of(s.url()), port);
  EXPECT_NE(http(port, "GET", "/state?" + t).find("200 OK"), std::string::npos);
}

TEST(GuiSession, QuantizedFramesCoverTheDieInFourBytesPerNode) {
  Session s(Config{.open_browser = false});
  ASSERT_TRUE(s.start());
  const int port = port_of(s.url());
  const auto t   = "t=" + s.token();
  const double x[4] = {0, 500, 1000, 250}, y[4] = {0, 500, 1000, 750};  // die 0..1000
  s.begin_phase(scene(), x, y, 4);
  const auto q = body_of(http(port, "GET", "/frame?q=16&" + t));
  ASSERT_EQ(q.size(), 32u + 4 * 4 + 4);
  EXPECT_EQ(q.substr(0, 4), "LHQ1");
  std::uint16_t qx[4], qy[4];
  std::memcpy(qx, q.data() + 32, 8);
  std::memcpy(qy, q.data() + 32 + 8, 8);
  EXPECT_EQ(qx[0], 0);
  EXPECT_EQ(qx[2], 65535);
  EXPECT_NEAR(qx[1], 32768, 1);
  EXPECT_NEAR(qy[3], 49151, 1);
}

TEST(GuiSession, HashIndexNamesResolveToNodeNames) {
  Session s(Config{.open_browser = false, .start_paused = false});
  ASSERT_TRUE(s.start());
  const int port = port_of(s.url());
  const auto t   = "t=" + s.token();
  const double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  s.begin_phase(scene(), x, y, 4);
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"move","nodes":["#1"],"x":[50],"y":[60],"final":true})").find("200 OK"), std::string::npos);
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"move","nodes":["#99"],"x":[50],"y":[60]})").find("400"), std::string::npos);
  const auto edits = s.poll(5, x, y, 4, {});
  ASSERT_EQ(edits.pins.size(), 1u);
  EXPECT_EQ(edits.pins[0].node, 1);
  EXPECT_NE(body_of(http(port, "GET", "/query?name=c1&" + t)).find("\"pinned\":true"), std::string::npos);
}

TEST(GuiSession, AgentQueryAndScreenshot) {
  Session s(Config{.open_browser = false});
  ASSERT_TRUE(s.start());
  const int port = port_of(s.url());
  const auto t   = "t=" + s.token();
  const double x[4] = {1, 200, 300, 400}, y[4] = {5, 600, 700, 800};
  s.begin_phase(scene(), x, y, 4);
  const auto one = body_of(http(port, "GET", "/query?name=ram&" + t));
  EXPECT_NE(one.find("\"kind\":\"macro\""), std::string::npos);
  EXPECT_NE(one.find("\"x\":300"), std::string::npos);
  const auto box = body_of(http(port, "GET", "/query?x0=0&y0=0&x1=250&y1=650&" + t));
  EXPECT_NE(box.find("\"count\":2"), std::string::npos);  // c0 and c1
  EXPECT_NE(body_of(http(port, "GET", "/query?name=nope&" + t)).find("error"), std::string::npos);
  const auto png = body_of(http(port, "GET", "/screenshot.png?w=120&h=90&" + t));
  ASSERT_GT(png.size(), 100u);
  EXPECT_EQ(png.substr(1, 3), "PNG");
}

TEST(GuiSession, StartsPausedAndRunReleasesGate) {
  Session s(Config{.open_browser = false});
  ASSERT_TRUE(s.start());
  const int port = port_of(s.url());
  const auto t   = "t=" + s.token();
  const double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  s.begin_phase(scene(), x, y, 4);
  std::atomic<bool> through{false};
  std::thread flow([&] {
    s.gate("final", x, y, 4, {});
    through = true;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  EXPECT_FALSE(through.load());  // paused at the first gate until the user presses run
  EXPECT_NE(body_of(http(port, "GET", "/state?" + t)).find("\"state\":\"gate\""), std::string::npos);
  EXPECT_NE(body_of(http(port, "GET", "/design?" + t)).find("\"kinds\":\"0019\""), std::string::npos);
  const auto frame = body_of(http(port, "GET", "/frame?" + t));
  ASSERT_EQ(frame.size(), 16u + 4 * 8 + 4);  // + the (empty) centroid section
  EXPECT_EQ(frame.substr(0, 4), "LHF1");
  EXPECT_EQ(http(port, "POST", "/cmd?" + t, R"({"cmd":"run"})").find("200 OK") != std::string::npos, true);
  flow.join();
  EXPECT_TRUE(through.load());
}

TEST(GuiSession, MovePinsByNameApplyAtPollAndTraceKeepsOnlyFinalMoves) {
  const auto trace = std::filesystem::temp_directory_path() / ("gui_trace_" + std::to_string(::getpid()) + ".jsonl");
  Session    s(Config{.open_browser = false, .trace_path = trace.string(), .start_paused = false});
  ASSERT_TRUE(s.start());
  const int    port = port_of(s.url());
  const auto   t    = "t=" + s.token();
  const double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  s.begin_phase(scene(), x, y, 4);
  EXPECT_EQ(http(port, "POST", "/cmd?" + t, R"({"cmd":"move","nodes":["c1"],"x":[500],"y":[600],"final":false})").find("200 OK") != std::string::npos, true);
  EXPECT_EQ(http(port, "POST", "/cmd?" + t, R"({"cmd":"move","nodes":["c1","ram"],"x":[510,20],"y":[610,30],"final":true})").find("200 OK") != std::string::npos, true);
  EXPECT_EQ(http(port, "POST", "/cmd?" + t, R"({"cmd":"move","nodes":["a"],"x":[1],"y":[1]})").find("200 OK") != std::string::npos, true);  // IO: ignored
  const auto edits = s.poll(10, x, y, 4, {});
  ASSERT_EQ(edits.pins.size(), 2u);
  for (const auto& p : edits.pins) {
    if (p.node == 1) {
      EXPECT_EQ(p.x, 510);
      EXPECT_EQ(p.y, 610);
    } else {
      EXPECT_EQ(p.node, 2);
      EXPECT_EQ(p.x, 20);
    }
  }
  EXPECT_TRUE(s.poll(20, x, y, 4, {}).empty());  // already applied
  EXPECT_NE(body_of(http(port, "GET", "/state?" + t)).find("\"pins\":[1,2]"), std::string::npos);
  EXPECT_EQ(http(port, "POST", "/cmd?" + t, R"({"cmd":"unpin","nodes":["c1"]})").find("200 OK") != std::string::npos, true);
  const auto released = s.poll(30, x, y, 4, {});
  ASSERT_EQ(released.releases.size(), 1u);
  EXPECT_EQ(released.releases[0], 1);
  // A later phase gets every surviving pin again, by name.
  s.begin_phase(scene(), x, y, 4);
  const auto again = s.poll(10, x, y, 4, {});
  ASSERT_EQ(again.pins.size(), 1u);
  EXPECT_EQ(again.pins[0].node, 2);
  std::ifstream f(trace);
  std::stringstream ss;
  ss << f.rdbuf();
  const auto text = ss.str();
  EXPECT_EQ(std::count(text.begin(), text.end(), '\n'), 3);  // final move, io move, unpin; the drag step is not traced
  EXPECT_NE(text.find(R"("cmd":"unpin")"), std::string::npos);
  EXPECT_NE(text.find(R"("iter":20)"), std::string::npos);  // applied at the poll that follows iteration 20
  std::filesystem::remove(trace);
}

TEST(GuiSession, AMacroPinnedInTheMacroPhaseIsNotSentToThePhaseWhereItIsFixed) {
  Session s(Config{.open_browser = false, .start_paused = false});
  ASSERT_TRUE(s.start());
  const int    port = port_of(s.url());
  const auto   t    = "t=" + s.token();
  const double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  s.begin_phase(scene(), x, y, 4);
  EXPECT_EQ(http(port, "POST", "/cmd?" + t, R"({"cmd":"move","nodes":["c1","ram"],"x":[510,20],"y":[610,30],"final":true})").find("200 OK") != std::string::npos, true);
  ASSERT_EQ(s.poll(10, x, y, 4, {}).pins.size(), 2u);
  // The final phase: the macro is a fixed obstacle now (the solver rejects a pin on it), the cell is still held.
  auto fixed = scene();
  fixed->nodes[2].movable = false;
  s.begin_phase(fixed, x, y, 4);
  const auto all = s.all_pins();
  ASSERT_EQ(all.pins.size(), 1u);
  EXPECT_EQ(all.pins[0].node, 1);
  const auto again = s.poll(10, x, y, 4, {});
  ASSERT_EQ(again.pins.size(), 1u);
  EXPECT_EQ(again.pins[0].node, 1);
  // A release of the macro is not an index the solver can take either.
  EXPECT_EQ(http(port, "POST", "/cmd?" + t, R"({"cmd":"unpin","nodes":["ram"]})").find("200 OK") != std::string::npos, true);
  EXPECT_TRUE(s.poll(20, x, y, 4, {}).releases.empty());
}

TEST(GuiSession, StageSwitchOffBlocksAndTogglingOnReleases) {
  Session s(Config{.open_browser = false, .start_paused = false});
  ASSERT_TRUE(s.start());
  const int port = port_of(s.url());
  const auto t   = "t=" + s.token();
  const double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  s.set_stages({{"fp", true, true}, {"final", false, true}});
  s.begin_phase(scene(), x, y, 4);
  s.gate("fp", x, y, 4, {});  // on: passes
  std::atomic<bool> through{false};
  std::thread flow([&] {
    s.gate("final", x, y, 4, {});
    through = true;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  EXPECT_FALSE(through.load());
  EXPECT_NE(body_of(http(port, "GET", "/state?" + t)).find("\"gate\":\"final\""), std::string::npos);
  http(port, "POST", "/cmd?" + t, R"({"cmd":"stage","name":"final","on":true})");
  flow.join();
  EXPECT_TRUE(through.load());
}

TEST(GuiSession, FinishUnblocksEverything) {
  Session s(Config{.open_browser = false});
  ASSERT_TRUE(s.start());
  const int port = port_of(s.url());
  const auto t   = "t=" + s.token();
  const double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  s.begin_phase(scene(), x, y, 4);
  std::thread flow([&] {
    s.poll(10, x, y, 4, {});
    s.wait_finish();
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  http(port, "POST", "/cmd?" + t, R"({"cmd":"finish"})");
  flow.join();
}

TEST(GuiSession, RunAtGpEndExtendsThenPausesAgainAndContinueMovesOn) {
  Session s(Config{.open_browser = false, .min_iters = 20, .start_paused = false});
  ASSERT_TRUE(s.start());
  const int port = port_of(s.url());
  const auto t   = "t=" + s.token();
  const double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  s.set_stages({{"final", true, true}, {"legalize", false, true}});  // stop before legalize
  s.begin_phase(scene(), x, y, 4);
  std::vector<int> extends;
  std::thread flow([&] {
    for (int round = 0; round < 3; ++round) {
      extends.push_back(s.gp_end(x, y, 4, {}).extend);
    }
  });
  const auto at_end = [&] {
    for (int i = 0; i < 200; ++i) {
      if (body_of(http(port, "GET", "/state?" + t)).find("\"gate\":\"gp_end\"") != std::string::npos) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
  };
  ASSERT_TRUE(at_end());
  http(port, "POST", "/cmd?" + t, R"({"cmd":"run"})");  // +min_iters, then the session stops at the next end again
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  ASSERT_TRUE(at_end());
  http(port, "POST", "/cmd?" + t, R"({"cmd":"run"})");
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  ASSERT_TRUE(at_end());
  http(port, "POST", "/cmd?" + t, R"({"cmd":"continue"})");
  flow.join();
  EXPECT_EQ(extends, (std::vector<int>{20, 20, 0}));
}

TEST(GuiSession, DeltaFramesCarryOnlyTheChangedNodes) {
  Session s(Config{.open_browser = false, .start_paused = false});
  ASSERT_TRUE(s.start());
  const int port = port_of(s.url());
  const auto t   = "t=" + s.token();
  const double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  s.begin_phase(scene(), x, y, 4);
  s.poll(5, x, y, 4, {});
  std::uint64_t base = 0;
  const auto    full = s.frame_binary(&base);
  ASSERT_GT(base, 0u);
  double x2[4] = {1, 2, 3, 4};
  x2[1]        = 99;  // one node moves
  s.poll(10, x2, y, 4, {});
  // 1 of 4 nodes changed: 12 bytes beats 32, so the server answers with a delta
  const auto delta = body_of(http(port, "GET", "/frame?since=" + std::to_string(base) + "&" + t));
  ASSERT_EQ(delta.substr(0, 4), "LHD1");
  std::uint32_t m = 0;
  std::memcpy(&m, delta.data() + 24, 4);
  EXPECT_EQ(m, 1u);
  std::uint32_t idx = 0;
  float         nx = 0;
  std::memcpy(&idx, delta.data() + 32, 4);
  std::memcpy(&nx, delta.data() + 32 + 4, 4);
  EXPECT_EQ(idx, 1u);
  EXPECT_EQ(nx, 99.f);
  // an unknown base falls back to the full frame
  EXPECT_EQ(body_of(http(port, "GET", "/frame?since=987654&" + t)).substr(0, 4), "LHF1");
}

// The page-facing cost of a 1M-node design: publishing a frame, packing 16-bit and delta frames. Generous bounds so the
// test is not flaky; the numbers are printed (bazel test -c opt --test_output=all).
TEST(GuiSession, MillionNodeFrameCosts) {
  constexpr size_t n = 1'000'000;
  auto             sc = std::make_shared<Scene>();
  sc->phase           = "final";
  sc->die             = {0, 0, 1e7, 1e7};
  sc->nodes.resize(n);
  for (size_t i = 0; i < n; ++i) {
    sc->nodes[i].name = "c" + std::to_string(i);
    sc->nodes[i].w = sc->nodes[i].h = 1000;
  }
  sc->build_index();
  std::vector<double> x(n), y(n);
  for (size_t i = 0; i < n; ++i) {
    x[i] = double((i * 7919) % 9'000'000);
    y[i] = double((i * 104729) % 9'000'000);
  }
  Session s(Config{.open_browser = false, .start_paused = false});
  const auto ms = [](auto from) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count(); };
  auto       t0 = std::chrono::steady_clock::now();
  s.begin_phase(sc, x.data(), y.data(), n);
  const double begin_ms = ms(t0);
  t0 = std::chrono::steady_clock::now();
  s.poll(5, x.data(), y.data(), n, {});
  const double poll_ms = ms(t0);
  t0                      = std::chrono::steady_clock::now();
  const auto   design     = s.design_json();
  const double design_ms  = ms(t0);
  std::uint64_t base = 0;
  t0                 = std::chrono::steady_clock::now();
  const auto full    = s.frame_binary(&base, false);
  const double full_ms = ms(t0);
  t0                   = std::chrono::steady_clock::now();
  const auto q16       = s.frame_binary(nullptr, true);
  const double q16_ms  = ms(t0);
  for (size_t i = 0; i < n; i += 100) {  // 1% of the nodes move
    x[i] += 10;
  }
  s.poll(10, x.data(), y.data(), n, {});
  t0                   = std::chrono::steady_clock::now();
  const auto delta     = s.frame_binary(nullptr, false, base);
  const double delta_ms = ms(t0);
  std::printf("1M nodes: design.json %.1f ms (%zu B), begin_phase %.1f ms, poll(publish) %.1f ms, full %.1f ms (%zu B), 16-bit %.1f ms (%zu B), delta %.1f ms (%zu B)\n",
              design_ms, design.size(), begin_ms, poll_ms, full_ms, full.size(), q16_ms, q16.size(), delta_ms, delta.size());
  EXPECT_EQ(full.size(), 16 + n * 8 + 4);
  EXPECT_EQ(q16.size(), 32 + n * 4 + 4);
  EXPECT_EQ(delta.substr(0, 4), "LHD1");
  EXPECT_LT(delta.size(), q16.size() / 10);
  EXPECT_LT(design_ms, 10000);
  EXPECT_LT(poll_ms, 5000);
  EXPECT_LT(full_ms, 5000);
  EXPECT_LT(delta_ms, 5000);
}

TEST(GuiSession, CheckpointsAreRecordedAndReviewable) {
  Session s(Config{.open_browser = false, .start_paused = false});
  ASSERT_TRUE(s.start());
  const int port = port_of(s.url());
  const auto t   = "t=" + s.token();
  double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  s.begin_phase(scene(), x, y, 4);
  s.gate("final", x, y, 4, {});
  x[0] = 100;
  s.poll(5, x, y, 4, {});
  s.gp_end(x, y, 4, {});
  s.gate("legalize", x, y, 4, {});
  const auto state = body_of(http(port, "GET", "/state?" + t));
  EXPECT_NE(state.find(R"("cps":[{"label":"final","nth":1)"), std::string::npos) << state;
  EXPECT_NE(state.find(R"("label":"gp_end","nth":1)"), std::string::npos);
  EXPECT_NE(state.find(R"("label":"legalize","nth":1)"), std::string::npos);
  EXPECT_NE(state.find(R"("epoch":")"), std::string::npos);
  // checkpoint 0 (before the placer moved cell 0) and checkpoint 2 (after) hold their own frames
  const auto before = body_of(http(port, "GET", "/frame?cp=0&" + t));
  const auto after  = body_of(http(port, "GET", "/frame?cp=2&" + t));
  float      b = 0, a = 0;
  std::memcpy(&b, before.data() + 16, 4);
  std::memcpy(&a, after.data() + 16, 4);
  EXPECT_EQ(b, 1.f);
  EXPECT_EQ(a, 100.f);
  EXPECT_NE(body_of(http(port, "GET", "/design?cp=1&" + t)).find("\"n\":4"), std::string::npos);
  EXPECT_NE(body_of(http(port, "GET", "/design?cp=99&" + t)).find("\"n\":0"), std::string::npos);
}

TEST(GuiSession, RestartCommandPassesTheCheckpointAndTheEditedFlagsToTheHost) {
  Restart_request got;
  int             calls = 0;
  Config          cfg{.open_browser = false, .start_paused = false};
  cfg.restart = [&](const Restart_request& r) {
    got = r;
    ++calls;
    return std::string("host says no");  // returns only when it could not restart
  };
  Session s(cfg);
  ASSERT_TRUE(s.start());
  const int port = port_of(s.url());
  const auto t   = "t=" + s.token();
  const double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  s.begin_phase(scene(), x, y, 4);
  s.gate("final", x, y, 4, {});
  s.poll(5, x, y, 4, {});
  s.gp_end(x, y, 4, {});
  s.set_options_json(R"([{"name":"synth.place.iterations","value":"80"}])");
  EXPECT_NE(body_of(http(port, "GET", "/options?" + t)).find("synth.place.iterations"), std::string::npos);
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"restart","checkpoint":1,"set":{"synth.place.iterations":"60"}})").find("200 OK"), std::string::npos);
  s.poll(10, x, y, 4, {});  // the flow thread applies queued commands at its next poll
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(got.label, "gp_end");
  EXPECT_EQ(got.nth, 1);
  EXPECT_FALSE(got.from_start);
  EXPECT_EQ(got.set.at("synth.place.iterations"), "60");
  EXPECT_NE(body_of(http(port, "GET", "/state?" + t)).find("host says no"), std::string::npos);
  // a bad value type is refused up front
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"restart","set":{"a":1}})").find("400"), std::string::npos);
}

TEST(GuiSession, ResumeReplaysTheTraceThenBecomesInteractiveAtTheCheckpoint) {
  const auto trace = std::filesystem::temp_directory_path() / ("gui_resume_" + std::to_string(::getpid()) + ".jsonl");
  {
    std::ofstream f(trace);
    f << R"({"seq":1,"phase":"final","iter":5,"cmd":"move","nodes":["c1"],"x":[500],"y":[600]})" << "\n";
    f << R"({"seq":2,"phase":"final","iter":9,"cmd":"move","nodes":["c0"],"x":[7],"y":[8]})" << "\n";  // after the checkpoint
  }
  Session s(Config{.open_browser = false, .replay_path = trace.string(), .resume_label = "gp_end", .resume_nth = 1, .replay_limit_seq = 1});
  ASSERT_TRUE(s.start());
  const int port = port_of(s.url());
  const auto t   = "t=" + s.token();
  const double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  EXPECT_TRUE(s.replaying());
  s.begin_phase(scene(), x, y, 4);
  s.gate("final", x, y, 4, {});  // replaying: never blocks
  const auto edits = s.poll(5, x, y, 4, {});
  ASSERT_EQ(edits.pins.size(), 1u);  // the recorded move is applied at its iteration
  EXPECT_EQ(edits.pins[0].node, 1);
  std::atomic<bool> through{false};
  std::thread       flow([&] {
    s.gp_end(x, y, 4, {});
    through = true;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  EXPECT_FALSE(through.load());  // interactive and paused at the resumed checkpoint
  EXPECT_FALSE(s.replaying());
  const auto state = body_of(http(port, "GET", "/state?" + t));
  EXPECT_NE(state.find("replayed to checkpoint gp_end #1"), std::string::npos) << state;
  EXPECT_NE(state.find(R"("gate":"gp_end")"), std::string::npos);
  http(port, "POST", "/cmd?" + t, R"({"cmd":"continue"})");
  flow.join();
  EXPECT_TRUE(through.load());
  std::filesystem::remove(trace);
}

TEST(GuiSession, TimelineKeepsASnapshotPerPublishAndMarksCheckpoints) {
  Session s(Config{.open_browser = false, .history_every = 1, .start_paused = false});
  ASSERT_TRUE(s.start());
  const int port = port_of(s.url());
  const auto t   = "t=" + s.token();
  double x[4] = {100, 200, 300, 400}, y[4] = {500, 600, 700, 800};
  s.begin_phase(scene(), x, y, 4);
  s.gate("final", x, y, 4, {});
  for (int it = 5; it <= 20; it += 5) {
    x[0] += 50;  // the placer moves cell 0 a little at every poll
    s.poll(it, x, y, 4, {});
  }
  Stats end;
  end.iteration = 20;
  s.gp_end(x, y, 4, end);
  const auto tl = body_of(http(port, "GET", "/timeline?" + t));
  // gate (cp 0), polls 5,10,15,20, gp_end republishes iteration 20 and becomes checkpoint 1
  EXPECT_NE(tl.find(R"("phases":["final"])"), std::string::npos) << tl;
  EXPECT_NE(tl.find("[0,0,"), std::string::npos);  // iteration 0 is the first gate checkpoint
  EXPECT_NE(tl.find("[5,-1,"), std::string::npos);
  EXPECT_NE(tl.find("[20,1,"), std::string::npos);  // the end of the GP is checkpoint 1
  EXPECT_NE(body_of(http(port, "GET", "/state?" + t)).find(R"("tl_n":5)"), std::string::npos);
  // snapshots expand to normal frames: entry 1 (iteration 5) has cell 0 at 150 (+- the 16-bit step of a 1000-unit die)
  const auto frame = body_of(http(port, "GET", "/frame?tl=1&" + t));
  ASSERT_EQ(frame.substr(0, 4), "LHF1");
  float x0 = 0;
  std::memcpy(&x0, frame.data() + 16, 4);
  EXPECT_NEAR(x0, 150, 0.1);
  float x_last = 0;
  std::memcpy(&x_last, body_of(http(port, "GET", "/frame?tl=4&" + t)).data() + 16, 4);
  EXPECT_NEAR(x_last, 300, 0.1);
  EXPECT_NE(body_of(http(port, "GET", "/design?tl=2&" + t)).find("\"n\":4"), std::string::npos);
  EXPECT_NE(body_of(http(port, "GET", "/design?tl=99&" + t)).find("\"n\":0"), std::string::npos);
}

TEST(GuiSession, TimelineIsThinnedWithinItsMemoryBudgetButKeepsCheckpoints) {
  Session s(Config{.open_browser = false, .history_every = 1, .history_mb = 1, .start_paused = false});
  constexpr size_t n = 100000;  // 400 KB of 16-bit positions per snapshot: the 1 MB budget holds two
  auto             sc = std::make_shared<Scene>();
  sc->phase           = "final";
  sc->die             = {0, 0, 1000, 1000};
  sc->nodes.resize(n);
  for (size_t i = 0; i < n; ++i) {
    sc->nodes[i].name = "c" + std::to_string(i);
  }
  sc->build_index();
  std::vector<double> x(n, 1), y(n, 2);
  s.begin_phase(sc, x.data(), y.data(), n);
  s.gate("final", x.data(), y.data(), n, {});
  for (int it = 1; it <= 40; ++it) {
    x[0] = it;
    s.poll(it, x.data(), y.data(), n, {});
  }
  Stats end;
  end.iteration = 40;
  s.gp_end(x.data(), y.data(), n, end);
  const auto tl = s.timeline_json();
  EXPECT_NE(tl.find("[0,0,"), std::string::npos);   // the first checkpoint survives thinning
  EXPECT_NE(tl.find("[40,1,"), std::string::npos);  // and so does the last one
  size_t entries = 0;
  for (size_t at = tl.find("],["); at != std::string::npos; at = tl.find("],[", at + 1)) {
    ++entries;
  }
  EXPECT_LT(entries, 30u);  // 42 snapshots were published
}

TEST(GuiSession, RegistersAreFlaggedAndNamedInTheDesignDocument) {
  Session s(Config{.open_browser = false});
  ASSERT_TRUE(s.start());
  const int port = port_of(s.url());
  const auto t   = "t=" + s.token();
  auto       sc  = scene();
  sc->nodes[1].seq = true;  // c1 is a flop
  const double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  s.begin_phase(sc, x, y, 4);
  const auto design = body_of(http(port, "GET", "/design?" + t));
  EXPECT_NE(design.find(R"("seq":"0100")"), std::string::npos) << design;
  EXPECT_NE(design.find(R"("1":"c1")"), std::string::npos);  // a register's name is in the document, a plain cell's is not
  EXPECT_EQ(design.find(R"("0":"c0")"), std::string::npos);
}

TEST(GuiSession, TimelineRecordsEveryFiftyIterationsAndAlwaysAtGatesAndServesStoredFrames) {
  Session s(Config{.open_browser = false, .start_paused = false});  // history_every = 50
  ASSERT_TRUE(s.start());
  const int port = port_of(s.url());
  const auto t   = "t=" + s.token();
  double x[4] = {100, 200, 300, 400}, y[4] = {500, 600, 700, 800};
  s.begin_phase(scene(), x, y, 4);
  s.gate("final", x, y, 4, {});
  for (int it = 10; it <= 200; it += 10) {  // a poll every 10 iterations
    x[0] += 1;
    s.poll(it, x, y, 4, {});
  }
  Stats end;
  end.iteration = 200;
  s.gp_end(x, y, 4, end);
  const auto tl = s.timeline_json();
  // iteration 0 (gate), 50 (first poll >= 50 after it), 100, 150, 200 (the poll, replaced by the gp_end checkpoint)
  EXPECT_NE(tl.find("[0,0,"), std::string::npos) << tl;
  EXPECT_NE(tl.find("[50,-1,"), std::string::npos);
  EXPECT_NE(tl.find("[100,-1,"), std::string::npos);
  EXPECT_NE(tl.find("[200,1,"), std::string::npos);
  EXPECT_EQ(tl.find("[10,-1,"), std::string::npos);
  EXPECT_EQ(tl.find("[60,-1,"), std::string::npos);
  EXPECT_NE(body_of(http(port, "GET", "/state?" + t)).find(R"("tl_n":5)"), std::string::npos);
  // the stored 16-bit snapshot goes out as it is (LHQ1: 4 bytes per node), no expansion
  const auto q = body_of(http(port, "GET", "/frame?tl=2&q=16&" + t));
  ASSERT_EQ(q.substr(0, 4), "LHQ1");
  EXPECT_EQ(q.size(), 32u + 4 * 4 + 4);
  std::uint16_t qx = 0;
  std::memcpy(&qx, q.data() + 32, 2);
  EXPECT_NEAR(qx * 1000.0 / 65535.0, 100 + 10, 0.2);  // iteration 100 = cell 0 at 100 + 10 polls
}

namespace {
std::shared_ptr<Scene> agent_scene() {
  auto s = scene();
  s->nodes[0].master = "sky130_fd_sc_hd__nand2_1";
  s->nodes[1].master = "sky130_fd_sc_hd__dfxtp_1";
  s->nodes[1].seq    = true;
  s->nodes[1].pins   = 4;
  s->nodes[2].master = "sram_16x32";
  s->nodes[2].klass  = "BLOCK";
  s->nodes[2].pins   = 40;
  s->dbu_per_um      = 1000;
  return s;
}
bool ok(const std::string& reply) { return reply.find("\"ok\":true") != std::string::npos; }
}  // namespace

TEST(GuiAgent, FindAndHighlightByRegexScopeAndField) {
  Session s(Config{.open_browser = false, .start_paused = false});
  const double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  s.begin_phase(agent_scene(), x, y, 4);
  EXPECT_NE(s.agent_call(R"({"cmd":"find","regex":"^c","scope":"cells"})").find("\"count\":2"), std::string::npos);
  const auto regs = s.agent_call(R"({"cmd":"find","regex":"^c","scope":"regs"})");
  EXPECT_NE(regs.find("\"count\":1"), std::string::npos) << regs;
  EXPECT_NE(regs.find("\"c1\""), std::string::npos);
  EXPECT_NE(s.agent_call(R"({"cmd":"find","regex":"ram","scope":"macros"})").find("\"count\":1"), std::string::npos);
  EXPECT_NE(s.agent_call(R"({"cmd":"find","regex":"dfxtp","field":"master"})").find("\"c1\""), std::string::npos);
  EXPECT_NE(s.agent_call(R"({"cmd":"find","regex":"(","scope":"all"})").find("bad regex"), std::string::npos);
  EXPECT_NE(s.agent_call(R"({"cmd":"find","regex":"c","scope":"nowhere"})").find("scope must be"), std::string::npos);
  EXPECT_NE(s.agent_call(R"({"cmd":"nope"})").find("unknown cmd"), std::string::npos);
  // highlight: painted by name, the same label repaints, a bad colour is refused, clear removes it
  const auto hl = s.agent_call(R"({"cmd":"highlight","regex":"^c","scope":"regs","color":"#ffd23f","label":"pc"})");
  EXPECT_TRUE(ok(hl)) << hl;
  EXPECT_NE(s.highlights_json().find("\"idx\":[1]"), std::string::npos);
  EXPECT_TRUE(ok(s.agent_call(R"({"cmd":"highlight","regex":"^c","scope":"comb","label":"pc"})")));
  const auto json = s.highlights_json();
  EXPECT_NE(json.find("\"idx\":[0]"), std::string::npos);
  EXPECT_EQ(json.find("\"id\":1,"), std::string::npos);  // the first highlight was replaced
  EXPECT_FALSE(ok(s.agent_call(R"j({"cmd":"highlight","regex":"c","color":"red;alert(1)"})j")));
  EXPECT_TRUE(ok(s.agent_call(R"({"cmd":"clear_highlight","label":"pc"})")));
  EXPECT_NE(s.highlights_json().find("\"h\":[]"), std::string::npos);
}

TEST(GuiAgent, SelectionRoundTripAndDescribe) {
  Session s(Config{.open_browser = false, .start_paused = false});
  const double x[4] = {1000, 2000, 3000, 4000}, y[4] = {5000, 6000, 7000, 8000};
  s.begin_phase(agent_scene(), x, y, 4);
  EXPECT_EQ(s.submit(R"({"cmd":"select","names":["#2"]})"), "");  // the page selects node 2 (the macro)
  const auto sel = s.agent_call(R"({"cmd":"selection"})");
  EXPECT_NE(sel.find("\"count\":1"), std::string::npos) << sel;
  EXPECT_NE(sel.find("\"master\":\"sram_16x32\""), std::string::npos);
  EXPECT_NE(sel.find("\"class\":\"BLOCK\""), std::string::npos);
  EXPECT_NE(sel.find("\"w_um\":0.1"), std::string::npos);  // 100 DBU at 1000 DBU/um
  EXPECT_NE(sel.find("\"pins\":40"), std::string::npos);
  EXPECT_NE(s.selection_json().find("\"idx\":[2]"), std::string::npos);
  // the agent selects registers; the page picks it up through the selection version
  const auto v0 = s.state_json();
  EXPECT_TRUE(ok(s.agent_call(R"({"cmd":"select","regex":".","scope":"regs"})")));
  EXPECT_NE(s.selection_json().find("\"idx\":[1]"), std::string::npos);
  EXPECT_NE(v0, s.state_json());
  const auto one = s.agent_call(R"({"cmd":"describe","name":"c1"})");
  EXPECT_NE(one.find("\"register\":true"), std::string::npos) << one;
  EXPECT_NE(one.find("dfxtp"), std::string::npos);
  EXPECT_FALSE(ok(s.agent_call(R"({"cmd":"describe","name":"zzz"})")));
}

TEST(GuiAgent, CriticalCellsAreAnsweredByTheFlowThreadAndPainted) {
  Session s(Config{.open_browser = false, .start_paused = false});
  double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  s.begin_phase(agent_scene(), x, y, 4);
  std::atomic<bool> stop{false};
  Timing_query      seen;
  std::thread       flow([&] {
    const Timing_provider provider = [&](Timing_query& q) {
      seen        = q;
      q.wns_ps    = -120;
      q.tns_ps    = std::nan("");
      q.critical  = {{"c1", -120.0}, {"c0", -80.0}};
      return true;
    };
    for (int it = 1; !stop.load(); ++it) {
      s.poll(it, x, y, 4, {}, nullptr, &provider);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  });
  const auto reply = s.agent_call(R"({"cmd":"critical","n":2,"margin_ps":50})", 5000);
  stop             = true;
  flow.join();
  EXPECT_TRUE(ok(reply)) << reply;
  EXPECT_TRUE(seen.want_critical);
  EXPECT_EQ(seen.n, 2);
  EXPECT_EQ(seen.margin_ps, 50);
  EXPECT_NE(reply.find(R"({"name":"c1","slack_ps":-120,"master":"sky130_fd_sc_hd__dfxtp_1","register":true})"), std::string::npos) << reply;
  EXPECT_NE(s.highlights_json().find("\"label\":\"critical\""), std::string::npos);
  EXPECT_NE(s.highlights_json().find("\"idx\":[1,0]"), std::string::npos);
  // nobody serves the request (a stage that does not poll): a clear error, not a hang
  Session idle(Config{.open_browser = false, .start_paused = false});
  idle.begin_phase(agent_scene(), x, y, 4);
  EXPECT_NE(idle.agent_call(R"({"cmd":"critical"})", 300).find("timed out"), std::string::npos);
}

TEST(GuiAgent, UnixSocketNeedsNoTokenAndIsOwnerOnly) {
  const std::string path = "/tmp/lhd-gui-test-" + std::to_string(::getpid()) + ".sock";
  Session           s(Config{.open_browser = false, .start_paused = false, .agent_socket = path});
  ASSERT_TRUE(s.start());
  const double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  s.begin_phase(agent_scene(), x, y, 4);
  struct stat st{};
  ASSERT_EQ(::stat(path.c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0600u);
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  sockaddr_un ua{};
  ua.sun_family = AF_UNIX;
  std::strncpy(ua.sun_path, path.c_str(), sizeof(ua.sun_path) - 1);
  ASSERT_EQ(::connect(fd, reinterpret_cast<sockaddr*>(&ua), sizeof(ua)), 0);
  const std::string body = R"({"cmd":"find","regex":"^c","scope":"cells"})";
  const std::string req  = "POST /agent HTTP/1.1\r\nHost: x\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
  ASSERT_EQ(::send(fd, req.data(), req.size(), 0), ssize_t(req.size()));
  std::string reply;
  char        buf[4096];
  for (ssize_t n; (n = ::recv(fd, buf, sizeof(buf), 0)) > 0;) {
    reply.append(buf, size_t(n));
  }
  ::close(fd);
  EXPECT_NE(reply.find("200 OK"), std::string::npos) << reply;
  EXPECT_NE(body_of(reply).find("\"count\":2"), std::string::npos);
  // the TCP side still refuses without the token, even for the same route
  EXPECT_NE(http(port_of(s.url()), "POST", "/agent", body).find("403"), std::string::npos);
}

namespace {
// A 400 x 400 um core inside a 420 x 420 um die (1000 dbu per um), 4000 um2 of cells (2.5% full).
std::shared_ptr<Scene> area_scene() {
  auto s       = scene();
  s->die       = {10000, 10000, 410000, 410000};
  s->frame_die = {0, 0, 420000, 420000};
  s->cell_area = 4e9;
  return s;
}
struct Area_host {
  Restart_request got;
  int             calls = 0;
};
}  // namespace

TEST(GuiSession, AreaMovesOneEdgeAndRestartsAtTheLatestCheckpointWithTheNewFrame) {
  Area_host host;
  Config    cfg{.open_browser = false, .start_paused = false};
  cfg.restart = [&](const Restart_request& r) {
    host.got = r;
    ++host.calls;
    return std::string("host says no");
  };
  Session s(cfg);
  ASSERT_TRUE(s.start());
  const int    port = port_of(s.url());
  const auto   t    = "t=" + s.token();
  const double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  s.begin_phase(area_scene(), x, y, 4);
  s.gate("final", x, y, 4, {});
  // The right edge moves in by 100 um: the die follows by the same amount, nothing moves the origin.
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"area","edge":"right","delta":-100})").find("200 OK"), std::string::npos);
  s.poll(5, x, y, 4, {});
  ASSERT_EQ(host.calls, 1);
  EXPECT_EQ(host.got.label, "final");
  EXPECT_FALSE(host.got.from_start);
  EXPECT_EQ(host.got.set.at("synth.place.core"), "10.000000,10.000000,310.000000,410.000000");
  EXPECT_EQ(host.got.set.at("synth.place.die"), "0.000000,0.000000,320.000000,420.000000");
  EXPECT_EQ(host.got.shift_x, 0);
  EXPECT_NE(body_of(http(port, "GET", "/state?" + t)).find("host says no"), std::string::npos);
  // Growing the left edge past the origin moves the frame (and the recorded actions with it) instead of going negative.
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"area","edge":"left","delta":50})").find("200 OK"), std::string::npos);
  s.poll(6, x, y, 4, {});
  ASSERT_EQ(host.calls, 2);
  EXPECT_EQ(host.got.set.at("synth.place.core"), "10.000000,10.000000,460.000000,410.000000");
  EXPECT_EQ(host.got.set.at("synth.place.die"), "0.000000,0.000000,470.000000,420.000000");
  EXPECT_EQ(host.got.shift_x, 50000);
  EXPECT_EQ(host.got.shift_y, 0);
  // The whole core can be set at once (um); the die keeps its margin.
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"area","box":[10,10,210,310]})").find("200 OK"), std::string::npos);
  s.poll(7, x, y, 4, {});
  ASSERT_EQ(host.calls, 3);
  EXPECT_EQ(host.got.set.at("synth.place.core"), "10.000000,10.000000,210.000000,310.000000");
  EXPECT_EQ(host.got.set.at("synth.place.die"), "0.000000,0.000000,220.000000,320.000000");
  // The page shows the fill the cells give: 4000 um2 in 160000 um2.
  EXPECT_NE(body_of(http(port, "GET", "/design?" + t)).find("\"cell_area\":4e+09"), std::string::npos);
}

TEST(GuiSession, AreaRefusesAFrameTheDesignCannotUseAndNeverRestarts) {
  Area_host host;
  Config    cfg{.open_browser = false, .start_paused = false};
  cfg.restart = [&](const Restart_request& r) {
    host.got = r;
    ++host.calls;
    return std::string{};
  };
  Session s(cfg);
  ASSERT_TRUE(s.start());
  const int    port = port_of(s.url());
  const auto   t    = "t=" + s.token();
  const double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  auto         with_macros = area_scene();
  with_macros->macro_area  = 5e10;  // 500 um x 100 um worth of macros and memory proxies, spacing included
  s.begin_phase(with_macros, x, y, 4);
  s.gate("final", x, y, 4, {});
  const auto note = [&] { return body_of(http(port, "GET", "/state?" + t)); };
  // The macros cannot be packed into a 200 x 100 um core (they need 250% of it): the cells alone would fit (20%), the
  // memory proxies are what the restart's packing dies on ("no space for square memory proxy"), so refuse up front.
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"area","box":[10,10,210,110]})").find("200 OK"), std::string::npos);
  s.poll(4, x, y, 4, {});
  EXPECT_NE(note().find("macros and memories need"), std::string::npos) << note();
  EXPECT_EQ(host.calls, 0);
  // 4000 um2 of cells do not fit a 60 x 60 um core
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"area","box":[10,10,70,70]})").find("200 OK"), std::string::npos);
  s.poll(5, x, y, 4, {});
  EXPECT_NE(note().find("full of cells"), std::string::npos) << note();
  // smaller than the 100 x 100 dbu macro
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"area","box":[10,10,10.05,10.05]})").find("200 OK"), std::string::npos);
  s.poll(6, x, y, 4, {});
  EXPECT_NE(note().find("cannot hold the largest"), std::string::npos) << note();
  // a pinned node outside the new core
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"move","nodes":["c0"],"x":[380000],"y":[100000],"final":true})").find("200 OK"), std::string::npos);
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"area","edge":"right","delta":-100})").find("200 OK"), std::string::npos);
  s.poll(7, x, y, 4, {});
  EXPECT_NE(note().find("pinned c0 would lie outside"), std::string::npos) << note();
  EXPECT_EQ(host.calls, 0);
  // malformed requests are refused up front
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"area","edge":"middle","delta":5})").find("400"), std::string::npos);
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"area","edge":"left"})").find("400"), std::string::npos);
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"area","edge":"left","delta":0})").find("400"), std::string::npos);
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"area","box":[5,5,4,9]})").find("400"), std::string::npos);
}

TEST(GuiSession, ShiftTraceMovesPositionsAndLeavesOtherActionsAlone) {
  const auto trace = std::filesystem::temp_directory_path() / ("gui_shift_" + std::to_string(::getpid()) + ".jsonl");
  {
    std::ofstream f(trace);
    f << R"({"seq":1,"phase":"final","iter":0,"cmd":"move","nodes":["a","b"],"x":[100,200],"y":[5,6]})" << "\n";
    f << R"({"seq":2,"phase":"final","iter":0,"cmd":"centroid","name":"g","x":7,"y":8})" << "\n";
    f << R"({"seq":3,"phase":"final","iter":0,"cmd":"stage","name":"legalize","on":false})" << "\n";
  }
  ASSERT_TRUE(shift_trace(trace.string(), 50, 10));
  std::ifstream in(trace);
  std::string   l1, l2, l3;
  std::getline(in, l1);
  std::getline(in, l2);
  std::getline(in, l3);
  EXPECT_NE(l1.find("\"x\":[150.0,250.0]"), std::string::npos) << l1;
  EXPECT_NE(l1.find("\"y\":[15.0,16.0]"), std::string::npos) << l1;
  EXPECT_NE(l2.find("\"x\":57.0"), std::string::npos) << l2;
  EXPECT_NE(l2.find("\"y\":18.0"), std::string::npos) << l2;
  EXPECT_EQ(l3, R"({"seq":3,"phase":"final","iter":0,"cmd":"stage","name":"legalize","on":false})");
  EXPECT_FALSE(shift_trace((trace.string() + ".missing"), 1, 1));
  std::filesystem::remove(trace);
}

TEST(GuiSession, ForkTakesTheCellsInsideTheBoxHandsThemToThePlacerAndLaunchesTheChild) {
  const auto dir = std::filesystem::temp_directory_path() / ("gui_fork_" + std::to_string(::getpid()));
  Fork_request   built;
  Fork_files     got;
  int            builds = 0, launches = 0;
  Config         cfg{.open_browser = false, .start_paused = false};
  cfg.fork_dir = dir.string();
  cfg.fork     = [&](const Fork_files& f, const Fork_request&) {
    got = f;
    ++launches;
    return Fork_launch{"http://127.0.0.1:1/?t=x", ""};
  };
  Session s(cfg);
  ASSERT_TRUE(s.start());
  const int    port = port_of(s.url());
  const auto   t    = "t=" + s.token();
  const double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  const auto   state = [&] { return body_of(http(port, "GET", "/state?" + t)); };
  s.begin_phase(scene(), x, y, 4);
  s.set_fork_builder([&](const Fork_request& r) {
    built = r;
    ++builds;
    Fork_files f;
    f.top = "fork_1";
    f.net_dir = r.dir + "/net";
    f.cells = r.cells.size();
    f.inputs = 3;
    f.outputs = 2;
    return f;
  });
  s.gate("final", x, y, 4, {});
  // the cells are taken by their centers: c0 and c1 are inside, the macro (centered at 53, 58 dbu) and the IO are not
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"fork","box":[0,0,0.05,0.05]})").find("200 OK"), std::string::npos);
  s.poll(5, x, y, 4, {});
  ASSERT_EQ(builds, 1);
  ASSERT_EQ(launches, 1);
  EXPECT_EQ(built.cells, (std::vector<std::string>{"c0", "c1"}));
  EXPECT_EQ(built.box.xh, 50);
  EXPECT_EQ(built.dir, dir.string() + "/fork_1");
  EXPECT_TRUE(std::filesystem::is_directory(built.dir));
  EXPECT_EQ(got.top, "fork_1");
  const auto st = state();
  EXPECT_NE(st.find(R"("forks":[{"url":"http://127.0.0.1:1/?t=x","top":"fork_1","cells":2,"inputs":3,"outputs":2}])"), std::string::npos) << st;
  EXPECT_NE(st.find("fork 1: 2 cells"), std::string::npos) << st;
  // by name: the box is the hull of the named cells; the next fork gets its own directory
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"fork","nodes":["c1"]})").find("200 OK"), std::string::npos);
  s.poll(6, x, y, 4, {});
  ASSERT_EQ(builds, 2);
  EXPECT_EQ(built.cells, (std::vector<std::string>{"c1"}));
  EXPECT_EQ(built.dir, dir.string() + "/fork_2");
  EXPECT_EQ(built.box.xl, 2);
  EXPECT_EQ(built.box.xh, 12);
  // an IO is not a cell
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"fork","nodes":["a"]})").find("200 OK"), std::string::npos);
  s.poll(7, x, y, 4, {});
  EXPECT_EQ(builds, 2);
  EXPECT_NE(state().find("a is not a placeable cell"), std::string::npos);
  // a region with no cells
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"fork","box":[0.5,0.5,0.6,0.6]})").find("200 OK"), std::string::npos);
  s.poll(8, x, y, 4, {});
  EXPECT_EQ(builds, 2);
  EXPECT_NE(state().find("no cells inside the region"), std::string::npos);
  std::filesystem::remove_all(dir);
}

TEST(GuiSession, ForkFailuresBecomeANoteAndNeverAForkEntry) {
  const auto dir = std::filesystem::temp_directory_path() / ("gui_fork_fail_" + std::to_string(::getpid()));
  int        launches = 0;
  Config     cfg{.open_browser = false, .start_paused = false};
  cfg.fork_dir = dir.string();
  cfg.fork     = [&](const Fork_files&, const Fork_request&) {
    ++launches;
    return Fork_launch{"", "the new session exited before it started"};
  };
  Session s(cfg);
  ASSERT_TRUE(s.start());
  const int    port = port_of(s.url());
  const auto   t    = "t=" + s.token();
  const double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  const auto   state = [&] { return body_of(http(port, "GET", "/state?" + t)); };
  s.begin_phase(scene(), x, y, 4);
  s.gate("final", x, y, 4, {});
  // no builder installed (no placement data): not available
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"fork","box":[0,0,0.05,0.05]})").find("200 OK"), std::string::npos);
  s.poll(5, x, y, 4, {});
  EXPECT_NE(state().find("fork: not available here"), std::string::npos);
  // the placer refuses the region
  s.set_fork_builder([&](const Fork_request&) {
    Fork_files f;
    f.error = "ram (BLOCK) has bus pins";
    return f;
  });
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"fork","box":[0,0,0.05,0.05]})").find("200 OK"), std::string::npos);
  s.poll(6, x, y, 4, {});
  EXPECT_NE(state().find("fork: ram (BLOCK) has bus pins"), std::string::npos);
  EXPECT_EQ(launches, 0);
  // the placer builds it, the child does not start
  s.set_fork_builder([&](const Fork_request& r) {
    Fork_files f;
    f.top = "fork_1";
    f.cells = r.cells.size();
    return f;
  });
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"fork","box":[0,0,0.05,0.05]})").find("200 OK"), std::string::npos);
  s.poll(7, x, y, 4, {});
  EXPECT_EQ(launches, 1);
  EXPECT_NE(state().find("fork: the new session exited before it started"), std::string::npos);
  EXPECT_NE(state().find(R"("forks":[])"), std::string::npos);
  // a new phase drops the builder: it reads the previous phase's data
  s.set_fork_builder([&](const Fork_request&) { return Fork_files{}; });
  s.begin_phase(scene(), x, y, 4);
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"fork","box":[0,0,0.05,0.05]})").find("200 OK"), std::string::npos);
  s.poll(8, x, y, 4, {});
  EXPECT_NE(state().find("fork: not available here"), std::string::npos);
  // malformed requests are refused up front
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"fork"})").find("400"), std::string::npos);
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"fork","box":[1,1,0,0]})").find("400"), std::string::npos);
  std::filesystem::remove_all(dir);
}

TEST(GuiSession, RegionsOverlayIsComputedOnRequestAndServedSeparatelyFromTheMap) {
  Session s(Config{.open_browser = false, .start_paused = false});
  ASSERT_TRUE(s.start());
  const int    port = port_of(s.url());
  const auto   t    = "t=" + s.token();
  const double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  std::vector<std::string> asked;
  const Map_provider maps = [&](const std::string& kind) {
    asked.push_back(kind);
    Grid g;
    if (kind == "regions") {
      g.rects          = {0, 0, 500, 1000, 30, 500, 0, 1000, 1000, 12};
      g.crossings      = {500, 100, 0, 500, 900, 0};
      g.crossing_total = 7;
      g.note           = "2 regions, 7 net crossings of their borders";
    }
    return g;
  };
  s.begin_phase(scene(), x, y, 4);
  // off: nothing is computed and the endpoint is empty
  s.gate("final", x, y, 4, {}, &maps);
  EXPECT_TRUE(asked.empty());
  EXPECT_NE(body_of(http(port, "GET", "/regions?" + t)).find(R"("on":false)"), std::string::npos);
  // on: computed at the next poll, together with a congestion map if one is asked for
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"regions","on":true})").find("200 OK"), std::string::npos);
  s.poll(5, x, y, 4, {}, &maps);
  ASSERT_EQ(asked, (std::vector<std::string>{"regions"}));
  auto st = body_of(http(port, "GET", "/state?" + t));
  EXPECT_NE(st.find(R"("regions_on":true)"), std::string::npos) << st;
  const auto r = body_of(http(port, "GET", "/regions?" + t));
  EXPECT_NE(r.find(R"("rects":[[0,0,500,1000,30],[500,0,1000,1000,12]])"), std::string::npos) << r;
  EXPECT_NE(r.find(R"("pins":[[500,100,0],[500,900,0]])"), std::string::npos) << r;
  EXPECT_NE(r.find(R"("pins_total":7)"), std::string::npos) << r;
  EXPECT_NE(r.find("2 regions, 7 net crossings"), std::string::npos) << r;
  // a gate recomputes it at the positions of that moment; a map request is independent of it
  s.gate("legalize", x, y, 4, {}, &maps);
  EXPECT_EQ(asked.size(), 2u);
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"map","kind":"rudy"})").find("200 OK"), std::string::npos);
  s.poll(6, x, y, 4, {}, &maps);
  EXPECT_EQ(asked.back(), "rudy");
  EXPECT_NE(body_of(http(port, "GET", "/regions?" + t)).find(R"("rects":[[0,0,500,1000,30])"), std::string::npos);
  // off again: dropped, and not part of the action trace (a view toggle)
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"regions","on":false})").find("200 OK"), std::string::npos);
  s.poll(7, x, y, 4, {}, &maps);
  const auto off = body_of(http(port, "GET", "/regions?" + t));
  EXPECT_NE(off.find(R"("on":false)"), std::string::npos) << off;
  EXPECT_NE(off.find(R"("rects":[])"), std::string::npos) << off;
  // a provider that throws gives an empty overlay with the reason
  EXPECT_NE(http(port, "POST", "/cmd?" + t, R"({"cmd":"regions"})").find("200 OK"), std::string::npos);
  const Map_provider broken = [](const std::string&) -> Grid { throw std::runtime_error("region mesh requires fixed macros"); };
  s.poll(8, x, y, 4, {}, &broken);
  EXPECT_NE(body_of(http(port, "GET", "/regions?" + t)).find("region mesh requires fixed macros"), std::string::npos);
}
