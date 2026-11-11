// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// The agent channel of the placement GUI (todo/livehd/2i-interact.html): a local process (an assistant, a script) asks
// questions and paints on the page. Highlights and the selection live in the session, so the page and the agent see the
// same thing; "critical" is answered by the flow thread through the timing provider.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <regex>
#include <unordered_set>

#include "gui_session.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::placement::gui {

namespace {
std::string esc(const std::string& s) {
  std::string out;
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
std::string num(double v) { return std::isfinite(v) ? std::format("{}", v) : "null"; }
std::string err_json(const std::string& why) { return std::format(R"({{"ok":false,"error":"{}"}})", esc(why)); }
const char* kind_name(Kind k) {
  switch (k) {
    case Kind::cell: return "cell";
    case Kind::macro: return "macro";
    case Kind::fixed: return "fixed";
    case Kind::blockage: return "blockage";
    case Kind::io: return "io";
  }
  return "?";
}
bool valid_color(const std::string& c) { return std::regex_match(c, std::regex(R"(^(#[0-9a-fA-F]{3,8}|[a-zA-Z]{3,20})$)")); }
constexpr size_t kMaxNames = 200000;
}  // namespace

std::string Session::node_details_locked(size_t i) const {
  if (!scene_ || i >= scene_->nodes.size()) {
    return "null";
  }
  const auto&  n   = scene_->nodes[i];
  const double um  = scene_->dbu_per_um > 0 ? scene_->dbu_per_um : 1;
  const double x   = frame_ && i < frame_->x.size() ? double(frame_->x[i]) : 0, y = frame_ && i < frame_->y.size() ? double(frame_->y[i]) : 0;
  return std::format(
      R"({{"name":"{}","kind":"{}","register":{},"master":"{}","class":"{}","x_um":{},"y_um":{},"w_um":{},"h_um":{},"area_um2":{},"movable":{},"pinned":{},"pins":{}}})",
      esc(n.name), kind_name(n.kind), n.seq ? "true" : "false", esc(n.master), esc(n.klass), num(x / um), num(y / um), num(n.w / um), num(n.h / um),
      num(n.w * n.h / (um * um)), n.movable ? "true" : "false", pins_.contains(n.name) ? "true" : "false", n.pins);
}

void Session::set_selection_locked(std::vector<std::string> names) {
  selection_count_ = names.size();
  if (names.size() > 5000) {
    names.resize(5000);
  }
  selection_ = std::move(names);
  ++sel_version_;
  bump_locked();
}

int Session::add_highlight_locked(const std::string& label, const std::string& color, const std::string& regex, const std::string& scope,
                                  std::vector<std::string> names, bool truncated) {
  std::erase_if(highlights_, [&](const Highlight& h) { return h.label == label; });  // the same label repaints
  Highlight h;
  h.id        = next_highlight_++;
  h.label     = label;
  h.color     = color;
  h.regex     = regex;
  h.scope     = scope;
  h.names     = std::move(names);
  h.truncated = truncated;
  highlights_.push_back(std::move(h));
  ++hl_version_;
  bump_locked();
  return highlights_.back().id;
}

std::string Session::highlights_json() const {
  std::lock_guard lk(m_);
  std::string     out = std::format(R"({{"v":{},"h":[)", hl_version_);
  for (size_t k = 0; k < highlights_.size(); ++k) {
    const auto& h = highlights_[k];
    std::string idx;
    size_t      count = 0;
    if (scene_) {
      for (const auto& name : h.names) {
        if (const auto it = scene_->index.find(name); it != scene_->index.end()) {
          idx += (count ? "," : "") + std::to_string(it->second);
          ++count;
        }
      }
    }
    out += std::format(R"({}{{"id":{},"label":"{}","color":"{}","regex":"{}","scope":"{}","total":{},"count":{},"truncated":{},"idx":[{}]}})", k ? "," : "",
                       h.id, esc(h.label), esc(h.color), esc(h.regex), esc(h.scope), h.names.size(), count, h.truncated ? "true" : "false", idx);
  }
  return out + "]}";
}

std::string Session::selection_json() const {
  std::lock_guard lk(m_);
  std::string     idx;
  size_t          count = 0;
  if (scene_) {
    for (const auto& name : selection_) {
      if (const auto it = scene_->index.find(name); it != scene_->index.end()) {
        idx += (count ? "," : "") + std::to_string(it->second);
        ++count;
      }
    }
  }
  return std::format(R"({{"v":{},"n":{},"idx":[{}]}})", sel_version_, selection_count_, idx);
}

std::string Session::agent_call(const std::string& json, int timeout_ms) {
  rapidjson::Document d;
  d.Parse(json.c_str());
  if (d.HasParseError() || !d.IsObject() || !d.HasMember("cmd") || !d["cmd"].IsString()) {
    return err_json("send a JSON object with a \"cmd\" (see cmd=help)");
  }
  const std::string cmd = d["cmd"].GetString();
  const auto        str = [&](const char* key, const std::string& def = "") {
    return d.HasMember(key) && d[key].IsString() ? std::string(d[key].GetString()) : def;
  };
  const auto integer = [&](const char* key, int def) { return d.HasMember(key) && d[key].IsInt() ? d[key].GetInt() : def; };
  const auto number  = [&](const char* key, double def) { return d.HasMember(key) && d[key].IsNumber() ? d[key].GetDouble() : def; };

  if (cmd == "help") {
    return R"HELP({"ok":true,"commands":{
"find":"{regex, scope?, field?, limit?} -> the matching nodes (nothing is painted). scope: all|cells|comb|regs|macros|io|movable|fixed|selected; field: name (default)|master|any",
"highlight":"{regex, scope?, field?, color?, label?} -> paint the matches in the page; the same label repaints",
"highlight_names":"{names:[...], color?, label?}",
"clear_highlight":"{id | label | all:true}",
"highlights":"{} -> what is painted",
"critical":"{n? (25), margin_ps?} -> the most critical cells (worst pin slack estimate through the flow's timer), painted as 'critical'",
"select":"{names:[...]} or {regex, scope?, field?} -> the page's selection",
"selection":"{} -> details of what the user selected in the page (name, kind, master, class, size/position in um, pins)",
"describe":"{name} -> the same details for one node",
"state":"{} -> phase, state, iteration, WNS/TNS, checkpoints",
"cmd":"{command: {...}} -> any page command (run, pause, continue, stage, move, unpin, centroid, map, timing, screenshot, restart, area {edge,delta um | box:[xl,yl,xh,yh]}, fork {box:[xl,yl,xh,yh] um | nodes:[...]} -> a new page for the region (state.forks), ...)"}})HELP";
  }
  if (cmd == "state") {
    return state_json();
  }
  if (cmd == "cmd") {  // the page's own command vocabulary (move, run, stage, ...), through the same queue
    if (!d.HasMember("command") || !d["command"].IsObject()) {
      return err_json("cmd needs a \"command\" object");
    }
    rapidjson::StringBuffer                    buf;
    rapidjson::Writer<rapidjson::StringBuffer> w(buf);
    d["command"].Accept(w);
    const auto err = submit(buf.GetString());
    return err.empty() ? R"({"ok":true})" : err_json(err);
  }

  if (cmd == "clear_highlight") {
    std::lock_guard lk(m_);
    const size_t    before = highlights_.size();
    if (d.HasMember("all") && d["all"].IsBool() && d["all"].GetBool()) {
      highlights_.clear();
    } else if (d.HasMember("id") && d["id"].IsInt()) {
      std::erase_if(highlights_, [&](const Highlight& h) { return h.id == d["id"].GetInt(); });
    } else if (d.HasMember("label") && d["label"].IsString()) {
      std::erase_if(highlights_, [&](const Highlight& h) { return h.label == d["label"].GetString(); });
    } else {
      return err_json("clear_highlight needs id, label or all:true");
    }
    ++hl_version_;
    bump_locked();
    return std::format(R"({{"ok":true,"removed":{}}})", before - highlights_.size());
  }
  if (cmd == "highlights") {
    std::lock_guard lk(m_);
    std::string     out = R"({"ok":true,"highlights":[)";
    for (size_t k = 0; k < highlights_.size(); ++k) {
      const auto& h = highlights_[k];
      out += std::format(R"({}{{"id":{},"label":"{}","color":"{}","regex":"{}","scope":"{}","count":{}}})", k ? "," : "", h.id, esc(h.label), esc(h.color),
                         esc(h.regex), esc(h.scope), h.names.size());
    }
    return out + "]}";
  }
  if (cmd == "selection") {
    std::lock_guard lk(m_);
    std::string     out = std::format(R"({{"ok":true,"count":{},"nodes":[)", selection_count_);
    size_t          shown = 0;
    if (scene_) {
      for (const auto& name : selection_) {
        const auto it = scene_->index.find(name);
        if (it == scene_->index.end() || shown >= 20) {
          continue;
        }
        out += (shown++ ? "," : "") + node_details_locked(size_t(it->second));
      }
    }
    return out + "]}";
  }
  if (cmd == "describe") {
    std::lock_guard lk(m_);
    if (!scene_) {
      return err_json("no design yet");
    }
    const auto it = scene_->index.find(str("name"));
    if (it == scene_->index.end()) {
      return err_json("no such node in the current phase: " + str("name"));
    }
    return R"({"ok":true,"node":)" + node_details_locked(size_t(it->second)) + "}";
  }

  if (cmd == "critical") {
    const int n = std::clamp(integer("n", 25), 1, 500);
    std::unique_lock lk(m_);
    if (finished_ || failed_ || !scene_) {
      return err_json("timing is only available while the flow is running or paused inside it");
    }
    crit_cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&] { return !crit_pending_; });  // one query at a time
    if (crit_pending_) {
      return err_json("another critical query is still waiting for the flow");
    }
    crit_request_.n         = n;
    crit_request_.margin_ps = number("margin_ps", -1);
    crit_pending_           = true;
    const auto before       = crit_seq_;
    cv_.notify_all();
    if (!crit_cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&] { return crit_seq_ != before || exit_.load(); })) {
      crit_pending_ = false;
      return err_json("timed out: the flow is inside a stage that does not poll; pause it or retry");
    }
    if (!crit_error_.empty()) {
      return err_json(crit_error_);
    }
    std::vector<std::string> names;
    std::string              cells;
    for (size_t k = 0; k < crit_result_.size(); ++k) {
      const auto& [name, slack] = crit_result_[k];
      names.push_back(name);
      const auto it = scene_->index.find(name);
      cells += std::format(R"({}{{"name":"{}","slack_ps":{},"master":"{}","register":{}}})", k ? "," : "", esc(name), num(slack),
                           it == scene_->index.end() ? "" : esc(scene_->nodes[size_t(it->second)].master),
                           it != scene_->index.end() && scene_->nodes[size_t(it->second)].seq ? "true" : "false");
    }
    const int id = add_highlight_locked("critical", str("color", "#ff4d4d"), "", "critical", std::move(names), false);
    return std::format(R"({{"ok":true,"id":{},"estimate":true,"wns_ps":{},"cells":[{}]}})", id, num(stats_.wns_ps), cells);
  }

  // The rest select nodes by name/regex: find, highlight, highlight_names, select.
  std::lock_guard lk(m_);
  if (!scene_) {
    return err_json("no design yet");
  }
  const auto& s     = *scene_;
  std::vector<std::string> names;
  bool                     truncated = false;
  std::string              regex_text = str("regex"), scope = str("scope", "all");
  if (cmd == "highlight_names" || (cmd == "select" && d.HasMember("names"))) {
    if (!d.HasMember("names") || !d["names"].IsArray()) {
      return err_json("names must be an array of strings");
    }
    for (const auto& v : d["names"].GetArray()) {
      if (!v.IsString()) {
        return err_json("names must be strings");
      }
      std::string name = v.GetString();
      if (!name.empty() && name[0] == '#') {  // node N of the current scene, as the page sends them
        const long i = std::strtol(name.c_str() + 1, nullptr, 10);
        if (i < 0 || size_t(i) >= s.nodes.size()) {
          return err_json("unknown node " + name);
        }
        name = s.nodes[size_t(i)].name;
      }
      if (names.size() >= kMaxNames) {
        truncated = true;
        break;
      }
      names.push_back(std::move(name));
    }
  } else if (cmd == "find" || cmd == "highlight" || cmd == "select") {
    if (regex_text.empty()) {
      return err_json(cmd + " needs a regex");
    }
    std::regex re;
    try {
      re = std::regex(regex_text, std::regex::ECMAScript | std::regex::optimize);
    } catch (const std::regex_error& e) {
      return err_json(std::string("bad regex: ") + e.what());
    }
    static const std::unordered_set<std::string> scopes{"all", "cells", "comb", "regs", "macros", "io", "movable", "fixed", "selected"};
    if (!scopes.contains(scope)) {
      return err_json("scope must be one of all, cells, comb, regs, macros, io, movable, fixed, selected");
    }
    const std::string field = str("field", "name");
    if (field != "name" && field != "master" && field != "any") {
      return err_json("field must be name, master or any");
    }
    std::unordered_set<std::string> chosen;
    if (scope == "selected") {
      chosen.insert(selection_.begin(), selection_.end());
    }
    for (const auto& n : s.nodes) {
      bool in = false;
      if (scope == "all") {
        in = n.kind != Kind::blockage;
      } else if (scope == "cells") {
        in = n.kind == Kind::cell;
      } else if (scope == "comb") {
        in = n.kind == Kind::cell && !n.seq;
      } else if (scope == "regs") {
        in = n.kind == Kind::cell && n.seq;
      } else if (scope == "macros") {
        in = n.kind == Kind::macro || n.kind == Kind::fixed;
      } else if (scope == "io") {
        in = n.kind == Kind::io;
      } else if (scope == "movable") {
        in = n.movable;
      } else if (scope == "fixed") {
        in = !n.movable && n.kind != Kind::io && n.kind != Kind::blockage;
      } else {
        in = chosen.contains(n.name);
      }
      if (!in) {
        continue;
      }
      const bool hit = (field != "master" && std::regex_search(n.name, re)) || (field != "name" && std::regex_search(n.master, re));
      if (hit) {
        if (names.size() >= kMaxNames) {
          truncated = true;
          break;
        }
        names.push_back(n.name);
      }
    }
  } else {
    return err_json("unknown cmd '" + cmd + "' (see cmd=help)");
  }
  std::string sample;
  for (size_t k = 0; k < names.size() && k < (cmd == "find" ? size_t(std::clamp(integer("limit", 50), 1, 1000)) : size_t(15)); ++k) {
    sample += std::format("{}\"{}\"", k ? "," : "", esc(names[k]));
  }
  if (cmd == "find") {
    return std::format(R"({{"ok":true,"count":{},"truncated":{},"names":[{}]}})", names.size(), truncated ? "true" : "false", sample);
  }
  if (cmd == "select") {
    const size_t count = names.size();
    set_selection_locked(std::move(names));
    return std::format(R"({{"ok":true,"count":{},"sample":[{}]}})", count, sample);
  }
  std::string label = str("label", regex_text.empty() ? "highlight" : regex_text), color = str("color");
  if (color.empty()) {
    static const char* palette[] = {"#ffd23f", "#3ddc97", "#ff6b6b", "#4cc9f0", "#f78fb3", "#b794f6"};
    color                        = palette[size_t(next_highlight_) % 6];
  }
  if (!valid_color(color)) {
    return err_json("color must be #rgb, #rrggbb or a CSS colour name");
  }
  const size_t count = names.size();
  const int    id    = add_highlight_locked(label, color, regex_text, scope, std::move(names), truncated);
  return std::format(R"({{"ok":true,"id":{},"label":"{}","color":"{}","count":{},"truncated":{},"sample":[{}]}})", id, esc(label), esc(color), count,
                     truncated ? "true" : "false", sample);
}

}  // namespace livehd::placement::gui
