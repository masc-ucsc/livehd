// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gui_render.hpp"
#include "gui_session.hpp"
#include "gui_worker.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <thread>

#include "gtest/gtest.h"
#include "rapidjson/document.h"

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

TEST(GuiSession, StopsAnAdmittedWorkerWhenTheCombinedSessionExceedsItsBudget) {
  // A long-running downstream command represents a live T4 worker. Grow the retained parent only after admission.
  auto proposal = std::make_shared<Session>(Config{.open_browser = false});
  const double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  proposal->begin_phase(scene(), x, y, 4);
  Config cfg;
  cfg.validation_args = {"/bin/sleep", "15"};
  cfg.session_root_pid = getpid();
  const auto root = std::filesystem::temp_directory_path() / ("gui_memory_" + std::to_string(getpid()));
  ASSERT_TRUE(std::filesystem::create_directory(root));
  const auto wrapper = root / "validator";
  ASSERT_TRUE(std::ofstream(wrapper) << "#!/bin/sh\nshift 2\nexec \"$1\" \"$2\"\n");
  ASSERT_EQ(chmod(wrapper.c_str(), 0700), 0);
  cfg.worker_args = {wrapper.string()};
  cfg.session_memory_bytes = livehd::process_tree_rss(getpid()) + (32ULL << 20);
  {
    Replay_worker worker(cfg, root, "route", proposal, 1, {});
    ASSERT_FALSE(worker.terminal());
    std::vector<unsigned char> retained(64ULL << 20, 1);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (!worker.terminal() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    rapidjson::Document row;
    row.Parse(worker.status().c_str());
    ASSERT_TRUE(row.IsObject());
    EXPECT_STREQ(row["state"].GetString(), "budget_exhausted");
    EXPECT_STREQ(row["reason"].GetString(), "session process tree exceeded memory budget");
    EXPECT_GT(row["sampled_session_rss_bytes"].GetUint64(), cfg.session_memory_bytes);
    EXPECT_LT(row["sampled_tree_rss_bytes"].GetUint64(), cfg.session_memory_bytes);
    EXPECT_TRUE(row["result"].IsNull());
    EXPECT_EQ(retained.back(), 1);
  }
  std::filesystem::remove_all(root);
}

TEST(GuiSession, RoutedWorkerRequiresPositivePoseAndLegalityChecks) {
  Config proposal_config;
  proposal_config.open_browser = false;
  proposal_config.score = {{"negative_wns_ps", {1., 250.}}};
  auto proposal = std::make_shared<Session>(proposal_config);
  const double x[] = {0, 100, 124, 900}, y[] = {0, 100, 397, 900};
  proposal->begin_phase(scene(), x, y, 4);
  const auto root = std::filesystem::path(testing::TempDir()) / ("gui_receipt_" + std::to_string(getpid()));
  ASSERT_TRUE(std::filesystem::create_directories(root));
  const auto wrapper = root / "score";
  ASSERT_TRUE(std::ofstream(wrapper) << R"(#!/bin/sh
shift 2
source_file=$1
while [ "$#" -ge 2 ]; do
  if [ "$1" = --receipt ]; then exec cp "$source_file" "$2"; fi
  shift
done
exit 1
)");
  ASSERT_EQ(chmod(wrapper.c_str(), 0700), 0);
  Config cfg;
  cfg.worker_args = {wrapper.string()};
  cfg.validation_args = {(root / "input.json").string()};
  for (auto stage : {"valid", "measured", "unconstrained", "stale_timing", "wrong_timing", "missing_drc", "cutoff", "missing", "moved", "illegal"}) {
    SCOPED_TRACE(stage);
    std::string_view which(stage);
    std::string checks;
    if (which != "missing") {
      checks = std::format(R"(,"placement_checks":{{"applied_macro_poses":"passed","routed_macro_poses":"{}","final_legality":"{}","legalization_failure_counts":[0,1,0]}})",
                           which == "moved" ? "failed" : "passed", which == "illegal" ? "not_run" : "passed");
    }
    std::string_view row_metrics, timing;
    if (which == "measured" || which == "stale_timing" || which == "wrong_timing") {
      row_metrics = R"(,"wns":-0.25,"tns":-3.0)";
    }
    if (which == "measured") {
      timing = R"(,"setup_timing":{"status":"measured","wns":-0.25,"tns":-3.0})";
    } else if (which == "wrong_timing") {
      timing = R"(,"setup_timing":{"status":"measured","wns":-0.25,"tns":0})";
    } else if (which == "unconstrained" || which == "stale_timing") {
      timing = R"(,"setup_timing":{"status":"unavailable","reason":"no constrained setup path"})";
    }
    const char* drc = which == "missing_drc" ? "" : which == "cutoff" ? R"(,"drc":1000)" : R"(,"drc":0)";
    ASSERT_TRUE(std::ofstream(root / "input.json") << R"({"row":{"status":"routed")" << drc << row_metrics << R"(},"inputs":{},"openroad_exit":0)" << checks << timing << "}");
    Replay_worker worker(cfg, root, stage, proposal, 1, {});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!worker.terminal() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    rapidjson::Document row;
    row.Parse(worker.status().c_str());
    ASSERT_TRUE(row.IsObject());
    const bool valid = which == "valid" || which == "measured" || which == "unconstrained";
    EXPECT_STREQ(row["state"].GetString(), valid ? "done" : "failed");
    EXPECT_EQ(row["result"].IsNull(), !valid);
    if (valid) {
      const auto& summary = row["result"]["summary"];
      const auto& q       = summary["quality"];
      EXPECT_STREQ(summary["timing"]["model"].GetString(), "final_downstream_sta");
      EXPECT_TRUE(summary["routing"]["overflow_bin_share"].IsNull());
      if (which == "measured") {
        EXPECT_DOUBLE_EQ(summary["timing"]["wns_ps"].GetDouble(), -250);
        EXPECT_DOUBLE_EQ(q["score"].GetDouble(), 50);
      } else {
        EXPECT_TRUE(summary["timing"]["wns_ps"].IsNull());
        EXPECT_TRUE(q["score"].IsNull());
      }
      rapidjson::Document viewed;
      viewed.Parse(worker.agent(R"({"cmd":"summary"})").c_str());
      ASSERT_TRUE(viewed.IsObject());
      EXPECT_EQ(viewed["quality"], q);
      EXPECT_EQ(viewed["timing"], summary["timing"]);
    }
    if (!valid) {
      const std::string reason = row["reason"].GetString();
      const auto expected = which == "missing" ? "missing placement checks" : which == "moved" ? "routed_macro_poses"
                            : which == "illegal" ? "final_legality" : (which == "missing_drc" || which == "cutoff") ? "final DRC" : "final setup measurement";
      EXPECT_NE(reason.find(expected), std::string::npos) << reason;
    }
  }
  std::filesystem::remove_all(root);
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
  const auto moved = http(port, "POST", "/cmd?" + t, R"({"cmd":"move","nodes":["#1"],"x":[50],"y":[60],"final":true})");
  EXPECT_EQ(body_of(moved), R"({"ok":true})");
  const auto invalid = http(port, "POST", "/cmd?" + t, R"({"cmd":"move","nodes":["#99\""],"x":[50],"y":[60]})");
  EXPECT_NE(invalid.find("400"), std::string::npos);
  rapidjson::Document ack;
  ack.Parse(body_of(invalid).c_str());
  ASSERT_FALSE(ack.HasParseError());
  EXPECT_FALSE(ack["ok"].GetBool());
  const auto edits = s.poll(5, x, y, 4, {});
  ASSERT_EQ(edits.pins.size(), 1u);
  EXPECT_EQ(edits.pins[0].node, 1);
  EXPECT_NE(body_of(http(port, "GET", "/query?name=c1&" + t)).find("\"pinned\":true"), std::string::npos);
}

TEST(GuiSession, MacroMetadataJoinsTheSelectedFrameWithOtherPassRecords) {
  Config cfg;
  cfg.open_browser = false;
  cfg.intent = R"([{"class":"livehd.sim.tuning","design":"chip","knobs":{"dirty":true},"source_cookie":18446744073709551615},{"class":"livehd.placement.state","design":"chip","id":"old"}])";
  Session session(cfg);
  auto model = scene();
  model->design = "chip";
  model->nodes[2].master = "sram";
  model->nodes[2].orient = "FN";
  model->nodes[2].external_name = "downstream_alias";
  const double x[] = {0, 100, 124, 900}, y[] = {0, 100, 397, 900};
  session.begin_phase(model, x, y, 4);
  rapidjson::Document result;
  result.Parse(session.agent_call(R"({"cmd":"export","what":"macros","format":"metadata"})").c_str());
  ASSERT_TRUE(result.IsObject() && result["ok"].GetBool());
  ASSERT_EQ(result["records"].Size(), 2u);
  EXPECT_TRUE(result["records"][0]["knobs"]["dirty"].GetBool());
  const auto& state = result["records"][1];
  EXPECT_STREQ(state["design"].GetString(), "chip");
  EXPECT_STREQ(state["id"].GetString(), "macro-positions");
  const auto& hints = state["templates"][0]["hints"];
  ASSERT_EQ(hints.Size(), 1u);
  const auto& p = hints[0];
  EXPECT_STREQ(p["subject"][0].GetString(), "ram");
  EXPECT_STREQ(p["master"].GetString(), "sram");
  EXPECT_STREQ(p["orientation"].GetString(), "FN");
  EXPECT_DOUBLE_EQ(p["x_um"].GetDouble(), .124);
  EXPECT_DOUBLE_EQ(p["y_um"].GetDouble(), .397);
  rapidjson::Document encoded, records;
  encoded.Parse(session.agent_call(R"({"cmd":"export","what":"macros","format":"metadata","encoding":"json_text"})").c_str());
  ASSERT_TRUE(encoded.IsObject() && encoded.HasMember("records_json") && encoded["records_json"].IsString());
  records.Parse(encoded["records_json"].GetString());
  ASSERT_TRUE(records.IsArray());
  EXPECT_EQ(records, result["records"]);
  EXPECT_EQ(records[0]["source_cookie"].GetUint64(), UINT64_MAX);
}

TEST(GuiSession, ReplayIdentityUsesThisDesignsPlacementInputs) {
  const auto model = scene();
  model->design = "chip";
  const double x[] = {0, 100, 124, 900}, y[] = {0, 100, 397, 900};
  const auto identities = [&](std::string records) {
    Config cfg;
    cfg.input_identity = "recipe";
    cfg.source_identity = "sources";
    cfg.intent = std::move(records);
    Session s(std::move(cfg));
    s.begin_phase(model, x, y, 4);
    rapidjson::Document d;
    d.Parse(s.state_json().c_str());
    return std::array<std::string, 3>{
        d["source_identity"].GetString(), d["input_identity"].GetString(), d["baseline_identity"].GetString()};
  };
  const std::string hint = R"({"class":"livehd.placement.hints","design":"chip","id":"arrangement"})";
  const std::string state = R"({"class":"livehd.placement.state","design":"chip","id":"poses"})";
  const auto expected = identities("[" + hint + "," + state + "]");
  EXPECT_EQ(expected, identities("[" + state + R"(,{"class":"livehd.sim.tuning","id":"sim"},{"class":"livehd.placement.hints","design":"elsewhere"},{"class":"livehd.placement.hint_results","design":"chip"},)" + hint + "]"));
  for (const auto& changed : {identities("[" + hint + "]"), identities("[" + state + "]")}) {
    for (size_t i = 0; i < expected.size(); ++i) {
      EXPECT_NE(changed[i], expected[i]);
    }
  }
}

TEST(GuiSession, ClearingPlacementIntentPreservesOtherDesignMetadata) {
  Config cfg;
  cfg.open_browser = false;
  cfg.intent = R"([{"class":"sim","id":"keep","z":[2,1]},{"class":"livehd.placement.hints","design":"chip","id":"obsolete"},{"class":"livehd.placement.hints","design":"other","id":"untouched"}])";
  Session session(cfg);
  auto model = scene();
  model->design = "chip";
  const double x[] = {0, 100, 124, 900}, y[] = {0, 100, 397, 900};
  session.begin_phase(model, x, y, 4);
  session.set_intent_provider([](const std::string&, const Frame&, bool) { return "{}"; });
  std::jthread flow([&] { session.gate("fp", x, y, 4, {}); });
  for (int i = 0; i < 200 && session.state_json().find(R"("state":"gate")") == std::string::npos; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  EXPECT_TRUE(session.submit(R"({"cmd":"intent_apply","records":[]})").empty());
  EXPECT_TRUE(session.submit(R"({"cmd":"run"})").empty());
  flow.join();
  rapidjson::Document result;
  result.Parse(session.agent_call(R"({"cmd":"export","what":"macros","format":"metadata"})").c_str());
  ASSERT_TRUE(result.IsObject() && result["ok"].GetBool());
  ASSERT_EQ(result["records"].Size(), 3u);
  EXPECT_STREQ(result["records"][0]["id"].GetString(), "keep");
  EXPECT_EQ(result["records"][0]["z"][0].GetInt(), 2);
  EXPECT_STREQ(result["records"][1]["id"].GetString(), "untouched");
  EXPECT_STREQ(result["records"][2]["class"].GetString(), "livehd.placement.state");
}

TEST(GuiSession, QueuedRelationshipsBindNamesAndResolveInOrder) {
  const auto trace = std::filesystem::temp_directory_path() / ("gui_relationships_" + std::to_string(getpid()) + ".jsonl");
  Session s(Config{.open_browser = false, .trace_path = trace.string(), .start_paused = false});
  const double x[] = {100, 200, 400, 900}, y[] = {100, 200, 400, 900};
  s.begin_phase(scene(), x, y, 4);
  const auto call = [](Session& session, const std::string& command) {
    rapidjson::Document reply;
    reply.Parse<rapidjson::kParseFullPrecisionFlag>(session.agent_call(command).c_str());
    return reply;
  };
  auto status = [&](uint64_t sequence) {
    return call(s, std::format(R"({{"cmd":"command_status","sequence":{}}})", sequence));
  };
  const auto placed = call(s, R"({"cmd":"cmd","command":{"cmd":"place_positions","nodes":["c1"],"x":[200],"y":[200]}})")["sequence"].GetUint64();
  s.poll(0, x, y, 4, {});
  const auto once = status(placed);
  ASSERT_STREQ(once["status"].GetString(), "applied");
  EXPECT_DOUBLE_EQ(once["resolved"][0]["x"].GetDouble(), 200);
  EXPECT_FALSE(once["resolved"][0]["pinned"].GetBool());
  const auto frame = call(s, R"({"cmd":"state"})")["frame"].GetUint64();
  EXPECT_FALSE(call(s, R"({"cmd":"move","nodes":["#oops"],"dx":[10],"dy":[0]})")["ok"].GetBool());
  EXPECT_FALSE(call(s, R"({"cmd":"select","names":["#1x"]})")["ok"].GetBool());
  EXPECT_FALSE(call(s, R"({"cmd":"batch","moves":[{"cmd":"move","nodes":["#1"],"align":"right","reference":"#oops"}]})")["ok"].GetBool());
  const auto first = call(s, R"({"cmd":"move","nodes":["#1"],"dx":[10.25],"dy":[0],"final":false})")["sequence"].GetUint64();
  const auto stale = call(s, std::format(R"({{"cmd":"cmd","expect":{{"frame":{}}},"command":{{"cmd":"policy","fast":true}}}})", frame))["sequence"].GetUint64();
  const auto second = call(s, R"({"cmd":"batch","moves":[{"cmd":"move","nodes":["#1"],"dx":[3.5],"dy":[-1]},{"cmd":"move","nodes":["ram"],"align":"right_of","reference":"#1","gap":1.25}]})")["sequence"].GetUint64();
  EXPECT_STREQ(status(second)["status"].GetString(), "queued");
  EXPECT_TRUE(call(s, R"({"cmd":"export","what":"relationships"})")["moves"].Empty());
  s.poll(1, x, y, 4, {});
  EXPECT_STREQ(status(first)["status"].GetString(), "applied");
  EXPECT_DOUBLE_EQ(status(first)["resolved"][0]["x"].GetDouble(), 210.25);
  EXPECT_STREQ(status(stale)["status"].GetString(), "stale");
  auto applied = status(second);
  ASSERT_STREQ(applied["status"].GetString(), "applied");
  EXPECT_DOUBLE_EQ(applied["resolved"][0]["x"].GetDouble(), 213.75);
  EXPECT_DOUBLE_EQ(applied["resolved"][1]["x"].GetDouble(), 225);
  auto result = call(s, R"({"cmd":"export","what":"relationships"})");
  auto& moves = result["moves"];
  ASSERT_EQ(moves.Size(), 3u);
  EXPECT_STREQ(moves[0]["nodes"][0].GetString(), "c1");
  EXPECT_DOUBLE_EQ(moves[0]["dx"][0].GetDouble(), 10.25);
  EXPECT_FALSE(moves[0]["final"].GetBool());
  EXPECT_STREQ(moves[2]["reference"].GetString(), "c1");
  EXPECT_DOUBLE_EQ(moves[2]["gap"].GetDouble(), 1.25);
  auto halos = scene();
  halos->halo_x = 12;
  halos->halo_y = 9;
  Session other(Config{.open_browser = false, .start_paused = false});
  other.begin_phase(halos, x, y, 4);
  const auto proposal = call(other, R"({"cmd":"explore","tier":"T0","patch":{"cmd":"move","nodes":["c0"],"align":"right_of","reference":"ram","edges":"halo","gap":5}})");
  ASSERT_TRUE(proposal["ok"].GetBool());
  const auto id = proposal["id"].GetString();
  const auto cell = call(other, std::format(R"({{"cmd":"describe","target":"{}","name":"c0"}})", id));
  EXPECT_DOUBLE_EQ(cell["node"]["x_um"].GetDouble(), 0.517);
  auto halo_patch = call(other, std::format(R"({{"cmd":"export","target":"{}","what":"relationships"}})", id));
  EXPECT_STREQ(halo_patch["moves"][0]["edges"].GetString(), "halo");
  Session replay(Config{.open_browser = false, .replay_path = trace.string()});
  replay.begin_phase(scene(), x, y, 4);
  replay.poll(1, x, y, 4, {});
  EXPECT_TRUE(call(replay, R"({"cmd":"export","what":"relationships"})")["moves"] == moves);
  std::filesystem::remove(trace);
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

TEST(GuiSession, MidGpPauseAndReplaySelectTheSamePollOccurrence) {
  const double x[4] = {10, 100, 300, 900}, y[4] = {10, 20, 300, 900};
  const auto trace = std::filesystem::temp_directory_path() / ("gui_pause_" + std::to_string(getpid()) + ".jsonl");
  const auto pause = [&](Session& session) {
    std::jthread evaluating([&] { session.poll(7, x, y, 4, {}); });
    for (int attempt = 0; attempt < 100 && session.state_json().find(R"("gate":"pause_final_7")") == std::string::npos; ++attempt) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    rapidjson::Document state;
    state.Parse(session.state_json().c_str());
    EXPECT_STREQ(state["state"].GetString(), "paused");
    EXPECT_STREQ(state["gate"].GetString(), "pause_final_7");
    std::string identity = state["baseline_identity"].GetString();
    EXPECT_TRUE(session.submit(R"({"cmd":"continue"})").empty());
    return identity;
  };
  std::string expected;
  {
    Session original(Config{.trace_path = trace.string(), .start_paused = false});
    original.begin_phase(scene(), x, y, 4);
    original.poll(7, x, y, 4, {});
    ASSERT_TRUE(original.submit(R"({"cmd":"pause"})").empty());
    expected = pause(original);
  }
  Session replay(Config{.replay_path = trace.string(), .resume_label = "pause_final_7", .resume_nth = 2});
  replay.begin_phase(scene(), x, y, 4);
  replay.poll(7, x, y, 4, {});
  EXPECT_TRUE(replay.replaying());
  EXPECT_EQ(pause(replay), expected);
  EXPECT_FALSE(replay.replaying());
  std::filesystem::remove(trace);
}

TEST(GuiSession, SamePoseAndIterationCannotReuseAnotherOptimizerState) {
  Session session(Config{.open_browser = false, .start_paused = false});
  const double x[] = {10, 100, 300, 900}, y[] = {10, 20, 300, 900};
  session.begin_phase(scene(), x, y, 4);
  auto read = [&] {
    rapidjson::Document state;
    state.Parse(session.state_json().c_str());
    return state;
  };
  EXPECT_TRUE(read()["optimizer_identity"].IsNull());
  Stats stats;
  stats.optimizer_identity = 0x1122334455667788ULL;
  session.poll(7, x, y, 4, stats);
  const auto first = read();
  EXPECT_STREQ(first["optimizer_identity"].GetString(), "1122334455667788");
  stats.optimizer_identity ^= 1;
  session.poll(7, x, y, 4, stats);
  const auto second = read();
  EXPECT_STREQ(first["placement_identity"].GetString(), second["placement_identity"].GetString());
  EXPECT_STRNE(first["baseline_identity"].GetString(), second["baseline_identity"].GetString());
  auto request = std::format(R"({{"cmd":"explore","tier":"T0","patch":[],"expect":{{"frame":{},"baseline":"{}"}}}})",
                             second["frame"].GetUint64(), first["baseline_identity"].GetString());
  auto reply = session.agent_call(request);
  EXPECT_NE(reply.find("stale baseline"), std::string::npos) << reply;
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

TEST(GuiSession, DragAcrossPinnedMacrosPreservesTheirAnchorsAndRestoresEnforcement) {
  Session s(Config{.open_browser = false, .start_paused = false});
  auto sc = scene();
  sc->phase = "macro";
  sc->nodes = {{"drag", Kind::macro, true, 60, 60}, {"a", Kind::macro, true, 60, 60}, {"b", Kind::macro, true, 60, 60}};
  sc->halo_x = sc->halo_y = 10;
  sc->build_index();
  double x[] = {100, 300, 600}, y[] = {100, 100, 100};
  s.begin_phase(sc, x, y, 3);
  auto call = [&](const std::string& req) {
    rapidjson::Document d;
    d.Parse(s.agent_call(req).c_str());
    return d;
  };
  ASSERT_TRUE(call(R"({"cmd":"pin","nodes":["a","b"]})")["ok"].GetBool());
  EXPECT_EQ(s.poll(1, x, y, 3, {}).pins.size(), 2u);
  auto step = [&](int iter, int dest, std::string_view suspended) {
    ASSERT_TRUE(call(std::format(R"({{"cmd":"move","nodes":["drag"],"x":[{}],"y":[100],"final":false}})", dest))["ok"].GetBool());
    auto edits = s.poll(iter, x, y, 3, {});
    x[0] = dest;
    auto state = call(R"({"cmd":"state"})");
    unsigned neighbors = 0;
    for (const auto& p : state["pin_intent"].GetArray()) {
      const std::string_view name = p["name"].GetString();
      if (name == "drag") {
        continue;
      }
      ++neighbors;
      EXPECT_EQ(p["x"].GetDouble(), name == "a" ? 300 : 600);
      EXPECT_EQ(p["y"].GetDouble(), 100);
      EXPECT_EQ(p["suspended"].GetBool(), name == suspended);
    }
    EXPECT_EQ(neighbors, 2u);
    if (!suspended.empty()) {
      EXPECT_TRUE(state["geometry_conflicts"].HasMember(std::string(suspended).c_str()));
      EXPECT_FALSE(call(R"({"cmd":"pin","nodes":["drag"]})")["ok"].GetBool());
    }
    if (iter == 2 || iter == 3) {
      const int other = iter == 2 ? 1 : 2;
      EXPECT_NE(std::find(edits.releases.begin(), edits.releases.end(), other), edits.releases.end());
    }
    if (iter >= 3) {
      const int restored = iter == 3 ? 1 : 2;
      EXPECT_TRUE(std::any_of(edits.pins.begin(), edits.pins.end(), [&](const auto& p) {
        return p.node == restored && p.x == x[restored] && p.y == y[restored];
      }));
    }
  };
  step(2, 300, "a");  // body overlap
  step(3, 531, "b");  // disjoint bodies but overlapping macro halos
  step(4, 100, {});
  // Solver overlap with an unpinned body is removable by legalization.
  x[0] = 310;
  ASSERT_TRUE(s.submit(R"({"cmd":"unpin","nodes":["drag"]})").empty());
  s.poll(5, x, y, 3, {});
  EXPECT_EQ(s.all_pins().pins.size(), 1u);
  EXPECT_EQ(s.all_pins(true).pins.size(), 2u);
  // A conflict between the anchors themselves cannot be removed this way.
  ASSERT_TRUE(s.submit(R"({"cmd":"move","nodes":["b"],"x":[300],"y":[100]})").empty());
  s.poll(6, x, y, 3, {});
  EXPECT_TRUE(s.all_pins(true).pins.empty());
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

// 2026-11-15 adversarial pass over the agent socket (todo/gui-work.html).
TEST(GuiAgent, RegexSearchRunsInLinearTimeAndRefusesWhatRe2CannotMatch) {
  Session s(Config{.open_browser = false, .start_paused = false});
  const double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  auto         sc = agent_scene();
  sc->nodes[0].name = std::string(4000, 'a');  // (a*)*c over this takes forever with a backtracking engine
  sc->build_index();
  s.begin_phase(sc, x, y, 4);
  const auto t0    = std::chrono::steady_clock::now();
  const auto nest  = s.agent_call(R"({"cmd":"find","regex":"^(a*)*c$","scope":"all"})");
  const auto nest2 = s.agent_call(R"({"cmd":"find","regex":"^(.*)*Q$","field":"any"})");
  EXPECT_LT(std::chrono::steady_clock::now() - t0, std::chrono::seconds(5));
  EXPECT_TRUE(ok(nest)) << nest;
  EXPECT_NE(nest.find("\"count\":0"), std::string::npos) << nest;
  EXPECT_TRUE(ok(nest2)) << nest2;
  // lookaround and backreferences do not exist in RE2: a clear refusal, never a silent miss
  EXPECT_NE(s.agent_call(R"({"cmd":"find","regex":"(a)\\1"})").find("bad regex"), std::string::npos);
  EXPECT_NE(s.agent_call(R"j({"cmd":"find","regex":"(?=a)"})j").find("bad regex"), std::string::npos);
  // the usual patterns of the docs still work
  EXPECT_NE(s.agent_call(R"({"cmd":"find","regex":"^c","scope":"cells"})").find("\"count\":1"), std::string::npos);
  EXPECT_NE(s.agent_call(R"({"cmd":"find","regex":"ram|c1","scope":"all"})").find("\"count\":2"), std::string::npos);
}

TEST(GuiAgent, ArgumentsAreCheckedNotIgnoredOrClamped) {
  Session s(Config{.open_browser = false, .start_paused = false});
  const double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  s.begin_phase(agent_scene(), x, y, 4);
  for (const char* bad : {R"({"cmd":"find","regex":".","limit":0})", R"({"cmd":"find","regex":".","limit":-5})", R"({"cmd":"find","regex":".","limit":"x"})",
                          R"({"cmd":"find","regex":".","limit":2.7})", R"({"cmd":"find","regex":".","limit":1001})", R"({"cmd":"critical","n":0})",
                          R"({"cmd":"critical","n":"abc"})", R"({"cmd":"critical","n":1e18})"}) {
    const auto reply = s.agent_call(bad);
    EXPECT_FALSE(ok(reply)) << bad << " -> " << reply;
    EXPECT_NE(reply.find("must be an integer in"), std::string::npos) << reply;
  }
  const auto one = s.agent_call(R"({"cmd":"find","regex":".","limit":1})");
  EXPECT_TRUE(ok(one)) << one;
  EXPECT_NE(one.find("\"count\":4"), std::string::npos);  // four nodes match, one name is shown
  EXPECT_EQ(one.find("\"c1\""), std::string::npos);
  // a NUL inside the command does not shorten it to a valid one
  EXPECT_NE(s.agent_call("{\"cmd\":\"state\\u0000x\"}").find("unknown cmd"), std::string::npos);
  // describe takes #index like move, select and highlight_names
  const auto d = s.agent_call(R"({"cmd":"describe","name":"#2"})");
  EXPECT_TRUE(ok(d)) << d;
  EXPECT_NE(d.find("sram_16x32"), std::string::npos);
  EXPECT_FALSE(ok(s.agent_call(R"({"cmd":"describe","name":"#99"})")));
}

TEST(GuiAgent, ColourValidationAcceptsOnlyHexAndNames) {
  Session s(Config{.open_browser = false, .start_paused = false});
  const double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  s.begin_phase(agent_scene(), x, y, 4);
  for (const char* good : {"#fff", "#ffd23f", "#ffd23fcc", "orange", "red"}) {
    EXPECT_TRUE(ok(s.agent_call(std::string(R"({"cmd":"highlight","regex":"c","color":")") + good + R"("})"))) << good;
  }
  for (const char* bad : {"#ff", "#fffffffff0", "#ggg", "re", "orange-red", "red;x", "url(x)"}) {
    EXPECT_FALSE(ok(s.agent_call(std::string(R"({"cmd":"highlight","regex":"c","color":")") + bad + R"("})"))) << bad;
  }
}

TEST(GuiAgent, MoveRefusesAPlacedMacroInsteadOfSkippingItSilently) {
  auto sc               = scene();
  sc->nodes[2].movable  = false;
  sc->nodes[2].kind     = Kind::fixed;  // a macro the macro stage already placed
  Session s(Config{.open_browser = false, .start_paused = false});
  const double x[4] = {1, 2, 500, 0}, y[4] = {5, 6, 200, 0};
  s.begin_phase(sc, x, y, 4);
  const auto refused = s.submit(R"({"cmd":"move","nodes":["ram"],"x":[300],"y":[300]})");
  EXPECT_NE(refused.find("macro_move"), std::string::npos) << refused;
  EXPECT_NE(s.submit(R"({"cmd":"move","nodes":["c0","ram"],"x":[300,300],"y":[300,300]})").find("macro_move"), std::string::npos);
  EXPECT_EQ(s.submit(R"({"cmd":"move","nodes":["c0"],"x":[300],"y":[300]})"), "");  // a cell still moves
  EXPECT_TRUE(ok(s.agent_call(R"({"cmd":"help"})")));
  EXPECT_NE(s.agent_call(R"({"cmd":"help"})").find("macro_move"), std::string::npos);
}

TEST(GuiAgent, MacroMoveCarriesAMirrorToTheRestartedPlacer) {
  auto sc              = scene();
  sc->nodes[2].movable = false;
  sc->nodes[2].kind    = Kind::fixed;  // a memory proxy the packer placed
  Session s(Config{.open_browser = false, .start_paused = false});
  const double x[4] = {1, 2, 500, 0}, y[4] = {5, 6, 200, 0};
  s.begin_phase(sc, x, y, 4);
  // a quarter turn would swap the proxy's axes; only mirrors and the half turn are accepted
  EXPECT_NE(s.submit(R"({"cmd":"macro_move","nodes":["ram"],"x":[300],"y":[300],"orientation":"W"})").find("N, S, FN or FS"),
            std::string::npos);
  EXPECT_TRUE(s.macro_moves().empty());
  EXPECT_EQ(s.submit(R"({"cmd":"macro_move","nodes":["ram"],"x":[300],"y":[300],"orientation":["FS"]})"), "");
  s.poll(1, x, y, 4, {});  // the page thread applies queued commands
  const auto moved = s.macro_moves();
  ASSERT_TRUE(moved.contains("ram"));
  EXPECT_EQ(moved.at("ram")[0], 300);
  EXPECT_EQ(moved.at("ram")[3], 6);  // Ann_place::Orientation::FS
  EXPECT_EQ(s.submit(R"({"cmd":"macro_move","nodes":["ram"],"x":[310],"y":[300]})"), "");
  s.poll(2, x, y, 4, {});
  EXPECT_EQ(s.macro_moves().at("ram")[3], 0);  // no orientation means N
}

TEST(GuiSession, MoveTakesOneNumberForEveryNodeOrOnePerNode) {
  Session s(Config{.open_browser = false, .start_paused = false});
  const double x[4] = {1, 2, 3, 4}, y[4] = {5, 6, 7, 8};
  s.begin_phase(scene(), x, y, 4);
  EXPECT_EQ(s.submit(R"({"cmd":"move","nodes":["c0","c1"],"x":500,"y":600})"), "");
  const auto edits = s.poll(10, x, y, 4, {});
  ASSERT_EQ(edits.pins.size(), 2u);
  for (const auto& p : edits.pins) {
    EXPECT_EQ(p.x, 500);
    EXPECT_EQ(p.y, 600);
  }
  EXPECT_EQ(s.submit(R"({"cmd":"move","nodes":["c0","c1"],"x":[1,2],"y":[3,4]})"), "");
  EXPECT_NE(s.submit(R"({"cmd":"move","nodes":["c0","c1"],"x":[1,2,3],"y":[3,4,5]})"), "");  // a length that matches nothing
  EXPECT_NE(s.submit(R"({"cmd":"move","nodes":["c0"],"x":"1","y":2})"), "");
  EXPECT_NE(s.submit(R"({"cmd":"move","nodes":["c0"],"x":1})"), "");
}

TEST(GuiSession, RestartRefusesFlagsAndCheckpointsThatWouldKillOrEraseTheSession) {
  int    calls = 0;
  Config cfg{.open_browser = false, .start_paused = false};
  cfg.restart = [&](const Restart_request&) {
    ++calls;
    return std::string{};
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
  s.set_options_json(R"([{"name":"synth.place.iterations","value":"80","kind":"integer","ro":false},)"
                     R"({"name":"synth.place.timing_driven","value":"true","kind":"boolean","ro":false},)"
                     R"({"name":"synth.place.tech_rc","value":"","kind":"text","ro":false},)"
                     R"({"name":"synth.place","value":"true","kind":"boolean","ro":true}])");
  const auto restart = [&](const std::string& body) { return http(port, "POST", "/cmd?" + t, body); };
  // each of these used to be queued as "ok" and then exit the re-executed lhd on its flag parser
  EXPECT_NE(restart(R"({"cmd":"restart","set":{"no.such.option":"1"}})").find("400"), std::string::npos);
  EXPECT_NE(restart(R"({"cmd":"restart","set":{"synth.place.iterations":"abc"}})").find("400"), std::string::npos);
  EXPECT_NE(restart(R"({"cmd":"restart","set":{"synth.place.iterations":""}})").find("400"), std::string::npos);
  EXPECT_NE(restart(R"({"cmd":"restart","set":{"synth.place.iterations":"1e3"}})").find("400"), std::string::npos);
  EXPECT_NE(restart(R"({"cmd":"restart","set":{"synth.place.timing_driven":"maybe"}})").find("400"), std::string::npos);
  EXPECT_NE(restart(R"({"cmd":"restart","set":{"synth.place":"false"}})").find("400"), std::string::npos);
  EXPECT_NE(restart(R"({"cmd":"restart","checkpoint":"x"})").find("400"), std::string::npos);
  EXPECT_NE(restart(R"({"cmd":"restart","checkpoint":-5})").find("400"), std::string::npos);
  s.poll(10, x, y, 4, {});
  EXPECT_EQ(calls, 0);
  // a checkpoint that does not exist is not "from the start" (that would drop every edit): a note and no restart
  EXPECT_NE(restart(R"({"cmd":"restart","checkpoint":99})").find("200 OK"), std::string::npos);
  s.poll(20, x, y, 4, {});
  EXPECT_EQ(calls, 0);
  EXPECT_NE(body_of(http(port, "GET", "/state?" + t)).find("no checkpoint 99"), std::string::npos);
  // valid ones pass, with or without the synth. prefix and with boolean spellings
  EXPECT_NE(restart(R"({"cmd":"restart","checkpoint":1,"set":{"synth.place.iterations":"60","place.timing_driven":"false","synth.place.tech_rc":"/x/y.tcl"}})").find("200 OK"), std::string::npos);
  s.poll(30, x, y, 4, {});
  EXPECT_EQ(calls, 1);
}

TEST(GuiRender, PngIsCompressedAndStillAValidPng) {
  const int   w = 64, h = 48;
  std::string rgb(size_t(w) * h * 3, '\x10');
  const auto  png = encode_png(w, h, rgb);
  ASSERT_GT(png.size(), 8u);
  EXPECT_EQ(png.substr(1, 3), "PNG");
  EXPECT_LT(png.size(), rgb.size() / 10);  // a flat picture: the stored (uncompressed) form was larger than the pixels
  EXPECT_NE(png.find("IDAT"), std::string::npos);
  EXPECT_NE(png.find("IEND"), std::string::npos);
}

TEST(GuiAgent, MacroScanRanksLegalPositionsAndOrientationsByPinWirelength) {
  Session s(Config{.open_browser = false, .start_paused = false});
  auto    sc = agent_scene();
  // the macro's one pin sits near its left edge and talks to both cells, which are far to the right
  sc->nodes[2].signal_pins = {{"d0", 10, 50, {0, 1}}};
  sc->nodes[2].orient      = "N";
  s.begin_phase(sc, std::array<double, 4>{800, 820, 100, 0}.data(), std::array<double, 4>{800, 820, 100, 0}.data(), 4);
  const auto grid = s.agent_call(R"({"cmd":"macro_scan","node":"ram","grid":{"nx":10,"ny":10},"top":3})");
  ASSERT_TRUE(ok(grid)) << grid;
  rapidjson::Document d;
  d.Parse(grid.c_str());
  ASSERT_TRUE(d.IsObject());
  EXPECT_EQ(d["nets"].GetUint64(), 1u);
  EXPECT_EQ(d["peers"].GetUint64(), 2u);
  EXPECT_EQ(d["evaluated"].GetUint64(), 101u);  // the grid and the current pose
  EXPECT_TRUE(d["current"]["legal"].GetBool());
  ASSERT_EQ(d["best"].Size(), 3u);
  const double here = d["current"]["hpwl_dbu"].GetDouble();
  EXPECT_LT(d["best"][0]["hpwl_dbu"].GetDouble(), here);
  EXPECT_LT(d["best"][0]["delta_dbu"].GetDouble(), 0);
  EXPECT_GE(d["best"][0]["x"].GetDouble(), 600);  // next to the cells, not at the far corner
  EXPECT_LE(d["best"][0]["hpwl_dbu"].GetDouble(), d["best"][1]["hpwl_dbu"].GetDouble());  // ranked
  // the net's half-perimeter is exact for one pin: macro at (500,500), pin at (510,550), peer centers (805,805) and (825,825)
  const auto exact = s.agent_call(R"({"cmd":"macro_scan","node":"ram","positions":[[500,500]]})");
  EXPECT_NE(exact.find("\"hpwl_dbu\":590"), std::string::npos) << exact;
  // this session is not at an fp or macro gate: an orientation change would be refused, so it is not ranked
  const auto flip = s.agent_call(R"({"cmd":"macro_scan","node":"ram","positions":[[500,500]],"orientations":["N","FN"]})");
  ASSERT_TRUE(ok(flip)) << flip;
  EXPECT_NE(flip.find("\"orientations_dropped\":[\"FN\"]"), std::string::npos) << flip;
  EXPECT_EQ(flip.find("\"orientation\":\"FN\""), std::string::npos) << flip;
  // the current pose is always a candidate and the best is refined downhill: from (500,500) toward the cells
  const auto stay = s.agent_call(R"({"cmd":"macro_scan","node":"ram","positions":[[500,500]],"top":1})");
  rapidjson::Document st;
  st.Parse(stay.c_str());
  ASSERT_TRUE(st.IsObject()) << stay;
  ASSERT_TRUE(st.HasMember("refined")) << stay;
  EXPECT_LT(st["refined"]["hpwl_dbu"].GetDouble(), st["best"][0]["hpwl_dbu"].GetDouble());
  EXPECT_GT(st["refined"]["x"].GetDouble(), 500);
  const auto plain = s.agent_call(R"({"cmd":"macro_scan","node":"ram","positions":[[500,500]],"refine":false})");
  EXPECT_EQ(plain.find("refined"), std::string::npos);
  EXPECT_NE(s.agent_call(R"({"cmd":"macro_scan","node":"ram","refine":"yes"})").find("refine must be"), std::string::npos);
  // positions outside the core are not offered
  const auto out = s.agent_call(R"({"cmd":"macro_scan","node":"ram","positions":[[2000,2000],[500,500]]})");
  ASSERT_TRUE(ok(out)) << out;
  EXPECT_NE(out.find("\"illegal\":1"), std::string::npos);
  EXPECT_NE(out.find("\"legal_candidates\":2"), std::string::npos);  // (500,500) and the current pose
  // refusals say why
  EXPECT_NE(s.agent_call(R"({"cmd":"macro_scan","node":"c0"})").find("not a macro"), std::string::npos);
  EXPECT_NE(s.agent_call(R"({"cmd":"macro_scan","node":"nope"})").find("no such node"), std::string::npos);
  EXPECT_NE(s.agent_call(R"({"cmd":"macro_scan"})").find("needs node"), std::string::npos);
  EXPECT_NE(s.agent_call(R"({"cmd":"macro_scan","node":"ram","orientations":["R90"]})").find("orientations must be"), std::string::npos);
  EXPECT_NE(s.agent_call(R"({"cmd":"macro_scan","node":"ram","grid":{"nx":0,"ny":3}})").find("grid needs"), std::string::npos);
  EXPECT_NE(s.agent_call(R"({"cmd":"macro_scan","node":"ram","grid":{"nx":2,"ny":2},"positions":[[1,1]]})").find("not both"), std::string::npos);
  EXPECT_NE(s.agent_call(R"({"cmd":"macro_scan","node":"ram","top":0})").find("top must be"), std::string::npos);
  EXPECT_NE(s.agent_call(R"({"cmd":"macro_scan","node":"#2","positions":[[1,"x"]]})").find("positions must be"), std::string::npos);
}

namespace {
// One macro master (the "ram" macro, orientation N): origins are accepted on a 100 DBU lattice in both axes.
void add_lattice_100(Scene& sc) {
  Macro_tracks t;
  t.orient   = "N";
  t.period_x = t.period_y = 100;
  t.phases_x = t.phases_y = {0};
  t.pins     = 1;
  sc.track_lattices[sc.nodes[2].master].push_back(t);
}
}  // namespace

TEST(GuiAgent, MacroPosesMustLandOnRoutingTracksOrAreNotPinnedAndNotOffered) {
  auto sc                  = agent_scene();
  add_lattice_100(*sc);
  sc->nodes[2].signal_pins = {{"d0", 10, 50, {0, 1}}};
  Session      s(Config{.open_browser = false, .start_paused = false});
  const double x[4] = {800, 820, 100, 0}, y[4] = {800, 820, 100, 0};
  s.begin_phase(sc, x, y, 4);
  // the scan snaps what it is given onto the lattice and never offers an off-track pose
  const auto scan = s.agent_call(R"({"cmd":"macro_scan","node":"ram","positions":[[530,570],[433,288]],"refine":false,"top":10})");
  ASSERT_TRUE(ok(scan)) << scan;
  rapidjson::Document d;
  d.Parse(scan.c_str());
  ASSERT_TRUE(d.IsObject());
  EXPECT_GE(d["snapped_to_tracks"].GetUint64(), 2u);
  for (const auto& b : d["best"].GetArray()) {
    EXPECT_EQ(std::llround(b["x"].GetDouble()) % 100, 0) << scan;
    EXPECT_EQ(std::llround(b["y"].GetDouble()) % 100, 0) << scan;
  }
  EXPECT_TRUE(d["current"]["legal"].GetBool());
  // a move to an off-track pose is applied but not pinned (a pin would be held exactly and end the run later), and says where to go
  uint64_t sequence = 0;
  ASSERT_EQ(s.submit(R"({"cmd":"move","nodes":["ram"],"x":[530],"y":[570]})", &sequence), "");
  s.poll(10, x, y, 4, {});
  const auto status = s.agent_call(std::format(R"({{"cmd":"command_status","sequence":{}}})", sequence));
  EXPECT_NE(status.find("misses_tracks (x axis; nearest aligned origin 500,600)"), std::string::npos) << status;
  EXPECT_NE(status.find("\"pinned\":false"), std::string::npos) << status;
  // an aligned one pins
  ASSERT_EQ(s.submit(R"({"cmd":"move","nodes":["ram"],"x":[500],"y":[600]})", &sequence), "");
  s.poll(20, x, y, 4, {});
  const auto ok_status = s.agent_call(std::format(R"({{"cmd":"command_status","sequence":{}}})", sequence));
  EXPECT_EQ(ok_status.find("misses_tracks"), std::string::npos) << ok_status;
  EXPECT_NE(ok_status.find("\"pinned\":true"), std::string::npos) << ok_status;
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

TEST(GuiAgent, FrozenRepairAdviceRequiresMeasuredViolationsOnTheCurrentFrame) {
  Session s(Config{.open_browser = false});
  const double x[]{1, 2, 3, 4}, y[]{5, 6, 7, 8};
  s.begin_phase(agent_scene(), x, y, 4);
  auto evidence = [&] {
    rapidjson::Document doc;
    doc.Parse(s.agent_call(R"({"cmd":"evidence"})").c_str());
    return doc;
  };
  ASSERT_TRUE(evidence()["repair_advice"].IsNull());
  unsigned measurements = 0;
  const auto measure = [&](double setup, double hold, int electrical) {
    s.run_frozen_timing("scratch", "[]", R"({"cmd":"score_config","components":[]})",
                        [](const std::string&, const Frame&) { return Grid{}; },
                        [&](Timing_query& q) {
                          EXPECT_TRUE(q.full);
                          ++measurements;
                          q.wns_ps = q.tns_ps = setup;
                          q.hold_wns_ps = hold;
                          q.electrical_violations = electrical;
                          return true;
                        }, "", {});
  };
  measure(-12, -3, 2);
  const auto frozen = evidence();
  const auto& advice = frozen["repair_advice"];
  ASSERT_TRUE(advice.IsObject());
  EXPECT_TRUE(advice["recommended"].GetBool());
  EXPECT_TRUE(advice["mutations_frozen"].GetBool());
  ASSERT_EQ(advice["reasons"].Size(), 3u);
  EXPECT_STREQ(advice["reasons"][0].GetString(), "negative_setup_slack");
  EXPECT_STREQ(advice["reasons"][1].GetString(), "negative_hold_slack");
  EXPECT_STREQ(advice["reasons"][2].GetString(), "electrical_violations");
  s.set_result({{"c1", 22, 23}});
  EXPECT_TRUE(evidence()["repair_advice"].IsNull());
  measure(5, std::nan(""), -1);
  const auto positive = evidence();
  EXPECT_FALSE(positive["repair_advice"]["recommended"].GetBool());
  EXPECT_TRUE(positive["repair_advice"]["reasons"].Empty());
  EXPECT_EQ(measurements, 2u);
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
  rapidjson::Document state_doc;
  state_doc.Parse(st.c_str());
  const auto& child = state_doc["forks"][0];
  EXPECT_STREQ(child["dir"].GetString(), built.dir.c_str());
  EXPECT_STREQ(child["url"].GetString(), "http://127.0.0.1:1/?t=x");
  EXPECT_STREQ(child["top"].GetString(), "fork_1");
  EXPECT_EQ(child["cells"].GetInt(), 2);
  EXPECT_EQ(child["inputs"].GetInt(), 3);
  EXPECT_EQ(child["outputs"].GetInt(), 2);
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
  const Map_provider       maps = [&](const std::string& kind, const Frame&) {
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
  const Map_provider broken
      = [](const std::string&, const Frame&) -> Grid { throw std::runtime_error("region mesh requires fixed macros"); };
  s.poll(8, x, y, 4, {}, &broken);
  EXPECT_NE(body_of(http(port, "GET", "/regions?" + t)).find("region mesh requires fixed macros"), std::string::npos);
}

TEST(GuiSession, FloorplanOutlineExcludesOverlappingCornersAndInteriorHoles) {
  auto design        = scene();
  design->die        = {0, 0, 100, 100};
  design->exclusions = {
      {40, 60, 105, 105},
      {60, 40, 110, 100},
      {10, 10,  20,  20}
  };
  design->build_index();
  EXPECT_DOUBLE_EQ(design->usable_area, 6700);
  ASSERT_EQ(design->outline.size(), 2u);
  double twice_area = 0;
  for (const auto& ring : design->outline) {
    for (size_t i = 0; i < ring.size(); ++i) {
      const auto a = ring[i], b = ring[(i + 1) % ring.size()];
      EXPECT_TRUE(a[0] == b[0] || a[1] == b[1]);
      twice_area += a[0] * b[1] - b[0] * a[1];
    }
  }
  EXPECT_DOUBLE_EQ(twice_area, 2 * design->usable_area);
  Session      session(Config{.open_browser = false});
  const double x[4] = {1, 2, 80, 4}, y[4] = {5, 6, 80, 8};
  session.begin_phase(design, x, y, 4);
  const auto          text = session.agent_call(R"({"cmd":"geometry"})");
  rapidjson::Document findings;
  findings.Parse(text.c_str());
  ASSERT_TRUE(findings.IsObject()) << text;
  EXPECT_DOUBLE_EQ(findings["usable_area_dbu2"].GetDouble(), 6700);
  EXPECT_EQ(findings["outline"].Size(), 2u);
  EXPECT_NE(session.state_json().find("excluded_area"), std::string::npos);
}

TEST(GuiSession, CongestionPicturesRespectExcludedFloorplanArea) {
  Scene scene;
  scene.die = {0, 0, 100, 100};
  scene.exclusions = {{50, 50, 100, 100}};
  Render_options options;
  options.congestion = true;
  Grid map;
  map.bx = map.by = 2;
  map.peak = 2;
  map.v = {0, 0, 0, 2};
  const Frame frame;
  const auto empty = render_png(scene, frame, {}, options);
  EXPECT_EQ(render_png(scene, frame, {}, options, &map), empty);
  map.v[0] = 2;
  EXPECT_NE(render_png(scene, frame, {}, options, &map), empty);
}

TEST(GuiSession, BoundaryClearanceUsesHaloAndPitchOfTheMeasuredSide) {
  auto sc = std::make_shared<Scene>();
  sc->die         = {0, 0, 100, 100};
  sc->halo_x      = 3;
  sc->halo_y      = 5;
  sc->site_width  = 2;
  sc->row_height  = 4;
  sc->nodes       = {{"top", Kind::macro, true, 10, 10}, {"left", Kind::macro, true, 10, 10},
                     {"overhang", Kind::macro, true, 10, 10}, {"corner", Kind::macro, true, 10, 10}};
  sc->build_index();
  Session s(Config{.open_browser = false});
  const double x[] = {45, 8, 75, 4}, y[] = {84, 45, 2, 85};
  s.begin_phase(sc, x, y, 4);
  rapidjson::Document findings;
  findings.Parse(s.agent_call(R"({"cmd":"geometry"})").c_str());
  const auto& m = findings["macros"];
  ASSERT_EQ(m.Size(), 4u);
  EXPECT_STREQ(m[0]["boundary_side"].GetString(), "top");
  EXPECT_DOUBLE_EQ(m[0]["edge_halo_clearance_dbu"].GetDouble(), 1);
  EXPECT_DOUBLE_EQ(m[0]["edge_pitch_capacity"].GetDouble(), 0);
  EXPECT_STREQ(m[1]["boundary_side"].GetString(), "left");
  EXPECT_DOUBLE_EQ(m[1]["edge_halo_clearance_dbu"].GetDouble(), 5);
  EXPECT_DOUBLE_EQ(m[1]["edge_pitch_capacity"].GetDouble(), 2);
  EXPECT_DOUBLE_EQ(m[2]["edge_halo_clearance_dbu"].GetDouble(), -3);
  EXPECT_DOUBLE_EQ(m[2]["edge_pitch_capacity"].GetDouble(), 0);
  // Different horizontal and vertical halos: the nearest macro body edge is
  // not necessarily where the halo leaves the least room for standard cells.
  EXPECT_STREQ(m[3]["boundary_side"].GetString(), "left");
  EXPECT_STREQ(m[3]["halo_boundary_side"].GetString(), "top");
  EXPECT_DOUBLE_EQ(m[3]["edge_halo_clearance_dbu"].GetDouble(), 0);
  const auto& faces = m[3]["pin_access_proxy"];
  EXPECT_DOUBLE_EQ(faces["left"]["edge_halo_clearance_dbu"].GetDouble(), 1);
  EXPECT_DOUBLE_EQ(faces["top"]["edge_pitch_capacity"].GetDouble(), 0);
  EXPECT_DOUBLE_EQ(faces["right"]["edge_pitch_capacity"].GetDouble(), 41);
  EXPECT_DOUBLE_EQ(faces["bottom"]["edge_pitch_capacity"].GetDouble(), 20);

  // A later publication can leave the scene unchanged while moving the same macro.
  // A client polling the inspector must then see the new gap and frame.
  const auto before = findings["frame"].GetUint64();
  s.set_result({{"top", 45, 76}});
  findings.Parse(s.agent_call(R"({"cmd":"geometry"})").c_str());
  EXPECT_GT(findings["frame"].GetUint64(), before);
  EXPECT_DOUBLE_EQ(findings["macros"][0]["pin_access_proxy"]["top"]["edge_halo_clearance_dbu"].GetDouble(), 9);
}

TEST(GuiSession, MacroStripsMeasureIntersectingCellBodiesAndExcludeObstacles) {
  for (bool transpose : {false, true}) {
    auto sc = std::make_shared<Scene>();
    sc->die = {0, 0, 100, 100};
    sc->nodes = {{"left", Kind::macro, true, 10, 55}, {"right", Kind::macro, true, 10, 50},
                 {"partial", Kind::cell, true, 15, 10}, {"fixed_std", Kind::cell, false, 10, 10},
                 {"in_mask", Kind::cell, true, 5, 20}, {"outside", Kind::cell, true, 4, 4},
                 {"pad", Kind::io, false, 4, 4}};
    sc->exclusions = {{20, 20, 25, 50}};
    sc->groups["logic"] = {2, 3};
    std::array<double, 7> x{10, 40, 15, 25, 20, 35, 30}, y{-5, 0, 15, 10, 20, 60, 10};
    if (transpose) {
      for (auto& node : sc->nodes) {
        std::swap(node.w, node.h);
      }
      auto& mask = sc->exclusions.front();
      std::swap(mask.xl, mask.yl);
      std::swap(mask.xh, mask.yh);
      x.swap(y);
    }
    sc->build_index();
    Session session(Config{.open_browser = false});
    ASSERT_TRUE(session.start());
    session.begin_phase(sc, x.data(), y.data(), x.size());
    const auto measure = [&](std::string_view target) {
      rapidjson::Document data;
      data.Parse(session.agent_call(std::format(R"({{"cmd":"geometry","target":"{}"}})", target)).c_str());
      return data;
    };
    const auto baseline = measure("main");
    ASSERT_EQ(baseline["strips"].Size(), 1u);
    const auto& strip = baseline["strips"][0];
    EXPECT_DOUBLE_EQ(strip["free_dbu2"].GetDouble(), 850);  // 20 x 50 minus the 5 x 30 exclusion
    EXPECT_EQ(strip["standard_cells"].GetUint(), 2u);
    EXPECT_DOUBLE_EQ(strip["standard_cell_area_dbu2"].GetDouble(), 175);  // 75 partial + 100 fixed
    EXPECT_DOUBLE_EQ(strip["standard_cell_utilization"].GetDouble(), 175.0 / 850);
    ASSERT_EQ(strip["entrances"].Size(), 1u);
    const auto& mouth = strip["entrances"][0];
    EXPECT_EQ(mouth[transpose ? 1 : 0].GetDouble(), 25);
    EXPECT_EQ(mouth[transpose ? 3 : 2].GetDouble(), 40);
    EXPECT_EQ(mouth[transpose ? 0 : 1].GetDouble(), 50);
    ASSERT_EQ(baseline["clusters"].Size(), 1u);
    EXPECT_STREQ(baseline["clusters"][0]["name"].GetString(), "logic");
    EXPECT_DOUBLE_EQ(baseline["clusters"][0]["half_perimeter_dbu"].GetDouble(), 35);

    rapidjson::Document proposal;
    proposal.Parse(session.agent_call(std::format(R"({{"cmd":"explore","tier":"T0","patch":{{"cmd":"move","nodes":["partial"],"dx":[{}],"dy":[{}]}}}})",
                                                  transpose ? 0 : 20, transpose ? 20 : 0)).c_str());
    ASSERT_TRUE(proposal["ok"].GetBool());
    const auto candidate = measure(proposal["id"].GetString());
    EXPECT_DOUBLE_EQ(candidate["strips"][0]["standard_cell_area_dbu2"].GetDouble(), 150);
    EXPECT_DOUBLE_EQ(candidate["clusters"][0]["half_perimeter_dbu"].GetDouble(), 40);
    EXPECT_DOUBLE_EQ(measure("main")["strips"][0]["standard_cell_area_dbu2"].GetDouble(), 175);
    const auto id = std::string(proposal["id"].GetString());
    rapidjson::Document maps;
    maps.Parse(session.agent_call(std::format(R"({{"cmd":"maps","target":"{}"}})", id)).c_str());
    ASSERT_TRUE(maps["ok"].GetBool());
    ASSERT_EQ(maps["maps"].Size(), 1u);
    EXPECT_STREQ(maps["maps"][0]["kind"].GetString(), "density");
    EXPECT_EQ(maps["maps"][0]["frame"].GetUint64(), candidate["frame"].GetUint64());
    const auto base_map = session.map_binary("density");
    const auto frame = baseline["frame"].GetUint64();
    EXPECT_EQ(session.read_document(std::format("/map?kind=density&frame={}", frame)), base_map);
    EXPECT_THROW(session.read_document(std::format("/map?kind=density&frame={}", frame + 1)), std::runtime_error);
    const auto proposal_map = body_of(http(port_of(session.url()), "GET", "/map?kind=density&t=" + session.token() + "&x=" + id));
    ASSERT_EQ(base_map.size(), 24 + 32 * 32 * sizeof(float));
    ASSERT_EQ(proposal_map.size(), base_map.size());
    EXPECT_NE(proposal_map, base_map);
  }
}

TEST(GuiSession, OrientationProposalMirrorsPinOffsetsAndPreservesMain) {
  auto sc                           = scene();
  sc->nodes[2].allowed_orientations = {"N", "FN"};
  sc->nodes[2].signal_pins          = {
      {"A", 10, 80}
  };
  Session s(Config{.open_browser = false});
  ASSERT_TRUE(s.start());
  double  x[] = {1, 2, 500, 0}, y[] = {1, 2, 200, 0};
  s.begin_phase(sc, x, y, 4);
  rapidjson::Document reply;
  reply.Parse(s.agent_call(R"({"cmd":"explore","tier":"T0","patch":{"cmd":"move","nodes":["ram"],"orientation":"FN"}})").c_str());
  ASSERT_TRUE(reply["ok"].GetBool());
  const auto get = [&](std::string target) {
    rapidjson::Document doc;
    doc.Parse(s.agent_call(std::format(R"({{"cmd":"describe","target":"{}","name":"ram"}})", target)).c_str());
    return doc;
  };
  auto proposal = get(reply["id"].GetString());
  EXPECT_STREQ(proposal["node"]["orient"].GetString(), "FN");
  EXPECT_DOUBLE_EQ(proposal["node"]["signal_pins_dbu"][0]["x"].GetDouble(), 590);
  EXPECT_DOUBLE_EQ(get("main")["node"]["signal_pins_dbu"][0]["x"].GetDouble(), 510);
  const auto get_png = [&](const std::string& query) {
    return body_of(http(port_of(s.url()), "GET", "/screenshot.png?t=" + s.token() + "&" + query));
  };
  const auto id = std::string(reply["id"].GetString());
  const auto layers = "w=320&h=240&layers=-cells,pin_sides";
  const auto image = get_png("x=" + id + "&" + layers);
  EXPECT_EQ(image, get_png("layers=-cells%2Cpin_sides&w=320&h=240&x=%78" + id.substr(1)));
  EXPECT_NE(image, get_png(layers));
  reply.Parse(s.agent_call(R"({"cmd":"explore","tier":"T0","patch":{"cmd":"move","nodes":["ram"],"orientation":"FS"}})").c_str());
  EXPECT_FALSE(reply["ok"].GetBool());
}
