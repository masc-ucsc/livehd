// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// HOW THIS FILE READS IN-EDGES: PINS, NEVER inp_edges()
//
// Every in-edge walk here is pin-centric, on the FLAT handle:
//   for (const auto& sink : n.inp_sorted_pins()) sink.get_driver_pin()
// (one driver per sink pin; `sink` IS what `edge.sink` used to be), with
// inp_pins_snapshot() where the arm INDEXES its operands, and on the
// HIERARCHICAL one:
//   for (const auto& sink : occ.inp_sorted_pins())
//     for (const auto& drv : sink.get_driver_pins()) ...
//
// Two readers in that shape are load-bearing, and swapping either one is a
// silent miscompile rather than a build error:
//
//  1. inp_sorted_pins(), NOT the raw inp_pins(). hhds stores port 0 as the node
//     itself, so the raw list OMITS it -- and port 0 is where a banked cell's
//     FIRST operand lives. The sorted reader yields the node-as-pin first and
//     then ascending port order, which is exactly the sequence inp_edges()
//     walked; the raw one drops an operand and loses the ordering.
//
//  2. the PLURAL get_driver_pins() on any sink that can belong to a Sub. A
//     compact loop's carry-in sink is the one pin pass/legalize sanctions with
//     TWO drivers (the seed, and a self edge from the same instance's carry-out
//     meaning "the previous ordinal"); hierarchically it resolves to the
//     previous ordinal's output PLUS the inactive-carry bypass. The singular
//     get_driver_pin() returns only the first and its assert compiles out under
//     -DNDEBUG. Walks whose op filter has already excluded Sub use the singular
//     form.
//
// The nested hierarchical walk reproduces Occurrence_node::inp_edges() term for
// term -- that reader was itself `for (sink : inp_sorted_pins()) for (edge :
// sink.inp_edges())` -- so anything that COUNTS or INDEXES occurrence inputs
// (Color_plan's `consumer_input` is literally "the input position", and the
// occurrence/definition code compares the two sequences by SIZE and by
// POSITION) still ticks once per DRIVER, which is what the edge walk counted.
// occurrence_sorted_inputs / occurrence_driver_on_port below are the two shared
// spellings of it.
//
// OUT-edges are a different question: out_edges() still encodes FANOUT that an
// out-pin walk would drop, so the digest keeps it (see sim_graph_digest).

#include "cgen_sim.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <print>
#include <regex>
#include <string>
#include <tuple>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "attrs.hpp"
#include "cell.hpp"  // Ntype / Ntype_op
#include "cgen_llvm.hpp"
#include "cgen_salt.hpp"  // livehd::kCgenSrcSalt — emitter content hash (L2)
#include "cpp_ident.hpp"  // livehd::cpp_ident — THE shared RTL-name -> C++ identifier rule
#include "diag.hpp"       // livehd::diag::err — Stage 0 comb-loop safety net
#include "file_name.hpp"  // livehd::unit_file_stem — the shared long-name policy
#include "hash_util.hpp"
#include "latch_contract.hpp"  // //graph — clock_op_of (the ONE shared ICG recognizer)
#include "node_util.hpp"
#include "occurrence_materialize.hpp"  // //graph — realize native loop groups in the private simulator library
#include "sim_color_plan.hpp"
#include "sim_tune_rt.hpp"    // kUnknownLiteralHelper / kTuneHashHelper — generated runtime text shared with prp_sim
#include "split_selfref.hpp"  // //graph — word-level cycle analysis for scheduling
#include "str_tools.hpp"      // str_tools::ends_with

using livehd::graph_util::bits_of;
using livehd::graph_util::const_of;
using livehd::graph_util::debug_name;
using livehd::graph_util::default_instance_name;
using livehd::graph_util::get_driver_of_sink_name;
using livehd::graph_util::is_type_flop;
using livehd::graph_util::is_type_register;
using livehd::graph_util::is_type_sub;
using livehd::graph_util::is_unsign;
using livehd::graph_util::pin_name_of;
using livehd::graph_util::real_width;
using livehd::graph_util::type_op_of;
using livehd::graph_util::wire_name;

namespace {
// Width of a pin, floored at 1 (Slop<N> requires N >= 1).
int wbits_of(const hhds::Pin_class& pin) {
  int b = pin.is_invalid() ? 1 : real_width(pin);
  return b <= 0 ? 1 : b;
}

const char* op_name(Ntype_op op) { return Ntype::get_name(op).data(); }

// ---- Dead-temporary sweep over ONE finished method body ----
//
// Binding is demand-driven and cone-based, so a temporary can be emitted for a
// value the method never reads: the settle pass walks a Moore-deferred
// instance's whole input cone (build_settle_cone traverses the Sub) but only
// the RISE emits that instance's `cycle(in)` call, and a group-scheduled slice
// read binds a pin whose consumer turns out to live in a different pass. The
// host compiler drops them at -O, but they still cost compile time and raise
// -Wunused-variable on every one.
//
// A dead line's operands can die with it (dropping the last reader of a group
// snapshot strands the snapshot too), so iterate to a fixpoint. A PURE
// initializer is deleted outright; an IMPURE one keeps its call as a discarded
// statement -- a memory read resolves through Slop::unknown when the address is
// not a plain integer or an `ordering="none"` write collides, and that is a
// draw from the seeded process PRNG.
//
// Every name matched here is a block-scope local of this method, so "used
// nowhere else in the body" is exactly "dead".

// The name a line DECLARES, or empty when the line is not a declaration this
// sweep owns. Covers the forms the emitter mints: `Slop<W> v = e;`,
// `Slop_u<W> v = e;` (and their brace-init spellings), plus `auto v = e;`.
std::string_view declared_temp(std::string_view line) {
  auto rest = line.substr(std::min(line.find_first_not_of(" \t"), line.size()));
  if (rest.starts_with("Slop<") || rest.starts_with("Slop_u<")) {
    const auto gt = rest.find('>');  // the width is an integer literal, never nested
    if (gt == std::string_view::npos) {
      return {};
    }
    rest = rest.substr(gt + 1);
  } else if (rest.starts_with("auto ")) {
    rest = rest.substr(5);
  } else {
    return {};
  }
  rest = rest.substr(std::min(rest.find_first_not_of(" \t"), rest.size()));
  if (rest.empty() || (std::isalpha(static_cast<unsigned char>(rest.front())) == 0 && rest.front() != '_')) {
    return {};
  }
  size_t n = 1;
  while (n < rest.size() && (std::isalnum(static_cast<unsigned char>(rest[n])) != 0 || rest[n] == '_')) {
    ++n;
  }
  // Must be the whole declarator: an initializer follows immediately.
  auto tail = rest.substr(n);
  tail      = tail.substr(std::min(tail.find_first_not_of(" \t"), tail.size()));
  if (!tail.starts_with('=') && !tail.starts_with('{')) {
    return {};
  }
  return rest.substr(0, n);
}

// A single-line declaration whose initializer has no side effect. Root/compact
// evaluator calls and memory reads are the exceptions; staged writes are
// statements, never initializers, but reject them too so a future emission
// shape cannot silently become removable.
bool pure_temp_decl(std::string_view line) {
  const auto code = line.substr(0, line.find("//"));
  if (code.find(';') == std::string_view::npos) {
    return false;  // not a complete statement on this line
  }
  for (auto bad : {".read<", ".read(", ".read_all(", ".cycle(", "__compact_", "slop_update", "apply_update"}) {
    if (line.find(bad) != std::string_view::npos) {
      return false;
    }
  }
  return true;
}

// A dead call cannot be deleted (see above), but it can stop DECLARING the
// value nobody reads: `Slop<W> v = mem.read<r>(a);` becomes the plain statement
// `mem.read<r>(a);`, which keeps the side effect and lets the host compiler
// drop the rest. Empty when the initializer is not a plain `= <call>;` (the
// `{}` spelling is only ever a pure Slop expression, deleted outright).
std::string discard_dead_call(std::string_view line, std::string_view name) {
  const auto eq = line.find(" = ", line.find(name));
  if (eq == std::string_view::npos) {
    return {};
  }
  auto rhs = line.substr(eq + 3);
  while (!rhs.empty() && (rhs.back() == '\n' || rhs.back() == '\r')) {
    rhs.remove_suffix(1);
  }
  const auto tag = rhs.find("//") == std::string_view::npos ? "  // value unused" : " (value unused)";
  return absl::StrCat("    ", rhs, tag, "\n");
}

// One line per element, each keeping its trailing '\n'.
std::vector<std::string> split_body_lines(const std::string& body) {
  std::vector<std::string> lines;
  for (size_t pos = 0; pos < body.size();) {
    const auto nl  = body.find('\n', pos);
    const auto end = nl == std::string::npos ? body.size() : nl + 1;
    lines.emplace_back(body, pos, end - pos);
    pos = end;
  }
  return lines;
}

// identifier -> occurrences in the LIVE body.
template <typename Key>
void count_identifier_uses(const std::vector<std::string>& lines, const std::vector<bool>& alive,
                           absl::flat_hash_map<Key, int>& uses) {
  uses.clear();
  for (size_t i = 0; i < lines.size(); ++i) {
    if (!alive[i]) {
      continue;
    }
    const std::string_view l = lines[i];
    for (size_t p = 0; p < l.size();) {
      if (std::isalpha(static_cast<unsigned char>(l[p])) == 0 && l[p] != '_') {
        ++p;
        continue;
      }
      size_t n = p + 1;
      while (n < l.size() && (std::isalnum(static_cast<unsigned char>(l[n])) != 0 || l[n] == '_')) {
        ++n;
      }
      ++uses[Key{l.substr(p, n - p)}];
      p = n;
    }
  }
}

std::string join_live_lines(const std::vector<std::string>& lines, const std::vector<bool>& alive) {
  std::string out;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (alive[i]) {
      out.append(lines[i]);
    }
  }
  return out;
}

size_t find_identifier(std::string_view text, std::string_view id);

void strip_dead_temps(std::string& body) {
  std::vector<std::string> lines = split_body_lines(body);

  // Generated temporaries are SSA and definitions precede their uses. A single
  // backward liveness walk therefore computes the same transitive dead chain
  // as the old fixpoint, without rescanning the whole body once per link. That
  // fixpoint was quadratic on Minion evaluator bodies.
  std::vector<bool>                alive(lines.size(), true);
  absl::flat_hash_set<std::string> live;
  bool                             any      = false;
  const auto                       add_uses = [&live](std::string_view line, std::string_view declared) {
    const size_t declaration_at = declared.empty() ? std::string_view::npos : find_identifier(line, declared);
    for (size_t p = 0; p < line.size();) {
      if (std::isalpha(static_cast<unsigned char>(line[p])) == 0 && line[p] != '_') {
        ++p;
        continue;
      }
      size_t n = p + 1;
      while (n < line.size() && (std::isalnum(static_cast<unsigned char>(line[n])) != 0 || line[n] == '_')) {
        ++n;
      }
      if (p != declaration_at) {
        live.emplace(line.substr(p, n - p));
      }
      p = n;
    }
  };

  for (size_t i = lines.size(); i-- > 0;) {
    const std::string name{declared_temp(lines[i])};
    if (!name.empty() && !live.contains(name)) {
      if (pure_temp_decl(lines[i])) {
        alive[i] = false;
        any      = true;
        continue;  // its operands are dead unless another live line reads them
      }
      if (auto discarded = discard_dead_call(lines[i], name); !discarded.empty()) {
        lines[i] = std::move(discarded);
        any      = true;
        add_uses(lines[i], {});  // the retained side-effecting call still reads its operands
        continue;
      }
    }
    if (!name.empty()) {
      live.erase(name);  // this definition satisfies every later use
    }
    add_uses(lines[i], name);
  }
  if (!any) {
    return;
  }
  body = join_live_lines(lines, alive);
}

// ---- Single-use temporary inlining over ONE finished method body ----
//
// Binding is per-node, so a value read exactly once still costs its own named
// `Slop<W>` local -- and a color body of a large design is mostly such one-shot
// locals. Fold the definition into its use when that use is the very NEXT live
// statement: ADJACENCY is what makes the move legal, because no other statement
// can run (and therefore no operand can change) in between. That is the whole
// safety argument, so do not relax it to "the single use, wherever it is": a
// `__rt.__color_slot_*` read folded past the write of that slot, or a memory
// read folded past a staged write, would change the value.

// Position of `id` in `text` as a WHOLE identifier, or npos.
size_t find_identifier(std::string_view text, std::string_view id) {
  for (auto at = text.find(id); at != std::string_view::npos; at = text.find(id, at + 1)) {
    const auto after = at + id.size();
    const bool left  = at == 0 || (std::isalnum(static_cast<unsigned char>(text[at - 1])) == 0 && text[at - 1] != '_');
    const bool right = after == text.size() || (std::isalnum(static_cast<unsigned char>(text[after])) == 0 && text[after] != '_');
    if (left && right) {
      return at;
    }
  }
  return std::string_view::npos;
}

// The text that replaces the folded name: the declaration's own initializer.
// The declared type has to be RESTATED unless the initializer already carries
// it -- the `Slop<W> v{e}` spelling IS a width conversion, and a copy
// initializer only provably yields `Slop<W>` when it is that same type's static
// factory (`Slop<W>::add_op(...)`, the shape node_expr emits). `auto` is
// decltype(e) by definition and needs nothing. Empty when the line is not a
// single-statement declaration this sweep can fold.
std::string inlined_initializer(std::string_view line, std::string_view name) {
  auto code = line.substr(0, line.find("//"));
  while (!code.empty() && (code.back() == '\n' || code.back() == '\r' || code.back() == ' ' || code.back() == '\t')) {
    code.remove_suffix(1);
  }
  if (code.empty() || code.back() != ';') {
    return {};
  }
  code.remove_suffix(1);

  auto             head = code.substr(std::min(code.find_first_not_of(" \t"), code.size()));
  std::string_view type;
  if (head.starts_with("Slop<") || head.starts_with("Slop_u<")) {
    const auto gt = head.find('>');  // the width is an integer literal, never nested
    if (gt == std::string_view::npos) {
      return {};
    }
    type = head.substr(0, gt + 1);
  } else if (!head.starts_with("auto ")) {
    return {};
  }

  const auto at = find_identifier(code, name);
  if (at == std::string_view::npos) {
    return {};
  }
  auto init       = code.substr(at + name.size());
  init            = init.substr(std::min(init.find_first_not_of(" \t"), init.size()));
  bool brace_form = false;
  if (init.starts_with('=')) {
    init.remove_prefix(1);
  } else if (init.starts_with('{')) {
    if (!init.ends_with('}')) {
      return {};
    }
    init.remove_prefix(1);
    init.remove_suffix(1);
    brace_form = true;
  } else {
    return {};
  }
  init = init.substr(std::min(init.find_first_not_of(" \t"), init.size()));
  if (init.empty()) {
    return {};
  }
  if (type.empty() || (!brace_form && init.starts_with(absl::StrCat(type, "::")))) {
    return absl::StrCat("(", init, ")");
  }
  return absl::StrCat(type, "{", init, "}");
}

void inline_single_use_temps(std::string& body) {
  std::vector<std::string> lines = split_body_lines(body);

  std::vector<bool>                     alive(lines.size(), true);
  bool                                  changed = true;
  bool                                  any     = false;
  // OWNING keys: a fold rewrites the use line, which would dangle views into
  // it. A fold never changes any OTHER temporary's count (the initializer's
  // operands merely move from the dropped line into the use line, and the
  // folded name leaves both), so ONE count serves every sweep. Recounting per
  // sweep hashed every identifier of the body once per fixpoint round: most of
  // this pass's time on minion's multi-MB evaluator bodies, for the same answer.
  absl::flat_hash_map<std::string, int> uses;
  count_identifier_uses(lines, alive, uses);
  while (changed) {
    changed = false;
    for (size_t i = 0; i < lines.size(); ++i) {
      if (!alive[i]) {
        continue;
      }
      const std::string name{declared_temp(lines[i])};
      // Exactly two occurrences: this declaration, and the one read.
      if (name.empty() || uses[name] != 2 || !pure_temp_decl(lines[i])) {
        continue;
      }
      size_t use = i + 1;
      while (use < lines.size() && !alive[use]) {
        ++use;
      }
      if (use == lines.size()) {
        continue;
      }
      const std::string_view use_line = lines[use];
      const auto             at       = find_identifier(use_line, name);
      if (at == std::string_view::npos) {
        continue;  // the single read is not the next statement
      }
      // A loop header would re-evaluate the folded expression once per
      // iteration: same value, more work.
      if (find_identifier(use_line, "for") != std::string_view::npos
          || find_identifier(use_line, "while") != std::string_view::npos) {
        continue;
      }
      auto replacement = inlined_initializer(lines[i], name);
      if (replacement.empty()) {
        continue;
      }
      // Folding a long chain end to end builds ONE deeply nested expression,
      // which costs the host compiler more than the named temporaries it
      // replaces. Stop growing a line past this budget and leave the rest of
      // the chain in SSA form.
      // Clang's -O2 cost grows sharply on the deeply nested template trees
      // formed by long folds. Minion's 4 KiB budget saved source lines but
      // doubled the host-build CPU time; keeping folds local retains the useful
      // declaration cleanup without building giant expression types.
      constexpr size_t kMaxFoldedLine = 768;
      if (use_line.size() + replacement.size() > kMaxFoldedLine) {
        continue;
      }
      lines[use] = absl::StrCat(use_line.substr(0, at), replacement, use_line.substr(at + name.size()));
      alive[i]   = false;
      changed    = true;
      any        = true;
    }
  }
  if (!any) {
    return;
  }
  body = join_live_lines(lines, alive);
}

// Walk `s` from `open` (which holds `oc`) to the MATCHING `cc`, skipping string
// literals; npos when unbalanced. The generated body carries `"..."` literals
// (assert texts, from_pyrope constants), so a naive bracket count can pair the
// wrong delimiters and a textual peephole would then rewrite across them.
size_t match_delim(std::string_view s, size_t open, char oc, char cc) {
  int  depth  = 0;
  bool in_str = false;
  for (size_t i = open; i < s.size(); ++i) {
    const char c = s[i];
    if (in_str) {
      if (c == '\\') {
        ++i;
      } else if (c == '"') {
        in_str = false;
      }
      continue;
    }
    if (c == '"') {
      in_str = true;
      continue;
    }
    depth += c == oc;
    depth -= c == cc;
    if (depth == 0) {
      return i;
    }
  }
  return std::string_view::npos;
}

// True when `s` is EXACTLY one `oc`..`cc` group spanning the whole view.
bool is_whole_delimited(std::string_view s, char oc, char cc) {
  return s.size() >= 2 && s.front() == oc && match_delim(s, 0, oc, cc) == s.size() - 1;
}

// `s` with any redundant wrapping parens removed.
std::string_view unwrap_parens(std::string_view s) {
  while (is_whole_delimited(s, '(', ')')) {
    s = s.substr(1, s.size() - 2);
  }
  return s;
}

// AND-reduction only observes [0, bits). A captured Sext expression may outlive
// its source's scheduler binding; peel signed conversions that leave those bits
// unchanged instead of looking up that no-longer-bound source again.
std::string reduction_operand(std::string text, int bits) {
  auto       value  = unwrap_parens(text);
  const auto suffix = absl::StrCat(".sext_op(", bits - 1, ")");
  while (true) {
    if (value.starts_with("Slop<")) {
      const auto close = value.find('>');
      int        width = 0;
      if (close != std::string_view::npos) {
        const auto parsed = std::from_chars(value.data() + 5, value.data() + close, width);
        const auto group  = value.substr(close + 1);
        if (parsed.ec == std::errc{} && parsed.ptr == value.data() + close && width >= bits
            && is_whole_delimited(group, '{', '}')) {
          value = unwrap_parens(group.substr(1, group.size() - 2));
          continue;
        }
      }
    }
    if (value.ends_with(suffix)) {
      const auto base  = value.substr(0, value.size() - suffix.size());
      // A member receiver is a whole group, a signed constructor, or a simple
      // identifier. Never strip a suffix from one branch of a larger expression.
      const auto brace = base.starts_with("Slop<") ? base.find('{') : std::string_view::npos;
      if (is_whole_delimited(base, '(', ')')
          || (brace != std::string_view::npos && is_whole_delimited(base.substr(brace), '{', '}'))
          || (!base.empty()
              && base.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")
                     == std::string_view::npos)) {
        value = unwrap_parens(base);
        continue;
      }
    }
    return std::string(value);
  }
}

void fold_reduction_sources(std::string& body) {
  constexpr std::string_view call = "::rand_op(";
  for (size_t scan = 0; scan < body.size();) {
    // Skip literals, including escaped quotes in assertion messages.
    if (body[scan] == '"') {
      ++scan;
      while (scan < body.size() && body[scan] != '"') {
        scan += body[scan] == '\\' && scan + 1 < body.size() ? 2 : 1;
      }
      scan += scan < body.size();
      continue;
    }
    if (body.compare(scan, call.size(), call) != 0) {
      ++scan;
      continue;
    }
    const auto open  = scan + call.size() - 1;
    const auto close = match_delim(body, open, '(', ')');
    if (close == std::string::npos) {
      break;
    }
    const auto comma = body.rfind(',', close);
    if (comma != std::string::npos && comma > open) {
      auto count = std::string_view(body).substr(comma + 1, close - comma - 1);
      while (!count.empty() && std::isspace(static_cast<unsigned char>(count.front()))) {
        count.remove_prefix(1);
      }
      int        bits   = 0;
      const auto parsed = std::from_chars(count.data(), count.data() + count.size(), bits);
      if (parsed.ec == std::errc{} && parsed.ptr == count.data() + count.size() && bits > 0) {
        const auto source  = body.substr(open + 1, comma - open - 1);
        const auto reduced = reduction_operand(source, bits);
        body.replace(open + 1, source.size(), reduced);
      }
    }
    scan = open + 1;
  }
}

// `Slop<N>{Slop<N>{x}}` is a copy construct wrapped in a copy construct: the
// landing restates a conversion the operation already made. The constructor
// analogue of append_zext's fuse, and like it a textual pass because the two
// wraps come from different emitters (land_operation puts one on, the landing
// the other).
void collapse_same_width_wraps(std::string& body) {
  // A `Slop<N>` / `Slop_u<N>` token ending at `at` (exclusive), or empty.
  const auto type_before = [&](size_t at) -> std::string_view {
    if (at == 0 || body[at - 1] != '>') {
      return {};
    }
    const auto lt = body.rfind('<', at - 1);
    if (lt == std::string::npos || lt + 1 >= at - 1) {
      return {};
    }
    for (size_t i = lt + 1; i + 1 < at; ++i) {
      if (body[i] < '0' || body[i] > '9') {
        return {};
      }
    }
    for (const std::string_view name : {std::string_view{"Slop_u"}, std::string_view{"Slop"}}) {
      if (lt >= name.size() && body.compare(lt - name.size(), name.size(), name) == 0) {
        return std::string_view{body}.substr(lt - name.size(), at - (lt - name.size()));
      }
    }
    return {};
  };
  const auto match_brace = [&](size_t open) { return match_delim(body, open, '{', '}'); };

  // Only braces OUTSIDE a string literal are structure; a `{` inside an assert
  // message or a from_pyrope constant is text. Collected in ONE forward scan --
  // these bodies are large, so a per-brace rescan would be quadratic.
  std::vector<size_t> opens;
  {
    bool in_str = false;
    for (size_t i = 0; i < body.size(); ++i) {
      if (in_str) {
        if (body[i] == '\\') {
          ++i;
        } else if (body[i] == '"') {
          in_str = false;
        }
      } else if (body[i] == '"') {
        in_str = true;
      } else if (body[i] == '{') {
        opens.push_back(i);
      }
    }
  }

  // Collapsing shifts everything after the outer `{`, so walk the positions
  // from the LAST one back: the earlier offsets stay valid.
  for (auto it = opens.rbegin(); it != opens.rend(); ++it) {
    const auto open1 = *it;
    if (open1 >= body.size() || body[open1] != '{') {
      continue;
    }
    const auto outer = type_before(open1);
    if (outer.empty()) {
      continue;
    }
    const auto close1 = match_brace(open1);
    if (close1 == std::string::npos) {
      continue;
    }
    size_t inner_start = open1 + 1;
    while (inner_start < close1 && body[inner_start] == ' ') {
      ++inner_start;
    }
    if (body.compare(inner_start, outer.size(), outer) != 0 || body[inner_start + outer.size()] != '{') {
      continue;
    }
    const auto open2  = inner_start + outer.size();
    const auto close2 = match_brace(open2);
    if (close2 == std::string::npos) {
      continue;
    }
    size_t after = close2 + 1;
    while (after < close1 && body[after] == ' ') {
      ++after;
    }
    if (after != close1) {
      continue;  // the inner wrap is not the whole operand
    }
    // A triple wrap collapses one level per position, and the NEXT position out
    // is still ahead in this reverse walk, so it sees the collapsed form.
    body.replace(open1, close1 + 1 - open1, std::string_view{body}.substr(open2, close2 + 1 - open2));
  }
}

// `a != b`, `a <= b` and `a >= b` have NO LGraph cell: upass_tolg lowers each to
// Xor(EQ/GT/LT(a, b), 1) (lower_negated; inou/slang emits Not(EQ), which cprop
// rewrites to the same shape). The fused statics say it in one call, with no
// intermediate compare and no 1-constant to xor against.
//
// A textual pass, and deliberately AFTER forestation: the compare only appears
// inside the xor once forestation has folded it there, and it only folds a
// SINGLE-USE temp -- which is exactly the condition under which re-deriving the
// compare here is free rather than a second evaluation.
void fold_negated_compares(std::string& body) {
  static constexpr std::string_view kXor = "::xor_op(";
  static constexpr std::pair<std::string_view, std::string_view> kNeg[]
      = {{"eq_op(", "ne_op("}, {"lt_op(", "ge_op("}, {"gt_op(", "le_op("}};

  // Everything between `at` and the matching close paren of the call that opens
  // at `open`, split on TOP-LEVEL commas (string literals skipped).
  const auto split_args = [](std::string_view in) {
    std::vector<std::string_view> args;
    int                           depth  = 0;
    bool                          in_str = false;
    size_t                        start  = 0;
    for (size_t i = 0; i < in.size(); ++i) {
      const char c = in[i];
      if (in_str) {
        if (c == '\\') {
          ++i;
        } else if (c == '"') {
          in_str = false;
        }
        continue;
      }
      if (c == '"') {
        in_str = true;
      } else if (c == '(' || c == '<' || c == '{') {
        ++depth;
      } else if (c == ')' || c == '>' || c == '}') {
        --depth;
      } else if (c == ',' && depth == 0) {
        args.push_back(in.substr(start, i - start));
        start = i + 1;
      }
      if (depth < 0) {
        // `->` in a lambda return type unbalances the `<`/`>` count kept for
        // template arguments. Give up rather than split in the wrong place.
        return std::vector<std::string_view>{};
      }
    }
    if (depth != 0) {
      return std::vector<std::string_view>{};
    }
    args.push_back(in.substr(start));
    for (auto& a : args) {
      while (!a.empty() && a.front() == ' ') {
        a.remove_prefix(1);
      }
      while (!a.empty() && a.back() == ' ') {
        a.remove_suffix(1);
      }
    }
    return args;
  };
  // Both sweeps build their output in ONE forward pass. Rewriting in place
  // (`body.replace` per match) moved the whole tail of the body per fold: on
  // minion's multi-MB evaluator bodies, with thousands of `!=` compares, that
  // was most of compact_body_temps. A replacement is swept again on its own
  // (recursively) -- the in-place version re-scanned from its start -- because
  // the compare's operands can hold further negated compares; nothing outside
  // the replacement can, so the scan then resumes after it.
  std::function<std::string(std::string_view)> fold_xor = [&](std::string_view text) {
    std::string out;
    size_t      copied = 0;
    for (size_t scan = 0;;) {
      const auto at = text.find(kXor, scan);
      if (at == std::string_view::npos) {
        break;
      }
      scan              = at + kXor.size();
      const size_t open = at + kXor.size() - 1;
      const auto   close = match_delim(text, open, '(', ')');
      if (close == std::string_view::npos) {
        break;
      }
      // The receiver: `Slop<N>` (or `Slop_u<N>`) immediately before `::xor_op`,
      // i.e. the last "Slop" before `at` with no "::" between the two. Walked
      // BACKWARD from `at` by hand: libc++'s string_view::rfind(const char*) is
      // std::find_end, a FORWARD scan from the start of the text, so it cost
      // O(body) per xor and made this sweep quadratic (100 ms per 600 KB
      // evaluator body, eight such bodies on minion).
      size_t recv_start = std::string_view::npos;
      for (size_t p = at; p-- > copied;) {
        if (text.compare(p, 2, "::") == 0) {
          break;  // another scope ends first: `at` has no Slop receiver
        }
        if (text.compare(p, 4, "Slop") == 0) {
          recv_start = p;
          break;
        }
      }
      if (recv_start == std::string_view::npos) {
        continue;
      }
      const auto args = split_args(text.substr(open + 1, close - open - 1));
      if (args.size() != 2) {
        continue;
      }
      for (int ci = 0; ci < 2; ++ci) {
        const auto konst = unwrap_parens(args[ci]);
        const auto cmp   = unwrap_parens(args[1 - ci]);
        if (!konst.ends_with("::create_integer(1)") || !konst.starts_with("Slop")) {
          continue;
        }
        const auto scope = cmp.find(">::");
        if (!cmp.starts_with("Slop") || scope == std::string_view::npos || !cmp.ends_with(")")) {
          continue;
        }
        const auto tail = cmp.substr(scope + 3);
        for (const auto& [from, to] : kNeg) {
          if (!tail.starts_with(from)) {
            continue;
          }
          // The compare must BE the whole operand, not a sub-term.
          if (match_delim(tail, from.size() - 1, '(', ')') != tail.size() - 1) {
            break;
          }
          // Keep the XOR's own receiver width: the negated compare lands where
          // the xor did.
          out.append(text.substr(copied, recv_start - copied));
          out.append(fold_xor(absl::StrCat(text.substr(recv_start, at - recv_start), "::", to, tail.substr(from.size()))));
          copied = close + 1;
          scan   = copied;
          break;
        }
        break;
      }
    }
    if (copied == 0) {
      return std::string(text);
    }
    out.append(text.substr(copied));
    return out;
  };
  body = fold_xor(body);

  // Second spelling of the same chain. `Xor(x, 1)` on a ONE-BIT x IS the
  // all-ones complement at that width, so the Xor arm emits it as
  // `Slop_u<1>::not_op(x)` -- still the negated compare when x is one, and only
  // at width 1 (a wider Slop_u complement is 2^N-1-x, not 1-x).
  static constexpr std::string_view kNot1 = "Slop_u<1>::not_op(";
  std::function<std::string(std::string_view)> fold_not1 = [&](std::string_view text) {
    std::string out;
    size_t      copied = 0;
    for (size_t scan = 0;;) {
      const auto at = text.find(kNot1, scan);
      if (at == std::string_view::npos) {
        break;
      }
      scan               = at + kNot1.size();
      const size_t open  = at + kNot1.size() - 1;
      const auto   close = match_delim(text, open, '(', ')');
      if (close == std::string_view::npos) {
        break;
      }
      const auto cmp   = unwrap_parens(text.substr(open + 1, close - open - 1));
      const auto scope = cmp.find(">::");
      if (!cmp.starts_with("Slop") || scope == std::string_view::npos || !cmp.ends_with(")")) {
        continue;
      }
      const auto tail = cmp.substr(scope + 3);
      for (const auto& [from, to] : kNeg) {
        if (!tail.starts_with(from)) {
          continue;
        }
        if (match_delim(tail, from.size() - 1, '(', ')') != tail.size() - 1) {
          break;
        }
        out.append(text.substr(copied, at - copied));
        out.append(fold_not1(absl::StrCat("Slop_u<1>::", to, tail.substr(from.size()))));
        copied = close + 1;
        scan   = copied;
        break;
      }
    }
    if (copied == 0) {
      return std::string(text);
    }
    out.append(text.substr(copied));
    return out;
  };
  body = fold_not1(body);
}

// `(Slop<2>::eq_op(a, b)).is_known_true()` materializes a compare only to read
// it straight back as a C++ bool. That pair is what single-use forestation
// leaves behind whenever a compare temp folds into its one condition -- a mux
// or hotmux selector, a clock gate, a commit guard. The *_bool statics answer
// the same question with no intermediate Slop.
//
// A textual pass for the same reason forestation is one: the compare is emitted
// by node_expr and the condition by a different emitter, and they only meet
// once the body is finished.
void fold_compare_bools(std::string& body) {
  static constexpr std::string_view kSuffix = ".is_known_true()";
  static constexpr std::string_view kCmp[]  = {"eq_op(", "ne_op(", "lt_op(", "gt_op(", "le_op(", "ge_op("};

  // Forward scan (string literals skipped) pairing every '(' with its ')', so
  // the parenthesized expression a `.is_known_true()` reads is known exactly.
  std::vector<size_t>                   open;
  std::vector<std::pair<size_t, size_t>> spans;  // (open, close) followed by kSuffix
  bool                                  in_str = false;
  for (size_t i = 0; i < body.size(); ++i) {
    const char c = body[i];
    if (in_str) {
      if (c == '\\') {
        ++i;
      } else if (c == '"') {
        in_str = false;
      }
      continue;
    }
    if (c == '"') {
      in_str = true;
    } else if (c == '(') {
      open.push_back(i);
    } else if (c == ')' && !open.empty()) {
      const size_t o = open.back();
      open.pop_back();
      if (body.compare(i + 1, kSuffix.size(), kSuffix) == 0) {
        spans.emplace_back(o, i);
      }
    }
  }

  // A trailing `.zext_to<..>()` between the compare and the truth test is the
  // width landing cgen puts on a 1-bit selector read. A compare produces 0/1,
  // and zero-extending 0/1 keeps it 0/1, so the test is unchanged by it.
  const auto strip_zext = [](std::string_view in) {
    static constexpr std::string_view kZext = ".zext_to<";
    while (in.ends_with(">()")) {
      const auto at = in.rfind(kZext);
      if (at == std::string_view::npos) {
        break;
      }
      const auto args = in.substr(at + kZext.size(), in.size() - (at + kZext.size()) - 3);
      if (args.empty()
          || args.find_first_not_of("0123456789, ") != std::string_view::npos) {
        break;
      }
      in = in.substr(0, at);
    }
    return in;
  };

  // Last span first, so the earlier offsets stay valid -- for DISJOINT spans.
  // Spans can also NEST (a forested mux condition inside a compare operand),
  // and rewriting the outer one shifts the text under the inner one's recorded
  // bounds, so re-validate every span against the CURRENT body before using it.
  for (auto it = spans.rbegin(); it != spans.rend(); ++it) {
    const auto [o, close] = *it;
    if (close + kSuffix.size() >= body.size() || body[o] != '(' || body.compare(close + 1, kSuffix.size(), kSuffix) != 0
        || match_delim(body, o, '(', ')') != close) {
      continue;  // an outer rewrite moved this span; leave it alone
    }
    auto inner = unwrap_parens(std::string_view{body}.substr(o + 1, close - o - 1));
    for (auto stripped = unwrap_parens(strip_zext(inner)); stripped != inner; stripped = unwrap_parens(strip_zext(inner))) {
      inner = stripped;
    }
    const bool unsign     = inner.starts_with("Slop_u<");
    if (!unsign && !inner.starts_with("Slop<")) {
      continue;
    }
    const auto scope = inner.find(">::");
    if (scope == std::string_view::npos) {
      continue;
    }
    const auto tail = inner.substr(scope + 3);
    for (const auto cmp : kCmp) {
      if (!tail.starts_with(cmp)) {
        continue;
      }
      // The call's paren must close at the very end: the compare has to BE the
      // whole expression, not a sub-term of a wider one.
      if (match_delim(tail, cmp.size() - 1, '(', ')') != tail.size() - 1) {
        break;
      }
      // The *_bool statics compare at the operands' own common width, so the
      // receiver width is irrelevant and an unsigned receiver maps onto Slop.
      const std::string receiver = unsign ? std::string{"Slop<2>::"} : absl::StrCat(inner.substr(0, scope + 3));
      body.replace(o,
                   close + 1 + kSuffix.size() - o,
                   absl::StrCat(receiver, cmp.substr(0, cmp.size() - 4), "_bool(", tail.substr(cmp.size())));
      break;
    }
  }
}

// Peepholes over one finished body: drop what nothing reads, fold what exactly
// one adjacent statement reads, then fuse the compare/condition pairs that
// folding just brought together.
void compact_body_temps(std::string& body) {
  strip_dead_temps(body);
  inline_single_use_temps(body);
  fold_reduction_sources(body);
  fold_negated_compares(body);
  fold_compare_bools(body);
  collapse_same_width_wraps(body);
}

// Split every LHD_SIM_COLD member in `text` into noinline, unoptimized lambdas
// of at most `chunk_statements` top-level statements.
//
// The checkpoint/debug/reset members are one statement per state field. On XS
// Rob each is 7-16 MB of straight-line code, and clang's backend time on ONE
// function of that size is superlinear even with optnone (every member of
// Rob.cpp took over 10 minutes alone while the whole cycle kernel compiled in
// 16 s). A lambda is its own backend function and captures `this`, the
// parameters and any leading local by reference, so the statement text is
// untouched. Anything this cannot prove safe leaves the member as it was: a
// `return` before the tail (it would return from the lambda instead), a local
// declared after the first statement (a later chunk could not see it), or
// unbalanced braces.
std::string chunk_cold_members(std::string_view text, size_t chunk_statements = 2000) {

  std::vector<std::string_view> lines;
  for (size_t pos = 0; pos < text.size();) {
    const auto nl = text.find('\n', pos);
    const auto e  = nl == std::string_view::npos ? text.size() : nl;
    lines.push_back(text.substr(pos, e - pos));
    pos = e + 1;
  }
  const auto brace_delta = [](std::string_view line) {
    int  delta  = 0;
    char quote  = 0;
    bool escape = false;
    for (const char c : line) {
      if (quote != 0) {
        if (escape) {
          escape = false;
        } else if (c == '\\') {
          escape = true;
        } else if (c == quote) {
          quote = 0;
        }
      } else if (c == '"' || c == '\'') {
        quote = c;
      } else if (c == '{') {
        ++delta;
      } else if (c == '}') {
        --delta;
      }
    }
    return delta;
  };
  // Hand-written matchers, not std::regex: these run once per line of every
  // cold member (millions of lines on minion), and libc++'s regex engine was
  // ~0.3 s of cgen there. Same languages as the regexes they replace:
  //   is_declaration: ^\s*(const\s+)?(auto|std::[\w:]+|u?int\d+_t)\s+[A-Za-z_]\w*\s*(=|\{)
  //   has_return:     \breturn\b
  const auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; };
  const auto is_word  = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; };
  const auto is_declaration = [&](std::string_view line) {
    size_t p = 0;
    while (p < line.size() && is_space(line[p])) {
      ++p;
    }
    // `const\s+` is optional; when the rest fails after it, the regex retried
    // from here without it, where `const` cannot start a type -- so taking it
    // whenever it is present decides the same.
    if (line.substr(p).starts_with("const") && p + 5 < line.size() && is_space(line[p + 5])) {
      p += 5;
      while (p < line.size() && is_space(line[p])) {
        ++p;
      }
    }
    const auto rest = line.substr(p);
    if (rest.starts_with("auto")) {
      p += 4;
    } else if (rest.starts_with("std::")) {
      size_t q = p + 5;
      while (q < line.size() && (is_word(line[q]) || line[q] == ':')) {
        ++q;
      }
      if (q == p + 5) {
        return false;
      }
      p = q;
    } else {
      size_t q = p;
      if (q < line.size() && line[q] == 'u') {
        ++q;
      }
      if (!line.substr(q).starts_with("int")) {
        return false;
      }
      q += 3;
      const size_t digits = q;
      while (q < line.size() && std::isdigit(static_cast<unsigned char>(line[q])) != 0) {
        ++q;
      }
      if (q == digits || !line.substr(q).starts_with("_t")) {
        return false;
      }
      p = q + 2;
    }
    if (p >= line.size() || !is_space(line[p])) {
      return false;
    }
    while (p < line.size() && is_space(line[p])) {
      ++p;
    }
    if (p >= line.size() || !(std::isalpha(static_cast<unsigned char>(line[p])) != 0 || line[p] == '_')) {
      return false;
    }
    while (p < line.size() && is_word(line[p])) {
      ++p;
    }
    while (p < line.size() && is_space(line[p])) {
      ++p;
    }
    return p < line.size() && (line[p] == '=' || line[p] == '{');
  };
  const auto has_return = [&](std::string_view line) {
    for (auto at = line.find("return"); at != std::string_view::npos; at = line.find("return", at + 1)) {
      const size_t end = at + 6;
      if ((at == 0 || !is_word(line[at - 1])) && (end == line.size() || !is_word(line[end]))) {
        return true;
      }
    }
    return false;
  };
  const auto is_return = [](std::string_view line) {
    const auto first = line.find_first_not_of(' ');
    return first != std::string_view::npos && line.substr(first).starts_with("return");
  };

  std::string out;
  out.reserve(text.size() + text.size() / 64);
  const auto put = [&out](std::string_view line) {
    out.append(line);
    out.push_back('\n');
  };
  size_t i = 0;
  while (i < lines.size()) {
    const auto head = lines[i];
    if (!head.starts_with("LHD_SIM_COLD ") || !head.ends_with("{")) {
      put(head);
      ++i;
      continue;
    }
    size_t end = i + 1;
    while (end < lines.size() && lines[end] != "}") {
      ++end;
    }
    if (end == lines.size()) {  // not a column-0 closed member: leave the rest alone
      for (; i < lines.size(); ++i) {
        put(lines[i]);
      }
      break;
    }

    // Top-level statement units, a prologue of leading locals and an epilogue
    // of trailing returns.
    std::vector<std::pair<size_t, size_t>> units;  // [first, last] line of each statement
    bool                                   safe  = true;
    int                                    depth = 0;
    size_t                                 start = i + 1;
    for (size_t l = i + 1; l < end && safe; ++l) {
      depth += brace_delta(lines[l]);
      if (depth < 0) {
        safe = false;
      } else if (depth == 0) {
        units.emplace_back(start, l);
        start = l + 1;
      }
    }
    safe = safe && depth == 0;
    size_t prologue = 0;
    while (safe && prologue < units.size() && units[prologue].first == units[prologue].second
           && is_declaration(lines[units[prologue].first])) {
      ++prologue;
    }
    size_t epilogue = units.size();
    while (safe && epilogue > prologue && units[epilogue - 1].first == units[epilogue - 1].second
           && is_return(lines[units[epilogue - 1].first])) {
      --epilogue;
    }
    for (size_t u = prologue; u < epilogue && safe; ++u) {
      for (size_t l = units[u].first; l <= units[u].second; ++l) {
        if (has_return(lines[l]) || (l == units[u].first && is_declaration(lines[l]))) {
          safe = false;
          break;
        }
      }
    }
    if (!safe || epilogue - prologue <= chunk_statements) {
      for (size_t l = i; l <= end; ++l) {
        put(lines[l]);
      }
      i = end + 1;
      continue;
    }

    put(head);
    for (size_t u = 0; u < prologue; ++u) {
      put(lines[units[u].first]);
    }
    for (size_t u = prologue; u < epilogue; u += chunk_statements) {
      put("  [&]() LHD_SIM_COLD {");
      const size_t last = std::min(epilogue, u + chunk_statements) - 1;
      for (size_t l = units[u].first; l <= units[last].second; ++l) {
        put(lines[l]);
      }
      put("  }();");
    }
    for (size_t u = epilogue; u < units.size(); ++u) {
      for (size_t l = units[u].first; l <= units[u].second; ++l) {
        put(lines[l]);
      }
    }
    put(lines[end]);
    i = end + 1;
  }
  return out;
}

// Rewrite the LHD_SIM_COLD members appended since `mark` (see chunk_cold_members).
// `chunk` bounds the statements per lambda. The sim.tune walkers use a much
// smaller one: their statements are tiny and uniform, and AArch64's Machine
// InstCombiner is superlinear in function size even under optnone -- measured
// on XS Rob's 162k-statement walker, 2000-statement chunks compiled in 33.6 s
// and 128-statement chunks in 5.3 s.
inline constexpr size_t kTuneColdChunkStatements = 128;
void chunk_cold_region(File_output& out, size_t mark, size_t chunk = 2000) {
  const auto text = out.detach_from(mark);
  out.append(chunk_cold_members(text, chunk));
}
}  // namespace

std::string Cgen_sim::cpp_port_path(std::string_view name) {
  const auto dot = name.find('.');
  if (dot == std::string_view::npos) {
    return cpp_id(name);
  }
  return absl::StrCat(cpp_id(name.substr(0, dot)), ".", cpp_id(name.substr(dot + 1)));
}

// Delegates to livehd::cpp_ident (core/cpp_ident.hpp) so this and prp_sim's
// manifest lookup cannot drift: the member is DECLARED here and REFERENCED
// there, and a Pyrope field named for a C++ reserved word or alternative token
// (`xor`) has to be escaped identically on both sides.
std::string Cgen_sim::cpp_id(std::string_view name) { return livehd::cpp_ident(name); }

// A graph name (`file.entity`, or `../dir/file.entity` for a path-qualified
// import) is used verbatim as the emitted .hpp/.cpp basename and in the sibling
// `#include`s. A '.' is legal in a filename, but a '/' (or the '/' inside a
// `..`) from a relative-path import is a directory separator that would write
// into a non-existent subdir. Sanitize ONLY the separators to '_', keeping the
// dotted `file.entity` form so ordinary (dot-only) filenames are unchanged.
// Applied identically to the module's own name and to a child's name in the
// `#include`, so the reference always resolves to the emitted file.
static std::string sim_file_stem(std::string_view name) { return livehd::unit_file_stem(name); }

hhds::Pin_class Cgen_sim::get_driver(const hhds::Pin_class& sink) {
  if (sink.is_invalid()) {
    return {};
  }
  // ONE DRIVER PER SINK PIN: this is exactly get_driver_pin()'s contract, and
  // it returns an INVALID pin (not an abort) for a sink nothing drives, which
  // is the miss this helper already reported with an empty edge vector.
  return sink.get_driver_pin();
}

hhds::Pin_class Cgen_sim::find_sink_pin(const hhds::Node_class& node, std::string_view name) {
  return livehd::graph_util::find_sink_pin(node, name);
}

hhds::Pin_class Cgen_sim::find_driver_pin(const hhds::Node_class& node, std::string_view name) {
  // Invalid-on-miss probe for a Sub instance's declared output: a declared
  // output with no consumer has no materialized pin (hhds get_driver_pin
  // asserts on it), and an edge-less pin is unnamed to the emitted code
  // anyway — walk the connected DRIVER PINS and match the resolved port_id.
  if (node.is_invalid()) {
    return {};
  }
  auto sub_io = node.get_subnode_io();
  if (!sub_io || !sub_io->has_output(name)) {
    return {};
  }
  auto pid = sub_io->get_output_port_id(name);
  for (const auto& odrv : node.out_sorted_pins()) {
    if (odrv.get_port_id() == pid) {
      return odrv;
    }
  }
  return {};
}

namespace {
// The `?`-literal runtime (livehd::sim::kUnknownLiteralHelper, sim_tune_rt.hpp)
// is emitted after `#include "slop.hpp"` into every generated prelude (the
// module header AND the standalone canonical-kernel .cpp files, which do not
// include that header), and prp_sim emits the same bytes into drv.cpp. The
// include guard makes a second copy inside one translation unit -- two module
// headers, or a header plus the driver's copy -- a no-op; identical copies
// across TUs are ordinary header semantics.
//
// Drawing ONCE per (width, spelling) is required, not an optimization. The same
// LGraph constant is emitted at several sites (the color writing `__out` and
// the one writing `__last_out`, plus any canonical kernel), and an independent
// draw per site would hand ONE net two different values. A function template's
// local static is a single object across every TU, so the key collapses them;
// it also initializes on FIRST CALL, which is after main() has run
// hlop_set_random_seed() (and applied a run-time `--set sim.unknown_zero=true`)
// -- a namespace-scope object would instead be initialized before main and
// could never see `--seed`.
//
// Two DISTINCT nets that carry the same spelling at the same width share the
// draw. That is the deliberate reading: an x constant has one value per run.
using livehd::sim::kTuneHashHelper;
using livehd::sim::kUnknownLiteralHelper;

// Pyrope text for a constant. Under sim.unknown_zero every UNKNOWN bit is
// forced to 0 here; otherwise the `?` characters SURVIVE into the emitted
// literal and Slop::from_pyrope draws each one at run time.
//
// Slop is a value type with no runtime unknowns, so the simulator has no way to
// represent an `x`: a `?` bit must become a concrete 0 or 1. Under
// sim.unknown_zero=true it is 0, and the whole literal then folds at C++
// compile time -- emitting the raw `0sb1????...` form forces a runtime parse,
// which was 27.8% of simulation time on the dino CPU when EVERY wide constant
// took that path. The default instead randomizes (see sim_const_expr): the draw
// is once per literal, so only the literals that actually carry a `?` pay, and
// they pay it once rather than per cycle.
std::string sim_const_text(const Dlop& c, bool unknown_zero) {
  auto txt = c.to_pyrope();
  if (unknown_zero && c.has_unknowns()) {
    std::replace(txt.begin(), txt.end(), '?', '0');
  }
  return txt;
}

// The emitted C++ for a compile-time constant of `width` bits.
//
// create_integer(N) is a constexpr array fill the compiler always folds;
// from_pyrope is a digit-by-digit parse that it must constant-evaluate (and, as
// a bare sub-expression in a hot loop, is free NOT to -- 27.8% of simulation
// time on the dino CPU before the constexpr-local wrapper below). Take the
// integer path whenever the SIMULATED value fits an int64: that includes the
// unknown-bit constants under sim.unknown_zero, because that policy forces every
// `?` to 0 and the forced value is usually a small integer (`0ub??0` -> `0ub000`
// -> 0). Only genuinely wide values and the non-Integer types (Boolean/String/
// Nil, which create_integer would flatten) still need the parse.
std::string sim_const_expr(std::string_view pyrope_text, std::string_view width) {
  // A literal that still carries `?` is the sim.unknown_zero=false path: it is
  // NOT constant-evaluable (Slop::random_bit_ throws when constant-evaluated,
  // since a compile-time random is not meaningful), so it must be a real
  // runtime parse -- routed through the kUnknownLiteralHelper template so the
  // parse, and the PRNG draw inside it, happen ONCE per (width, spelling) for
  // the whole program. See that helper for why once is required.
  // fnv1a over the spelling is the template's identity key, so one (width,
  // spelling) has exactly ONE static in the whole program however many sites
  // emit it. Shared with the too-wide-to-constant-evaluate path below.
  const auto once_per_program = [&] {
    const auto key = livehd::hash_util::fnv1a64(pyrope_text);
    return absl::StrCat("__lhd_unknown_literal<", width, ", ", key, "ull>(\"", pyrope_text, "\")");
  };
  if (pyrope_text.find('?') != std::string_view::npos) {
    return once_per_program();
  }
  // Same bits by construction: create_integer and from_pyrope both leave the
  // value UNMASKED in base_[0] and sign-extend into the upper words, and
  // is_just_i64() (<= 62 bits, no unknowns) makes the int64 round-trip exact.
  if (const auto v = Dlop::from_pyrope(std::string{pyrope_text}); v && v->is_integer() && v->is_just_i64()) {
    return absl::StrCat("Slop<", width, ">::create_integer(", v->to_just_i64(), ")");
  }
  // Folded at COMPILE time via a constexpr local... but only while the compiler
  // can actually fold it. `from_pyrope` is a digit-by-digit parse, so its
  // constexpr cost grows with digits x words, and past a few thousand bits it
  // blows clang's -fconstexpr-steps budget and the generated C++ DOES NOT
  // COMPILE:
  //
  //   error: constexpr variable '_k' must be initialized by a constant expression
  //   note: constexpr evaluation hit maximum step limit; possible infinite loop?
  //
  // Measured on XiangShan `Rob`: a Slop<10922> literal in one color-eval shard
  // killed the whole host build, so the design could be lowered and then not
  // built. A width threshold is not a guess here — it is the difference between
  // a design that simulates and one that does not.
  //
  // Above the threshold, route the literal through the SAME kUnknownLiteralHelper
  // template the `?` path uses. It keeps the property that actually mattered
  // (the parse happens ONCE for the whole program, which is what bought the
  // 27.8% noted above — the cost was re-parsing per evaluation, not the parse
  // itself) while asking the compiler to constant-evaluate nothing. Ruling I3 is
  // satisfied: one guarded load per use instead of an immediate, against one
  // parse per program either way.
  //
  // A `static const` inside a per-site lambda would NOT keep that property:
  // every emission site is a distinct closure type with its own static, so N
  // uses of one wide literal would be N parses and N copies in .bss. The helper
  // is keyed on (width, spelling), so it is genuinely once.
  //
  // Raising -fconstexpr-steps instead would only move the cliff, and it would
  // move it in a flag the generated build does not own on every toolchain.
  constexpr uint64_t kMaxConstexprBits = 2048;
  uint64_t           bits              = 0;
  const auto         w                 = std::from_chars(width.data(), width.data() + width.size(), bits);
  // A width that does not parse means "I cannot prove this is small", so it
  // takes the path that always compiles rather than the one with a cliff in it.
  if (w.ec != std::errc{} || bits > kMaxConstexprBits) {
    return once_per_program();
  }
  return absl::StrCat("([]{ constexpr auto _k = Slop<", width, ">::from_pyrope(\"", pyrope_text, "\"); return _k; }())");
}

std::string cpp_string_literal(std::string_view text) {
  std::string result{"\""};
  result.reserve(text.size() + 2);
  for (const unsigned char ch : text) {
    switch (ch) {
      case '\\': result += "\\\\"; break;
      case '"' : result += "\\\""; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (ch < 0x20 || ch == 0x7f) {
          // Exactly three octal digits cannot consume a following source
          // character. A C++ `\\xNN` escape has no fixed length, so a literal
          // byte followed by [0-9a-fA-F] could silently become another value.
          result += '\\';
          result += static_cast<char>('0' + ((ch >> 6) & 0x7));
          result += static_cast<char>('0' + ((ch >> 3) & 0x7));
          result += static_cast<char>('0' + (ch & 0x7));
        } else {
          result += static_cast<char>(ch);
        }
    }
  }
  result += '"';
  return result;
}
}  // namespace

// Append a `.zext_to<target>()` to an already-built expression, FUSING it with a
// zext the expression already ends in.
//
// `x.zext_to<a>().zext_to<b>()` is never two operations. Keeping the low `a`
// bits and then the low `b` bits keeps the low `min(a,b)` bits, at carrier `b`:
//
//   a >= b :  x.zext_to<b>()        (the inner mask is entirely subsumed)
//   a <  b :  x.zext_to<a, b>()     (the fused Keep/Carrier form hlop already has)
//
// Both collapse to ONE call, so this is a rewrite, not a heuristic. It matters
// because the two halves of this emitter do not trust each other: the color
// boundary glue converts a slot read at the consumer width, and operand() then
// appends its own conversion because a narrowing unsigned slot read is
// deliberately outside `canonical_`. On minion's shards that duplicated pair was
// ~105k of the ~335k emitted zext_to calls, and ~90% of all chains have a == b.
//
// Deliberately a STRING fuse rather than a canonical_ policy change: it is valid
// for every producer of the inner zext without having to prove, per call site,
// that the glue's width and the driver pin's declared width agree.
// A 1-bit control read as a C++ condition. Named because 18 call sites spell
// it, and because fold_compare_bools (above) rewrites exactly this shape when
// the value folded into it turns out to be a compare.
static std::string emit_known_true(std::string_view expr) { return absl::StrCat("(", expr, ").is_known_true()"); }

// `(Slop<W>::create_integer(K)).is_known_true()` with K != 0: a guard that a
// folded clock tick or a constant enable leaves behind. It adds no condition.
static bool is_constant_true_guard(std::string_view expr) {
  constexpr std::string_view head = "(Slop<";
  constexpr std::string_view mid  = ">::create_integer(";
  constexpr std::string_view tail = ")).is_known_true()";
  if (!expr.starts_with(head) || !expr.ends_with(tail)) {
    return false;
  }
  const auto at = expr.find(mid);
  if (at == std::string_view::npos) {
    return false;
  }
  const auto value = expr.substr(at + mid.size(), expr.size() - tail.size() - at - mid.size());
  if (value.empty() || value == "0") {
    return false;
  }
  return std::ranges::all_of(value, [](char c) { return c >= '0' && c <= '9'; });
}

static std::string append_zext(std::string expr, int target_bits) {
  const std::string_view tail = expr;
  if (tail.ends_with(">()")) {
    const auto open = expr.rfind(".zext_to<");
    if (open != std::string::npos) {
      // Everything between the angle brackets must be 1-2 plain integers, else
      // this is some other templated call and the suffix match was a
      // coincidence.
      const auto args_begin = open + std::string_view(".zext_to<").size();
      const auto args       = std::string_view(expr).substr(args_begin, expr.size() - args_begin - 3);
      int        keep       = 0;
      int        carrier    = 0;
      int        parsed     = 0;
      bool       ok         = !args.empty();
      int        cur        = 0;
      bool       any_digit  = false;
      for (size_t i = 0; ok && i <= args.size(); ++i) {
        const char c = i < args.size() ? args[i] : ',';
        if (c >= '0' && c <= '9') {
          cur       = cur * 10 + (c - '0');
          any_digit = true;
        } else if (c == ',') {
          if (!any_digit || parsed >= 2) {
            ok = false;
            break;
          }
          (parsed == 0 ? keep : carrier) = cur;
          ++parsed;
          cur       = 0;
          any_digit = false;
        } else if (c != ' ') {
          ok = false;
        }
      }
      if (ok && parsed >= 1) {
        // The inner call's KEEP width is what survives; its carrier is
        // irrelevant here because this outer zext restates it.
        expr.resize(open);
        if (keep >= target_bits) {
          return absl::StrCat(expr, ".zext_to<", target_bits, ">()");
        }
        return absl::StrCat(expr, ".zext_to<", keep, ", ", target_bits, ">()");
      }
    }
  }
  return absl::StrCat(expr, ".zext_to<", target_bits, ">()");
}

std::string Cgen_sim::operand(const hhds::Pin_class& dpin, int target_bits, int sign_mode) {
  const std::string tw = std::to_string(target_bits);
  if (dpin.is_invalid()) {
    return absl::StrCat("Slop<", tw, ">::create_integer(0)");
  }
  if (dpin.is_const()) {
    const auto& c = const_of(dpin);
    if (c.is_numeric() && c.is_just_i64()) {  // fast path -- the overwhelmingly common case
      return absl::StrCat("Slop<", tw, ">::create_integer(", c.to_just_i64(), ")");
    }
    return sim_const_expr(sim_const_text(c, unknown_zero_), tw);
  }
  auto it = pin2var.find(dpin.get_class_index());
  if (it == pin2var.end()) {
    // A VALID, NON-CONST driver with no pin2var binding can only be a combinational
    // cycle back-edge: its producer node sorts AFTER this use in forward_class (a
    // real comb loop, or a FALSE loop through an atomic Sub call). There is no valid
    // schedule; record it so do_from_graph() fails loudly instead of silently
    // simulating 0. (A genuinely undriven pin already returned at is_invalid above.)
    cycle_unresolved_ = true;
    ++unresolved_operands_;
    if (cycle_first_label_.empty()) {
      cycle_first_label_ = absl::StrCat("`", debug_name(dpin.get_master_node()), "` (a combinational-cycle back-edge)");
    }
    return absl::StrCat("Slop<", tw, ">::create_integer(0) /*UNRESOLVED-CYCLE*/");  // already a Slop<tw> expr
  }
  const std::string& base         = it->second;
  // A Sum with a subtrahend (a `-` operand, sink pid != 0) can go negative; its
  // value wraps at the node width, so WIDENING it must sign-extend to propagate
  // the borrow -- matching Verilog re-evaluating `a - b` at the consumer width.
  // Safe: a non-negative sum has top bit 0, so sext == zext there.
  bool               sum_with_sub = false;
  if (sign_mode == 0 && is_unsign(dpin)) {
    auto mn = dpin.get_master_node();
    if (!mn.is_invalid() && type_op_of(mn) == Ntype_op::Sum) {
      for (const auto& isink : mn.inp_sorted_pins()) {
        // Bank parity: an ODD Sum sink pid is a SUBTRACTED operand, whatever
        // slot of the bank it landed in (graph/cell.hpp).
        if (Ntype::sink_bank(Ntype_op::Sum, isink.get_port_id()) != 0) {
          sum_with_sub = true;
          break;
        }
      }
    }
  }
  const bool unsigned_ = !sum_with_sub && ((sign_mode == -1) || (sign_mode == 0 && is_unsign(dpin)));
  if (unsigned_) {
    // Computed unsigned values are already canonical non-negative integers.
    // Widening those through the signed carrier preserves the value and avoids
    // a redundant zext/getmask in the hot path.
    //
    // Boundary carriers (ports, memories, flop members, Sub outputs) reach this
    // branch too whenever sim.slop_u declared them `Slop_u<W>` -- that type IS
    // canonical, and mark_slop_u_binding() records them. A SIGNED boundary is
    // still not canonical and takes the physical W-bit -> non-negative
    // conversion below. The branch immediately below exists specifically to
    // serve unsigned boundaries.
    const int source_bits = wbits_of(dpin);
    if (canonical_.contains(dpin.get_class_index()) && source_bits > 0 && source_bits <= target_bits) {
      // Keep operand()'s concrete-Slop contract even when `base` is a Slop_u.
      // A wider Slop_u conversion is a carrier word copy, but an ordinary
      // Slop<W> can still carry an unsigned boundary/state value whose data
      // msb is 1. Its cross-width constructor would interpret that bit as a
      // sign, so only the type-proven Slop_u case may take the free widening;
      // every other unsigned representation takes the fused zext below.
      if (source_bits < target_bits && slop_u_values_.contains(dpin.get_class_index())) {
        return absl::StrCat("Slop<", tw, ">{", base, "}");
      }
      return append_zext(base, target_bits);
    }
    return append_zext(base, target_bits);  // zero-extend / mask (fused with any zext `base` ends in)
  }
  return absl::StrCat("Slop<", tw, ">{", base, "}");  // signed sext via the hlop cross-width ctor
}

// The value bound into a child instance's `__in` field. When the source is
// already the destination's exact type -- a `Slop_u<W>` object of the same
// unsigned width -- name it directly: `operand()` would spell `.zext_to<W>()`,
// a full carrier copy that `slop_update` then masked into a second temporary
// before comparing. A loop body forwarding a 7,800-bit invariant to its inner
// loop paid two 1 KB copies per iteration for a value that never changed.
std::string Cgen_sim::bind_operand(const hhds::Pin_class& dpin, int target_bits, bool unsign_dst) {
  if (unsign_dst && slop_u_ && !dpin.is_invalid() && !dpin.is_const() && is_unsign(dpin) && wbits_of(dpin) == target_bits
      && slop_u_values_.contains(dpin.get_class_index())) {
    if (const auto it = pin2var.find(dpin.get_class_index()); it != pin2var.end()) {
      return it->second;
    }
  }
  return operand(dpin, target_bits);
}

std::string Cgen_sim::raw_operand(const hhds::Pin_class& dpin, int fallback_bits) {
  const std::string fw = std::to_string(fallback_bits);
  if (dpin.is_invalid()) {
    return absl::StrCat("Slop<", fw, ">::create_integer(0)");
  }
  if (dpin.is_const()) {
    const auto& c = const_of(dpin);
    if (c.is_numeric() && c.is_just_i64()) {
      return absl::StrCat("Slop<", fw, ">::create_integer(", c.to_just_i64(), ")");
    }
    return sim_const_expr(sim_const_text(c, unknown_zero_), fw);
  }
  auto it = pin2var.find(dpin.get_class_index());
  if (it == pin2var.end()) {
    // Same unschedulable-comb-cycle guard as operand(): record it so
    // do_from_graph() fails loudly instead of silently simulating 0.
    cycle_unresolved_ = true;
    ++unresolved_operands_;
    if (cycle_first_label_.empty()) {
      cycle_first_label_ = absl::StrCat("`", debug_name(dpin.get_master_node()), "` (a combinational-cycle back-edge)");
    }
    return absl::StrCat("Slop<", fw, ">::create_integer(0) /*UNRESOLVED-CYCLE*/");
  }
  const auto binding_width = slop_u_binding_width_.find(it->second);
  if (binding_width != slop_u_binding_width_.end() && binding_width->second + 1 > fallback_bits) {
    return absl::StrCat("(", it->second, ").zext_to<", fallback_bits, ">()");
  }
  if (!canonical_.contains(dpin.get_class_index()) || (is_unsign(dpin) && !slop_u_values_.contains(dpin.get_class_index()))) {
    // A boundary value (module input, memory read, sub output) may hold a
    // non-canonical word for its declared width, so it still needs the
    // declared-width re-interpretation. The scheduler's `canonical_` bit means
    // the value is stable/exact at its graph width; it does NOT turn ordinary
    // Slop storage into canonical-unsigned storage. Only a tracked Slop_u may
    // take the bare unsigned path.
    return operand(dpin, fallback_bits);
  }
  return it->second;  // BARE value -- the op deduces its width
}

std::string Cgen_sim::stored_operand(const hhds::Pin_class& dpin, int fallback_bits) {
  const std::string fw = std::to_string(fallback_bits);
  if (dpin.is_invalid()) {
    return absl::StrCat("Slop<", fw, ">::create_integer(0)");
  }
  if (dpin.is_const()) {
    const auto& c = const_of(dpin);
    if (c.is_numeric() && c.is_just_i64()) {
      return absl::StrCat("Slop<", fw, ">::create_integer(", c.to_just_i64(), ")");
    }
    return sim_const_expr(sim_const_text(c, unknown_zero_), fw);
  }
  auto it = pin2var.find(dpin.get_class_index());
  if (it == pin2var.end()) {
    cycle_unresolved_ = true;
    ++unresolved_operands_;
    if (cycle_first_label_.empty()) {
      cycle_first_label_ = absl::StrCat("`", debug_name(dpin.get_master_node()), "` (a combinational-cycle back-edge)");
    }
    return absl::StrCat("Slop<", fw, ">::create_integer(0) /*UNRESOLVED-CYCLE*/");
  }
  return it->second;
}

// The widest low lane any reader of `output` keeps -- every reader is a
// Get_mask, or a two-input And, with a constant contiguous mask [0, W) -- or 0
// when some reader keeps more. The bits above max(W) of that value are never
// observed, so its producer may compute only the low lane.
int Cgen_sim::low_lane_readers_width(const hhds::Pin_class& output) {
  if (output.is_invalid()) {
    return 0;
  }
  int  wmax   = 0;
  bool anyone = false;
  for (const auto& edge : output.out_edges()) {
    const auto consumer = edge.sink.get_master_node();
    if (consumer.is_invalid()) {
      return 0;
    }
    const auto      op = type_op_of(consumer);
    hhds::Pin_class mask;
    if (op == Ntype_op::Get_mask) {
      if (edge.sink.get_port_id() != Ntype::get_sink_pid(Ntype_op::Get_mask, "a")) {
        return 0;
      }
      mask = get_driver_of_sink_name(consumer, "mask");
    } else if (op == Ntype_op::And) {
      int count = 0;
      for (const auto& sink : consumer.inp_sorted_pins()) {
        const auto in_drv = sink.get_driver_pin();
        ++count;
        if (in_drv.is_const()) {
          mask = in_drv;
        }
      }
      if (count != 2) {
        return 0;
      }
    } else {
      return 0;
    }
    if (mask.is_invalid() || !mask.is_const()) {
      return 0;
    }
    const auto& mv = const_of(mask);
    if (mv.has_unknowns() || mv.is_negative()) {
      return 0;
    }
    const auto [mb, me] = mv.get_mask_range();
    if (mb != 0 || me <= 0) {
      return 0;
    }
    wmax   = std::max<int>(wmax, me);
    anyone = true;
  }
  return anyone ? wmax : 0;
}

bool Cgen_sim::proven_unsigned_result(const hhds::Node_class& node, const hhds::Pin_class& output) const {
  if (output.is_invalid() || !is_unsign(output)) {
    return false;
  }

  // SRA is the one semantic exception: a signed value must retain sign
  // extension even if stale output metadata claims unsigned. Every other
  // producer is trusted to honor the width/sign contract.
  if (type_op_of(node) == Ntype_op::SRA) {
    const auto a = get_driver_of_sink_name(node, "a");
    if (!a.is_invalid() && !is_unsign(a)) {
      return false;
    }
  }
  return true;
}

bool Cgen_sim::proven_canonical_unsigned_result(const hhds::Node_class& node, const hhds::Pin_class& output) const {
  if (!proven_unsigned_result(node, output)) {
    return false;
  }

  const auto op = type_op_of(node);
  // Set_mask writes a finite low window but does not clear bits above the
  // declared unsigned result. A signed/unknown base can therefore leave a
  // non-canonical sign fill even when every result bit is otherwise correct.
  // The landing mask is the finite reinterpretation boundary.
  if (op == Ntype_op::Set_mask) {
    return false;
  }
  const bool needs_nonnegative_inputs = op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Xor || op == Ntype_op::Sum
                                        || op == Ntype_op::Mult || op == Ntype_op::SHL || op == Ntype_op::Mux
                                        || op == Ntype_op::Hotmux;
  if (op == Ntype_op::Sum || op == Ntype_op::Mult || op == Ntype_op::SHL) {
    for (const auto& edge : output.out_edges()) {
      if (livehd::graph_util::is_graph_output_pin(edge.sink) || type_op_of(edge.sink.get_master_node()) == Ntype_op::Sub) {
        // A module-port demand may cap this arithmetic result below its
        // mathematical range. Unsigned metadata then describes the landing,
        // not a proof that the unmasked expression is already canonical.
        return false;
      }
    }
  }
  const auto control_end = op == Ntype_op::Hotmux ? livehd::graph_util::hotmux_control_end(node) : 0;
  if (needs_nonnegative_inputs || op == Ntype_op::SRA) {
    for (const auto& sink : node.inp_sorted_pins()) {
      const auto drv = sink.get_driver_pin();
      if ((op == Ntype_op::Mux && sink.get_port_id() == 0)
          || (op == Ntype_op::Hotmux && livehd::graph_util::is_hotmux_control(sink.get_port_id(), control_end))) {
        continue;
      }
      if ((op == Ntype_op::SHL || op == Ntype_op::SRA) && sink.get_port_id() != 0) {
        continue;
      }
      const auto width = drv.is_const() ? const_of(drv).get_signed_bits() : wbits_of(drv);
      if (width > wbits_of(output)) {
        return false;  // a narrowed carrier still needs its unsigned landing mask
      }
    }
  }
  if (!needs_nonnegative_inputs) {
    return true;
  }

  // An unsigned result may be a FINITE reinterpretation rather than a proof
  // that every value operand is already a non-negative Slop value. A signed
  // -2^64 can legitimately become u65 2^64 after an Or or merge; that landing
  // needs the one mask. When every value input is unsigned, operand() presents
  // a canonical value and these operations preserve canonicality for free.
  for (const auto& sink : node.inp_sorted_pins()) {
    const auto drv = sink.get_driver_pin();
    if ((op == Ntype_op::Mux && sink.get_port_id() == 0)
        || (op == Ntype_op::Hotmux && livehd::graph_util::is_hotmux_control(sink.get_port_id(), control_end))) {
      continue;  // selector, not a result value
    }
    if (op == Ntype_op::SHL && sink.get_port_id() != 0) {
      continue;  // shift amount, not a result value
    }
    if (drv.is_const()) {
      if (const_of(drv).is_negative()) {
        return false;
      }
    } else if (!is_unsign(drv)) {
      return false;
    }
  }
  return true;
}

// Can a Get_mask WIDTH-ADJUST of `drv` to `wbits` be dropped and the source
// passed through RAW? Two conditions, both necessary:
//
//   UNSIGNED — the value is already non-negative, so making it positive and
//   the trailing trim are both the identity. A signed source keeps the
//   unsigned read: to-positive of a negative value must mask.
//
//   NOT WIDER than the node — raw_operand's contract is that the string it
//   hands out is exact AT ITS DECLARED WIDTH, and the ops it feeds are
//   width-templated, so they take the operand at the source's own width. A
//   source wider than `wbits` still holds live bits above the node's declared
//   width, and dropping the adjust would let them into a downstream eq_op /
//   and_op / fold that the `.zext_to<wbits>()` read had masked them out of.
//   Narrower is fine: the value is exact and non-negative, so every later
//   landing (zext read, or the sign-extending cross-width ctor) reproduces it.
bool Cgen_sim::raw_width_adjust_ok(const hhds::Pin_class& drv, int wbits) {
  if (!is_unsign(drv)) {
    return false;
  }
  const int sw = wbits_of(drv);
  if (sw > wbits) {
    return false;
  }
  if (sw < wbits) {
    sub_width_expr_ = true;  // the caller's temp declaration must brace-init
  }
  return true;
}

std::string Cgen_sim::node_expr(const hhds::Node_class& node, int wbits) {
  slop_u_expr_  = false;
  const auto op = type_op_of(node);
  const auto tw = std::to_string(wbits);
  // STORED and INDEXED: every arm below reads its operands positionally
  // (e[0] value, e[1] mask, ...), so this needs random access, not a lazy
  // range. The walk is read-only, but inp_pins_snapshot() is the right
  // shape anyway: it is inp_sorted_pins() materialized, so the node-as-pin
  // (port 0) comes first and the rest in ascending port order, one driver per
  // element. Read a driver with e[i].get_driver_pin().
  const auto e  = node.inp_pins_snapshot();

  const auto operation_width = [&](const hhds::Pin_class& pin) {
    if (pin.is_const()) {
      return std::max({wbits_of(pin), static_cast<int>(const_of(pin).get_signed_bits()), 1});
    }
    const int bits = std::max(wbits_of(pin), 1);
    return is_unsign(pin) ? bits + 1 : bits;
  };
  const auto operation_operand = [&](const hhds::Pin_class& pin) { return raw_operand(pin, operation_width(pin)); };
  int        operation_bits    = wbits;
  for (const auto& edge : e) {
    operation_bits = std::max(operation_bits, operation_width(edge.get_driver_pin()));
  }
  const auto otw            = std::to_string(operation_bits);
  // Inference can prove a narrow range, or cap an output-only result. HLOP's
  // operation carrier must still accept every operand; truncate at the landing.
  const auto land_operation = [&](std::string expression) {
    return operation_bits == wbits ? expression : absl::StrCat("Slop<", tw, ">{", expression, "}");
  };

  // 1-to-1 fold: `Slop<W>::op(a, b, ...)` over operands read at their OWN widths.
  // Arithmetic stays left-associated to match the previous member-chain result
  // exactly. Associative bitwise ops use bounded-depth emission: wide
  // decoder/packing nodes can have hundreds of inputs, and spelling those as
  // a left-deep C++ expression needlessly exceeds Clang's default
  // bracket-depth limit.
  auto fold = [&](const char* method, bool balance) -> std::string {
    if (e.empty()) {
      return absl::StrCat("Slop<", tw, ">::create_integer(0)");
    }
    if (e.size() == 1) {
      // A single operand still has to land at the node width.
      return absl::StrCat("Slop<", tw, ">{", operation_operand(e[0].get_driver_pin()), "}");
    }
    if (balance) {
      std::vector<std::string> layer;
      layer.reserve(e.size());
      for (const auto& edge : e) {
        layer.push_back(operation_operand(edge.get_driver_pin()));
      }
      while (layer.size() > 1) {
        std::vector<std::string> next;
        next.reserve((layer.size() + 1) / 2);
        for (size_t i = 0; i < layer.size(); i += 2) {
          if (i + 1 == layer.size()) {
            next.push_back(std::move(layer[i]));
          } else {
            next.push_back(absl::StrCat("Slop<", otw, ">::", method, "(", layer[i], ", ", layer[i + 1], ")"));
          }
        }
        layer = std::move(next);
      }
      return land_operation(std::move(layer.front()));
    }
    std::string s = operation_operand(e[0].get_driver_pin());
    for (size_t i = 1; i < e.size(); ++i) {
      s = absl::StrCat("Slop<", otw, ">::", method, "(", s, ", ", operation_operand(e[i].get_driver_pin()), ")");
    }
    return land_operation(std::move(s));
  };

  // The shift count of a SHL/SRA as a C++ int expression: a literal, or the
  // single word of an amount narrow enough that `lo + width` stays inside an
  // int. A signed count is read as is: hlop's shift asserts a non-negative
  // amount, so the literal `sra_op` lowering never saw a negative one either
  // (a loop ordinal times a lane width is stamped signed by the index port).
  // Empty = not usable that way.
  const auto shift_count_expr = [&](const hhds::Pin_class& n) -> std::string {
    if (n.is_invalid()) {
      return {};
    }
    if (n.is_const()) {
      const auto& nv = const_of(n);
      if (!nv.is_integer() || !nv.is_just_i64() || nv.to_just_i64() < 0 || nv.to_just_i64() > (int64_t{1} << 30)) {
        return {};
      }
      return std::to_string(nv.to_just_i64());
    }
    if (wbits_of(n) > 30 || !pin2var.contains(n.get_class_index())) {
      return {};
    }
    return absl::StrCat("static_cast<int>((", operation_operand(n), ").to_i64_low())");
  };
  // `(x >> n) & (2^W - 1)` / `Get_mask(x >> n, 2^W - 1)` with a RUNTIME `n` is
  // the shape upass.tolg lowers a constant-width slice at a runtime offset to
  // (`x#[(i*W) ..+ W]`, the per-entry read of a packed register file).
  // Evaluated literally it shifts and masks `x` at ITS width to keep W bits --
  // XiangShan's RenameTable does that on a 7,800-bit `diff_writes` (122 words)
  // for every entry x lane iteration. `get_mask_op_opt` accepts a runtime `lo`
  // and reads only the one or two source words the range touches. `shift_drv`
  // is the SRA output; `me` the mask's width. Empty = not that shape.
  const auto dynamic_extract = [&](const hhds::Pin_class& shift_drv, int me) -> std::string {
    const auto output = node.get_driver_pin(0);
    if (output.is_invalid() || !is_unsign(output) || wbits < 2 || shift_drv.is_invalid() || shift_drv.is_const()) {
      return {};
    }
    const auto shift = shift_drv.get_master_node();
    if (shift.is_invalid() || type_op_of(shift) != Ntype_op::SRA) {
      return {};
    }
    const auto x = get_driver_of_sink_name(shift, "a");
    const auto n = get_driver_of_sink_name(shift, "b");
    if (x.is_invalid() || x.is_const() || !pin2var.contains(x.get_class_index())) {
      return {};
    }
    // An unsigned source arrives canonical -- raw_operand names a tracked
    // Slop_u object or re-lands anything else through its zero-extend -- so
    // nothing above its width leaks; a signed source keeps its storage sign,
    // which is what `>>` fills with.
    const auto lo = shift_count_expr(n);
    if (lo.empty()) {
      return {};
    }
    const int len = std::min<int>(me, wbits - 1);
    if (len <= 0) {
      return {};
    }
    std::string carrier;
    if (slop_u_ && wbits == wbits_of(output) + 1) {
      slop_u_expr_ = true;
      carrier      = absl::StrCat("Slop_u<", wbits - 1, ">");
    } else {
      carrier = absl::StrCat("Slop<", tw, ">");
    }
    return absl::StrCat("([&]{ const int __lo = ",
                        lo,
                        "; return ",
                        carrier,
                        "::get_mask_op_opt(",
                        operation_operand(x),
                        ", __lo, __lo + ",
                        len,
                        "); }())");
  };
  // `(base & ~(K << n)) | ((v << n) & (K << n))` with K = 2^W - 1 and a RUNTIME
  // `n`: upass.tolg's lower_dynamic_mask_rmw for the packed write
  // `base#[(i*W) ..+ W] = v`. Seven full-width operations plus their temporaries
  // per write, on a 376-bit table three times per RenameTable entry;
  // `set_mask_op_opt` copies the base once and rewrites the one or two words
  // the range touches. Empty = not that shape.
  const auto dynamic_insert = [&]() -> std::string {
    if (e.size() != 2) {
      return {};
    }
    const auto output = node.get_driver_pin(0);
    if (output.is_invalid() || !is_unsign(output) || wbits < 2) {
      return {};
    }
    // One And keeps `base & inverted`, the other places `shifted & placed_mask`.
    struct Half {
      hhds::Pin_class value;  // base, or the shifted value
      hhds::Pin_class mask;   // Xor(M, all-ones), or M
    };
    const auto and_halves = [&](const hhds::Pin_class& drv) -> std::optional<Half> {
      if (drv.is_invalid() || drv.is_const()) {
        return std::nullopt;
      }
      const auto andn = drv.get_master_node();
      if (andn.is_invalid() || type_op_of(andn) != Ntype_op::And) {
        return std::nullopt;
      }
      std::vector<hhds::Pin_class> ins;
      for (const auto& isnk : andn.inp_sorted_pins()) {
        const auto idrv = isnk.get_driver_pin();
        ins.push_back(idrv);
      }
      if (ins.size() != 2) {
        return std::nullopt;
      }
      return Half{ins[0], ins[1]};
    };
    // M = SHL(K, n) with K a low-contiguous constant mask: returns (K width, n).
    const auto placed_mask = [&](const hhds::Pin_class& m) -> std::optional<std::pair<int, hhds::Pin_class>> {
      if (m.is_invalid() || m.is_const()) {
        return std::nullopt;
      }
      const auto shl = m.get_master_node();
      if (shl.is_invalid() || type_op_of(shl) != Ntype_op::SHL) {
        return std::nullopt;
      }
      const auto k = get_driver_of_sink_name(shl, "a");
      const auto n = get_driver_of_sink_name(shl, "b");
      if (k.is_invalid() || !k.is_const() || n.is_invalid()) {
        return std::nullopt;
      }
      const auto& kv = const_of(k);
      if (kv.has_unknowns() || kv.is_negative()) {
        return std::nullopt;
      }
      const auto [kb, ke] = kv.get_mask_range();
      if (kb != 0 || ke <= 0) {
        return std::nullopt;
      }
      return std::pair{static_cast<int>(ke), n};
    };
    const auto inverted_of = [&](const hhds::Pin_class& inv) -> hhds::Pin_class {  // Xor(M, all-ones) -> M
      if (inv.is_invalid() || inv.is_const()) {
        return {};
      }
      const auto xorn = inv.get_master_node();
      if (xorn.is_invalid() || type_op_of(xorn) != Ntype_op::Xor) {
        return {};
      }
      hhds::Pin_class m;
      bool            all_ones = false;
      int             count    = 0;
      for (const auto& sink : xorn.inp_sorted_pins()) {
        const auto drv = sink.get_driver_pin();
        ++count;
        if (drv.is_const()) {
          const auto& cv = const_of(drv);
          if (cv.has_unknowns() || cv.is_negative()) {
            return {};
          }
          const auto [cb, ce] = cv.get_mask_range();
          all_ones            = cb == 0 && ce >= wbits_of(output);
        } else {
          m = drv;
        }
      }
      return count == 2 && all_ones ? m : hhds::Pin_class{};
    };
    for (int keep_index = 0; keep_index < 2; ++keep_index) {
      const auto keep  = and_halves(e[keep_index].get_driver_pin());
      const auto place = and_halves(e[1 - keep_index].get_driver_pin());
      if (!keep || !place) {
        continue;
      }
      for (int ki = 0; ki < 2; ++ki) {
        const auto& base = ki == 0 ? keep->value : keep->mask;
        const auto  m    = inverted_of(ki == 0 ? keep->mask : keep->value);
        if (m.is_invalid()) {
          continue;
        }
        for (int pi = 0; pi < 2; ++pi) {
          const auto& shifted = pi == 0 ? place->value : place->mask;
          const auto& pm      = pi == 0 ? place->mask : place->value;
          if (!(pm == m)) {
            continue;
          }
          const auto placed = placed_mask(m);
          if (!placed || shifted.is_invalid() || shifted.is_const()) {
            continue;
          }
          const auto shl = shifted.get_master_node();
          if (shl.is_invalid() || type_op_of(shl) != Ntype_op::SHL) {
            continue;
          }
          const auto v  = get_driver_of_sink_name(shl, "a");
          const auto vn = get_driver_of_sink_name(shl, "b");
          if (v.is_invalid() || vn.is_invalid() || !(vn == placed->second) || base.is_invalid() || base.is_const()
              || !pin2var.contains(base.get_class_index())) {
            continue;
          }
          const auto lo = shift_count_expr(placed->second);
          if (lo.empty()) {
            continue;
          }
          const int width = placed->first;
          const int bw    = wbits_of(base);
          const int vw    = v.is_const() ? static_cast<int>(const_of(v).get_signed_bits()) : wbits_of(v);
          if (bw <= 0 || vw <= 0 || vw > bw) {
            continue;
          }
          const bool canonical_base = is_unsign(base) && slop_u_values_.contains(base.get_class_index());
          if (canonical_base && slop_u_ && wbits == bw + 1 && wbits_of(output) == bw) {
            // Slop_u<bw>::set_mask_op_opt keeps the base canonical; the value
            // rides its Slop<bw+1> carrier.
            slop_u_expr_ = true;
            return absl::StrCat("([&]{ const int __lo = ",
                                lo,
                                "; return ",
                                operation_operand(base),
                                ".set_mask_op_opt(__lo, std::min(__lo + ",
                                width,
                                ", ",
                                bw,
                                "), Slop<",
                                bw + 1,
                                ">{",
                                operation_operand(v),
                                "}); }())");
          }
          // A SIGNED base only at the SAME width: `set_mask_op_opt` returns a
          // Slop<bw> that keeps the base's sign bit, and the Or node this
          // replaces is unsigned (checked above), so its bits at and above `bw`
          // must be ZERO. The `Slop<tw>{...}` cross-width ctor sign-extends
          // instead, which for a negative base lands a negative carrier where
          // the `|` yields a positive value. Widening is left to the generic
          // or_op fold rather than spelled with the wrong extension here.
          if (!is_unsign(base) && !canonical_.contains(base.get_class_index()) && wbits == bw) {
            return absl::StrCat("([&]{ const int __lo = ",
                                lo,
                                "; return ",
                                operation_operand(base),
                                ".set_mask_op_opt(__lo, std::min(__lo + ",
                                width,
                                ", ",
                                bw,
                                "), Slop<",
                                bw,
                                ">{",
                                operation_operand(v),
                                "}); }())");
          }
        }
      }
    }
    return {};
  };

  switch (op) {
    case Ntype_op::Sum: {
      std::string result = absl::StrCat("Slop<", tw, ">::create_integer(0)");
      for (const auto& ed : e) {
        result = absl::StrCat("Slop<",
                              otw,
                              ">::",
                              Ntype::sink_bank(Ntype_op::Sum, ed.get_port_id()) == 0 ? "add_op" : "sub_op",
                              "(",
                              result,
                              ", ",
                              operation_operand(ed.get_driver_pin()),
                              ")");
      }
      return land_operation(std::move(result));
    }
    case Ntype_op::And: {
      if (e.size() == 2) {
        for (int mask_index = 0; mask_index < 2; ++mask_index) {
          const auto& mask_drv = e[mask_index].get_driver_pin();
          if (!mask_drv.is_const()) {
            continue;
          }
          const auto& mv = const_of(mask_drv);
          if (mv.has_unknowns() || mv.is_negative()) {
            continue;
          }
          const auto [mb, me] = mv.get_mask_range();
          if (mb != 0 || me <= 0) {
            continue;
          }
          if (auto extract = dynamic_extract(e[1 - mask_index].get_driver_pin(), me); !extract.empty()) {
            return extract;
          }
        }
      }
      return fold("and_op", true);
    }
    case Ntype_op::Or: {
      if (auto insert = dynamic_insert(); !insert.empty()) {
        return insert;
      }
      return fold("or_op", true);
    }
    case Ntype_op::Xor: {
      // Verilog `~x` reaches LGraph as Xor(x, all-ones-at-the-result-width) --
      // there is no Not cell on that path -- and cgen used to emit the xor
      // against a materialized constant (every wide `constexpr` literal in the
      // measured corpus was one of these). `Slop_u<W>::not_op` is the same
      // value in one call, mask included, with no constant at all.
      if (e.size() == 2 && slop_u_) {
        const auto output = node.get_driver_pin(0);
        const int  ow     = output.is_invalid() ? 0 : wbits_of(output);
        if (ow > 0 && wbits == ow + 1 && proven_unsigned_result(node, output)) {
          for (int ci = 0; ci < 2; ++ci) {
            if (!e[ci].get_driver_pin().is_const() || e[1 - ci].get_driver_pin().is_const()) {
              continue;
            }
            // A mask covering [0, ow) flips exactly the bits the unsigned
            // result keeps; anything it selects above ow the landing drops.
            const auto window = livehd::graph_util::mask_window_of(const_of(e[ci].get_driver_pin()));
            if (!window || window->first != 0 || window->second < ow) {
              continue;
            }
            slop_u_expr_ = true;
            return absl::StrCat("Slop_u<", ow, ">::not_op(", operation_operand(e[1 - ci].get_driver_pin()), ")");
          }
        }
      }
      return fold("xor_op", true);
    }
    case Ntype_op::Mult: return fold("mult_op", false);
    case Ntype_op::Div : {
      // Binary, order-sensitive, and sign-aware (slop div_op dispatches on the
      // operand sign). Without this case Div fell through to the default and
      // simulated as the bare dividend. Read each operand at its own width with
      // one headroom bit when either side is signed, so a signed operand actually
      // sign-extends before the divide (same reasoning as the LT/GT/SRA cases);
      // the quotient then fits to the node width on assignment.
      if (e.size() < 2) {
        return e.empty() ? absl::StrCat("Slop<", tw, ">::create_integer(0)") : operand(e[0].get_driver_pin(), wbits);
      }
      int cw = std::max({wbits_of(e[0].get_driver_pin()), wbits_of(e[1].get_driver_pin()), wbits, 1});
      if (!is_unsign(e[0].get_driver_pin()) || !is_unsign(e[1].get_driver_pin())) {
        cw += 1;
      }
      auto expression = absl::StrCat(operand(e[0].get_driver_pin(), cw), ".div_op(", operand(e[1].get_driver_pin(), cw), ")");
      return cw == wbits ? expression : absl::StrCat("Slop<", tw, ">{", expression, "}");
    }
    case Ntype_op::Rem: {
      // Same shape and the SAME reason as Div above: this switch has no
      // fail-closed default, so a missing arm here would silently simulate
      // `a % b` as the bare dividend. Truncated remainder, sign following the
      // dividend -- Dlop::rem_op is exactly that, and there is only one flavour
      // because every value is signed.
      if (e.size() < 2) {
        return e.empty() ? absl::StrCat("Slop<", tw, ">::create_integer(0)") : operand(e[0].get_driver_pin(), wbits);
      }
      int cw = std::max({wbits_of(e[0].get_driver_pin()), wbits_of(e[1].get_driver_pin()), wbits, 1});
      if (!is_unsign(e[0].get_driver_pin()) || !is_unsign(e[1].get_driver_pin())) {
        cw += 1;
      }
      auto expression = absl::StrCat(operand(e[0].get_driver_pin(), cw), ".rem_op(", operand(e[1].get_driver_pin(), cw), ")");
      return cw == wbits ? expression : absl::StrCat("Slop<", tw, ">{", expression, "}");
    }
    case Ntype_op::Rxor    :
    case Ntype_op::Popcount: {
      const auto input = get_driver_of_sink_name(node, "a");
      return absl::StrCat("Slop<",
                          tw,
                          ">::",
                          op == Ntype_op::Rxor ? "rxor_op" : "popcount_op",
                          "(",
                          operation_operand(input),
                          ", ",
                          livehd::graph_util::reduction_count(node),
                          ")");
    }
    case Ntype_op::Ror: {
      // OR-reduction: 1 iff ANY bit of ANY operand is set. ONE variadic call --
      // the Ror cell folds every driver of its single sink pin, and the static
      // materializes the 0/1 MAGNITUDE at the node width, so the member chain's
      // trailing clamp (the member reduce returned a Boolean at the OPERAND
      // width) is gone. Each operand is still read at its OWN full width, not
      // the 1-bit node width. Matches the Verilog cgen `|a` / `|{a|b|...}`.
      if (e.empty()) {
        return absl::StrCat("Slop<", tw, ">::create_integer(0)");
      }
      std::string args;
      for (size_t i = 0; i < e.size(); ++i) {
        absl::StrAppend(&args, i == 0 ? "" : ", ", operand(e[i].get_driver_pin(), std::max(wbits_of(e[i].get_driver_pin()), 1)));
      }
      return absl::StrCat("Slop<", tw, ">::ror_op(", args, ")");
    }
    case Ntype_op::Not: {
      if (e.empty()) {
        return absl::StrCat("Slop<", tw, ">::create_integer(0)");
      }
      // `~x` sets every bit above the operand's magnitude, so an UNSIGNED result
      // has to be re-masked. Slop_u<W>::not_op does that inside the call, which
      // is the same one mask the `land_operation` brace-wrap costs -- and one
      // fewer operation in the emitted text.
      const auto output = node.get_driver_pin(0);
      if (slop_u_ && !output.is_invalid() && proven_unsigned_result(node, output) && wbits == wbits_of(output) + 1) {
        slop_u_expr_ = true;
        return absl::StrCat("Slop_u<", wbits - 1, ">::not_op(", operation_operand(e[0].get_driver_pin()), ")");
      }
      return land_operation(absl::StrCat("Slop<", otw, ">::not_op(", operation_operand(e[0].get_driver_pin()), ")"));
    }
    case Ntype_op::LT:
    case Ntype_op::GT:
    case Ntype_op::EQ: {
      if (e.size() < 2) {
        return absl::StrCat("Slop<", tw, ">::create_integer(0)");
      }
      // AND-reduction lowers to EQ(Sext(x, k), -1). Its width is the explicit
      // Sext operand, so a narrow signed source still extends through bit k-1.
      if (op == Ntype_op::EQ && e.size() == 2) {
        for (int ci = 0; ci < 2; ++ci) {
          const auto& constant = e[ci].get_driver_pin();
          const auto& value    = e[1 - ci].get_driver_pin();
          if (!constant.is_const() || !livehd::graph_util::is_whole_value_mask(const_of(constant)) || value.is_const()) {
            continue;
          }
          const auto sx = value.get_master_node();
          if (type_op_of(sx) != Ntype_op::Sext) {
            continue;
          }
          const auto input = get_driver_of_sink_name(sx, "a");
          const auto count = get_driver_of_sink_name(sx, "b");
          if (input.is_invalid() || !count.is_const()) {
            continue;
          }
          const auto& width = const_of(count);
          if (!width.is_just_i64() || width.has_unknowns() || width.to_just_i64() <= 0
              || width.to_just_i64() > std::numeric_limits<int>::max()) {
            continue;
          }
          const auto bits   = static_cast<int>(width.to_just_i64());
          const auto source = input.is_const() || pin2var.contains(input.get_class_index())
                                  ? operation_operand(input)
                                  : reduction_operand(operation_operand(value), bits);
          return absl::StrCat("Slop<", tw, ">::rand_op(", source, ", ", bits, ")");
        }
      }
      int cw = std::max({wbits_of(e[0].get_driver_pin()), wbits_of(e[1].get_driver_pin()), 1});
      // An ORDERED compare (LT/GT) is sign-aware: a signed operand read at its OWN
      // width is not sign-extended, so its stored value stays a positive magnitude
      // (0xF8 == 248, not -8) and the compare goes wrong. Give one extra bit of
      // headroom when EITHER side is signed so `operand()`'s signed read actually
      // sign-extends. EQ is a bit-pattern compare -- no headroom (would break the
      // signed-vs-unsigned same-bits case).
      if (op != Ntype_op::EQ && (!is_unsign(e[0].get_driver_pin()) || !is_unsign(e[1].get_driver_pin()))) {
        cw += 1;
      }
      const char* m = (op == Ntype_op::LT) ? "lt_op" : (op == Ntype_op::GT) ? "gt_op" : "eq_op";
      // `!x` lowers to EQ(x, 0) (upass_tolg's lower_log_not), and so does a
      // written `x == 0`. Both are lnot_op, which asks the question without
      // materializing a FULL-WIDTH zero to compare against -- on a 512-bit
      // operand that constant was the whole cost of the cell.
      int  lnot_side = -1;
      if (op == Ntype_op::EQ) {
        for (int i = 0; i < 2; ++i) {
          if (e[i].get_driver_pin().is_const() && const_of(e[i].get_driver_pin()).is_known_zero()) {
            lnot_side = 1 - i;
            break;
          }
        }
      }
      // 1-to-1: the mixed-width compare reads both operands at their own widths
      // (sign-extending each into the compare, so a narrow signed operand still
      // compares signed) and materializes a 0/1 MAGNITUDE at the node width.
      // That replaces three emitted conversions -- two operand reads plus the
      // `.zext_to<1>().zext_to<tw>()` clamp that existed only because the member
      // form used to return an all-ones true. The `cw += 1` headroom above is
      // likewise unnecessary here: it existed only to force cw != operand width so
      // the cross-width ctor would fire instead of the copy ctor.
      const auto  output = node.get_driver_pin(0);
      const auto  args   = lnot_side >= 0 ? raw_operand(e[lnot_side].get_driver_pin(), cw)
                                          : absl::StrCat(raw_operand(e[0].get_driver_pin(), cw), ", ", raw_operand(e[1].get_driver_pin(), cw));
      if (lnot_side >= 0) {
        m = "lnot_op";
      }
      if (slop_u_ && !output.is_invalid() && proven_unsigned_result(node, output) && wbits == wbits_of(output) + 1) {
        slop_u_expr_ = true;
        return absl::StrCat("Slop_u<", wbits - 1, ">::", m, "(", args, ")");
      }
      return absl::StrCat("Slop<", tw, ">::", m, "(", args, ")");
    }
    case Ntype_op::SHL:
    case Ntype_op::SRA: {
      const bool is_shl          = op == Ntype_op::SHL;
      const int  value_sign_mode = is_shl ? 0 : (!e.empty() && is_unsign(e[0].get_driver_pin()) ? -1 : 1);
      if (e.size() < 2) {
        return e.empty() ? absl::StrCat("Slop<", tw, ">::create_integer(0)") : operand(e[0].get_driver_pin(), wbits, value_sign_mode);
      }
      // A shift AMOUNT is a count, not a value in the datapath. When it is
      // constant, hand the int64 overload the number directly instead of
      // materializing a whole Slop<W> constant just to pass it (which, at
      // W > 64, was a multi-word object built per shift).
      if (e[1].get_driver_pin().is_const()) {
        const auto& amt = const_of(e[1].get_driver_pin());
        if (amt.is_integer() && amt.is_just_i64() && amt.to_just_i64() >= 0) {
          return land_operation(absl::StrCat("Slop<",
                                             otw,
                                             ">::",
                                             is_shl ? "shl_op" : "sra_op",
                                             "(",
                                             operation_operand(e[0].get_driver_pin()),
                                             ", ",
                                             amt.to_just_i64(),
                                             ")"));
        }
      }
      // A materialized `x >> n` whose every reader keeps only its low W bits
      // (the Get_mask lanes of a runtime slice that the color ABI pre-extracts
      // at the boundary, so the shift itself became a boundary member) needs
      // only max(W) bits: extract those directly instead of shifting the whole
      // word. Bits above are never observed by any consumer. Not under
      // observation: a VCD/probe would show the narrowed value.
      if (!is_shl && !observation_on && e[1].get_driver_pin().is_valid() && !e[1].get_driver_pin().is_const() && is_unsign(e[0].get_driver_pin())) {
        const auto output        = node.get_driver_pin(0);
        const bool all_low_masks = !output.is_invalid() && is_unsign(output) && wbits >= 2;
        const int  wmax          = all_low_masks ? low_lane_readers_width(output) : 0;
        if (all_low_masks && wmax > 0 && pin2var.contains(e[0].get_driver_pin().get_class_index())) {
          if (const auto lo = shift_count_expr(e[1].get_driver_pin()); !lo.empty()) {
            const int len = std::min<int>(wmax, wbits - 1);
            if (slop_u_ && wbits == wbits_of(output) + 1) {
              slop_u_expr_ = true;
              return absl::StrCat("([&]{ const int __lo = ",
                                  lo,
                                  "; return Slop_u<",
                                  wbits - 1,
                                  ">::get_mask_op_opt(",
                                  operation_operand(e[0].get_driver_pin()),
                                  ", __lo, __lo + ",
                                  len,
                                  "); }())");
            }
            return absl::StrCat("([&]{ const int __lo = ",
                                lo,
                                "; return Slop<",
                                tw,
                                ">::get_mask_op_opt(",
                                operation_operand(e[0].get_driver_pin()),
                                ", __lo, __lo + ",
                                len,
                                "); }())");
          }
        }
      }
      // Runtime amount width is independent of the datapath width. SRA is
      // arithmetic for signed inputs and logical for unsigned inputs, just as
      // cgen.verilog selects $signed only for a signed driver.
      if (is_shl) {
        return land_operation(
            absl::StrCat("Slop<", otw, ">::shl_op(", operation_operand(e[0].get_driver_pin()), ", ", operation_operand(e[1].get_driver_pin()), ")"));
      }
      return land_operation(
          absl::StrCat("Slop<", otw, ">::sra_op(", operation_operand(e[0].get_driver_pin()), ", ", operation_operand(e[1].get_driver_pin()), ")"));
    }
    case Ntype_op::Get_mask: {
      // The direct color ABI can carry an exact constant lane instead of the
      // producer's packed word. In that occurrence the mask operation has
      // already happened at the boundary; preserve the narrow binding as the
      // node value rather than extracting the same lane a second time.
      if (!e.empty() && preextracted_get_masks_.contains(node.get_class_index())) {
        const auto binding = pin2var.find(e[0].get_driver_pin().get_class_index());
        I(binding != pin2var.end());
        slop_u_expr_ = slop_u_values_.contains(e[0].get_driver_pin().get_class_index());
        return binding->second;
      }
      // value (e[0]) + optional mask (e[1]). The unary form is the common tolg
      // width-adjust; lower it to a plain zext.
      if (e.empty()) {
        return absl::StrCat("Slop<", tw, ">::create_integer(0)");
      }
      if (e.size() == 1) {
        if (raw_width_adjust_ok(e[0].get_driver_pin(), wbits)) {
          return raw_operand(e[0].get_driver_pin(), wbits);
        }
        return operand(e[0].get_driver_pin(), wbits, /*unsigned=*/-1);
      }
      // graph/cell.hpp: the mask pin is a CONSTANT that is either -1 ("the
      // whole value", the to-positive idiom) or ONE window [mb, me). Both land
      // on a single mask instruction, so nothing here builds a mask VALUE --
      // which used to mean a per-bit gather loop plus a from_pyrope parse of a
      // wide literal every cycle (68.9% + 27.8% of dino's simulation time,
      // against 2.2% in the actual module bodies).
      {
        livehd::graph_util::require_mask(e[1].get_driver_pin());
        const auto& mv = const_of(e[1].get_driver_pin());
        {
          // (a) mask == -1 is the to-positive idiom. The value is read UNSIGNED
          // (zext_to), which already yields a non-negative value, so making it
          // positive is the identity -- as is the trailing trim. The whole
          // `x.zext_to<W>().get_mask_op(-1).zext_to<W>()` sequence collapses to
          // `x.zext_to<W>()`.
          if (livehd::graph_util::is_whole_value_mask(mv)) {
            if (raw_width_adjust_ok(e[0].get_driver_pin(), wbits)) {  // same raw pass-through as the unary arm above
              return raw_operand(e[0].get_driver_pin(), wbits);
            }
            return operand(e[0].get_driver_pin(), wbits, /*unsigned=*/-1);
          }
          // (b) otherwise the mask is ONE window [mb, me), packed LSB-first in
          // place -- one bitfield extract, and no mask CONSTANT at all, so a
          // >64-bit mask no longer costs a from_pyrope string parse per cycle.
          {
            auto [mb, me] = livehd::graph_util::mask_window(mv);  // half-open
            // A window that reaches past the value's declared width selects its
            // SIGN bits. For an UNSIGNED value those are all zero, so a
            // zero-extended read already reproduces them; only a signed value
            // needs the sign actually replicated.
            //
            // Not a corner case: dino's ImmediateGenerator and top level mask
            // 4- and 15-bit intermediates with the 64-bit all-ones constant (a
            // plain "keep the low 64 bits"), and every one of those is out of
            // width.
            const bool in_width = me <= wbits_of(e[0].get_driver_pin());
            if (mb == 0 && me > 0) {
              if (auto extract = dynamic_extract(e[0].get_driver_pin(), me); !extract.empty()) {
                return extract;
              }
            }
            if (!in_width && !is_unsign(e[0].get_driver_pin())) {
              // A window reaching past a SIGNED value's declared width selects
              // its SIGN bits. Read the value at a carrier wide enough to hold
              // the window -- a signed read replicates the sign all the way up
              // -- and the same bitfield extract then produces those bits.
              // The lane lands one bit wider than the span so `len == N` never
              // makes get_mask_op_opt sign-extend the top extracted bit: a
              // Get_mask result is an UNSIGNED pack.
              const int span    = me - mb;
              const int read_cw = std::max({me, wbits_of(e[0].get_driver_pin()), 1});
              auto      lane    = absl::StrCat("Slop<", span + 1, ">::get_mask_op_opt(",
                                               operand(e[0].get_driver_pin(), read_cw, /*signed=*/1), ", ", mb, ", ", me, ")");
              return span + 1 == wbits ? lane : absl::StrCat("Slop<", tw, ">{", lane, "}");
            }
            {
              // get_mask packs the selected bits LSB-FIRST. Keep the contiguous
              // range as Get_mask and pass its literal bounds to HLOP. The
              // optimized implementation lowers a sub-word range to a direct
              // bitfield extraction and avoids both a mask and an SRA temporary.
              //
              // CONTRACT: `get_mask_op_opt(x, lo, hi)` requires `hi - lo <= N`
              // (the result carrier must hold the whole span) -- it asserts
              // otherwise, and clamps into a wrong value in a release build.
              // The span is NOT bounded by the carrier here: the very masks
              // this fast path exists for are the 64-bit all-ones constants
              // dino applies to 4- and 15-bit intermediates, whose Get_mask
              // result pin is stamped at the value's own (4/15-bit) range.
              // Clip the span to the carrier: those high positions are either
              // above an unsigned source (all zero) or would be truncated by
              // the landing into that carrier anyway, so clipping is exactly
              // the value the wide-mask read produces.
              const auto output   = node.get_driver_pin(0);
              const auto fast_end = [&](int carrier_bits) { return std::min<int>(me, mb + std::max(carrier_bits, 1)); };
              if (slop_u_ && !output.is_invalid() && proven_unsigned_result(node, output) && wbits == wbits_of(output) + 1) {
                slop_u_expr_ = true;
                return absl::StrCat("Slop_u<",
                                    wbits - 1,
                                    ">::get_mask_op_opt(",
                                    operation_operand(e[0].get_driver_pin()),
                                    ", ",
                                    mb,
                                    ", ",
                                    fast_end(wbits - 1),
                                    ")");
              }
              return absl::StrCat("Slop<",
                                  tw,
                                  ">::get_mask_op_opt(",
                                  operation_operand(e[0].get_driver_pin()),
                                  ", ",
                                  mb,
                                  ", ",
                                  fast_end(wbits),
                                  ")");
            }
          }
        }
      }
    }
    case Ntype_op::Set_mask: {
      // value.set_mask_op(mask, newbits) — best effort at the node width. The
      // inserted bits (e[2]) are read per their declared sign so a SIGNED source
      // sign-fills a slot wider than its width (e.g. `s#[5..=12] = i#sext[28..=31]`);
      // an unsigned source is unchanged (is_unsign -> zero-extend).
      if (e.size() < 3) {
        return e.empty() ? absl::StrCat("Slop<", tw, ">::create_integer(0)") : operand(e[0].get_driver_pin(), wbits, -1);
      }

      livehd::graph_util::require_mask(e[1].get_driver_pin());
      const auto& mask = const_of(e[1].get_driver_pin());
      if (livehd::graph_util::is_whole_value_mask(mask)) {
        return operand(e[2].get_driver_pin(), wbits);
      }
      auto [lo, hi]   = livehd::graph_util::mask_window(mask);
      const auto base = operand(e[0].get_driver_pin(), wbits);
      if (lo >= wbits) {
        return base;  // the entire write is outside the result carrier
      }
      hi = std::min(hi, wbits);
      if (lo == 0 && hi == wbits) {
        return operand(e[2].get_driver_pin(), wbits);
      }
      if (e[2].get_driver_pin().is_known_false()) {
        return absl::StrCat(base, ".clear_mask_op_opt(", lo, ", ", hi, ")");
      }
      return absl::StrCat(base, ".set_mask_op_opt(", lo, ", ", hi, ", ", operand(e[2].get_driver_pin(), wbits), ")");
    }

    case Ntype_op::Concat: {
      // MSB-first lane assembly. The lane table comes from concat_lanes(), never
      // from `e`: a lane's window width is an explicit const OPERAND (odd sink
      // pids) precisely because it is not recoverable from the driver, and
      // reading it back off `wbits_of` would shift every lane above the one that
      // got narrowed.
      const auto lanes = livehd::graph_util::concat_lanes(node);
      if (lanes.empty()) {
        // FAIL CLOSED. The generic pass-through below would emit lane 0 alone --
        // i.e. silently drop every other lane AND leave it unshifted, which no
        // differential test can distinguish from a plain assignment.
        livehd::diag::err("inou.cgen.sim", "concat-malformed", "internal")
            .msg("cell `{}` is a Concat whose lane table could not be decoded", debug_name(node))
            .hint(
                "a Concat's sinks are interleaved (value, width) pairs on p0,p1,p2,...; every odd pid must carry a "
                "positive integer constant")
            .fatal();
        return absl::StrCat("Slop<", tw, ">::create_integer(0)");
      }
      const int total = livehd::graph_util::concat_total_width(lanes);  // already decoded above
      // Each lane is materialized as a canonical unsigned Slop_u<w>: the ctor's
      // one zext_to<w> is what turns an over-wide or NEGATIVE lane into its
      // two's-complement window (-1 at w=3 becomes 0b111), and it is exactly the
      // mask hlop's concat_op then elides for that lane. Naming Slop_u also
      // carries the width in the TYPE, which is how concat_op learns the lane
      // table without a second operand list.
      struct Concat_expr {
        int         width;
        std::string text;
      };
      std::vector<Concat_expr> exprs;
      exprs.reserve(lanes.size());
      for (size_t i = 0; i < lanes.size(); ++i) {
        const auto& l = lanes[i];
        // Read the lane at w+1 bits, NOT at its own declared width.
        // `Slop<M>::zext_to<W>()` keeps min(M, W) bits -- it clamps to the
        // SOURCE carrier -- so a lane handed over in a carrier narrower than
        // its window silently loses the bits above that carrier. A CONSTANT
        // driver is the trap: const pins carry no `bits` attr, so sizing from
        // wbits_of() gave `Slop<1>::create_integer(4)`, and `Slop_u<4>{}` of
        // that kept ONE bit -- every constant lane collapsed to its LSB. (dino
        // is full of `{..., 4'd4, ...}`; it retired one instruction and hung.)
        //
        // operand() also gets the SIGN right at that width: a signed lane
        // sign-extends into the carrier first, so the Slop_u mask below takes
        // its true two's-complement window rather than a zero-padded one.
        exprs.push_back({l.width, absl::StrCat("Slop_u<", l.width, ">{", operand(l.value, l.width + 1), "}")});
      }
      // Clang limits a fold expression to 256 operands. Large generated
      // packed buses can legitimately exceed that (Minion has a 321-lane
      // concat), so assemble fixed-size groups first and concatenate those
      // groups at the root. This preserves the MSB-first lane order while
      // keeping every Slop_u::concat_op instantiation comfortably below the
      // compiler's expression-nesting limit.
      constexpr size_t kConcatGroup = 128;
      while (exprs.size() > kConcatGroup) {
        std::vector<Concat_expr> grouped;
        grouped.reserve((exprs.size() + kConcatGroup - 1) / kConcatGroup);
        for (size_t begin = 0; begin < exprs.size(); begin += kConcatGroup) {
          const auto  end         = std::min(begin + kConcatGroup, exprs.size());
          int         group_width = 0;
          std::string group_args;
          for (size_t i = begin; i < end; ++i) {
            if (i != begin) {
              absl::StrAppend(&group_args, ", ");
            }
            group_width += exprs[i].width;
            absl::StrAppend(&group_args, exprs[i].text);
          }
          grouped.push_back({group_width, absl::StrCat("Slop_u<", group_width, ">::concat_op(", group_args, ")")});
        }
        exprs = std::move(grouped);
      }
      std::string args;
      for (size_t i = 0; i < exprs.size(); ++i) {
        if (i) {
          absl::StrAppend(&args, ", ");
        }
        absl::StrAppend(&args, exprs[i].text);
      }
      // Assemble UNSIGNED and land signed. Slop<N>::concat_op would sign-extend
      // from the top lane's MSB, but a Concat cell's value is defined to be the
      // non-negative sum-of-windows, so the unsigned flavour is the correct one;
      // the outer Slop<tw>{} is then the free (already-canonical) widening.
      //
      // That last step is only free while tw > total. hlop's Slop<N>{Slop_u<M>}
      // ctor SIGN-EXTENDS FROM BIT N-1 when N <= M (hlop/slop.hpp:206-212, whose
      // own comment reads "Slop<8>{Slop_u<8>{255}} is -1 while
      // Slop<9>{Slop_u<8>{255}} is 255"), so landing a 7-bit concat of all ones
      // in a Slop<7> yields -1 instead of 127 -- a silent miscompile, not a
      // truncation anyone would notice. Concat's literal result width is
      // sum(w); the unsigned expression landing deliberately requests its
      // one-bit-wider Slop carrier here.
      I(wbits > total,
        std::format("internal: Concat carrier Slop<{}> is not wider than its {}-bit assembly -- hlop would sign-extend "
                    "from bit {}, turning the all-ones value into -1; the unsigned landing carrier must be sum(w)+1 = {}",
                    wbits,
                    total,
                    wbits - 1,
                    total + 1)
            .c_str());
      const auto output = node.get_driver_pin(0);
      if (slop_u_ && !output.is_invalid() && proven_unsigned_result(node, output) && wbits == total + 1) {
        slop_u_expr_ = true;
        return absl::StrCat("Slop_u<", total, ">::concat_op(", args, ")");
      }
      return absl::StrCat("Slop<", tw, ">{Slop_u<", total, ">::concat_op(", args, ")}");
    }
    case Ntype_op::Sext: {
      // value sign-extended from a bit position (2nd input, normally constant).
      if (e.empty()) {
        return absl::StrCat("Slop<", tw, ">::create_integer(0)");
      }
      int frombit = wbits - 1;
      if (e.size() > 1 && e[1].get_driver_pin().is_const()) {
        // LGraph Sext(a, b) keeps b bits [b-1:0] (cgen_verilog emits `a[b-1:0]`),
        // i.e. the sign bit is at b-1. Slop::sext_op(fb) takes fb as the sign-bit
        // position, so pass b-1 (NOT b) or the value stays unsigned/off-by-one.
        frombit = static_cast<int>(const_of(e[1].get_driver_pin()).to_just_i64()) - 1;
      }
      // `x#sext[lo..=hi]`: a contiguous Get_mask lane whose width IS the
      // sign-extension width. `Slop<W>::get_mask_op_opt` sign-extends when the
      // range fills the carrier (len == N), so the lane lands signed in one
      // bitfield extract instead of extract + re-sign (matched_filter's 64
      // multiplier operands paid the pair twice per tap).
      if (frombit >= 0 && frombit + 1 <= wbits && !e[0].get_driver_pin().is_invalid() && !e[0].get_driver_pin().is_const()) {
        const auto extract = e[0].get_driver_pin().get_master_node();
        if (!extract.is_invalid() && type_op_of(extract) == Ntype_op::Get_mask) {
          if (preextracted_get_masks_.contains(extract.get_class_index())) {
            // The ABI binding is already LSB-aligned. Reinterpret that lane's
            // own sign bit, without applying its original source offset again.
            const auto lane = absl::StrCat("Slop<",
                                           frombit + 1,
                                           ">::get_mask_op_opt(",
                                           operation_operand(e[0].get_driver_pin()),
                                           ", 0, ",
                                           frombit + 1,
                                           ")");
            return frombit + 1 == wbits ? lane : absl::StrCat("Slop<", tw, ">{", lane, "}");
          }
          const auto x    = get_driver_of_sink_name(extract, "a");
          const auto mask = get_driver_of_sink_name(extract, "mask");
          if (!x.is_invalid() && !mask.is_invalid() && (x.is_const() || pin2var.contains(x.get_class_index()))) {
            livehd::graph_util::require_mask(mask);
            // The window reader is shared with every other Get_mask consumer;
            // it answers nothing for the -1 whole-value spelling, which is not
            // a bitfield extract and so is not fusable here either.
            if (const auto window = livehd::graph_util::mask_window_of(const_of(mask)); window) {
              const auto [mb, me] = *window;
              // Same admissibility the ordinary Get_mask lowering requires of
              // this cell: a range reaching past a SIGNED source's width is not
              // a bitfield extract there, so it must not become one here.
              const bool in_width = me <= wbits_of(x);
              if (me - mb == frombit + 1 && (in_width || is_unsign(x))) {
                const auto lane
                    = absl::StrCat("Slop<", frombit + 1, ">::get_mask_op_opt(", operation_operand(x), ", ", mb, ", ", me, ")");
                return frombit + 1 == wbits ? lane : absl::StrCat("Slop<", tw, ">{", lane, "}");
              }
            }
          }
        }
      }
      // read the source wide enough to preserve the sign bit before extending
      int sw = std::max({wbits, frombit + 1, wbits_of(e[0].get_driver_pin())});
      return absl::StrCat("Slop<", tw, ">{", operand(e[0].get_driver_pin(), sw, /*signed=*/1), ".sext_op(", std::to_string(frombit), ")}");
    }
    case Ntype_op::Hotmux: {
      // Semantically `Slop<tw>::hotmux_op(c0, v0, c1, v1, ... [, default])` and
      // MUST stay identical to it (hlop slop.hpp): control active on NON-ZERO,
      // first-wins on a contract-violating overlap, all-zero reads the trailing
      // default or 0. It is open-coded rather than called for the same reason
      // the 2-arm Mux below is a ternary: a call evaluates EVERY arm, and under
      // single-use forestation each arm is a whole inlined expression tree.
      const auto  inputs = livehd::graph_util::hotmux_inputs(node);
      std::string result = absl::StrCat("([&]() -> Slop<", tw, "> { int __hotmux_arm = -1;");
      for (size_t i = 0; i < inputs.arms.size(); ++i) {
        const auto& control = inputs.arms[i].first;
        // `assert` is compiled out of a release simulator, so the arm must also
        // be claimed FIRST-WINS rather than by the last matching control: that
        // is the priority cgen_verilog's `unique case` and the SMT encoders use,
        // and it keeps a (contract-violating) multi-hot run agreeing with them
        // instead of silently reporting a different arm.
        absl::StrAppend(&result,
                        " if (",
                        emit_known_true(raw_operand(control, std::max(wbits_of(control), 1))),
                        ") { assert(__hotmux_arm == -1 && \"hotmux controls overlap\");"
                        " if (__hotmux_arm == -1) { __hotmux_arm = ",
                        i,
                        "; } }");
      }
      absl::StrAppend(&result, " switch (__hotmux_arm) {");
      for (size_t i = 0; i < inputs.arms.size(); ++i) {
        absl::StrAppend(&result, " case ", i, ": return Slop<", tw, ">{", operand(inputs.arms[i].second, wbits), "};");
      }
      absl::StrAppend(&result,
                      " default: return ",
                      inputs.fallback.is_invalid() ? absl::StrCat("Slop<", tw, ">::create_integer(0)")
                                                   : absl::StrCat("Slop<", tw, ">{", operand(inputs.fallback, wbits), "}"),
                      "; } }())");
      return result;
    }
    case Ntype_op::Mux: {
      if (e.size() < 3) {
        return absl::StrCat("Slop<", tw, ">::create_integer(0)");
      }
      const size_t n_vals = e.size() - 1;
      if (op == Ntype_op::Mux && n_vals == 2) {
        // A 2-arm Mux selector is a CONDITION, not an index: ANY nonzero value
        // selects arm 1. That matches cgen_verilog (`sel ? arm1 : arm0`, whose
        // netlist `lhd lec` proves) and the LGraph model, where an `if` cone
        // hands the Mux whatever the condition computed -- `a & 0x80` arrives
        // as 128, not as 1 (a low-bit truncation here silently took arm 0 for
        // any nonzero-but-even condition; the nonzero test maps both the
        // all-ones boolean and 128 to 1).
        //
        // Emitted as a C++ TERNARY with the selector read RAW at its own
        // width. Two costs die at once: the old form materialized the
        // selector at the RESULT width plus a second result-width 0/1 Slop
        // (two wide temporaries of pure ceremony — the largest zext
        // population in the dino emission), and ANY call-based mux
        // evaluates BOTH arms eagerly — with single-use forestation the
        // arms are whole inlined expression trees, so lazy arm evaluation
        // is the point, exactly like cgen_verilog's `sel ? a : b`. Each arm is
        // read at the result width in the selected branch. This landing may
        // truncate: bitwidth can cap a mux used only by a declared output.
        // The generic HLOP mux/hotmux APIs require lossless carriers, so the
        // explicit branch-local operand conversion is important here.
        //
        // Returned BEFORE the arm list / selector below are built: with
        // forestation each arm is a whole inlined tree, and the call form's
        // `vals` + result-width `sel` would be built and thrown away for every
        // one of the 2-arm muxes that dominate a real design.
        return absl::StrCat("(",
                            emit_known_true(raw_operand(e[0].get_driver_pin(), std::max(wbits_of(e[0].get_driver_pin()), 1))),
                            " ? ",
                            operand(e[2].get_driver_pin(), wbits),
                            " : ",
                            operand(e[1].get_driver_pin(), wbits),
                            ")");
      }

      // Indexed Mux selectors keep their full width so an out-of-range
      // high bit cannot be truncated into range.
      const int   sel_w = e[0].get_driver_pin().is_const() ? std::max({wbits_of(e[0].get_driver_pin()), const_of(e[0].get_driver_pin()).get_signed_bits(), 1})
                                                 : std::max(wbits_of(e[0].get_driver_pin()), 1);
      const auto  sel   = operand(e[0].get_driver_pin(), sel_w, /*unsigned=*/-1);
      std::string vals;
      for (size_t i = 1; i < e.size(); ++i) {
        if (!vals.empty()) {
          vals += ", ";
        }
        // Arms are VALUES in the result context. In particular, a u1 arm read
        // as Slop<1> turns bit 0 into a sign bit (-1); reading it in the u1
        // result's Slop<2> carrier preserves +1 and lets hotmux/mux copy a
        // canonical value. Signed arms still sign-extend through operand().
        vals += operand(e[i].get_driver_pin(), wbits);
      }
      return absl::StrCat("Slop<", tw, ">::", "mux_op", "(", sel, ", ", vals, ")");
    }
    case Ntype_op::Clock_cell:
      // FAIL CLOSED, never fall into the pass-through below. A Clock_cell's
      // first input is `clk_ref`, so the generic fallback would emit the
      // UNGATED reference clock and silently drop the gate -- the flop would
      // then commit every tick with the enable as dead code. That is the same
      // silent miscompile `gated-clock-unsupported` refuses one level up, and a
      // clock gate is precisely the shape where it is invisible to a
      // before/after comparison. The M9 lowering resolves the cell to a commit
      // guard instead; until it lands for this shape, refuse by name.
      livehd::diag::err("inou.cgen.sim", "clock-cell-unsupported", "unsupported")
          .msg("cell `{}` is a Clock_cell reaching expression emission", debug_name(node))
          .hint(
              "a Clock_cell must be lowered to a flop/memory COMMIT GUARD, never evaluated as a data expression -- "
              "emitting it as a value would hand the simulator an ungated clock with the enable as dead code")
          .fatal();
      return absl::StrCat("Slop<", tw, ">::create_integer(0)");
    default:
      // Compiling fallback: pass the first input through at the node width (the
      // per-operand width conversion already enforces wbits). Covers width-trim
      // Get_mask and not-yet-modeled ops; the iverilog differential test flags
      // any that need exact lowering.
      return e.empty() ? absl::StrCat("Slop<", tw, ">::create_integer(0)") : operand(e[0].get_driver_pin(), wbits);
  }
}

namespace {
// Inline a PURE-COMB Sub instance that sits on a FALSE combinational loop -- its
// output feeds back into one of its OWN inputs through parent logic -- into `g`.
// inou.cgen.sim schedules a Sub atomically (all inputs -> all outputs via one
// child.cycle()), so such an instance is an uncuttable node-level cycle even
// though the callee's output cones are independent; flattening it lets
// forward_class order the now-flat DAG. Only STATELESS (pure-comb) callees are
// flattened, so flop/memory/VCD/checkpoint semantics are unchanged, and only a
// Sub genuinely ON a false loop is touched (normal hierarchical sims are
// identical). Runs on the live sim graph, before emission. Returns #flattened.

// A Sub S sits on a FALSE combinational loop when a backward COMB walk from one
// of its input drivers reaches S itself (stopping at other state/Sub/IO
// boundaries). Returns the S output PORT-IDS the loop threads through (the
// fed-back outputs); empty means S is not on a false loop. The Moore-sub
// deferral only needs non-emptiness; the per-output-cone (Stage-2) deferral
// checks each fed-back port is a pure state read.
// `sub_out_is_state_only(node, pid)` classifies another Sub's OUTPUT reached
// mid-walk: a pure current-state read is a REAL boundary (its value exists
// before any call), but a comb-dependent output is an atomic pass-through --
// the value needs the call, the call needs all its inputs, so the walk
// continues through the callee's input drivers. Traversing pass-through Subs
// is what lets a MULTI-INSTANCE false loop (e.g. if_id -> hazard -> if_id,
// the dino dual-issue shape) reach `s` at all; the old all-Subs-opaque walk
// only caught direct self-feedback through parent comb logic.
template <typename F>
absl::flat_hash_set<uint32_t> sub_false_loop_output_pids(const hhds::Node_class& s, F&& sub_out_is_state_only) {
  namespace gu = livehd::graph_util;
  absl::flat_hash_set<uint32_t>         pids;
  absl::flat_hash_set<hhds::Node_class> seen;
  std::vector<hhds::Pin_class>          stk;
  // STAYS ON EDGES (this seed and the Sub pass-through walk below): `s` and any
  // Sub reached mid-walk may be a COMPACT LOOP, whose carry-in sink is the one
  // pin hhds still sanctions with two drivers (seed + previous-ordinal self
  // edge; see pass/legalize's verify_single_driver_sinks). An in-PIN walk would
  // yield that pin once and get_driver_pin() would return only one of the two,
  // so a real external ring could go unseen -- and debug builds assert on it.
  for (auto e_sink : s.inp_sorted_pins()) {
    for (auto e_drv : e_sink.get_driver_pins()) {
      stk.push_back(e_drv);
    }
  }
  while (!stk.empty()) {
    auto d = stk.back();
    stk.pop_back();
    if (d.is_invalid() || d.is_const()) {
      continue;
    }
    auto m = d.get_master_node();
    if (m == s) {
      pids.insert(static_cast<uint32_t>(d.get_port_id()));
      continue;  // stop AT s; keep walking the rest for every fed-back port
    }
    auto op = gu::type_op_of(m);
    if (op == Ntype_op::Memory || gu::is_type_register(m) || op == Ntype_op::IO) {
      continue;  // a real state boundary -- the loop does not thread through it
    }
    if (op == Ntype_op::Sub) {
      if (sub_out_is_state_only(m, static_cast<uint32_t>(d.get_port_id()))) {
        continue;  // Moore/state-read output: available pre-call, boundary
      }
      if (!seen.insert(m).second) {
        continue;
      }
      for (auto e_sink : m.inp_sorted_pins()) {
        for (auto e_drv : e_sink.get_driver_pins()) {  // comb pass-through: the call's inputs
          stk.push_back(e_drv);
        }
      }
      continue;
    }
    if (!seen.insert(m).second) {
      continue;
    }
    for (const auto& sink : m.inp_sorted_pins()) {
      const auto drv = sink.get_driver_pin();
      stk.push_back(drv);
    }
  }
  return pids;
}

// Same boundary question for a compact loop, excluding its descriptor carry
// self-edges. Any remaining path from an external input driver back to this
// node is a parent-level false ring; the native eager wrapper deliberately does
// not solve those and the caller uses occurrence expansion as its backstop.
bool compact_loop_has_external_ring(const hhds::Node_class& s) {
  namespace gu = livehd::graph_util;
  absl::flat_hash_set<hhds::Node_class> seen;
  std::vector<hhds::Pin_class>          stk;
  // STAYS ON EDGES: this is the compact-loop carry itself -- the filter is
  // per-EDGE (drop the self edge, keep the seed) and that sink pin has BOTH.
  for (auto e_sink : s.inp_sorted_pins()) {
    for (auto e_drv : e_sink.get_driver_pins()) {
      if (e_drv.get_master_node() != s) {
        stk.push_back(e_drv);
      }
    }
  }
  while (!stk.empty()) {
    auto d = stk.back();
    stk.pop_back();
    if (d.is_invalid() || d.is_const()) {
      continue;
    }
    auto n = d.get_master_node();
    if (n == s) {
      return true;
    }
    const auto op = gu::type_op_of(n);
    if (gu::is_type_register(n) || op == Ntype_op::Memory || op == Ntype_op::IO) {
      continue;
    }
    if (!seen.insert(n).second) {
      continue;
    }
    // Sub is NOT excluded here, so `n` may be a compact loop: stays on edges
    // for the carry-in reason spelled out in sub_false_loop_output_pids.
    for (auto e_sink : n.inp_sorted_pins()) {
      for (auto e_drv : e_sink.get_driver_pins()) {
        stk.push_back(e_drv);
      }
    }
  }
  return false;
}

// TRUE when NO output of the callee depends COMBINATIONALLY on any of its
// inputs -- a Moore machine: every output is a pure function of state/consts
// (the DivUnit/SRT16DividerDataModule handshake shape, where `ready` is a
// register read and the fed-back `kill` only affects NEXT state). Conservative:
// anything unbounded (a nested Sub on an output cone, a reached input) -> false.
template <typename SIO>
bool callee_is_moore(const std::shared_ptr<hhds::Graph>& cg, const SIO& sio) {
  namespace gu = livehd::graph_util;
  if (!cg || !sio) {
    return false;
  }
  auto driver_of = [](const hhds::Pin_class& sink) -> hhds::Pin_class {
    if (sink.is_invalid()) {
      return {};
    }
    return sink.get_driver_pin();  // one driver per sink pin; invalid if undriven
  };
  absl::flat_hash_set<hhds::Node_class> seen;
  std::vector<hhds::Pin_class>          stk;
  for (const auto& od : sio->get_output_pin_decls()) {
    auto drv = driver_of(cg->get_output_pin(od.name));
    if (!drv.is_invalid()) {
      stk.push_back(drv);
    }
  }
  while (!stk.empty()) {
    auto d = stk.back();
    stk.pop_back();
    if (d.is_invalid() || d.is_const()) {
      continue;
    }
    if (gu::is_graph_input_pin(d)) {
      return false;  // a combinational in->out path (Mealy)
    }
    auto m  = d.get_master_node();
    auto op = gu::type_op_of(m);
    if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch) {
      continue;  // true state boundary: q is last period's value
    }
    if (op == Ntype_op::Memory) {
      // NOT a blanket boundary: an ASYNC read's dout is COMB in the read
      // ADDRESS (and, under write-forwarding orderings, in the write cones).
      // `rf_q[raddr]` is exactly how dino's register file reaches its read
      // data, and classifying the cell as state called that callee Moore —
      // its outputs were pre-bound from LAST period's addresses, a silent
      // one-period-stale miscompile (bench dino_prog read x2=0 forever).
      // Walk THROUGH the cell: every sink driver joins the cone. That is
      // conservative for a sync read or a read-first ordering (their douts
      // are input-independent), which only under-defers, never miscompiles.
      if (!seen.insert(m).second) {
        continue;
      }
      for (const auto& sink : m.inp_sorted_pins()) {
        const auto drv = sink.get_driver_pin();
        stk.push_back(drv);
      }
      continue;
    }
    if (op == Ntype_op::Sub || op == Ntype_op::IO) {
      return false;  // conservative: a nested sub may be comb-through
    }
    if (!seen.insert(m).second) {
      continue;
    }
    for (const auto& sink : m.inp_sorted_pins()) {
      const auto drv = sink.get_driver_pin();
      stk.push_back(drv);
    }
  }
  return true;
}

// The PER-OUTPUT slice of the Moore check: port-ids of the callee outputs whose
// cone has NO combinational dependence on any callee input (pure functions of
// state/consts). A MEALY callee (rejected whole by callee_is_moore) still has
// state-only outputs -- the CSR/NewCSR::MipModule / DivUnit-SRT16 shape, where
// the fed-back `ready` is a register read but a sibling `echo` output is a comb
// in->out path. Conservative per output: anything unbounded (a nested Sub on
// the cone, a reached IO) disqualifies that output only; an undriven output is
// never included.
template <typename SIO>
absl::flat_hash_set<uint32_t> callee_state_only_outputs(const std::shared_ptr<hhds::Graph>& cg, const SIO& sio) {
  namespace gu = livehd::graph_util;
  absl::flat_hash_set<uint32_t> res;
  if (!cg || !sio) {
    return res;
  }
  auto driver_of = [](const hhds::Pin_class& sink) -> hhds::Pin_class {
    if (sink.is_invalid()) {
      return {};
    }
    return sink.get_driver_pin();  // one driver per sink pin; invalid if undriven
  };
  for (const auto& od : sio->get_output_pin_decls()) {
    auto drv = driver_of(cg->get_output_pin(od.name));
    if (drv.is_invalid()) {
      continue;
    }
    bool                                  state_only = true;
    absl::flat_hash_set<hhds::Node_class> seen;
    std::vector<hhds::Pin_class>          stk{drv};
    while (!stk.empty() && state_only) {
      auto d = stk.back();
      stk.pop_back();
      if (d.is_invalid() || d.is_const()) {
        continue;
      }
      if (gu::is_graph_input_pin(d)) {
        state_only = false;  // a combinational in->out path
        break;
      }
      auto m  = d.get_master_node();
      auto op = gu::type_op_of(m);
      if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch) {
        continue;  // true state boundary: q is last period's value
      }
      if (op == Ntype_op::Memory) {
        // Same rule as callee_is_moore above: an async dout is comb in the
        // read address, so the memory's own input cone joins this output's.
        if (!seen.insert(m).second) {
          continue;
        }
        for (const auto& isnk : m.inp_sorted_pins()) {
          const auto idrv = isnk.get_driver_pin();
          stk.push_back(idrv);
        }
        continue;
      }
      if (op == Ntype_op::Sub || op == Ntype_op::IO) {
        state_only = false;  // conservative: a nested sub may be comb-through
        break;
      }
      if (!seen.insert(m).second) {
        continue;
      }
      for (const auto& isnk : m.inp_sorted_pins()) {
        const auto idrv = isnk.get_driver_pin();
        stk.push_back(idrv);
      }
    }
    if (state_only) {
      res.insert(static_cast<uint32_t>(od.port_id));
    }
  }
  return res;
}

// Ports of `def` that carry a TICK GUARD — the recursive form of the question
// `clock_in_fields` used to answer locally. An input port is a guard port when
// state anywhere in the def's SUBTREE commits on a clock derived from it:
//   (a) a local state element's clock_pin resolves to the port (directly,
//       through width wrappers, or as the ROOT of a local gate cone), or
//   (b) the port forwards — again possibly through a gate — into a CHILD's
//       guard port.
// (b) is what a stateless pass-through wrapper needs: minion's txfma chain
// crosses THREE such boundaries between the gate (vpu_lane's cgate_txfma) and
// the flops it withholds (txfmafrac's stages), and before this went recursive
// the wrapper declared no tick field, the parent's gate had nowhere to write,
// and the leaf committed every tick — silently (tests/sim/gate_through_wrapper
// is the anchor). The shared analysis also drives conditional-activation gate
// insertion and gate-cell recognition, so all three consumers agree.
//
// The cache is a PARAMETER, never a static: latch_contract makes it
// caller-owned because graph preparation inlines gate cells and materializes
// occurrences, so an answer computed before a rewrite must not outlive it. A
// process-lifetime memo keyed by bare `const hhds::Graph*` would also survive
// the scratch library it was built from, and a later emission allocating a
// graph at that freed address would read back the previous design's ports.
const absl::flat_hash_set<uint32_t>& clock_guard_ports(const std::shared_ptr<hhds::Graph>&       def,
                                                       livehd::latch_contract::Clock_port_cache& cache) {
  return livehd::latch_contract::clock_input_ports(def, cache);
}

const livehd::latch_contract::Reset_input_ports& reset_guard_ports(const std::shared_ptr<hhds::Graph>&       def,
                                                                   livehd::latch_contract::Clock_port_cache& cache) {
  return livehd::latch_contract::reset_input_ports(def, cache);
}

}  // namespace

// Name (raw port name) of the INPUT port that clocks module `g`: the port
// feeding a flop's clock_pin, else -- recursively -- the port wired straight
// through to a sub-instance's own clock port (a flopless wrapper still has a
// clock). Empty when no input-driven clock exists (purely combinational, or
// an internally derived clock). Memoized per Cgen_sim (one graph emission);
// the in-progress "" entry terminates a recursive instantiation.
std::string Cgen_sim::clock_input_of(hhds::Graph* g) {
  const auto key = std::string{g->get_name()};
  if (auto it = clk_memo_.find(key); it != clk_memo_.end()) {
    return it->second;
  }
  clk_memo_.emplace(key, "");
  auto gio = g->get_io();
  if (!gio) {
    return "";
  }
  // See through the identity wrappers tolg puts on a typed port read -- a unary
  // Get_mask (width adjust) or Get_mask(v, -1) (to-positive) -- so `clock:u1`
  // wired straight into a sub's clock port still resolves to the input pin.
  auto resolve_passthrough = [](hhds::Pin_class p) {
    for (int hops = 0; hops < 8 && !p.is_invalid(); ++hops) {
      auto n = p.get_master_node();
      if (type_op_of(n) != Ntype_op::Get_mask) {
        break;
      }
      // Indexed, so snapshot the sink pins: e[0]=value, e[1]=optional mask
      // (node_expr's convention). Read-only; Get_mask is never a Sub.
      auto e = n.inp_pins_snapshot();
      if (e.empty()) {
        break;
      }
      if (e.size() >= 2) {  // binary form: identity only for the mask==-1 idiom
        const auto mask_drv = e[1].get_driver_pin();
        if (!mask_drv.is_const()) {
          break;
        }
        const auto& mv = const_of(mask_drv);
        if (!mv.is_just_i64() || mv.to_just_i64() != -1) {
          break;
        }
      }
      p = e[0].get_driver_pin();
    }
    return p;
  };
  std::string found;
  bool        has_flops = false;
  for (auto node : g->body().nodes()) {
    if (!livehd::graph_util::is_type_flop(node)) {
      continue;
    }
    has_flops = true;
    auto d    = resolve_passthrough(get_driver(find_sink_pin(node, "clock_pin")));
    if (d.is_invalid()) {
      continue;
    }
    if (livehd::graph_util::is_graph_input_pin(d)) {
      found = std::string{pin_name_of(d)};
      break;
    }
  }
  // name fallback, mirroring the top-level one: flops whose clock_pin is left
  // implicit still make an input literally named `clock` THE clock port (so a
  // parent wiring its own clock into it resolves through this module too).
  if (found.empty() && has_flops) {
    for (const auto& d : gio->get_input_pin_decls()) {
      if (d.name == "clock") {
        found = "clock";
        break;
      }
    }
  }
  if (found.empty()) {
    // Collect EVERY parent input wired into some sub-instance's clock port, and
    // accept only an unambiguous result: a candidate literally named "clock"
    // wins, else a single distinct candidate. A gated-clock idiom (two nets
    // clocking different subs) stays undetected -- picking whichever instance
    // the traversal visits first would mislabel a poked DATA input as the
    // free-running clock waveform and silently drop its real trace.
    absl::flat_hash_set<std::string> candidates;
    for (auto node : g->body().nodes()) {
      if (!livehd::graph_util::is_type_sub(node)) {
        continue;
      }
      auto sio = node.get_subnode_io();
      auto sg  = node.get_subnode_graph();
      if (!sio || !sg) {
        continue;
      }
      const auto callee_clk = clock_input_of(sg.get());
      if (callee_clk.empty()) {
        continue;
      }
      uint32_t clk_pid  = 0;
      bool     have_pid = false;
      for (const auto& d : sio->get_input_pin_decls()) {
        if (d.name == callee_clk) {
          clk_pid  = static_cast<uint32_t>(d.port_id);
          have_pid = true;
          break;
        }
      }
      if (!have_pid) {
        continue;
      }
      // In-PIN walk, and the driver is fetched only AFTER the pid matches: the
      // node is a Sub, which may be a compact loop whose carry-in sink is the
      // one sanctioned two-driver pin. A clock port is never that pin, so
      // asking only the matched pin keeps get_driver_pin() on legal ground.
      for (const auto& csink : node.inp_sorted_pins()) {
        if (static_cast<uint32_t>(csink.get_port_id()) != clk_pid) {
          continue;
        }
        auto d = resolve_passthrough(csink.get_driver_pin());
        if (!d.is_invalid() && livehd::graph_util::is_graph_input_pin(d)) {
          candidates.insert(std::string{pin_name_of(d)});
        }
        break;
      }
    }
    if (candidates.contains("clock")) {
      found = "clock";
    } else if (candidates.size() == 1) {
      found = *candidates.begin();
    }
  }
  clk_memo_[key] = found;
  return found;
}

// ---- incremental generation digests ----------------------------------------
// The digest covers what the generated C++ depends on for ONE module: IO
// decls, node ops/names/constants, edge topology (via traversal-order node
// indices — stable now that graph construction is deterministic), per-pin
// width/sign, plus the generation-affecting options and a generator version
// (BUMP kSimGenVersion whenever the emitted C++ shape changes).
// 3: per-module <stem>.iface.json manifest + observable outputs/memories (2f-sim B0/B)
// simgen-5: next-state and the commit enable became persistent members (_din,
// _cen) so the commit can be its own method. simgen-4: cycle() gained its
// trailing __settle() and peek() was removed. The
// bump is not cosmetic -- this digest gates a per-module early return that skips
// emission entirely, so a warm workdir would otherwise keep stale settle-only
// .cpp files whose parent calls a peek() that no longer exists (a compile error
// at best; at worst a stale child that commits at the other end of the period).
// simgen-6: unique struct-member names for repeated instance/memory names,
// state-only prebind for any Sub on the schedule cycle, on-demand memory
// emission (emit_memory), fixpoint gate-cell/false-loop inlining pre-steps.
// simgen-7: callee Moore/state-only classification walks THROUGH a Memory
// (async dout is comb in the read address — the dino register-file stale-read
// miscompile); simgen-6 artifacts classified such callees as Moore.
// simgen-8: recursive clock-guard ports — `<field>__tick` is declared for
// every input port whose SUBTREE clocks state on it (stateless wrappers
// included) and FORWARDED at call sites, composing with local gate folds.
// simgen-11: single-use forestation (a fan-out-1 comb node binds as a pasted
// expression, not a `cg_N` temp), 2-arm muxes as a lazy C++ ternary, guarded
// next-state and lazy write staging on gated state, and the Get_mask raw
// width-adjust pass-through.
// simgen-15: native rolled-loop ABI. The digest includes the complete HHDS
// Subnode_loop descriptor so a warm workdir cannot reuse code for a different
// count/domain/role mapping.
// simgen-16: dead-temporary sweep — a local no statement in the method reads is
// dropped (with its now-dead operands) instead of emitted; a dead call keeps
// its side effect as a discarded statement.
// simgen-17: conditional Sub calls execute behind their structural __valid OR
// reset guard, so an inactive hierarchy costs one branch instead of a full
// evaluation whose clock commits are merely disabled at the end.
// simgen-18: activation-bearing Subnode_loop descriptors stay native. Their
// std::array wrapper carries the active recurrence and emits a runtime if per
// ordinal instead of materializing one physical Sub per source iteration.
// simgen-19: a callee's forwarded top-level __valid is already enforced at its
// activation boundary. Do not redundantly wrap every internal child/group in
// that same guard (which can also hide recursively scheduled declarations in a
// nested C++ scope); only a locally composed path guard creates a runtime if.
// simgen-24: after the ordinary period settle has propagated post-rise inputs
// down the hierarchy, refresh mixed-phase children's negedge state for the
// next period. This preserves the established rise schedule while preventing
// a nested negedge preview from retaining the prior cycle's input. Preserve a
// Hotmux's control widths independently of its result width. Let
// reset_cycle(bool) supply zero for otherwise-uninitialized state (sim.init_zero).
// simgen-57: hierarchical value mirrors are compile-time instrumentation.
// A normal non-VCD/non-probe/non-query build has no observation boundary
// slots, publication assignments, runtime observation branches, VCD storage,
// or VCD tick updates.
// simgen-61: associative bitwise nodes use bounded-depth folds and oversized
// Hotmux nodes emit a lazy switch, keeping generated C++ parser/template depth
// logarithmic or constant for very wide decode cones.
// simgen-62: the opt-in LLVM backend emits every supported canonical color
// kernel, including classes with one occurrence, as a native object plus a
// narrow packed-word ABI adapter.
// simgen-65: sim.tune (sim_profile.md P0/P1). The tune vector (dirty / fence /
// live_words / backend) is folded into the COLOR ROOT's key only, as its
// canonical tv1 text; the emitter-debug env switches are keyed; every module
// gains the cold `__tune_hash` / `__tune_count` state walkers and the shared
// canonical-hash helper; an executable root gains `<stem>.tune-id.cpp`, the
// `__tune_support()` / `__tune_sources()` declarations and `<stem>.tune.cpp`.
// The `?` helper became the shared V2 text (run-time zero fill, draw counter).
// simgen-64: a literal wider than 2048 bits goes through the shared
// __lhd_unknown_literal<W,K> helper instead of a `constexpr` local. Past that
// width the digit-by-digit from_pyrope parse exceeds clang's -fconstexpr-steps
// budget and the generated C++ fails to compile outright (measured: a
// Slop<10922> in a XiangShan `Rob` color-eval shard). The helper is keyed on
// (width, spelling), so it stays one parse per program however many sites emit
// it.
// simgen-63: gen_digests.json records the COMPLETE artifact set per module
// alongside a HIERARCHICAL key, so the occurrence-wide color root — the most
// expensive module in the tree, and the only one that was excluded — reuses
// its generation like every other module. The bump is load-bearing twice
// over: the on-disk record shape changed (a bare digest string became
// {"d","f"}), and the key changed meaning (a child edit now moves every
// ancestor's key, which is what makes the root reusable at all).
// simgen-60: generated unsigned proof landings default to mask-free
// Slop_u::from_proven; sim.debug retains materializing Slop_u::land checks.
// The color planner uses word-oriented structural refinement hashes.
// simgen-59: generated color simulation is serial and no longer emits or stages
// Taskflow; large color evaluators are split into bounded translation units.
// simgen-61: an unknown (`?`) literal bit is drawn from the run's seeded PRNG
// (once per literal, behind a `static const`) unless sim.unknown_zero forces the
// old deterministic zero. Drawing ONCE is what keeps simgen-58's color
// classification true: the value is still constant across periods, so such a
// color is not per-period random work.
// simgen-58: unknown literal bits are deterministic zero in simulation, so
// they no longer make their colors run every period as runtime-random work.
// simgen-53: verified canonical bodies are digest-named translation units with
// stale-file pruning; the root keeps only occurrence binding/schedule code.
// simgen-52: canonical color calls use persistent type-erased binding tables,
// so one shared body serves every verified occurrence without a case per call.
// simgen-51: direct-color runtime metadata uses compact indexed arrays and
// verified repeated pure-data colors call one canonical typed kernel body.
// simgen-50: discovery structural seeds hash canonical typed fields directly;
// large occurrence plans no longer allocate formatted node descriptions.
// simgen-46: the color runtime is a source-only implementation detail and one
// indexed dispatcher replaces plan-sized method declarations in the module ABI.
// simgen-45: direct clock eligibility resolves identity/width shaping through
// the hierarchy-aware structural control root; checkpoint design hashes bind
// the color schedule, boundary ABI, scheduler runtime, and generator version.
// simgen-44: occurrence-bound hierarchy state joins the direct color path and
// every color skips on unchanged input/state slot generations.
// simgen-43: flat ordinary-register roots execute the plan's real per-color
// kernels over producer-owned ABI slots in a reusable color DAG.
// simgen-42: every generated module exposes the uniform checkpoint quiesce
// hook; a parallel root drops its runtime so worker threads are joined before
// the driver forks a checkpoint child.
// simgen-41: a fully legal staged hierarchy root owns a lazily constructed,
// reusable parallel phase schedule; its digest also includes the exact color
// plan so a warm workdir cannot retain a stale root ABI.
// simgen-40: uninitialized reset-free memories power on zero under both
// policies, and a posclk=false flop on a sole explicit clock net whose
// spelling is not clock-like (Design_clocks::name_looks_like_clock) is
// edge-detected via sec_clock instead of committing once per period.
// simgen-39: group and state-boundary snapshots copy only demanded Slop fields,
// never an entire child Out struct; cyclic children retain their full boundary
// settle while acyclic children defer it to the root walk.
// simgen-35: cycle() returns persistent __last_out by const reference; acyclic
// child calls skip their private trailing settle because the root post-edge
// walk will visit them. Cyclic boundaries retain it to publish post-commit
// state-only outputs that break the parent's settle ring.
// simgen-32: direct constant Set_mask splices also cover multi-word results.
// simgen-26: constant one-word Set_mask cells emit direct scalar splices.
// simgen-25: the negedge refresh is STRUCTURAL (a callee whose only rise-half
// state is a latch) rather than a whitelist of four port names, so every child
// the latch classification moves out of `negedge_only` gets the compensating
// refresh; a compact loop never propagates it (the wrapper declares no
// refresh_negedge); a negedge-only child advanced in the parent's fall reads
// its outputs from `__out`, not from a `cycle()` snapshot it never took; and an
// unknown memory power-on fills per ENTRY instead of through one whole-array
// Slop. The bump is not cosmetic: a warm workdir would otherwise pair a fresh
// parent with a stale child that declares no `refresh_negedge()`.
static constexpr std::string_view kSimGenVersion = "simgen-65";

static inline uint64_t fnv1a(uint64_t h, uint64_t v) { return livehd::hash_util::fnv1a64_u64(v, h); }
static inline uint64_t fnv1a_str(uint64_t h, std::string_view s) {
  return livehd::hash_util::fnv1a64_u64(s.size(), livehd::hash_util::fnv1a64(s, h));
}

uint64_t Cgen_sim::sim_graph_digest(hhds::Graph* g) {
  namespace gu                                       = livehd::graph_util;
  uint64_t                                         h = livehd::hash_util::kFnv1a64_offset;
  absl::flat_hash_map<hhds::Class_index, uint32_t> seq;
  uint32_t                                         ni = 0;
  for (auto n : g->body().nodes()) {
    seq[n.get_class_index()] = ni++;
  }
  auto gio = g->get_io();
  // `unsign` is TYPE-AFFECTING for a port: the emitted field is
  // value_type(bits, unsign), i.e. `Slop_u<n>` vs `Slop<n>`. Under the literal
  // width contract a signed and an unsigned port of the same size carry the
  // SAME `bits`, so without folding the sign a signedness-only edit produced a
  // byte-identical digest and the stale header was reused -- declaring a field
  // the regenerated parent then reads at the other type. (The per-node fold
  // below only reaches DRIVER pins; a graph output is a sink and contributes
  // nothing there.) The low bit stays the input/output discriminator.
  for (const auto& d : gio->get_input_pin_decls()) {
    h = fnv1a_str(h, d.name);
    h = fnv1a(h, static_cast<uint64_t>(d.port_id));
    h = fnv1a(h, static_cast<uint64_t>(d.bits) * 4 + (d.unsign ? 2 : 0) + 1);
  }
  for (const auto& d : gio->get_output_pin_decls()) {
    h = fnv1a_str(h, d.name);
    h = fnv1a(h, static_cast<uint64_t>(d.port_id));
    h = fnv1a(h, static_cast<uint64_t>(d.bits) * 4 + (d.unsign ? 2 : 0));
  }
  for (auto n : g->body().nodes()) {
    auto op = gu::type_op_of(n);
    h       = fnv1a(h, static_cast<uint64_t>(op));
    if (gu::has_name(n)) {
      h = fnv1a_str(h, gu::node_name_of(n));
    }
    if (op == Ntype_op::Sub) {
      auto cg = n.get_subnode_graph();
      h       = fnv1a_str(h, cg ? cg->get_name() : std::string_view{});
      if (auto loop = n.subnode_loop()) {
        h                            = fnv1a(h, 0x4c4f4f50ULL);  // "LOOP": distinguish descriptor absence
        h                            = fnv1a(h, static_cast<uint64_t>(loop->first));
        h                            = fnv1a(h, static_cast<uint64_t>(loop->step));
        h                            = fnv1a(h, loop->count);
        const auto fold_optional_pid = [&](uint64_t hh, const std::optional<hhds::Port_id>& pid) {
          hh = fnv1a(hh, pid ? 1u : 0u);
          return pid ? fnv1a(hh, static_cast<uint64_t>(*pid)) : hh;
        };
        h = fold_optional_pid(h, loop->index_input);
        h = fold_optional_pid(h, loop->activation_input);
        h = fold_optional_pid(h, loop->next_active_output);
      } else {
        h = fnv1a(h, 0);
      }
    }
    // This is a workdir CACHE KEY, so both walks must fold one term per EDGE.
    // The in-walk gets that from the PLURAL get_driver_pins(): the singular
    // get_driver_pin() would drop the second driver of a compact loop's
    // carry-in sink (pass/legalize's one sanctioned exception). The out-walk
    // STAYS ON out_edges(), because an out-PIN form would stop encoding FANOUT
    // -- one driver pin can carry many sinks. Either weakening is a false hit
    // on a warm workdir, i.e. stale generated C++.
    for (auto e_sink : n.inp_sorted_pins()) {
      for (auto e_drv : e_sink.get_driver_pins()) {
        h = fnv1a(h, static_cast<uint64_t>(e_sink.get_port_id()));
        if (e_drv.is_const()) {
          // A constant operand is its VALUE, not its pool slot: slots are handed
          // out in creation order, so `x + 1` and `x + 2` would otherwise digest
          // the same and a warm workdir would be reused with a stale constant.
          h = fnv1a(h, 0x434f4e5354ULL);  // "CONST"
          h = fnv1a_str(h, const_of(e_drv).serialize());
          continue;
        }
        h = fnv1a(h, seq[e_drv.get_master_node().get_class_index()]);
        h = fnv1a(h, static_cast<uint64_t>(e_drv.get_port_id()));
      }
    }
    for (auto e : n.out_edges()) {  // lazy view: iterate only, never snapshot
      h = fnv1a(h, static_cast<uint64_t>(e.driver.get_port_id()));
      h = fnv1a(h, static_cast<uint64_t>(wbits_of(e.driver)) * 2 + (is_unsign(e.driver) ? 1 : 0));
    }
  }
  return h;
}

// A module's key over its OWN body plus the bodies of everything it
// instantiates, transitively. `sim_graph_digest` folds only a child's NAME, so
// on its own it cannot see a leaf edit -- and the code emitted for an ancestor
// does depend on the descendant subtree (clock-guard forwarding at call sites,
// Moore/state-only classification through a Memory, and for the color root the
// entire occurrence plan). Folding the children turns the per-module digest
// into a merkle hash, which is the same discipline semdiff's canonical digest
// uses and the reason the color root can be reused at all.
//
// Memoized per Gid: a diamond hierarchy would otherwise re-walk a shared leaf
// once per path, and a cyclic instance graph (which HHDS does not allow, but a
// malformed library could present) would not terminate.
uint64_t Cgen_sim::hier_graph_digest(hhds::Graph* g) {
  if (g == nullptr) {
    return 0;
  }
  auto&      memo = shared_digest_memo_ != nullptr ? *shared_digest_memo_ : hier_digest_memo_;
  const auto gid  = g->get_gid();
  if (auto it = memo.find(gid); it != memo.end()) {
    return it->second;
  }
  memo[gid] = 0;  // cycle guard: a re-entry folds 0 rather than recursing

  uint64_t                      h = sim_graph_digest(g);
  // Child bodies form a SET: repeated instances do not contribute again.
  // The parent's own body hash already carries the instantiation structure.
  absl::flat_hash_set<uint64_t> children;
  for (auto n : g->body().nodes()) {
    if (livehd::graph_util::type_op_of(n) != Ntype_op::Sub) {
      continue;
    }
    if (auto cg = n.get_subnode_graph()) {
      children.insert(hier_graph_digest(cg.get()));
    }
  }
  hhds::Field_combiner child_hash;
  for (auto c : children) {
    child_hash.add(c);
  }
  h         = fnv1a(h, child_hash.value());
  memo[gid] = h;
  return h;
}

// The `"gen"` tag: the human-facing generator version AND the emitter content
// salt. Carrying the salt here as well as in each key means an emitter edit
// drops the whole index in one step, instead of leaving every record to
// mismatch individually — which would strand a record for a module that no
// longer exists in the design.
static std::string gen_schema_tag() { return absl::StrCat(kSimGenVersion, "-", absl::Hex(livehd::kCgenSrcSalt, absl::kZeroPad16)); }

// {"gen":"simgen-63-<salt>","modules":{"file.entity":{"d":"0123456789abcdef",
//                                                     "f":["file.entity.cpp",...]}}}
//
// Hand-rolled, like the abc and formal caches: the shapes are fixed and adding
// a JSON dependency to the emitter is not worth it. Anything that does not
// parse is treated as a cold start, never as a partial record.
void Cgen_sim::load_gen_digests() {
  gen_digests_loaded_ = true;
  std::ifstream ifs(absl::StrCat(std::string(odir), "/gen_digests.json"));
  if (!ifs) {
    return;
  }
  std::stringstream ss;
  ss << ifs.rdbuf();
  const std::string t = ss.str();
  if (t.find(absl::StrCat("\"gen\":\"", gen_schema_tag(), "\"")) == std::string::npos) {
    return;  // other generator version or a changed emitter -> cold
  }
  size_t p = t.find("\"modules\"");
  if (p == std::string::npos) {
    return;
  }
  p = t.find('{', p);
  if (p == std::string::npos) {
    return;
  }
  ++p;
  const auto quoted = [&](size_t& at, std::string& out) {
    size_t q0 = t.find('"', at);
    if (q0 == std::string::npos) {
      return false;
    }
    size_t q1 = t.find('"', q0 + 1);
    if (q1 == std::string::npos) {
      return false;
    }
    out = t.substr(q0 + 1, q1 - q0 - 1);
    at  = q1 + 1;
    return true;
  };
  while (true) {
    std::string module;
    if (!quoted(p, module)) {
      break;
    }
    size_t rec = t.find('{', p);  // the per-module record
    if (rec == std::string::npos) {
      break;
    }
    size_t end = t.find('}', rec);
    if (end == std::string::npos) {
      break;
    }
    Gen_record record;
    size_t     dp = t.find("\"d\":", rec);
    if (dp == std::string::npos || dp > end || !(dp += 4, quoted(dp, record.digest))) {
      break;
    }
    size_t fp = t.find("\"f\":[", rec);
    if (fp != std::string::npos && fp < end) {
      fp                    += 5;
      const size_t list_end  = t.find(']', fp);
      std::string  name;
      while (list_end != std::string::npos && fp < list_end && quoted(fp, name)) {
        record.files.push_back(name);
      }
    }
    // A record with no artifact list is from an older schema (or truncated):
    // drop it rather than let an empty list read as "nothing to check".
    if (!record.files.empty()) {
      gen_digests_[module] = std::move(record);
    }
    p = end + 1;
  }
}

void Cgen_sim::save_gen_digests() {
  std::vector<std::string> keys;
  keys.reserve(gen_digests_.size());
  for (const auto& [k, _] : gen_digests_) {
    keys.push_back(k);
  }
  std::sort(keys.begin(), keys.end());  // stable file bytes
  std::string text  = absl::StrCat("{\"gen\":\"", gen_schema_tag(), "\",\"modules\":{");
  bool        first = true;
  for (const auto& k : keys) {
    const auto& record = gen_digests_.at(k);
    absl::StrAppend(&text, first ? "" : ",", "\"", k, "\":{\"d\":\"", record.digest, "\",\"f\":[");
    for (size_t i = 0; i < record.files.size(); ++i) {
      absl::StrAppend(&text, i ? ",\"" : "\"", record.files[i], "\"");
    }
    absl::StrAppend(&text, "]}");
    first = false;
  }
  absl::StrAppend(&text, "}}\n");
  // Through File_output like every other generated artifact: an unchanged
  // warm run then leaves the whole sim tree, this index included, untouched.
  File_output out{absl::StrCat(std::string(odir), "/gen_digests.json")};
  out.append(text);
}

std::string Cgen_sim::generation_key(hhds::Graph* g, bool color_root) {
  const std::string gname(g->get_name());
  const auto        entity = gname.substr(gname.rfind('.') + 1);
  const bool        is_top = top.empty() || entity == top || gname == top;

  uint64_t gd = hier_graph_digest(g);
  // The build-time content hash of every inou/cgen source. This, not the
  // hand-bumped kSimGenVersion below, is what makes a forgotten version bump
  // harmless: change the emitter and every cached generation invalidates on the
  // next run, with no human in the loop (docs/opt_loop_incr.md L2 / trap T4).
  gd          = fnv1a(gd, livehd::kCgenSrcSalt);
  gd          = fnv1a_str(gd, kSimGenVersion);
  gd          = fnv1a_str(gd, vcd_file);
  gd          = fnv1a_str(gd, top);
  gd          = fnv1a(gd, (is_top ? 2u : 0u) | (vcd_fakedelay ? 1u : 0u));
  gd          = fnv1a(gd, compact_kernel_ ? 1u : 0u);
  gd          = fnv1a(gd, runtime_support_on ? 1u : 0u);
  gd          = fnv1a(gd, slop_u_ ? 1u : 0u);
  gd          = fnv1a(gd, debug_ ? 1u : 0u);
  gd          = fnv1a(gd, unknown_zero_ ? 1u : 0u);
  // Conservative: every emission read of the backend sits inside a color root,
  // and the root set it changes is already keyed by the color_root bit below,
  // but a backend flip regenerating everything is cheap insurance.
  gd          = fnv1a(gd, llvm_backend_ ? 1u : 0u);
  gd          = fnv1a(gd, env_.bits());
  // `dut_` is NOT implied by `is_top`: with an empty --top every module reports
  // is_top, while dut_ additionally requires the module not to be instantiated.
  // It selects version-gated `__in` forwarding and the `__color_port_ver_*`
  // boundary refresh, so a body generated for one must never be reused as the
  // other (a dut_=false body reused as the DUT ignores every driver poke).
  gd          = fnv1a(gd, (color_root ? 2u : 0u) | (observation_on ? 1u : 0u) | (dut_ ? 4u : 0u));
  // The sim.tune vector, ROOT ONLY (see tune_dirty()): every knob in it reaches
  // the code through the root's plan or the root's emission, so folding it into
  // a non-root key would regenerate (and recompile) the whole tree on a tune
  // flip for byte-identical output. The canonical text also makes spellings
  // that build the same plan (live_words 0 vs 256, fence -1 vs 16) key alike.
  if (color_root) {
    gd = fnv1a_str(gd, tune_vector_);
  }
  char hex[17];
  std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(gd));
  return hex;
}

Cgen_sim::Sim_env Cgen_sim::Sim_env::read() {
  Sim_env env;
  env.nogate   = ::getenv("LIVEHD_SIM_NOGATE") != nullptr;
  env.nolazy   = ::getenv("LIVEHD_SIM_NOLAZY") != nullptr;
  env.noinline = ::getenv("LIVEHD_SIM_NOINLINE") != nullptr;
  return env;
}

bool Cgen_sim::tune_dirty() const {
  // An explicit check, not I(): iassert compiles away under NDEBUG, and this
  // guards cache soundness in the build users run. The error is also what
  // keeps the module's Gen_record from being written (fail closed).
  if (!emitting_color_root_ && !tune_misuse_reported_) [[unlikely]] {
    tune_misuse_reported_ = true;
    livehd::diag::err("inou.cgen.sim", "tune-knob-outside-root", "internal")
        .msg("sim.tune.dirty was read while emitting a module that is not a color root")
        .hint("the tune vector is folded into the color root's generation key only; a non-root read would be served stale")
        .emit();
  }
  return tune_.dirty;
}

bool Cgen_sim::generation_current(hhds::Graph* g, bool color_root) {
  if (odir.empty()) {
    return false;
  }
  if (!gen_digests_loaded_) {
    load_gen_digests();
  }
  const std::string gname(g->get_name());
  gen_key_ = generation_key(g, color_root);

  auto it = gen_digests_.find(gname);
  if (it != gen_digests_.end() && it->second.digest == gen_key_) {
    std::error_code ec;
    const bool      complete = std::ranges::all_of(it->second.files, [&](const std::string& f) {
      return std::filesystem::exists(absl::StrCat(odir, "/", f), ec);
    });
    if (complete) {
      return true;
    }
    // The key matched but an artifact is gone (a hand-deleted file, a
    // half-swept directory). Retract the record on disk BEFORE re-emitting: if
    // this emission then dies partway, a run after it must not find a matching
    // key over a tree we were in the middle of rewriting. This is the only
    // repair path that needs an extra index write, and it is rare — the
    // ordinary miss leaves a record whose key already differs from the one the
    // next run computes, which is self-invalidating.
    gen_digests_.erase(gname);
    save_gen_digests();
  }
  gen_digests_.erase(gname);  // re-recorded only after a clean emission
  return false;
}

std::shared_ptr<File_output> Cgen_sim::open_out(std::string_view basename) {
  note_emitted(basename);
  return std::make_shared<File_output>(odir.empty() ? std::string(basename) : absl::StrCat(odir, "/", basename));
}

void Cgen_sim::note_emitted(std::string_view basename) { emitted_files_.emplace_back(basename); }

// ICG FOLD (todo/livehd/2f-latch M5). A clock-gating cell's output is
// `<clock> & <enable>`: it has a rising edge exactly on the reference clock's
// rising edges where the enable is high. Since one sim tick IS one reference
// period, that folds to "commit this tick iff every non-clock operand of the
// gate is true" — no edge detection, no clock net in the scheduler. This is the
// same abstraction LEC uses (CIRCT's arc.state has a native `enable` operand
// for exactly this); nobody proves ICGs in industry practice, they normalize
// them away.
//
// Returns the guard operand pins, or EMPTY when the cone is not a foldable ICG.
// Empty is "cannot fold", never "no guard needed": the caller must then ask
// `plain_clock_cone` whether the cone was a gate at all, and REFUSE if it was,
// rather than silently commit every tick.
//
// todo_sim_pipeline.md step 1d: this used to be a PRIVATE ICG matcher — the
// FOURTH in the tree (latch_contract.hpp names the other three) — and it cost
// exactly what a duplicated shared answer costs: it hardcoded `pn == "clk"` and
// missed `clk_i` on 7 flops, and it matched only ONE `And` level, so a gate on
// an already gated clock came out with no clock operand at all and the whole
// chain was refused. It is now a thin wrapper over `clock_op_of`, the ONE
// recognizer LEC and the phase schedule already use, plus the two policy checks
// that are genuinely sim's own (below).
std::vector<hhds::Pin_class> Cgen_sim::icg_guards(const hhds::Pin_class& clock_driver, std::string_view clock_port,
                                                  const livehd::latch_contract::Design_clocks* clocks, bool* fall_commit) {
  if (clocks == nullptr || clock_driver.is_invalid()) {
    return {};  // no clock_pin: the module's implicit clock, never gated
  }
  auto cone = livehd::latch_contract::clock_op_of(clock_driver, *clocks);
  if (!cone || cone->enables.empty()) {
    return {};
  }
  // SIM POLICY 2 — a DIVIDER is not the reference clock. One tick is one
  // reference period, so a div-by-N gate does not fold into a per-tick guard
  // until the divider's initial phase is modelled (the named refusal
  // latch_contract.hpp documents for `div != 1`).
  if (cone->div != 1) {
    return {};
  }
  // SIM POLICY 1 — an INVERTED reference (`~clk & en`) gates the FALLING edge.
  // A caller with no fall sub-tick (null `fall_commit`) keeps the refusal: the
  // rise fold would move the commit half a period with no diagnostic. A FLOP
  // endpoint opts in and commits these guards in the FALL pass instead. The
  // cone's clock may itself be a GATED net (`~(clk & e1) & e2` resolves to
  // clock=<the inner gate's output>): the fall edge of that net only exists
  // in a period where the inner gate pulsed, so absorb every inner cone's
  // enables down to the root clock — each enable is held by its cell's own
  // enable latch, so evaluating ALL of them in the fall pass reads each at
  // the phase its latch already pinned (the step-1 per-cell guard records,
  // folded here at the endpoint).
  if (cone->clock_inverted) {
    if (fall_commit == nullptr) {
      return {};
    }
    auto            guards  = cone->enables;
    hhds::Pin_class clk_net = cone->clock;
    for (int hops = 0; hops < 4 && !clk_net.is_invalid() && !clk_net.is_const() && !livehd::graph_util::is_graph_input_pin(clk_net);
         ++hops) {
      auto inner = livehd::latch_contract::clock_op_of(clk_net, *clocks);
      if (!inner || inner->div != 1) {
        break;
      }
      if (inner->clock_inverted) {
        return {};  // double inversion: not a shape the two sub-ticks model
      }
      guards.insert(guards.end(), inner->enables.begin(), inner->enables.end());
      clk_net = inner->clock;
    }
    *fall_commit = true;
    return guards;
  }
  // WHICH clock domain this gate belongs to is deliberately NOT re-asked here.
  // `resolve_icg` already identified the cone's clock through `Design_clocks`
  // (a net some flop clocks on, else a conventionally named input), and adding
  // an exact `pin_name_of(clock) == clock_port` test on top of that measured
  // WORSE on minion, not better: it refused cones the shared analysis had
  // correctly resolved. A design with two clock INPUTS whose gate rides the
  // non-reference one is still folded into this tick — a real hole, and the
  // reason step 1a puts the resolved endpoint (root + edge + guards) on one
  // record instead of leaving every consumer to re-derive the domain.
  (void)clock_port;
  return cone->enables;
}

// Is `clock_driver` a PLAIN clock net wearing an identity wrapper — `clk & 1`
// (the slang reader's 1-bit width mask), `clk == 1`, a Get_mask/Sext width
// adjust — rather than a gate? Such a cone needs no guard AND no refusal: it
// ticks exactly when the reference clock does.
//
// The distinction is load-bearing. `resolve_icg` skips constant operands (a
// constant is a mask, not an enable) and then refuses a cone with no enables
// left, so `clk & 1` comes back as "not a foldable ICG" — indistinguishable, to
// a caller that only looks at the guard list, from a genuinely underivable
// clock. Reading that as a refusal cost two modules on minion (intpipe_csr_file,
// prim_mul_div) whose clock cones are masks, not gates.
bool Cgen_sim::plain_clock_cone(const hhds::Pin_class& clock_driver, const livehd::latch_contract::Design_clocks& clocks) {
  if (clock_driver.is_invalid()) {
    return false;
  }
  // A cone the shared recognizer DOES decode as a gate is never "plain", even
  // when this emitter declined to fold it (an inverted reference, a divider).
  // Skipping this test turns every unfoldable gate into a silent commit-every-
  // tick — the exact miscompile the refusal exists to prevent.
  if (livehd::latch_contract::clock_op_of(clock_driver, clocks)) {
    return false;
  }
  // STRICT IDENTITY PEEL — deliberately NOT control_root(). That helper
  // resolves PHASE, not value: it walks a `cond ? clk : 0` mux to its
  // SELECTOR and an `x == 0` to x. A mux-style clock gate therefore peels to
  // its ENABLE, and `is_clock` answers true about the enable because
  // `Design_clocks` seeds its roots with `resolve_phase(clock_pin)` of every
  // flop — including this one. The cone then reads "an identity wrapper, not a
  // gate", the flop is emitted with no commit guard, and the gate is dead
  // code. Only VALUE-PRESERVING wrappers may be peeled here.
  hhds::Pin_class p     = clock_driver;
  bool            moved = false;
  for (int hops = 0; hops < 16 && !p.is_invalid(); ++hops) {
    if (livehd::graph_util::is_graph_input_pin(p) || p.is_const()) {
      break;
    }
    auto       n  = p.get_master_node();
    const auto op = type_op_of(n);
    // Snapshot the sink pins (indexed below: e[0] value, e[1] mask/constant).
    // Taken before the op test, so it must not ask for a DRIVER here -- the
    // arms below do that, and only on Get_mask / And / EQ, none of which is a
    // compact-loop Sub.
    auto       e  = n.inp_pins_snapshot();
    if (op == Ntype_op::Get_mask && !e.empty()) {
      if (e.size() >= 2) {
        if (!e[1].get_driver_pin().is_const()) {
          break;
        }
        const auto& mv = const_of(e[1].get_driver_pin());
        if (!mv.is_just_i64() || mv.to_just_i64() != -1) {
          break;  // only the to-positive `mask == -1` idiom is an identity
        }
      }
      p     = e[0].get_driver_pin();  // unary width adjust, or Get_mask(v, -1)
      moved = true;
      continue;
    }
    if ((op == Ntype_op::And || op == Ntype_op::EQ) && e.size() == 2) {
      const int ci = e[1].get_driver_pin().is_const() ? 1 : (e[0].get_driver_pin().is_const() ? 0 : -1);
      if (ci < 0) {
        break;  // both operands real: an ICG cone (`clk & en`), never an identity
      }
      const auto& cv = const_of(e[ci].get_driver_pin());
      if (op == Ntype_op::And) {
        // `x & <low mask>`: the width mask the slang reader puts on a boolean
        // control. Any other constant trims real bits.
        if (cv.has_unknowns() || cv.is_negative()) {
          break;
        }
        auto [mb, me] = cv.get_mask_range();
        if (mb != 0 || me <= 0) {
          break;
        }
      } else if (!cv.is_just_i64() || cv.to_just_i64() != 1) {
        break;  // `x == 1` is the identity; `x == 0` is a NEGATION (a negedge domain)
      }
      p     = e[1 - ci].get_driver_pin();
      moved = true;
      continue;
    }
    break;
  }
  // The walk must have actually PEELED something, and must land on a module
  // INPUT. Anything else — a Sub output, a flop Q, a memory dout — is a
  // DERIVED clock, and `is_clock` would answer about it only because the state
  // hanging off this very cone registered it as a root: a self-answer.
  if (!moved || p.is_invalid() || !livehd::graph_util::is_graph_input_pin(p)) {
    return false;
  }
  return clocks.is_clock(p);
}

// Node count of one def's body. Memoized: a def is reached once per
// instantiation site, and counting is a full traversal.
int Cgen_sim::graph_node_count(hhds::Graph* g) {
  if (g == nullptr) {
    return 0;
  }
  if (auto it = node_count_memo_.find(g); it != node_count_memo_.end()) {
    return it->second;
  }
  int n = 0;
  for ([[maybe_unused]] auto node : g->body().nodes()) {
    ++n;
  }
  node_count_memo_.emplace(g, n);
  return n;
}

// Every STRUCTURAL rewrite the emitter makes to a body, factored out of
// do_from_graph so the caller can run it over the WHOLE library first.
//
// Run once per graph, before occurrence planning retains any graph handles.
bool Cgen_sim::prepare_graph(const std::shared_ptr<hhds::Graph>& graph) {
  hhds::Graph* g      = graph.get();
  const auto   gname  = std::string{graph->get_name()};
  const auto   entity = gname.substr(gname.rfind('.') + 1);
  if (!entity.empty() && entity.front() == '%') {
    return true;  // a `test` block: never emitted, so never rewritten (see do_from_graph)
  }
  // EXACTLY ONCE per body. The rewrites below are not idempotent — realizing an
  // occurrence or folding a gate cell exposes new nodes a second pass would act
  // on again — so re-running after the partition pre-scan would re-open the
  // staleness this split exists to close. Process-wide (one `lhd`
  // invocation): the emitter builds a fresh Cgen_sim per graph. The map holds
  // a STRONG reference so a freed graph's address can never be reused by a
  // later one and read back as "already prepared".
  // The bool is whether preparation SUCCEEDED: a second call must report the
  // first one's failure rather than answering "already prepared, all good".
  static absl::flat_hash_map<const hhds::Graph*, std::pair<std::shared_ptr<hhds::Graph>, bool>> prepared;
  if (auto [pit, fresh] = prepared.try_emplace(g, graph, false); !fresh) {
    return pit->second.second;
  }
  // Bring an instantiated clock-gate cell's gate INTO this body — the same
  // pre-step pass.single_edge and the formal commands run.
  //
  // An ICG arrives as a Sub (`prim_clk_gate`: a low-transparent latch capturing
  // `en_i`, then `clk_o = clk_i & en_latch`), and while it stays a Sub it breaks
  // sim codegen twice over. Its output is a clock this module's flops carry as
  // `clock_pin`, which icg_guards() cannot fold because that matcher walks an
  // AND cone in the LOCAL body and the gate is behind a call — so every such
  // flop is refused as "a derived clock inou.cgen.sim cannot fold into a commit
  // guard". And because a Sub is simulated atomically (all inputs -> all
  // outputs), the perfectly ordinary feedback `clk_o -> flops -> en_i -> clk_o`
  // reads as a combinational loop THROUGH the instance. Inlining the gate turns
  // both into the local AND cone icg_guards already folds into a flop enable,
  // which is the same treatment lec gives it.
  // A parent-level feedback ring still takes the shared expansion fallback.
  // Activation/break descriptors remain compact: the native wrapper below
  // carries the cumulative active bit, bypasses carries for inactive lanes,
  // and skips their calls at runtime (while reset keeps the call open).
  // Snapshot in reverse storage order for the occurrence formatter's
  // module-scoped ordinal accounting.
  std::vector<hhds::Node_class> fallback_loops;
  for (auto n : g->body().nodes()) {
    if (auto d = n.subnode_loop(); d && compact_loop_has_external_ring(n)) {
      fallback_loops.push_back(n);
    }
  }
  for (auto it = fallback_loops.rbegin(); it != fallback_loops.rend(); ++it) {
    if (!livehd::graph_util::materialize_occurrence(g, *it, "inou.cgen.sim")) {
      return false;
    }
  }
  // No false-loop inlining: the occurrence-wide color plan resolves hierarchy
  // crossings without cloning. The old inliners multiplied cloned subtrees up
  // the hierarchy (77 MB minion_top.cpp, hour-plus host-clang). RULING: a cgen
  // pass never flattens -- `sim.flatten` was deleted outright, not defaulted
  // off. The only structural inlining left is the CLOCK-GATE CELL fold.
  //
  // WHY THE FOLD IS STILL A REWRITE, and what it will take to retire it.
  // `clock_op_of` now decodes an INSTANTIATED gate read-only and re-roots its
  // enable onto the parent (graph/latch_contract.cpp `sub_icg_cone`), which is
  // enough for a STATE-FREE gate wrapper -- tests/sim/hier_gate_port folds from
  // that path alone, a case inlining never fixed. It is NOT enough for a real
  // ICG, because the cone abstracts the enable LATCH away and with it the
  // SAMPLE POINT: inlined, the emitter sees a Latch and reads the enable
  // pre-edge; from a bare cone it reads `en` at the edge, one cycle late.
  // MEASURED on tests/sim/gate_exposed_by_false_loop_inline: silently wrong
  // values (bad_cnt=4 at clock=5), not a refusal. Retiring this rewrite needs
  // the cone to CARRY the sample point to the flop emitter -- the same contract
  // a materialized `Clock_cell` asserts ("en is sampled at clk_ref's active
  // edge") -- not just the enable pin. Gate cells can sit behind gate cells, so
  // iterate until quiet.
  int inlined_gate_cells = 0;
  for (int round = 0; round < 8; ++round) {
    const int ncg = livehd::latch_contract::inline_clock_gate_cells(g, "inou.cgen.sim");
    if (::getenv("LIVEHD_SIM_CLK_DEBUG") != nullptr) {
      std::fprintf(stderr, "[icgdbg] %s: gate-cell round %d inlined %d\n", gname.c_str(), round, ncg);
    }
    if (ncg == 0) {
      break;
    }
    inlined_gate_cells += ncg;
  }
  if (inlined_gate_cells > 0) {
    livehd::diag::info("inou.cgen.sim", "clock-gate-inlined", "progress")
        .msg("`{}`: inlined {} clock-gate cell(s) so their gate folds into a flop enable", gname, inlined_gate_cells)
        .emit();
  }
  prepared.at(g).second = true;  // re-looked-up: the steps above may have inserted
  return true;
}

void Cgen_sim::do_from_graph(const std::shared_ptr<hhds::Graph>& graph) {
  emitting_color_root_ = false;  // set below, once this module's color_root verdict is known
  emitted_files_.clear();        // the Gen_record lists THIS module's artifacts only
  pin2var.clear();
  slop_u_values_.clear();
  slop_u_binding_width_.clear();
  preextracted_get_masks_.clear();
  tmp_cnt           = 0;
  cycle_unresolved_ = false;
  cycle_reported_   = false;
  cycle_first_label_.clear();

  hhds::Graph* g     = graph.get();
  auto         gio   = g->get_io();
  const auto   gname = std::string{graph->get_name()};
  const auto   mod   = cpp_id(gname);

  // VCD trace (sim.vcd): only the --top module emits it (so a
  // hierarchy writes one file). top empty -> single-module run, emit anyway.
  const auto entity = gname.substr(gname.rfind('.') + 1);
  // A `test` block lowers to a compiler-minted (`%`-named) comb — a testbench,
  // not synthesizable hardware. Its asserts are checked by running the `lhd sim`
  // driver (prp_sim), never by emitting a Slop unit (which would pull in the
  // formal-property header it does not need). Skip it; the kernel's sim_into()
  // drops it from the build's source list too so the two stay consistent.
  if (!entity.empty() && entity.front() == '%') {
    return;
  }
  // Every structural rewrite this emitter makes ran in prepare_graph(), which
  // to_cgen_sim drives over the WHOLE library before it measures anything.
  // Calling it here too keeps a standalone Cgen_sim user correct; it is a
  // no-op on an already-prepared graph. A refusal (an unexpandable replicated
  // instance) means this body cannot be emitted correctly — stop, the located
  // diagnostic already came from prepare_graph.
  if (!prepare_graph(graph)) {
    return;
  }
  // The shared clock-role analysis -- the same `Design_clocks` that pass/lec's
  // phase schedule and pass.single_edge build. Built AFTER the rewrites above,
  // which change the graph.
  const livehd::latch_contract::Design_clocks                              design_clocks(g, /*hier=*/false);
  absl::flat_hash_map<hhds::Graph*, livehd::latch_contract::Design_clocks> local_clock_cache;
  const auto local_clocks_for = [&](hhds::Graph* body) -> const livehd::latch_contract::Design_clocks& {
    if (body == g) {
      return design_clocks;
    }
    return local_clock_cache.try_emplace(body, body, /*hier=*/false).first->second;
  };
  std::optional<livehd::latch_contract::Design_clocks> hierarchy_clock_cache;
  const auto hierarchy_clocks_for_root = [&]() -> const livehd::latch_contract::Design_clocks& {
    if (!hierarchy_clock_cache.has_value()) {
      hierarchy_clock_cache.emplace(g, /*hier=*/true);
    }
    return *hierarchy_clock_cache;
  };
  // Structural clock/reset interface memo for this emission only. Same reason
  // as design_clocks above: it answers about the graph AS PREPARED, so it must
  // not outlive the body it was measured on.
  livehd::latch_contract::Clock_port_cache port_cache;

  // `is_top` = the testbench-driven module (vs an instantiated sub-module); it
  // only gates which module BAKES the VCD file path (avoids two modules opening
  // the same baked file). The VCD machinery itself (vars, snapshots, the phased
  // hierarchical dump methods) is emitted for EVERY module when tracing is on:
  // the root instance's writer is shared down the hierarchy by __vcd_hier(), so
  // one VCD carries the whole design tree, not just the top's io.
  // `top` may be the bare entity or the full internal `file.entity` name — the
  // full form is the only spelling that disambiguates two same-entity modules.
  const bool is_top             = top.empty() || entity == top || gname == top;
  //
  // `color_root` must agree with the caller's plan hand-out (inou_cgen.cpp's
  // `root` for the pre-plan probe and its plan loop): the key folds the tune
  // vector only for a root, so a disagreement is a stale hit or a permanent
  // miss. In practice color_root <=> a plan was handed.
  const bool color_root         = (is_top || compact_kernel_) && color_plan_ != nullptr;
  const bool color_storage_only = !color_root && !compact_kernel_;
  // iface.json `executable:true`: the class a testbench drives, which owns the
  // tune identity TU and the tune support tables.
  const bool executable_root    = color_root && !compact_kernel_;
  emitting_color_root_          = color_root;

  // Liveness: only logic something REAL consumes gets emitted — backward BFS
  // from the sinks (IO nodes cover the graph outputs; state elements, Memory
  // and Sub calls gather their input cones at emission).
  live_.clear();
  {
    std::vector<hhds::Node_class> lstk;
    for (auto n : g->body().nodes()) {
      auto nop = livehd::graph_util::type_op_of(n);
      if (nop == Ntype_op::Sub || nop == Ntype_op::Memory || nop == Ntype_op::IO || livehd::graph_util::is_type_register(n)) {
        if (live_.insert(n.get_class_index()).second) {
          lstk.push_back(n);
        }
      }
    }
    // Graph outputs seed from the same accessor the output emission uses —
    // the IO node's edges are not reliably enumerable via fast_class.
    for (const auto& d : gio->get_output_pin_decls()) {
      auto opin = g->get_output_pin(d.name);
      auto drv  = opin.is_invalid() ? hhds::Pin_class{} : get_driver(opin);
      if (!drv.is_invalid() && !drv.is_const()) {
        auto m = drv.get_master_node();
        if (!m.is_invalid() && live_.insert(m.get_class_index()).second) {
          lstk.push_back(m);
        }
      }
    }
    while (!lstk.empty()) {
      auto n = lstk.back();
      lstk.pop_back();
      // STAYS ON EDGES: this liveness walk visits ARBITRARY nodes, compact-loop
      // Subs included, and an in-pin walk would see that carry-in sink once and
      // hand back only one of its two drivers. Losing the seed here marks a
      // LIVE node dead, which is a miscompile, not a missed optimization.
      for (auto e_sink : n.inp_sorted_pins()) {
        for (auto e_drv : e_sink.get_driver_pins()) {
          auto m = e_drv.get_master_node();
          if (!m.is_invalid() && live_.insert(m.get_class_index()).second) {
            lstk.push_back(m);
          }
        }
      }
    }
  }
  const bool        vcd_on = !vcd_file.empty();
  const std::string fstem  = sim_file_stem(gname);
  const std::string base   = odir.empty() ? fstem : absl::StrCat(odir, "/", fstem);

  // Incremental generation: matching HIERARCHICAL digest + every recorded
  // artifact still present -> this module's C++ is already up to date, so skip
  // the emission (and the file rewrite) entirely. MUST happen before
  // File_output creation, which truncates.
  //
  // The color ROOT participates here too, which it did not before. It is by far
  // the most expensive module to emit -- it owns the evaluator and state-commit
  // shards, every canonical kernel TU and, under the LLVM backend, one LLVM
  // codegen per color -- so excluding it meant a no-change rebuild still paid
  // essentially the whole generation cost. Two things had to be true first:
  //
  //   * the digest must see the whole cone (hier_graph_digest), because the
  //     root's code is derived from an occurrence plan spanning every
  //     descendant, and a leaf edit leaves the root's own body untouched;
  //   * the record must list EVERY artifact the emission produced, so a
  //     hit restores a complete set and a half-swept directory misses cold.
  //
  // `color_plan_->report()` deliberately no longer feeds the key. It is a
  // DERIVED value -- the plan is a pure function of the cone plus
  // `observation_on`, both of which are folded here -- and it is not a safe
  // substitute anyway: past 100k version sites the report drops its exhaustive
  // per-site detail and degenerates into summary counts, which two different
  // designs can share. Keying on the inputs is both sound and cheaper.
  if (!odir.empty() && generation_current(g, color_root)) {
    return;
  }

  // Switching a compact definition from LLVM to Slop removes its color
  // evaluator. The host builder enumerates generated sources/bitcode, so stale
  // color artifacts would otherwise keep both backends in the same build.
  if (!odir.empty() && !color_root) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(std::string(odir), ec)) {
      const auto filename = entry.path().filename().string();
      // The error_code overload: a concurrent `lhd sim` (or the user) removing
      // an entry between enumeration and stat must not throw out of generation.
      if (!entry.is_regular_file(ec)) {
        continue;
      }
      if (filename.starts_with(fstem + ".color-kernel-") || filename.starts_with(fstem + ".color-eval-")
          || filename.starts_with(fstem + ".color-commit-") || filename.starts_with(fstem + ".color-bind-")
          || filename == fstem + ".color-runtime.hpp" || filename == fstem + ".color-kernels.hpp"
          || filename == fstem + ".color-plan.txt") {
        std::filesystem::remove(entry.path(), ec);
      }
    }
  }
  // The sim.tune identity TU and support tables belong to an EXECUTABLE root
  // only (an LLVM compact root is a root but not executable). `lhd sim`
  // compiles every *.cpp in the directory, so a stale one would keep a dead
  // identity in the link. A module's own `<stem>.cpp` always has a `.hpp` next
  // to it, which is how a module literally named `<fstem>.tune` is told apart
  // from this module's support TU.
  if (!odir.empty() && !executable_root) {
    std::error_code ec;
    for (const std::string_view suffix : {".tune-id", ".tune"}) {
      const auto stem = absl::StrCat(odir, "/", fstem, suffix);
      if (!std::filesystem::exists(absl::StrCat(stem, ".hpp"), ec)) {
        std::filesystem::remove(absl::StrCat(stem, ".cpp"), ec);
      }
    }
  }
  auto hout = open_out(absl::StrCat(fstem, ".hpp"));  // interface
  auto fout = open_out(absl::StrCat(fstem, ".cpp"));  // definitions ("the slop")

  // Header (<name>.hpp): data members + In/Out + method DECLARATIONS only. A
  // module that instantiates this one #includes this small header (by-value
  // member), so it recompiles when this interface changes, not when the body
  // (in the .cpp) does. The cycle()/reset_cycle() bodies live in the .cpp and
  // are compiled exactly once.
  hout->append("// Generated by inou.cgen.sim (LiveHD). Do not edit.\n");
  hout->append(
      "#pragma once\n#include <array>\n#include <cstdint>\n#include <map>\n#include <string>\n#include <vector>\n"
      "#include \"slop.hpp\"\n#include \"memory.hpp\"\n");
  hout->append(kUnknownLiteralHelper);
  // The canonical state hash the sim.tune walkers (__tune_hash below) and a
  // root's __tune_sources write words with. Every module header carries it, so
  // the walkers of any module compile without a root in the TU.
  hout->append(kTuneHashHelper);
  hout->append(
      "#ifndef LHD_SIM_PRESERVE_LOOP\n"
      "#if defined(__clang__)\n"
      "#define LHD_SIM_PRESERVE_LOOP _Pragma(\"clang loop unroll(disable)\")\n"
      "#elif defined(__GNUC__)\n"
      "#define LHD_SIM_PRESERVE_LOOP _Pragma(\"GCC unroll 1\")\n"
      "#else\n#define LHD_SIM_PRESERVE_LOOP\n#endif\n#endif\n");
  // The checkpoint/debug/reset members are straight-line, one statement per
  // state field, and run once or on demand -- never in the cycle loop. On XS
  // Rob they are 68 of the 71 MB of Rob.cpp, and clang's -O2 time on a single
  // function that size is superlinear (the TU compiled for over 50 minutes).
  // Compile them unoptimized; the cycle kernels keep -O2.
  hout->append(
      "#ifndef LHD_SIM_COLD\n"
      "#if defined(__clang__)\n"
      "#define LHD_SIM_COLD __attribute__((cold, noinline, optnone))\n"
      "#elif defined(__GNUC__)\n"
      "#define LHD_SIM_COLD __attribute__((cold, noinline, optimize(\"O0\")))\n"
      "#else\n#define LHD_SIM_COLD\n#endif\n#endif\n");
  if (color_root) {
    hout->append("#include <memory>\n");
  }
  // Forward-declare the signal record (full definition in checkpoint.hpp, included
  // by the .cpp); the header only needs it for the describe_signals() signature.
  hout->append("namespace hlop::ckpt { struct Signal; }\n");
  if (vcd_on) {
    hout->append("#include <memory>\n#include \"vcd_writer.hpp\"\n");
  }
  hout->append("\n");

  // Source (<name>.cpp): includes its own header (which transitively pulls the
  // child interface headers) and holds every method body.
  fout->append("// Generated by inou.cgen.sim (LiveHD). Do not edit.\n");
  fout->append(absl::StrCat("#include \"", fstem, ".hpp\"\n"));
  fout->append("#include \"checkpoint.hpp\"  // name-keyed dump_state/load_state helpers\n");
  fout->append("#include <cassert>\n#include <cstddef>\n");
  fout->append("\n");
  if (color_root) {
    fout->append("#include <algorithm>\n\n");
  }
  // ---- IO decls (sorted by port_id) ----
  struct Io {
    std::string field;
    std::string raw;
    int         bits;
    bool        unsign;
    bool        is_input;
    uint32_t    port_id;
  };
  // C++ access path for a port. A tuple leaf keeps its dot — it is a member of
  // the nested struct emitted for the tuple below — so the RTL name and the C++
  // path stay the same text; only the individual segments are mangled.
  std::vector<Io> ios;
  if (gio) {
    for (const auto& [d, is_input] : gio->decls_in_port_order()) {
      ios.push_back({cpp_port_path(d->name), std::string{d->name}, 0, d->unsign, is_input, static_cast<uint32_t>(d->port_id)});
    }
  }
  for (auto& io : ios) {
    auto pin = io.is_input ? g->get_input_pin(io.raw) : g->get_output_pin(io.raw);
    io.bits  = pin.is_invalid() ? 1 : bits_of(pin, *gio, io.raw);
    if (io.bits <= 0) {
      io.bits = 1;
    }
  }

  // ---- fail closed on a clock this scheduler cannot honor (2f-latch M0) ----
  // One step() == one full clock period and EVERY flop commits in ONE unified
  // loop, regardless of its `clock_pin`. Two shapes are therefore simulated
  // wrong today while reporting success:
  //   * a GATED / derived clock — the gate is dead code, so the flop loads every
  //     tick even when the gate says hold (lifted by M5, which honors clock_pin);
  //   * TWO OR MORE distinct clock nets — every clock is advanced as if it were
  //     the one clock, and clock_input_of() keeps only the first flop's net for
  //     the VCD while the rest degrade to poked data (lifted by M6).
  // Both used to exit 0 with a plausible-looking VCD, which is the worst
  // possible outcome: a silently wrong waveform reads as a passing test.
  {
    // The module's reference clock port: one tick IS one period of it, so it is
    // the net an ICG fold consumes (see icg_guards).
    const std::string clock_port          = clock_input_of(g);
    // See through tolg's identity port wrappers (unary Get_mask width-adjust,
    // and the Get_mask(v,-1) to-positive idiom), same as clock_input_of().
    auto              resolve_passthrough = [](hhds::Pin_class p) {
      for (int hops = 0; hops < 8 && !p.is_invalid(); ++hops) {
        auto n = p.get_master_node();
        if (type_op_of(n) != Ntype_op::Get_mask) {
          break;
        }
        auto e = n.inp_pins_snapshot();  // indexed: [0]=value, [1]=optional mask
        if (e.empty()) {
          break;
        }
        if (e.size() >= 2) {
          const auto mask_drv = e[1].get_driver_pin();
          if (!mask_drv.is_const()) {
            break;
          }
          const auto& mv = const_of(mask_drv);
          if (!mv.is_just_i64() || mv.to_just_i64() != -1) {
            break;
          }
        }
        p = e[0].get_driver_pin();
      }
      return p;
    };
    absl::flat_hash_set<std::string> clock_nets;  // distinct clock nets driving state
    for (auto node : g->body().nodes()) {
      if (!is_type_flop(node)) {
        continue;
      }
      auto d = resolve_passthrough(get_driver(find_sink_pin(node, "clock_pin")));
      if (d.is_invalid()) {
        clock_nets.insert("\x01implicit");  // no clock_pin => the module's implicit clock
        continue;
      }
      if (livehd::graph_util::is_graph_input_pin(d)) {
        clock_nets.insert(std::string{pin_name_of(d)});
        continue;
      }
      // A clock_pin driven by LOGIC. The ICG shape `clk & en` is FOLDED into a
      // commit guard (2f-latch M5) — see icg_guards() — so only a derived clock
      // we cannot fold is refused. Refusing matters: sim would otherwise commit
      // the flop every tick with the gate as dead code, which is a silent
      // miscompile, not a slowdown. A FLOP endpoint also folds the ACTIVE-LOW
      // flavour (fall-committed guards), so the scan opts in the same way the
      // flop construction below does.
      bool scan_fall = false;
      if (!icg_guards(d, clock_port, &design_clocks, is_type_flop(node) ? &scan_fall : nullptr).empty()) {
        clock_nets.insert("\x01implicit");  // folded: commits on the reference clock, qualified
        continue;
      }
      if (plain_clock_cone(d, design_clocks)) {
        clock_nets.insert("\x01implicit");  // an identity wrapper, not a gate: ticks with the reference
        continue;
      }
      if (::getenv("LIVEHD_SIM_CLK_DEBUG") != nullptr) {
        auto        cone = livehd::latch_contract::clock_op_of(d, design_clocks);
        std::string dbg  = absl::StrCat("[clkdbg] flop ",
                                        debug_name(node),
                                        " clock_op_of=",
                                        cone ? (cone->clock_inverted ? "INVERTED" : (cone->div != 1 ? "DIV" : "cone")) : "nullopt");
        if (cone) {
          absl::StrAppend(&dbg,
                          " clock=",
                          cone->clock.is_invalid() ? "<inv>" : debug_name(cone->clock.get_master_node()),
                          " enables=",
                          cone->enables.size(),
                          " div=",
                          cone->div);
        }
        absl::StrAppend(&dbg, "\n[clkdbg] cone walk from ", debug_name(d.get_master_node()), ":\n");
        std::vector<hhds::Pin_class>          stk{d};
        absl::flat_hash_set<hhds::Node_class> seen;
        int                                   budget = 40;
        while (!stk.empty() && budget-- > 0) {
          auto p = stk.back();
          stk.pop_back();
          if (p.is_invalid()) {
            continue;
          }
          auto n = p.get_master_node();
          if (!seen.insert(n).second) {
            continue;
          }
          absl::StrAppend(&dbg,
                          "[clkdbg]   ",
                          debug_name(n),
                          " op=",
                          op_name(type_op_of(n)),
                          " gin=",
                          livehd::graph_util::is_graph_input_pin(p) ? pin_name_of(p) : "-",
                          "\n");
          if (livehd::graph_util::is_graph_input_pin(p) || p.is_const()) {
            continue;
          }
          for (auto e_sink : n.inp_sorted_pins()) {
            for (auto e_drv : e_sink.get_driver_pins()) {
              stk.push_back(e_drv);
            }
          }
        }
        std::fputs(dbg.c_str(), stderr);
      }
      livehd::diag::err("inou.cgen.sim", "gated-clock-unsupported", "unsupported")
          .msg("module `{}`: flop `{}` has a derived clock inou.cgen.sim cannot fold into a commit guard", gname, debug_name(node))
          .hint(
              "only the ICG shape `<clock> & <enable>` is folded (commit when the enable is high at the reference "
              "edge); any other derived clock would be simulated as if it ticked every step, with the gate as dead "
              "code — a silent miscompile. Model the gate as the flop's `enable` instead, or simulate the emitted "
              "Verilog with an event-driven simulator")
          .emit();
      return;
    }
    // A MEMORY is state too, and its clock is the same question. Iterating
    // `is_type_flop` only would let a clock-gated memory compile CLEAN and
    // write every tick with the gate as dead code —
    // the silent half of this gap (`inou.cgen.verilog` gets it right on the
    // same graph: the emitted `cgen_memory_*` instance takes `.clk(clk & en)`).
    // The fold itself lands on the write enable further down; here we only
    // refuse the cones it cannot fold.
    for (auto node : g->body().nodes()) {
      if (type_op_of(node) != Ntype_op::Memory) {
        continue;
      }
      // A Memory carries a clock_pin PER PORT (`<n>clock_pin`), and the fold
      // below keeps a single `m.clock_guards` for the whole array. So this
      // probe must see every port's clock, and two DIFFERENT ones are refused
      // outright: inspecting one port and folding another's cone silently
      // either drops a gate or applies it to writes it does not qualify.
      std::vector<hhds::Pin_class> distinct;
      livehd::graph_util::for_each_memory_clock_driver(node, [&](const hhds::Pin_class& raw) {
        auto rd = resolve_passthrough(raw);
        if (rd.is_invalid()) {
          return;
        }
        const bool dup = std::any_of(distinct.begin(), distinct.end(), [&](const hhds::Pin_class& o) {
          return o.get_class_index() == rd.get_class_index();
        });
        if (!dup) {
          distinct.push_back(rd);
        }
      });
      if (distinct.size() > 1) {
        livehd::diag::err("inou.cgen.sim", "gated-clock-unsupported", "unsupported")
            .msg("module `{}`: memory `{}` has {} distinct port clocks; inou.cgen.sim models one clock per array",
                 gname,
                 debug_name(node),
                 distinct.size())
            .hint(
                "a per-port gated clock would be folded into a single array-wide write guard, so one port's enable "
                "would qualify another port's writes — a silent miscompile. Give every port the same clock, or "
                "simulate the emitted Verilog with an event-driven simulator")
            .emit();
        return;
      }
      auto d = distinct.empty() ? hhds::Pin_class{} : distinct.front();
      if (d.is_invalid() || livehd::graph_util::is_graph_input_pin(d) || d.is_const()) {
        continue;  // unclocked array, or a plain clock net
      }
      if (!icg_guards(d, clock_port, &design_clocks).empty() || plain_clock_cone(d, design_clocks)) {
        continue;  // folded into the write enable, or not a gate at all
      }
      livehd::diag::err("inou.cgen.sim", "gated-clock-unsupported", "unsupported")
          .msg("module `{}`: memory `{}` has a derived clock inou.cgen.sim cannot fold into a write guard", gname, debug_name(node))
          .hint(
              "only the ICG shape `<clock> & <enable>` is folded (write when the enable is high at the reference "
              "edge); any other derived clock would be simulated as if the memory ticked every step, with the gate "
              "as dead code — a silent miscompile. Model the gate as the memory's write enable instead, or simulate "
              "the emitted Verilog with an event-driven simulator")
          .emit();
      return;
    }
    // A DERIVED CLOCK CROSSING INTO A CHILD is the same question one level
    // down, and the two scans above cannot see it: they iterate state in THIS
    // body, and the module that merely computes a gate and wires it to a
    // child's clock port often has no local state on that net at all.
    //
    // The only thing the `__tick` channel can say is "the reference period,
    // qualified by these enables". An INVERTED or DIVIDED cone has no spelling
    // there, so emit_sub_call writes no guard word, the child's default `true`
    // stands, and the child commits every period with the gate as dead code.
    for (auto node : g->body().nodes()) {
      if (type_op_of(node) != Ntype_op::Sub) {
        continue;
      }
      auto cdef = node.get_subnode_graph();
      if (!cdef) {
        continue;  // body-less blackbox: nothing of ours commits inside it
      }
      const auto& guard_pids = clock_guard_ports(cdef, port_cache);
      if (guard_pids.empty()) {
        continue;
      }
      // In-PIN walk with the driver fetched only after the pid matches: `node`
      // is a Sub and may be a compact loop, whose carry-in sink is the one
      // sanctioned two-driver pin. A clock-guard port is never that pin.
      for (const auto& gsink : node.inp_sorted_pins()) {
        if (!guard_pids.contains(static_cast<uint32_t>(gsink.get_port_id()))) {
          continue;  // not a clock port the child clocks state on
        }
        auto d = resolve_passthrough(gsink.get_driver_pin());
        if (d.is_invalid() || livehd::graph_util::is_graph_input_pin(d) || d.is_const()) {
          continue;  // a plain clock net: forwarded (or ungated) as-is
        }
        // Exactly the emit-site arm that DROPS the word: a decoded gate whose
        // enables cannot ride the tick channel because the reference is
        // inverted or divided. Everything else either folds (the enables
        // become the conjunction) or forwards the root's own word.
        auto cone = livehd::latch_contract::clock_op_of(d, design_clocks);
        if (!cone || cone->enables.empty() || (!cone->clock_inverted && cone->div == 1)) {
          continue;
        }
        livehd::diag::err("inou.cgen.sim", "gated-clock-unsupported", "unsupported")
            .msg("module `{}`: instance `{}` is driven with a derived clock inou.cgen.sim cannot forward as a tick guard",
                 gname,
                 debug_name(node))
            .hint(
                "the child is told `<port>__tick` — the reference period qualified by enables — so only the ICG shape "
                "`<clock> & <enable>` crosses the boundary. An inverted or divided clock has no spelling there, and "
                "the child would commit every period with the gate as dead code — a silent miscompile. Model the gate "
                "as the child's `enable` instead, or simulate the emitted Verilog with an event-driven simulator")
            .emit();
        return;
      }
    }
    // MULTI-CLOCK is supported as of M6 — the M0 refusal here is LIFTED. State
    // on a net other than the reference clock commits on a detected EDGE of
    // that net (see Flop::sec_clock), so a second clock is simply a data input
    // the testbench toggles. What is NOT lifted is a derived clock we cannot
    // fold (handled above): that still fails closed, because there is no net to
    // detect an edge on without inventing one.
    (void)clock_nets;
  }

  // CLOCK INPUT PORTS this definition's state actually commits on. Each gets a
  // companion `<field>__tick` bool in `In`, defaulted TRUE.
  //
  // WHY. A clock GATE in the parent whose output crosses into this module as a
  // clock port is invisible from here: the port is an ordinary graph input, so
  // this emitter picks it as the reference clock and commits every tick with the
  // parent's gate as dead code. That is a SILENT miscompile, not a refusal, and
  // it covered 681 state elements on minion. The guard is the channel the parent
  // uses to say "the clock did not tick for you this period".
  //
  // Defaulted true and emitted for EVERY clock port, so an ungated
  // instantiation, an existing checkpoint and every hand-written testbench are
  // bit-for-bit unchanged; only a parent that gates the port ever writes it.
  absl::flat_hash_set<std::string> clock_in_fields;
  {
    // The RECURSIVE guard-port analysis: covers state clocked on the port
    // directly, state clocked through a local gate ROOTED at the port, and
    // ports that merely FORWARD into a child's guard port (the stateless
    // wrapper levels of minion's txfma chain).
    const auto& gports = clock_guard_ports(graph, port_cache);
    if (!gports.empty() && gio) {
      for (const auto& d : gio->get_input_pin_decls()) {
        if (!gports.contains(static_cast<uint32_t>(d.port_id))) {
          continue;
        }
        for (const auto& io : ios) {
          if (io.is_input && io.raw == d.name) {
            clock_in_fields.insert(io.field);
            break;
          }
        }
      }
    }
  }

  // ---- flops (Flop cells; Latch/Memory -> later phase) ----
  // Generated STRUCT MEMBER names — flops, memories, sub-instances — must be
  // unique, and node names alone do not guarantee it. Two producers of
  // collisions are real: a generate-loop's instances all carry the same
  // callsite name (`::[name=u_unit_compressor]` repeated per iteration in
  // generated Pyrope — minion's txfma_4_2_compressor_array declares nine
  // members named u_unit_compressor), and the false-loop inliner cloning
  // several same-named instances' bodies (vpu_top's eight lanes are all
  // `lane`, so eight copies of `lane.f3_tenb_pass` land in one graph). Legal
  // in the graph, an immediate host-compile error in C++.
  // First occurrence keeps the plain name; later ones get `__i2`, `__i3`, ...
  // Every consumer (calls, checkpoint, iface.json, VCD hierarchy) reads the
  // stored member string, so the suffix is consistent everywhere.
  absl::flat_hash_set<std::string> used_member_names;
  // A PORT name is claimed before any of them. Ports are struct fields too, and
  // the emitted `cycle()` builds a local `Out <port>` on top of that — so a sub
  // instance named after the port it drives (`o = subcnt(en=en)`, whose
  // instance name is now the LHS variable) shadows the member and the body
  // reads `o.__gen` off the Out struct. Reserving here demotes the collider to
  // `<port>__i2` instead.
  if (gio) {
    // Reserve the STRUCT MEMBER, which is the top-level name: a dotted tuple
    // leaf (`io.data`) is emitted as a field of the GROUP member `io` (see
    // cpp_port_path / emit_io_block), so reserving cpp_id("io.data") ==
    // `io_data` would claim a name that is never a member — demoting an
    // unrelated instance called `io_data` to `io_data__i2` — while leaving the
    // real member `io` free for an instance to shadow.
    const auto member_of = [](std::string_view n) {
      const auto dot = n.find('.');
      return cpp_id(dot == std::string_view::npos ? n : n.substr(0, dot));
    };
    for (const auto& d : gio->get_input_pin_decls()) {
      used_member_names.insert(member_of(d.name));
    }
    for (const auto& d : gio->get_output_pin_decls()) {
      used_member_names.insert(member_of(d.name));
    }
  }
  auto unique_member = [&used_member_names](std::string candidate) -> std::string {
    if (candidate.empty()) {
      candidate = "u";
    }
    std::string name = candidate;
    for (int i = 2; !used_member_names.insert(name).second; ++i) {
      name = absl::StrCat(candidate, "__i", i);
    }
    return name;
  };
  // ONE authority for "what C++ type does a stored value of this width and sign
  // get". This used to be a second, independent copy of Cgen_sim::stored_type()
  // that had already drifted -- stored_type guards `bits > 0`, this did not, so
  // value_type(0, true) would have spelled the illegal `Slop_u<0>`. Forward
  // instead of duplicating; any future rule change then has one place to land.
  const auto value_type    = [&](int bits, bool unsign) { return stored_type(bits, unsign); };
  const auto unknown_value = [&](int bits, bool unsign) {
    const auto unknown = absl::StrCat("Slop<", bits, ">::unknown(", bits, ")");
    return absl::StrCat(value_type(bits, unsign), "{", unknown, "}");
  };
  const auto mark_slop_u_binding = [&](const hhds::Pin_class& pin) {
    if (!pin.is_invalid() && slop_u_ && is_unsign(pin)) {
      canonical_.insert(pin.get_class_index());
      slop_u_values_.insert(pin.get_class_index());
    }
  };
  const auto stored_value_operand = [&](const hhds::Pin_class& pin, int bits, bool unsign) {
    if (!slop_u_ || !unsign) {
      return operand(pin, bits);
    }
    if (pin.is_const()) {
      const auto& value = const_of(pin);
      if (value.is_numeric() && value.is_just_i64()) {
        return absl::StrCat("Slop_u<", bits, ">::create_integer(", value.to_just_i64(), ")");
      }
    }
    const int source_bits = pin.is_invalid() ? bits : std::max(1, wbits_of(pin));
    if (!pin.is_invalid() && slop_u_values_.contains(pin.get_class_index())) {
      const auto source = raw_operand(pin, source_bits);
      return source_bits == bits ? source : absl::StrCat("(", source, ").zext_to_u<", bits, ">()");
    }
    // The expression is not concretely tracked as Slop_u. Materialize it once
    // at the stored width so a guarded assignment's two arms have one exact
    // C++ type (and do not become an ambiguous Slop_u<M>/Slop_u<N> ternary).
    return absl::StrCat("Slop_u<", bits, ">{", operand(pin, bits), "}");
  };

  struct Flop {
    hhds::Node_class             node;
    std::string                  member;
    int                          bits;
    int                          depth;              // pipe_min shift-register depth (>=1)
    std::vector<std::string>     stages;             // depth-1 intermediate stage members (q is the last)
    bool                         posedge    = true;  // false = negedge flop (posclk known-false)
    bool                         is_latch   = false;
    // 2f-latch M8 step 0d. On a LATCH, `posclk` is the ENABLE POLARITY, not an
    // edge (graph/cell.cpp): known-false means transparent while enable == 0,
    // so the write test is INVERTED. Reading the pin with the Flop meaning did
    // two wrong things at once — it put the latch in the negedge sub-tick and
    // left the enable un-inverted — for the one shape that produces it, a
    // yosys-imported active-low $dlatch. cgen_verilog gets this right
    // (`neg_en` -> `if (!en)`), so sim silently disagreed with its own Verilog.
    bool                         neg_enable = false;
    // ICG fold (2f-latch M5): non-clock operands of a `<clock> & <enable>` clock
    // cone. Non-empty => this flop commits only in ticks where every guard is
    // true. Empty => an ungated clock, i.e. commit every tick.
    std::vector<hhds::Pin_class> clock_guards;
    // SECONDARY CLOCK (2f-latch M6). Invalid => this flop rides the module's
    // REFERENCE clock, one edge per tick, exactly as before. Valid => the flop
    // hangs on a DIFFERENT clock net (a second clock port the testbench drives
    // as data), so it commits only on a detected edge of that net, tracked by
    // `prev_member`.
    hhds::Pin_class              sec_clock;
    std::string                  prev_member;
    // The `In` field of the clock PORT this element commits on, when that port
    // carries a tick guard (see clock_in_fields). Empty otherwise. A parent that
    // gates the port drives `<field>__tick` false for the periods it withholds.
    std::string                  tick_field;
    // ACTIVE-LOW gate flavour (icg_guards' fall_commit): the clock cone is an
    // INVERTED gated reference, so this flop commits in the FALL sub-tick
    // (posedge=false) AND its clock_guards must be sampled THERE — the guard
    // latches are transparent during the high phase, so their rise-pass
    // (pre-tick) values are one half-period stale. emit_commit_enable skips
    // such a flop in the rise; the fall pass computes its `_cen` fresh.
    bool                         gate_fall = false;
    // Bitwidth proves the Q pin unsigned. Keep state canonical too: a register
    // is not a special signedness boundary, and storing it as Slop would force
    // every downstream use to mask/zext the same declared width again.
    bool                         unsign    = false;
  };
  std::vector<Flop> flops;
  for (auto node : g->body().nodes()) {
    // A LATCH rides the flop path (2f-latch M5). Under the no-time-borrowing
    // scope ruling a latch is a flop-with-enable that commits at its window's
    // closing edge, and for END-OF-TICK observation — the shared observation
    // model for sim / BMC / Icarus — that collapses to exactly the flop update
    // this emitter already performs. tolg has already baked the hold mux into
    // din (`din = en ? d : q`) and wired `enable = en`, so
    //     q_next = en ? din : q = en ? d : q
    // which IS transparency sampled at the end of the tick. The scheduler below
    // evaluates a low window and then a high window, carrying the settled low
    // value into the latter. That supplies both master/slave ordering and the
    // coincident-edge read-through needed by the L1 fixture (verified against
    // the Icarus schedules in tests/sim/latch_sim_{master_slave,l1_buffer}.prp).
    //
    // The Latch cell shares Flop's pin ids (M2), so every named lookup below —
    // din, enable, and the absent reset/initial/pipe_min — resolves unchanged.
    if (!is_type_flop(node) && type_op_of(node) != Ntype_op::Latch) {
      continue;
    }
    auto qpin  = node.get_driver_pin(0);
    int  depth = 1;
    if (auto pm = get_driver(find_sink_pin(node, "pipe_min")); pm.is_const()) {
      depth = std::max<int>(1, static_cast<int>(const_of(pm).to_just_i64()));
    }
    Flop f{node,
           unique_member(cpp_id(wire_name(qpin))),
           wbits_of(qpin),
           depth,
           {},
           true,
           type_op_of(node) == Ntype_op::Latch,
           false,
           {},
           {},
           {},
           {}};
    f.unsign                    = is_unsign(qpin);
    const std::string ref_clock = clock_input_of(g);
    bool              gate_fall = false;
    f.clock_guards
        = icg_guards(get_driver(find_sink_pin(node, "clock_pin")), ref_clock, &design_clocks, f.is_latch ? nullptr : &gate_fall);
    {
      // The port whose tick guard this element honors: the clock driver
      // resolved to its ROOT — directly a port, or a local gate cone whose
      // reference is the port (the guards fold into clock_guards above; the
      // parent's word about the PORT itself composes through tick_field).
      auto            cdp  = get_driver(find_sink_pin(node, "clock_pin"));
      hhds::Pin_class root = cdp;
      if (!cdp.is_invalid() && !livehd::graph_util::is_graph_input_pin(cdp)) {
        if (auto cone = livehd::latch_contract::clock_op_of(cdp, design_clocks); cone && !cone->clock.is_invalid()) {
          root = cone->clock;
        } else {
          root = livehd::latch_contract::control_root(cdp).net;
        }
      }
      if (!root.is_invalid() && livehd::graph_util::is_graph_input_pin(root)) {
        for (const auto& io : ios) {
          if (io.is_input && io.raw == pin_name_of(root) && clock_in_fields.contains(io.field)) {
            f.tick_field = io.field;
            break;
          }
        }
      }
    }
    // SECONDARY CLOCK (2f-latch M6): a clock_pin wired to a graph input that is
    // NOT the module's reference clock. One tick is one period of the REFERENCE
    // clock, so a second clock cannot also tick once per call — it is a signal
    // the testbench drives, and its flops commit on a detected edge of it.
    if (f.clock_guards.empty()) {
      auto cd = get_driver(find_sink_pin(node, "clock_pin"));
      for (int hops = 0; hops < 8 && !cd.is_invalid(); ++hops) {
        auto cn = cd.get_master_node();
        if (type_op_of(cn) != Ntype_op::Get_mask) {
          break;
        }
        auto ce = cn.inp_pins_snapshot();  // indexed; Get_mask, never a Sub
        if (ce.empty()) {
          break;
        }
        if (ce.size() >= 2 && !ce[1].get_driver_pin().is_const()) {
          break;
        }
        cd = ce[0].get_driver_pin();
      }
      if (!cd.is_invalid() && livehd::graph_util::is_graph_input_pin(cd)) {
        const std::string cd_name{pin_name_of(cd)};
        // A NEGEDGE flop whose net IS the adopted reference still needs a
        // detected edge when that net is not conventionally SPELLED as a
        // clock: clock_input_of() then adopted it only because no register
        // sits on the implicit reference clock (flop_sim_negedge_sole_clock --
        // `clock_pin=ref rclk` as the design's sole clock), and the net is
        // really a signal the testbench drives, so a posclk=false flop would
        // commit at every tick instead of holding across the rises.
        //
        // Deliberately NARROW on both axes, because each wider form breaks a
        // pinned behavior:
        //  * posedge flops keep the one-tick-one-period model even on an
        //    unconventional sole net -- gate_through_wrapper/hier_gate_port
        //    children commit per gated period on a `gclk` PORT through the
        //    `<port>__tick` channel, never by a toggling port value;
        //  * a conventionally spelled reference (clk, clock, clk_i, rf_clk_i,
        //    ...) is never edge-detected -- conditional_state_named_clock pins
        //    that a sole explicit `clk_i` commits once per tick. The spelling
        //    test is the shared Design_clocks::name_looks_like_clock notion,
        //    not a private list.
        bool              ref_needs_edge = false;
        if (cd_name == ref_clock && !livehd::latch_contract::Design_clocks::name_looks_like_clock(cd_name)
            && type_op_of(node) != Ntype_op::Latch) {
          auto pc        = get_driver(find_sink_pin(node, "posclk"));
          ref_needs_edge = pc.is_known_false();
        }
        if (cd_name != ref_clock || ref_needs_edge) {
          f.sec_clock   = get_driver(find_sink_pin(node, "clock_pin"));
          f.prev_member = absl::StrCat("__clkprev_", cpp_id(cd_name));
        }
      }
    }
    // posedge (default) vs negedge clock: the comptime `posclk` pin, known-false
    // means negedge -- and, since M5, it also picks the sub-tick the element
    // commits in. On a LATCH the SAME pin means enable polarity instead, so it
    // must never reach f.posedge: a latch always commits in the primary
    // sub-tick and carries its polarity in `neg_enable` (M8 step 0d).
    if (auto pc = get_driver(find_sink_pin(node, "posclk")); pc.is_const()) {
      const bool pos = !const_of(pc).is_known_false();
      if (type_op_of(node) == Ntype_op::Latch) {
        f.neg_enable = !pos;
      } else {
        f.posedge = pos;
      }
    }
    // ACTIVE-LOW gate flavour (icg_guards' fall_commit): the flop's clock is
    // an inverted gated reference. Applied AFTER any posclk word, because the
    // gate's inversion COMPOSES with the element's own edge rather than
    // replacing it:
    //   posedge element on `~clk & en` -> samples on clk's FALL: fall sub-tick,
    //                                     guards read there (gate_fall);
    //   negedge element on `~clk & en` -> the two inversions cancel: it samples
    //                                     on clk's RISE, which is the ordinary
    //                                     rise sub-tick with rise-read guards.
    // Overriding posedge unconditionally put the second shape a half period
    // late, silently.
    if (gate_fall) {
      const bool cancels = !f.posedge;  // a negedge element: inversion cancels
      f.posedge          = cancels;
      f.gate_fall        = !cancels;
    }
    for (int i = 0; i < depth - 1; ++i) {
      f.stages.push_back(absl::StrCat(f.member, "_p", i));
    }
    flops.push_back(std::move(f));
  }

  // Identify the top-level clock INPUT port -- the net feeding the flops'
  // clock_pin. In a cycle-based sim it is otherwise just an input forced to 0
  // (which is why the clock never toggled), so we trace it as a dedicated clock
  // waveform driven by `step` rather than as an ordinary io signal. Reset is NOT
  // special: it is an ordinary input the testbench pokes (`acc.reset = ...`) and
  // is traced like any other input.
  std::string clk_field;
  {
    // clock_input_of covers both this module's own flops AND the pass-through
    // case (a flopless wrapper whose `clock` input is wired straight into its
    // sub-instances' clock ports -- e.g. the lecfail dut pair): without the
    // recursive walk that input traced as a flat ordinary signal and the
    // synthetic waveform label collided into `clock_vcd0`.
    const auto raw_clk = clock_input_of(g);
    if (!raw_clk.empty()) {
      for (const auto& io : ios) {
        if (io.is_input && io.raw == raw_clk) {
          clk_field = io.field;
          break;
        }
      }
    }
    // name fallback for the common `clock` port -- UNCONDITIONAL: an input
    // named `clock` is always THE clock waveform, never
    // an ordinary traced signal. Generated RTL (CIRCT) stamps a clock port on
    // every module including pure-comb ones and modules whose only state is a
    // Memory array (no flops); tracing those as data made the synthetic clock
    // label uniquify into `clock_vcd0` and desynced registration from
    // __vcd_clk (the hier-VCD 'clock_vcd0 do not registered' abort).
    if (clk_field.empty()) {
      for (const auto& io : ios) {
        if (io.is_input && io.field == "clock") {
          clk_field = "clock";
          break;
        }
      }
    }
  }
  // The VCD clock waveform label defaults to the real port name; a `tick
  // clocks=(name=ratio)` clause can override the label at run time.
  const std::string clk_label = clk_field.empty() ? "clock" : clk_field;

  // ---- memories (Ntype_op::Memory -> std::array<Slop<bits>,size> member) ----
  struct MemPort {
    bool            rd  = false;
    size_t          pid = 0;  // Memory port group (raw sink pid / stride)
    hhds::Pin_class addr, enable, din;
    int             dout_pid = -1;  // read port: driver pin id = n_wr + rd_index
    int             rdidx    = -1;
    int             wridx    = -1;  // write port index (for the FWD bit)
  };
  struct Mem {
    hhds::Node_class node;
    std::string      member;
    int              bits = 1, size = 0, type = 2;
    // Per-(read,write) forwarding matrix (graph/cell.cpp): bit (rdidx*n_wr +
    // wridx). Held as the const itself — Dlop::bit_test is arbitrary precision,
    // and n_rd*n_wr overflows an int on wide shapes.
    spool_ptr<Dlop>  fwd;
    // Same layout: bit (rdidx*n_wr + wridx) set => that collision is UNDEFINED
    // (Pyrope ordering="none"). Slop has no x, so the ruling's "sim randomizes"
    // applies — a latent collision then fails loudly instead of silently
    // reading the committed value.
    spool_ptr<Dlop>  undef;
    int              n_wr    = 0;
    int              n_rd    = 0;
    int              wensize = 1;  // sub-word write-enable lanes (1 = whole entry)
    // The two matrices classified into one of the four hlop::Memory_* types.
    // Every `ordering` mode makes each row a PREFIX over the user write ports
    // (upass_tolg.cpp ~3585), so a row is a count, never a bit vector.
    enum class Order { old, fwd, program, none };
    Order                        order     = Order::old;
    int                          n_user_wr = 0;  // trailing ports are reset/restore: never forward
    std::vector<int>             fwd_upto;       // per read port; Order::program only
    std::vector<MemPort>         ports;          // real ports, in port order (phantoms dropped)
    // Whole-array support (the `update` bus is driven): one update/read_all bus
    // instead of N per-entry ports. registered when a clock is present.
    hhds::Pin_class              update, update_enable, init, reset, clock;
    // In-field of the guarded clock PORT this memory commits on (see Flop).
    std::string                  tick_field;
    // ICG FOLD, memory side (todo_sim_pipeline.md step 1). Non-empty => this
    // memory's clock is a recognized gate, and every write is qualified by the
    // gate's enables. Empty => an ungated clock (the refusal probe above has
    // already rejected any derived clock that does NOT fold), i.e. write every
    // tick, exactly as before.
    std::vector<hhds::Pin_class> clock_guards;
    bool                         has_read_all = false;
    // All observable element-value pins agree that the stored word is
    // unsigned. The memory backend can then retain the same canonical Slop_u
    // representation as its inputs/outputs instead of converting at each read.
    bool                         unsign       = false;
    bool                         is_whole() const { return !update.is_invalid(); }
    bool                         registered() const { return !clock.is_invalid(); }
    // A whole-array NEXT-STATE stage exists for a whole-array cell (its `update`
    // bus) and for any per-port memory with a `reset`: the reset reloads the
    // `initial` contents in one cycle (`reg m:[N]T = <const>`), staged through
    // the same whole-array din/cen pair as a bulk update.
    bool                         has_reset_stage() const { return is_whole() || !reset.is_invalid(); }
  };
  const auto infer_memory_unsign = [&](Mem& memory) {
    bool saw_value_pin = false;
    bool all_unsigned  = true;
    for (const auto& port : memory.ports) {
      if (!port.rd || port.dout_pid < 0) {
        continue;
      }
      const auto dout = memory.node.get_driver_pin(static_cast<hhds::Port_id>(port.dout_pid));
      if (!dout.is_invalid()) {
        saw_value_pin  = true;
        all_unsigned  &= is_unsign(dout);
      }
    }
    if (memory.has_read_all) {
      const auto read_all = memory.node.get_driver_pin(static_cast<hhds::Port_id>(Ntype::Memory_readall_pid));
      if (!read_all.is_invalid()) {
        saw_value_pin  = true;
        all_unsigned  &= is_unsign(read_all);
      }
    }
    // A write-only memory has no output from which to recover the element
    // signedness. Its data inputs are still valid bitwidth evidence.
    if (!saw_value_pin) {
      for (const auto& port : memory.ports) {
        if (!port.rd && !port.din.is_invalid()) {
          saw_value_pin  = true;
          all_unsigned  &= is_unsign(port.din);
        }
      }
      if (!memory.update.is_invalid()) {
        saw_value_pin  = true;
        all_unsigned  &= is_unsign(memory.update);
      }
    }
    memory.unsign = saw_value_pin && all_unsigned;
  };
  std::vector<Mem> mems;
  for (auto node : g->body().nodes()) {
    if (type_op_of(node) != Ntype_op::Memory) {
      continue;
    }
    Mem m;
    m.node = node;
    std::vector<MemPort> pv;  // indexed by port_id (raw_pid/12)
    for (const auto& msink : node.inp_sorted_pins()) {
      const auto mdrv = msink.get_driver_pin();
      int  raw = static_cast<int>(msink.get_port_id());
      auto pn  = Ntype::get_sink_name(Ntype_op::Memory, raw);
      auto pid = static_cast<size_t>(raw) / Ntype::Memory_port_stride;
      if (pn == "bits") {
        m.bits = static_cast<int>(const_of(mdrv).to_just_i64());
      } else if (pn == "size") {
        m.size = static_cast<int>(const_of(mdrv).to_just_i64());
      } else if (pn == "type") {
        m.type = static_cast<int>(const_of(mdrv).to_just_i64());
      } else if (pn == "fwd") {
        m.fwd = Dlop::clone(const_of(mdrv));
      } else if (pn == "undef") {
        m.undef = Dlop::clone(const_of(mdrv));
      } else if (pn == "update") {
        m.update = mdrv;
      } else if (pn == "update_enable") {  // MUST precede ends_with("enable") below
        m.update_enable = mdrv;
      } else if (pn == "reset") {
        m.reset = mdrv;
      } else if (pn == "initial") {
        m.init = mdrv;  // whole-array reset-value bus (runtime); plain mem: comptime, still assumed 0
      } else if (pn == "wensize") {
        m.wensize = static_cast<int>(const_of(mdrv).to_just_i64());
      } else {
        if (pv.size() <= pid) {
          pv.resize(pid + 1);
        }
        pv[pid].pid = pid;
        if (str_tools::ends_with(pn, "clock_pin")) {
          m.clock = mdrv;  // presence marks a registered whole-array (timing only)
        } else if (str_tools::ends_with(pn, "addr")) {
          pv[pid].addr = mdrv;
        } else if (str_tools::ends_with(pn, "enable")) {
          pv[pid].enable = mdrv;
        } else if (str_tools::ends_with(pn, "din")) {
          pv[pid].din = mdrv;
        } else if (str_tools::ends_with(pn, "rdport")) {
          pv[pid].rd = mdrv.is_const() && !const_of(mdrv).is_known_false();
        }
      }
    }
    for (const auto& odrv : node.out_sorted_pins()) {  // read_all is a DRIVER pin (never on the sink side)
      if (static_cast<hhds::Port_id>(odrv.get_port_id()) == Ntype::Memory_readall_pid) {
        m.has_read_all = true;
        break;
      }
    }
    if (m.bits <= 0) {
      m.bits = 1;
    }
    // ICG FOLD, memory side. The refusal probe above has already rejected any
    // derived memory clock this cannot fold, so an empty result here means
    // "ungated", never "unrecognized".
    m.clock_guards = icg_guards(m.clock, clock_input_of(g), &design_clocks);
    {
      hhds::Pin_class root = m.clock;
      if (!root.is_invalid() && !livehd::graph_util::is_graph_input_pin(root)) {
        if (auto cone = livehd::latch_contract::clock_op_of(root, design_clocks); cone && !cone->clock.is_invalid()) {
          root = cone->clock;
        } else {
          root = livehd::latch_contract::control_root(root).net;
        }
      }
      if (!root.is_invalid() && livehd::graph_util::is_graph_input_pin(root)) {
        for (const auto& io : ios) {
          if (io.is_input && io.raw == pin_name_of(root) && clock_in_fields.contains(io.field)) {
            m.tick_field = io.field;
            break;
          }
        }
      }
    }
    int n_wr = 0;
    for (auto& p : pv) {
      if (!p.addr.is_invalid() && !p.rd) {
        ++n_wr;
      }
    }
    int rd = 0, wr = 0;
    for (auto& p : pv) {
      if (p.addr.is_invalid() && p.din.is_invalid() && p.enable.is_invalid()) {
        continue;  // phantom slot (shared clock landed here)
      }
      if (p.rd) {
        p.dout_pid = n_wr + rd;
        p.rdidx    = rd++;
      } else {
        p.wridx = wr++;
      }
      m.ports.push_back(p);
    }
    // Row stride of the `fwd` matrix: the write-port count `wridx` ranks over
    // (tolg mints every write port with addr+din+enable, so this matches the
    // n_wr the producer laid the matrix out with).
    m.n_wr   = wr;
    m.n_rd   = rd;
    m.member = unique_member(cpp_id(default_instance_name(node)));
    infer_memory_unsign(m);
    if (m.wensize < 1 || m.bits % m.wensize != 0) {
      livehd::diag::err("inou.cgen.sim", "mem-wensize-not-a-divisor", "unsupported")
          .msg("memory `{}` has wensize={}, which does not split bits={} into equal lanes", m.member, m.wensize, m.bits)
          .hint("the write enable is a per-lane vector; a lane width of bits/wensize must be an integer")
          .fatal();
      m.wensize = 1;
    }

    // ---- classify the `fwd`/`undef` matrices into one of the four modes ----
    // Bit (r*n_wr + w). Both are prefix rows by construction, so a row reduces
    // to a count; a NON-prefix row cannot come from an `ordering` attribute
    // (only the deprecated numeric `fwd=` escape hatch could produce one), and
    // guessing a mode for it would silently simulate a different memory.
    auto row_prefix = [&](const spool_ptr<Dlop>& mat, int r) -> int {
      if (!mat) {
        return 0;
      }
      int pre = 0;
      while (pre < m.n_wr && mat->bit_test(r * m.n_wr + pre)) {
        ++pre;
      }
      for (int w = pre; w < m.n_wr; ++w) {
        if (mat->bit_test(r * m.n_wr + w)) {
          return -1;  // a hole: not a prefix
        }
      }
      return pre;
    };
    if (!m.registered()) {
      // A `mut`/`const` array (type=2) has no clock: tolg lowers it
      // writes-before-reads, so every write is visible to every read and the
      // matrix is unused. That is exactly ordering="fwd".
      m.order     = Mem::Order::fwd;
      m.n_user_wr = m.n_wr;
    } else {
      std::vector<int> fwd_upto(static_cast<size_t>(m.n_rd), 0);
      int              max_fwd = 0, max_undef = 0;
      bool             fwd_uniform = true;
      bool             bad_row     = false;
      for (int r = 0; r < m.n_rd; ++r) {
        const int f = row_prefix(m.fwd, r);
        const int u = row_prefix(m.undef, r);
        if (f < 0 || u < 0) {
          bad_row = true;
          break;
        }
        fwd_upto[static_cast<size_t>(r)] = f;
        max_fwd                          = std::max(max_fwd, f);
        max_undef                        = std::max(max_undef, u);
        if (r > 0 && f != fwd_upto[0]) {
          fwd_uniform = false;
        }
      }
      if (bad_row) {
        livehd::diag::err("inou.cgen.sim", "mem-ordering-matrix-not-a-prefix", "unsupported")
            .msg("memory `{}` has a `fwd`/`undef` row that is not a prefix over its {} write ports", m.member, m.n_wr)
            .hint(
                "every `ordering` value (none/old/program/fwd) produces a prefix row; a hole means the matrix came "
                "from the deprecated numeric `fwd=` attribute, which cgen.sim cannot map to a hlop::Memory_* type -- "
                "use `ordering=` instead")
            .fatal();
      }
      // `fwd` and `undef` are MUTUALLY EXCLUSIVE per (read,write) pair, and
      // every `ordering` value drives exactly one of the two matrices. Both
      // non-empty means the matrices were not built from an `ordering` attr,
      // and one of the two meanings would be silently dropped below.
      if (max_fwd > 0 && max_undef > 0) {
        livehd::diag::err("inou.cgen.sim", "mem-fwd-and-undef-both-set", "unsupported")
            .msg("memory `{}` drives BOTH the `fwd` and the `undef` matrix", m.member)
            .hint(
                "the two are mutually exclusive per (read,write) pair -- each `ordering` value sets one of them, so "
                "a cell with both did not come from an `ordering` attribute and has no hlop::Memory_* equivalent")
            .fatal();
      }
      // The reset/restore write ports are the tail no row ever names, so the
      // widest prefix IS the user write-port count.
      if (max_undef > 0) {
        m.order     = Mem::Order::none;
        m.n_user_wr = max_undef;
      } else if (max_fwd == 0) {
        m.order     = Mem::Order::old;
        m.n_user_wr = m.n_wr;
      } else if (fwd_uniform) {
        m.order     = Mem::Order::fwd;
        m.n_user_wr = max_fwd;
      } else {
        m.order     = Mem::Order::program;
        m.n_user_wr = max_fwd;
        m.fwd_upto  = std::move(fwd_upto);
      }
    }
    mems.push_back(std::move(m));
  }

  // ---- sub-module instances (Ntype_op::Sub -> nested struct member) ----
  // ---- Versioned wide inputs ---------------------------------------------
  // Every In field wider than one machine word carries `__ver_<field>`, a
  // change counter that EVERY generated writer bumps when the value changed:
  // the sub-call binding below, the compact-loop wrapper's broadcast and
  // per-ordinal lane binding, and load_state. A module that forwards such an
  // input UNCHANGED into a child instance then re-binds it only when its own
  // copy's version moved, instead of comparing the whole bus on every
  // evaluation -- XiangShan's RenameTable re-bound a 7,800-bit loop invariant
  // 228 times per period, one memcmp each, for 20% of its cycle.
  //
  // The DUT's inputs are written by the driver, which does not version them,
  // so the DUT (dut_) never forwards by version; every other writer of an
  // `__in` field is generated here.
  const auto versioned_bits = [](int bits) { return bits > 64; };
  const auto ver_path       = [](std::string_view field) -> std::string {
    const auto dot = field.rfind('.');
    return dot == std::string_view::npos ? absl::StrCat("__ver_", field)
                                         : absl::StrCat(field.substr(0, dot + 1), "__ver_", field.substr(dot + 1));
  };
  const auto fwd_member
      = [](std::string_view inst, std::string_view field) { return absl::StrCat("__fwd_", inst, "_", cpp_id(field)); };
  // The input port of THIS module that `drv` reads directly, when both it and
  // the `bits`-wide destination are versioned; nullptr otherwise.
  const auto forward_io_of = [&](const hhds::Pin_class& drv, int bits) -> const Io* {
    if (dut_ || drv.is_invalid() || drv.is_const() || !versioned_bits(bits) || !livehd::graph_util::is_graph_input_pin(drv)) {
      return nullptr;
    }
    const auto name = pin_name_of(drv);
    for (const auto& io : ios) {
      if (io.is_input && io.raw == name && versioned_bits(io.bits)) {
        return &io;
      }
    }
    return nullptr;
  };
  // The `__fwd_*` members declared in this struct, and what each one STANDS FOR.
  // Not a plain set of names: `fwd_member` joins two already-sanitized names
  // with '_' and `cpp_id` flattens a tuple leaf's '.', so the name is NOT
  // injective -- instance `u` + field `a_b` and instance `u_a` + field `b` mint
  // the same member, and so do the fields `io.data` and `io_data` of ONE
  // instance (cpp_port_path keeps the dot, cpp_id turns it into '_'). Two pairs
  // sharing one counter is a SILENT stale forward: whichever binds first stamps
  // the version, the other then sees it already stamped and skips a bind it
  // owed, leaving a wide bus a period behind with no diagnostic. Record the
  // pair so every binding site can CHECK it -- a collision, or a site whose
  // source port the pre-scan did not predict, falls back to the ungated bump,
  // which is always correct and only slower.
  struct Forward_stamp {
    std::string field;      // the child input field this member stamps
    std::string src_field;  // this module's input port whose version it holds
  };
  absl::flat_hash_map<std::string, Forward_stamp> forward_stamps;
  // `<inst>.__in.<field>` <- rhs, maintaining the field's version. `io` non-null
  // = a pure forward of this module's own versioned input: gate on its version.
  const auto                                      emit_in_binding =
      [&](std::string_view indent, std::string_view inst, std::string_view field, int bits, const std::string& rhs, const Io* io) {
        if (!versioned_bits(bits)) {
          fout->append(absl::StrCat(indent, inst, ".__gen += slop_update(", inst, ".__in.", field, ", ", rhs, ");\n"));
          return;
        }
        const auto bump = absl::StrCat("if (slop_update(",
                                       inst,
                                       ".__in.",
                                       field,
                                       ", ",
                                       rhs,
                                       ")) { ++",
                                       inst,
                                       ".__gen; ++",
                                       inst,
                                       ".__in.",
                                       ver_path(field),
                                       "; }");
        // The member has to be one the pre-scan declared AND has to stand for
        // THIS (field, source port): gating against a counter that belongs to
        // some other pair skips a bind that was owed. Refusing here costs one
        // full bus compare per evaluation; getting it wrong costs a stale value.
        const auto fwd  = fwd_member(inst, field);
        const auto it   = forward_stamps.find(fwd);
        if (io == nullptr || it == forward_stamps.end() || it->second.field != field || it->second.src_field != io->field) {
          fout->append(absl::StrCat(indent, bump, "\n"));
          return;
        }
        const auto ver = absl::StrCat("__in.", ver_path(io->field));
        fout->append(absl::StrCat(indent,
                                  "if (",
                                  fwd,
                                  " != ",
                                  ver,
                                  ") {\n",
                                  indent,
                                  "  ",
                                  fwd,
                                  " = ",
                                  ver,
                                  ";\n",
                                  indent,
                                  "  ",
                                  bump,
                                  "\n",
                                  indent,
                                  "}\n"));
      };

  struct Sub {
    hhds::Node_class                                 node;
    std::string                                      inst;           // member name
    std::string                                      callee_struct;  // cpp_id of the callee module name
    bool                                             refresh_negedge;
    bool                                             child_refresh_negedge;
    bool                                             negedge_only;  // state advances in the fall half, after the parent's rise
    std::optional<hhds::Subnode_loop>                loop;
    std::vector<std::pair<std::string, std::string>> carries;  // callee input -> output
    std::string                                      loop_struct;
  };
  std::vector<Sub>         subs;
  std::vector<std::string> sub_includes;  // distinct callee headers
  struct Phase_info {
    bool posedge = false;  // a FLOP that commits at the rise
    bool negedge = false;  // a flop that commits at the fall
    // LEVEL-SENSITIVE state. A Latch commits in the RISE sub-tick (the `flops`
    // build above gives one posedge=true unconditionally: on a Latch `posclk`
    // is the enable POLARITY, not an edge), so a child that owns one is not
    // fall-only either. Tracked apart from `posedge` because the refresh
    // predicate below has to tell the two apart.
    bool latch   = false;
    bool refresh = false;  // this graph has a descendant needing the refresh
  };
  absl::flat_hash_map<const hhds::Graph*, Phase_info> phase_memo;
  absl::flat_hash_set<const hhds::Graph*>             phase_visiting;
  // Does this callee need the post-settle negedge refresh (`refresh_negedge`)?
  // A child that owns BOTH rise-half and fall-half state runs its whole
  // `cycle()` inside the parent's RISE, so its fall half samples inputs the
  // parent has not committed yet -- one period stale, forever.
  //
  // Deliberately narrowed to the children whose rise-half state is a LATCH and
  // nothing else, because those are EXACTLY the ones `latch` keeps out of
  // `negedge_only` below: a fall-deferred child already sees post-rise inputs
  // and needs no refresh, so the reclassification and its compensation always
  // travel together (this used to be a whitelist of four port names, which
  // compensated one of the three preview register files and left the other two
  // reclassified-but-stale). A child that ALSO has a posedge flop was
  // rise-scheduled long before any of this, and re-running its fall half is
  // only sound while that half is a pure input capture -- a negedge flop whose
  // next state reads its own Q would ADVANCE TWICE -- so that shape keeps the
  // behavior it has always had.
  const auto needs_neg_refresh = [](const Phase_info& i) { return i.negedge && i.latch && !i.posedge; };
  std::function<Phase_info(const std::shared_ptr<hhds::Graph>&)> graph_phases;
  graph_phases = [&](const std::shared_ptr<hhds::Graph>& pg) -> Phase_info {
    if (!pg) {
      return {};
    }
    if (auto it = phase_memo.find(pg.get()); it != phase_memo.end()) {
      return it->second;
    }
    if (!phase_visiting.insert(pg.get()).second) {
      return {};
    }
    Phase_info info;
    for (auto pn : pg->body().nodes()) {
      if (type_op_of(pn) == Ntype_op::Latch) {
        info.latch = true;
      } else if (is_type_flop(pn)) {
        bool pos = true;
        if (auto pc = get_driver(find_sink_pin(pn, "posclk")); pc.is_const()) {
          pos = !const_of(pc).is_known_false();
        }
        info.posedge |= pos;
        info.negedge |= !pos;
      } else if (is_type_sub(pn)) {
        // MIRROR the filters the `subs` build below applies, or this summary
        // and the callee's own `has_refresh` disagree and the parent emits a
        // call to a `refresh_negedge()` the child never declares: a compact
        // loop is dropped (`!loop` guards both refresh fields, and the wrapper
        // struct has no refresh method at all), and so is anything that never
        // becomes a `Sub` entry.
        auto pio = pn.get_subnode_io();
        if (!pio || pn.subnode_loop()) {
          continue;
        }
        const std::string pname{pio->get_name()};
        if (pname.empty() || pname == livehd::graph_util::lgassert_module_name
            || pname == livehd::graph_util::fproperty_module_name) {
          continue;
        }
        const auto sub  = graph_phases(pn.get_subnode_graph());
        info.refresh   |= needs_neg_refresh(sub) || sub.refresh;
      }
    }
    phase_visiting.erase(pg.get());
    return phase_memo.emplace(pg.get(), info).first->second;
  };
  for (auto node : g->body().nodes()) {
    if (!is_type_sub(node)) {
      continue;
    }
    auto sio = node.get_subnode_io();
    if (!sio) {
      continue;
    }
    std::string cname{sio->get_name()};
    if (cname.empty() || cname == livehd::graph_util::lgassert_module_name || cname == livehd::graph_util::fproperty_module_name) {
      // Recognized primitives, not real sub-graphs: instantiating one emitted
      // an #include for a module with no body and broke the sim host-compile
      // (any design with an undischarged assert). Skipped like cgen_verilog's
      // special-case; EXECUTING the runtime check in sim is the pending
      // runtime-fallback item (2f-formal / 4b slop emission).
      continue;
    }
    const auto                                       child_phase = graph_phases(node.get_subnode_graph());
    const std::string                                inst        = unique_member(cpp_id(default_instance_name(node)));
    std::optional<hhds::Subnode_loop>                loop        = node.subnode_loop();
    std::vector<std::pair<std::string, std::string>> carries;
    if (loop) {
      try {
        node.subnode_group().validate();
      } catch (const std::exception& e) {
        livehd::diag::err("inou.cgen.sim", "compact-loop-invalid", "unsupported")
            .msg("cannot emit compact loop instance '{}': {}", inst, e.what())
            .fatal();
      }
      for (const auto& c : node.subnode_group().carries()) {
        std::string in_name;
        std::string out_name;
        for (const auto& d : sio->get_input_pin_decls()) {
          if (d.port_id == c.input_port()) {
            in_name = d.name;
            break;
          }
        }
        for (const auto& d : sio->get_output_pin_decls()) {
          if (d.port_id == c.output_port()) {
            out_name = d.name;
            break;
          }
        }
        if (in_name.empty() || out_name.empty()) {
          livehd::diag::err("inou.cgen.sim", "compact-loop-carry", "internal")
              .msg("compact loop instance '{}' has a carry whose callee port declaration is unavailable", inst)
              .fatal();
        }
        carries.emplace_back(std::move(in_name), std::move(out_name));
      }
    }
    subs.push_back({node,
                    inst,
                    cpp_id(cname),
                    !loop && needs_neg_refresh(child_phase),
                    !loop && child_phase.refresh,
                    child_phase.negedge && !child_phase.posedge && !child_phase.latch,
                    loop,
                    std::move(carries),
                    // The wrapper struct is defined at GLOBAL scope in this
                    // module's header, and the parent #includes every callee
                    // header, so the name must be unique across the whole
                    // include tree. `inst` alone is not: rolled-loop instances
                    // are numbered by a PER-MODULE counter, so the first rolled
                    // loop of every module is `u_loop_0` and two callee headers
                    // would both define `struct __compact_u_loop_0`. A callee
                    // can also be shared by several containing definitions;
                    // qualify the wrapper by its owner and local instance.
                    loop ? absl::StrCat("__compact_", mod, "_", inst) : std::string{}});
    auto hdr = absl::StrCat(sim_file_stem(cname), ".hpp");
    if (std::find(sub_includes.begin(), sub_includes.end(), hdr) == sub_includes.end()) {
      sub_includes.push_back(hdr);
    }
  }

  for (const auto& h : sub_includes) {
    hout->append(absl::StrCat("#include \"", h, "\"\n"));
  }

  // Which child inputs this module forwards straight from its own versioned
  // inputs (see emit_in_binding). Decided here, before the struct header, so
  // the `__fwd_*` members exist by the time the bodies bind them.
  for (const auto& s : subs) {
    auto sio = s.node.get_subnode_io();
    if (!sio) {
      continue;
    }
    for (const auto& d : sio->get_input_pin_decls()) {
      const int  bits = d.bits > 0 ? static_cast<int>(d.bits) : 1;
      const auto sink = find_sink_pin(s.node, d.name);
      if (sink.is_invalid()) {
        continue;
      }
      for (auto edge_drv : sink.get_driver_pins()) {
        if (edge_drv.get_master_node() == s.node) {
          continue;
        }
        if (const auto* fio = forward_io_of(edge_drv, bits); fio != nullptr) {
          auto field  = cpp_port_path(d.name);
          auto member = fwd_member(s.inst, field);  // before `field` is moved from: argument order is unsequenced
          // try_emplace, never overwrite: on a name collision the FIRST pair
          // keeps the member and every binding site of the loser fails the
          // check above and bumps ungated. Nothing correct is lost, and the
          // winner's gate stays exact.
          forward_stamps.try_emplace(std::move(member), Forward_stamp{std::move(field), fio->field});
        }
        break;
      }
    }
  }

  // Stateless loops have no autonomous child transitions: their wrapper
  // generation already tracks every change to inputs and outputs. Leaf bodies
  // can additionally reuse one scratch callee across ordinals. Keep per-ordinal
  // caches above nested loops, where they avoid repeating an unchanged inner
  // reduction when a different input to the outer body changes.
  struct Loop_storage_shape {
    bool stateless   = true;
    bool nested_loop = false;
  };
  absl::flat_hash_map<const hhds::Graph*, Loop_storage_shape>            loop_storage_memo;
  absl::flat_hash_set<const hhds::Graph*>                                loop_storage_visiting;
  std::function<Loop_storage_shape(const std::shared_ptr<hhds::Graph>&)> loop_storage_shape;
  loop_storage_shape = [&](const std::shared_ptr<hhds::Graph>& body_graph) -> Loop_storage_shape {
    if (!body_graph) {
      return {false, false};
    }
    if (auto it = loop_storage_memo.find(body_graph.get()); it != loop_storage_memo.end()) {
      return it->second;
    }
    if (!loop_storage_visiting.insert(body_graph.get()).second) {
      return {false, false};
    }
    Loop_storage_shape shape;
    for (auto node : body_graph->body().nodes()) {
      const auto op = type_op_of(node);
      if (livehd::graph_util::is_type_register(node) || op == Ntype_op::Memory || op == Ntype_op::Clock_cell) {
        shape.stateless = false;
      } else if (is_type_sub(node)) {
        const auto io = node.get_subnode_io();
        // Mirror the emitter's filters: detached Sub placeholders and proof
        // primitives have no generated callee or persistent simulator state.
        if (!io || io->get_name().empty() || io->get_name() == livehd::graph_util::lgassert_module_name
            || io->get_name() == livehd::graph_util::fproperty_module_name) {
          continue;
        }
        const auto child   = loop_storage_shape(node.get_subnode_graph());
        shape.stateless   &= child.stateless;
        shape.nested_loop |= node.is_loop_subnode() || child.nested_loop;
      }
    }
    loop_storage_visiting.erase(body_graph.get());
    loop_storage_memo.emplace(body_graph.get(), shape);
    return shape;
  };

  for (const auto& s : subs) {
    if (!s.loop) {
      continue;
    }
    const auto& loop          = *s.loop;
    auto        sio           = s.node.get_subnode_io();
    const auto  storage_shape = loop_storage_shape(s.node.get_subnode_graph());
    const bool  shared_lane   = !vcd_on && loop.count != 0 && storage_shape.stateless && !storage_shape.nested_loop;
    const auto  carry_out_for = [&](std::string_view out) -> const std::string* {
      for (const auto& [in_name, out_name] : s.carries) {
        if (out_name == out) {
          return &in_name;
        }
      }
      return nullptr;
    };
    std::string activation_field;
    if (loop.activation_input) {
      for (const auto& d : sio->get_input_pin_decls()) {
        if (d.port_id == *loop.activation_input) {
          activation_field = cpp_port_path(d.name);
          break;
        }
      }
      if (activation_field.empty()) {
        livehd::diag::err("inou.cgen.sim", "compact-loop-activation", "internal")
            .msg("compact loop instance '{}' has no declaration for its activation input", s.inst)
            .fatal();
      }
    }
    bool        activation_skip_safe = false;
    std::string reset_run_condition;
    if (loop.activation_input) {
      const auto  child_graph = s.node.get_subnode_graph();
      const auto& resets      = reset_guard_ports(child_graph, port_cache);
      activation_skip_safe    = resets.complete && resets.ports.size() <= 1;
      if (activation_skip_safe && !resets.ports.empty()) {
        const auto& rp = resets.ports.front();
        for (const auto& d : sio->get_input_pin_decls()) {
          if (static_cast<uint32_t>(d.port_id) == rp.port_id) {
            reset_run_condition
                = absl::StrCat("(lane.__in.", cpp_port_path(d.name), rp.active_low ? ").is_known_false()" : ").is_known_true()");
            break;
          }
        }
        if (reset_run_condition.empty()) {
          activation_skip_safe = false;
        }
      }
    }
    auto lane_prefix_expr = [](std::string_view path_expr, std::string_view ordinal) {
      return absl::StrCat("(",
                          path_expr,
                          ".empty() ? std::string{} : ",
                          path_expr,
                          ".substr(0, ",
                          path_expr,
                          ".size() - 1)) + \"[\" + std::to_string(",
                          ordinal,
                          ") + \"].\"");
    };
    hout->append("\nstruct ", s.loop_struct, " {\n");
    hout->append("  using Callee = ", s.callee_struct, ";\n");
    hout->append("  using In = Callee::In;\n  using Out = Callee::Out;\n");
    hout->append("  static constexpr std::size_t count = ", std::to_string(loop.count), ";\n");
    hout->append("  static constexpr std::size_t storage_count = ", shared_lane ? "1" : "count", ";\n");
    hout->append("  std::array<Callee, ",
                 shared_lane ? "1" : "count",
                 "> lanes{};\n  In __in{};\n  Out __last_out{};\n  Out __out{};\n");
    hout->append("  uint64_t __gen = 1, __kids = 0;\n");
    hout->append("  bool __broadcast_valid = false;\n");
    // `cpp_id` flattens a tuple leaf's '.', so the member name is NOT injective:
    // the ports `io.data` and `io_data` mint the same `__bcast_ver_io_data`.
    // Declaring it twice is a hard C++ error, so count first and keep the
    // counter only for names that are unique -- a colliding port falls back to
    // the ungated whole-bus compare below, which is always correct.
    absl::flat_hash_map<std::string, int> bcast_ver_uses;
    for (const auto& d : sio->get_input_pin_decls()) {
      if (versioned_bits(d.bits > 0 ? static_cast<int>(d.bits) : 1)) {
        ++bcast_ver_uses[cpp_id(cpp_port_path(d.name))];
      }
    }
    {
      std::vector<std::string> bcast_members;  // flat_hash_map order is not reproducible across runs
      for (const auto& [member, uses] : bcast_ver_uses) {
        if (uses == 1) {
          bcast_members.push_back(member);
        }
      }
      std::ranges::sort(bcast_members);
      for (const auto& member : bcast_members) {
        hout->append("  uint32_t __bcast_ver_", member, " = 0;  // version last broadcast\n");
      }
    }
    const bool gate_loop = vcd_file.empty() && !env_.nogate;
    if (gate_loop) {
      hout->append("  uint64_t __done_publish = 0, __kids_publish = 0, __done_advance = 0, __kids_advance = 0;\n");
    }
    if (vcd_on) {
      hout->append("  std::shared_ptr<vcd::VCDWriter> __vcd;\n  std::string __vcd_path;\n");
    }
    if (storage_shape.stateless) {
      hout->append("  void __sync_kids() {}  // no autonomous state in this subtree\n");
    } else {
      hout->append(
          "  void __sync_kids() { uint64_t n = 0; LHD_SIM_PRESERVE_LOOP for (const auto& lane : lanes) n += lane.__gen; "
          "if (n != __kids) { __kids = n; ++__gen; } }\n");
    }
    // All ordinals share invariant inputs. Comparing a wide invariant inside
    // the ordinal walk copies/scans the whole bus count times even when it
    // has not changed. Lane zero represents the last broadcast; reset/load
    // invalidate that representative because they can update lanes separately.
    hout->append("  void __compact_bind_invariants() {\n    if constexpr (count != 0) {\n");
    for (const auto& d : sio->get_input_pin_decls()) {
      if ((loop.index_input && d.port_id == *loop.index_input) || (loop.activation_input && d.port_id == *loop.activation_input)
          || std::ranges::any_of(s.carries, [&](const auto& carry) { return carry.first == d.name; })) {
        continue;
      }
      const auto field = cpp_port_path(d.name);
      if (versioned_bits(d.bits > 0 ? static_cast<int>(d.bits) : 1) && bcast_ver_uses[cpp_id(field)] == 1) {
        // A versioned invariant: the parent's binding bumped `__in.__ver_F`
        // when the bus changed, so one counter compare replaces the bus scan.
        const auto bver = absl::StrCat("__bcast_ver_", cpp_id(field));
        hout->append(absl::StrCat("      if (!__broadcast_valid || ",
                                  bver,
                                  " != __in.",
                                  ver_path(field),
                                  ") {\n        ",
                                  bver,
                                  " = __in.",
                                  ver_path(field),
                                  ";\n"
                                  "        LHD_SIM_PRESERVE_LOOP for (auto& lane : lanes) { if (slop_update(lane.__in.",
                                  field,
                                  ", __in.",
                                  field,
                                  ")) { ++lane.__gen; ++lane.__in.",
                                  ver_path(field),
                                  "; } }\n"
                                  "      }\n"));
      } else {
        hout->append(absl::StrCat("      if (!__broadcast_valid || !lanes[0].__in.",
                                  field,
                                  ".identical(__in.",
                                  field,
                                  ")) {\n"
                                  "        LHD_SIM_PRESERVE_LOOP for (auto& lane : lanes) lane.__gen += slop_update(lane.__in.",
                                  field,
                                  ", __in.",
                                  field,
                                  ");\n"
                                  "      }\n"));
      }
      if (auto cg = s.node.get_subnode_graph();
          cg && clock_guard_ports(cg, port_cache).contains(static_cast<uint32_t>(d.port_id))) {
        hout->append(absl::StrCat("      if (!__broadcast_valid || lanes[0].__in.",
                                  field,
                                  "__tick != __in.",
                                  field,
                                  "__tick) {\n"
                                  "        LHD_SIM_PRESERVE_LOOP for (auto& lane : lanes) lane.__gen += slop_update(lane.__in.",
                                  field,
                                  "__tick, __in.",
                                  field,
                                  "__tick);\n      }\n"));
      }
    }
    hout->append("      __broadcast_valid = true;\n    }\n  }\n");
    const auto emit_lane_inputs = [&](std::string_view lane, std::string_view ordinal) {
      for (const auto& d : sio->get_input_pin_decls()) {
        const std::string field = cpp_port_path(d.name);
        std::string       rhs;
        if (loop.index_input && d.port_id == *loop.index_input) {
          rhs = absl::StrCat("Slop<",
                             std::max<uint32_t>(1, d.bits),
                             ">::create_integer(",
                             loop.first,
                             " + static_cast<int64_t>(",
                             ordinal,
                             ") * ",
                             loop.step,
                             ")");
        } else if (loop.activation_input && d.port_id == *loop.activation_input) {
          rhs = "__active";
        } else {
          for (size_t ci = 0; ci < s.carries.size(); ++ci) {
            if (s.carries[ci].first == d.name) {
              rhs = absl::StrCat("__carry", ci);
              break;
            }
          }
          if (rhs.empty()) {
            continue;  // already broadcast once, outside the ordinal walk
          }
        }
        if (versioned_bits(d.bits > 0 ? static_cast<int>(d.bits) : 1)) {
          hout->append(absl::StrCat("      if (slop_update(",
                                    lane,
                                    ".__in.",
                                    field,
                                    ", ",
                                    rhs,
                                    ")) { ++",
                                    lane,
                                    ".__gen; ++",
                                    lane,
                                    ".__in.",
                                    ver_path(field),
                                    "; }\n"));
        } else {
          hout->append(absl::StrCat("      ", lane, ".__gen += slop_update(", lane, ".__in.", field, ", ", rhs, ");\n"));
        }
        if (auto cg = s.node.get_subnode_graph();
            cg && clock_guard_ports(cg, port_cache).contains(static_cast<uint32_t>(d.port_id))) {
          hout->append(
              absl::StrCat("      ", lane, ".__gen += slop_update(", lane, ".__in.", field, "__tick, __in.", field, "__tick);\n"));
        }
      }
    };
    const auto emit_carry_decls = [&] {
      for (size_t ci = 0; ci < s.carries.size(); ++ci) {
        hout->append("    auto __carry", std::to_string(ci), " = __in.", cpp_port_path(s.carries[ci].first), ";\n");
      }
    };
    const auto emit_activation_decl = [&] {
      if (loop.activation_input) {
        hout->append("    auto __active = __in.", activation_field, ".zext_to<1>();\n");
      }
    };
    const auto emit_carry_advances = [&](std::string_view out) {
      for (size_t ci = 0; ci < s.carries.size(); ++ci) {
        if (loop.activation_input) {
          hout->append(absl::StrCat("      if (__lane_active) __carry",
                                    ci,
                                    " = ",
                                    out,
                                    ".",
                                    cpp_port_path(s.carries[ci].second),
                                    ";  // inactive lane preserves its carry\n"));
        } else {
          hout->append(absl::StrCat("      __carry", ci, " = ", out, ".", cpp_port_path(s.carries[ci].second), ";\n"));
        }
      }
    };
    const auto emit_next_active = [&](std::string_view out) {
      if (!loop.activation_input || !loop.next_active_output) {
        return;
      }
      for (const auto& d : sio->get_output_pin_decls()) {
        if (d.port_id == *loop.next_active_output) {
          hout->append("      __active = __lane_active ? ",
                       out,
                       ".",
                       cpp_port_path(d.name),
                       ".zext_to<1>() : Slop<1>::create_integer(0);\n");
          return;
        }
      }
    };
    const auto emit_outputs = [&](std::string_view dst, std::string_view last) {
      for (const auto& d : sio->get_output_pin_decls()) {
        const std::string field = cpp_port_path(d.name);
        if (const auto* carry_in = carry_out_for(d.name)) {
          size_t ci = 0;
          for (; ci < s.carries.size() && s.carries[ci].first != *carry_in; ++ci) {
          }
          hout->append(absl::StrCat("    __gen += slop_update(", dst, ".", field, ", __carry", ci, ");\n"));
        } else if (loop.count != 0) {
          hout->append(absl::StrCat("    __gen += slop_update(", dst, ".", field, ", ", last, ".", field, ");\n"));
        }
      }
    };
    // Use the same entry-generation gate as a normal compact body. Saving
    // ENTRY generations keeps autonomous state transitions running until they
    // reach a fixed point. Gate the wrapper before binding or walking lanes:
    // gating only each callee still pays count calls and stack probes per phase.
    const auto emit_loop_gate = [&](std::string_view phase, std::string_view result) {
      if (!gate_loop) {
        return;
      }
      hout->append("    const uint64_t __g0 = __gen;\n    uint64_t __k0 = 0;\n");
      if (!storage_shape.stateless) {
        hout->append("    LHD_SIM_PRESERVE_LOOP for (const auto& lane : lanes) __k0 += lane.__gen;\n");
      }
      hout->append(absl::StrCat("    if (__done_", phase, " == __g0 && __kids_", phase, " == __k0) return", result, ";\n"));
    };
    const auto emit_loop_done = [&](std::string_view phase) {
      if (gate_loop) {
        hout->append(absl::StrCat("    __done_",
                                  phase,
                                  storage_shape.stateless ? " = __gen;\n    __kids_" : " = __g0;\n    __kids_",
                                  phase,
                                  " = __k0;\n"));
      }
    };
    hout->append("  void __compact_publish() {\n");
    emit_loop_gate("publish", "");
    hout->append("    __compact_bind_invariants();\n");
    emit_carry_decls();
    emit_activation_decl();
    hout->append("    LHD_SIM_PRESERVE_LOOP for (std::size_t ordinal = 0; ordinal < count; ++ordinal) {\n      auto& lane = lanes[",
                 shared_lane ? "0" : "ordinal",
                 "];\n");
    emit_lane_inputs("lane", "ordinal");
    if (loop.activation_input) {
      hout->append("      const bool __lane_active = (__active).is_known_true();\n");
    }
    if (loop.activation_input && activation_skip_safe) {
      hout->append("      if (__lane_active",
                   reset_run_condition.empty() ? "" : absl::StrCat(" || ", reset_run_condition),
                   ") lane.__compact_publish();  // conditional activation (reset keeps it open)\n");
    } else {
      hout->append("      lane.__compact_publish();\n");
    }
    emit_carry_advances("lane.__out");
    emit_next_active("lane.__out");
    hout->append("    }\n");
    emit_outputs("__out", "lanes[storage_count - 1].__out");
    hout->append("    __sync_kids();\n");
    emit_loop_done("publish");
    hout->append("  }\n");
    hout->append("  const Out& __compact_advance() {\n");
    emit_loop_gate("advance", " __last_out");
    hout->append("    __compact_bind_invariants();\n");
    emit_carry_decls();
    emit_activation_decl();
    const bool needs_last = loop.count != 0 && std::ranges::any_of(sio->get_output_pin_decls(), [&](const auto& d) {
                              return carry_out_for(d.name) == nullptr;
                            });
    if (needs_last) {
      hout->append("    const Out* __last = nullptr;\n");
    }
    hout->append("    LHD_SIM_PRESERVE_LOOP for (std::size_t ordinal = 0; ordinal < count; ++ordinal) {\n      auto& lane = lanes[",
                 shared_lane ? "0" : "ordinal",
                 "];\n");
    emit_lane_inputs("lane", "ordinal");
    if (loop.activation_input) {
      hout->append("      const bool __lane_active = (__active).is_known_true();\n");
      hout->append("      const Out* __period = &lane.__out;\n");
      if (activation_skip_safe) {
        hout->append("      if (__lane_active",
                     reset_run_condition.empty() ? "" : absl::StrCat(" || ", reset_run_condition),
                     ") __period = &lane.__compact_advance();\n");
      } else {
        hout->append("      __period = &lane.__compact_advance();\n");
      }
    } else {
      hout->append("      const auto& __period = lane.__compact_advance();\n");
    }
    emit_carry_advances(loop.activation_input ? "(*__period)" : "__period");
    emit_next_active(loop.activation_input ? "(*__period)" : "__period");
    if (needs_last) {
      hout->append(loop.activation_input ? "      __last = __period;\n" : "      __last = &__period;\n");
    }
    hout->append("    }\n");
    emit_outputs("__last_out", "(*__last)");
    hout->append("    __sync_kids();\n");
    emit_loop_done("advance");
    hout->append("    return __last_out;\n  }\n");
    hout->append(
        "  void reset_cycle(bool zero_uninitialized = false) { LHD_SIM_PRESERVE_LOOP for (auto& lane : lanes) "
        "lane.reset_cycle(zero_uninitialized); __broadcast_valid = false; ++__gen; __compact_publish(); }\n");
    if (vcd_on) {
      hout->append(
          "  void __vcd_hier(vcd::VCDWriter* w, const std::string& s) { LHD_SIM_PRESERVE_LOOP for (std::size_t i = 0; i < "
          "storage_count; ++i) { "
          "lanes[i].__vcd.reset(); lanes[i].__vcd_path.clear(); lanes[i].__vcd_hier(w, s + \"[\" + std::to_string(i) + \"]\"); } "
          "}\n");
      hout->append(
          "  void __vcd_clk(vcd::VCDWriter* w, bool rise) { LHD_SIM_PRESERVE_LOOP for (auto& lane : lanes) lane.__vcd_clk(w, "
          "rise); }\n");
      if (vcd_fakedelay) {
        hout->append(
            "  void __vcd_dump_x(vcd::VCDWriter* w, bool pos, bool root) { LHD_SIM_PRESERVE_LOOP for (auto& lane : lanes) "
            "lane.__vcd_dump_x(w, pos, root); }\n");
      }
      hout->append(
          "  void __vcd_dump_data(vcd::VCDWriter* w, bool pos, bool root) { LHD_SIM_PRESERVE_LOOP for (auto& lane : lanes) "
          "lane.__vcd_dump_data(w, pos, root); }\n");
      // The parent's snapshot barrier recurses into EVERY sub-instance, loop
      // wrappers included, so this forwarder is what keeps `--set sim.vcd=true`
      // compiling on a design with a rolled `for`.
      hout->append(
          "  void __vcd_snapshot(bool pos) { LHD_SIM_PRESERVE_LOOP for (auto& lane : lanes) lane.__vcd_snapshot(pos); }\n");
    }
    const std::string lp = lane_prefix_expr("p", "i");
    hout->append(
        "  void dump_state(const std::string& p, std::map<std::string, std::string>& r, const std::string& d) const { "
        "LHD_SIM_PRESERVE_LOOP for (std::size_t i = 0; i < storage_count; ++i) lanes[i].dump_state(",
        lp,
        ", r, d); }\n");
    hout->append(
        "  void load_state(const std::string& p, const std::map<std::string, std::string>& r, const std::string& d) { "
        "LHD_SIM_PRESERVE_LOOP for (std::size_t i = 0; i < storage_count; ++i) lanes[i].load_state(",
        lp,
        ", r, d); __broadcast_valid = false; ++__gen; }\n");
    hout->append(
        "  std::uint64_t design_hash() const { std::uint64_t h = 14695981039346656037ULL; auto fold = [&](std::uint64_t v) { "
        "LHD_SIM_PRESERVE_LOOP for (int b = 0; b < 8; ++b) { h ^= (v >> (b * 8)) & 0xffU; h *= 1099511628211ULL; } }; fold(count); "
        "LHD_SIM_PRESERVE_LOOP for (const auto& lane : lanes) fold(lane.design_hash()); return h; }\n");
    // sim.tune walkers: the STORED lanes, in ordinal order (a shared stateless
    // lane writes no words). Inline, so they cost nothing unless called.
    hout->append(
        "  void __tune_hash(std::uint64_t*& __lhd_to, bool = false) const { LHD_SIM_PRESERVE_LOOP for (std::size_t i = 0; i < "
        "storage_count; ++i) lanes[i].__tune_hash(__lhd_to); }\n");
    hout->append(
        "  std::size_t __tune_count(bool = false) const { std::size_t n = 0; LHD_SIM_PRESERVE_LOOP for (std::size_t i = 0; i < "
        "storage_count; ++i) n += lanes[i].__tune_count(); return n; }\n");
    hout->append(
        "  void describe_signals(const std::string& p, std::vector<hlop::ckpt::Signal>& v) const { LHD_SIM_PRESERVE_LOOP for "
        "(std::size_t i = 0; i < "
        "storage_count; ++i) "
        "lanes[i].describe_signals(",
        lp,
        ", v); }\n");
    hout->append(
        "  void probe_signals(const std::string& p, std::map<std::string, long>& m) const { LHD_SIM_PRESERVE_LOOP for (std::size_t "
        "i = 0; i < storage_count; "
        "++i) "
        "lanes[i].probe_signals(",
        lp,
        ", m); }\n");
    hout->append(
        "  void observe_signals(const std::string& p, std::map<std::string, std::string>& m) const { LHD_SIM_PRESERVE_LOOP for "
        "(std::size_t i = 0; i < "
        "storage_count; ++i) "
        "lanes[i].observe_signals(",
        lp,
        ", m); }\n");
    hout->append(
        "  bool observe_mem(const std::string& n, long i, std::string& o) const { if (n.empty() || n[0] != '[') return false; "
        "const auto rb = n.find(\"]\"); if (rb == std::string::npos || rb + 1 >= n.size() || n[rb + 1] != '.') return false; "
        "std::size_t ordinal = 0; try { ordinal = static_cast<std::size_t>(std::stoull(n.substr(1, rb - 1))); } catch (...) { "
        "return false; } "
        "return ordinal < storage_count && lanes[ordinal].observe_mem(n.substr(rb + 2), i, o); }\n");
    hout->append("};\n");
  }
  // The hlop::Memory_* type a memory lowers to. ordering="program" additionally
  // needs its per-read-port forwarding prefix as a non-type template argument,
  // emitted as a namespace-scope constexpr array just above the struct.
  auto mem_prefix_name = [](const Mem& m) { return absl::StrCat("__", m.member, "_fwd_upto"); };
  auto mem_type        = [&](const Mem& m) -> std::string {
    const std::string common = absl::StrCat(value_type(m.bits, m.unsign),
                                            ", ",
                                            m.bits,
                                            ", ",
                                            m.size,
                                            ", ",
                                            m.n_rd,
                                            ", ",
                                            m.n_wr,
                                            ", ",
                                            m.n_user_wr,
                                            ", ",
                                            m.wensize);
    switch (m.order) {
      case Mem::Order::fwd    : return absl::StrCat("hlop::Memory_fwd<", common, ">");
      case Mem::Order::none   : return absl::StrCat("hlop::Memory_none<", common, ">");
      case Mem::Order::program: return absl::StrCat("hlop::Memory_program<", common, ", ", mem_prefix_name(m), ">");
      case Mem::Order::old    : break;
    }
    return absl::StrCat("hlop::Memory_old<", common, ">");
  };
  // The write enable a port stages with. The Memory cell's `enable` (pid 4) IS
  // the lane mask: one bit at wensize==1, a wensize-bit vector otherwise. An
  // absent enable pin means every lane writes unconditionally.
  // "This array takes an edge this period": every local gate enable high, and
  // (for a memory whose clock arrives through a port the parent gates) the
  // parent's tick word set. Empty means ungated. ONE definition, because every
  // edge-triggered thing the array does — writes, the whole-array update, the
  // sync-read register — has to be qualified by exactly the same condition.
  auto mem_gate_cond = [&](const Mem& m) -> std::string {
    std::string cond;
    for (const auto& gp : m.clock_guards) {
      absl::StrAppend(&cond, cond.empty() ? "" : " && ", emit_known_true(operand(gp, 1)));
    }
    if (!m.tick_field.empty()) {
      absl::StrAppend(&cond, cond.empty() ? "" : " && ", "__in.", m.tick_field, "__tick");
    }
    return cond;
  };
  auto emit_wen = [&](const Mem& m, const MemPort& wp) -> std::string {
    std::string wen
        = wp.enable.is_invalid() ? absl::StrCat("Slop<", m.wensize, ">::create_integer(-1)") : raw_operand(wp.enable, m.wensize);
    // A GATED CLOCK is a write qualifier: the memory takes an edge only on the
    // reference edges where every gate enable is high, which for a one-tick-is-
    // one-period sim is exactly "write iff the guards are true". Gating the
    // enable rather than skipping the staged write keeps the read port's
    // same-address forwarding consistent with what actually lands.
    const std::string cond = mem_gate_cond(m);
    if (cond.empty()) {
      return wen;
    }
    return absl::StrCat("((", cond, ") ? (", wen, ") : Slop<", m.wensize, ">::create_integer(0))");
  };
  for (const auto& m : mems) {
    if (m.order != Mem::Order::program) {
      continue;
    }
    hout->append(absl::StrCat("inline constexpr std::array<uint16_t, ", m.n_rd, "> ", mem_prefix_name(m), "{"));
    for (int r = 0; r < m.n_rd; ++r) {
      hout->append(absl::StrCat(r ? ", " : "", m.fwd_upto[static_cast<size_t>(r)]));
    }
    hout->append("};  // ordering=\"program\": writes preceding each read port\n");
  }

  // Does this module have a FALL half at all? Only then is `eval_negedge`
  // emitted and called — dino has none, so it keeps exactly one pass per tick.
  bool has_fall = false;
  for (const auto& f : flops) {
    if (!f.is_latch && !f.posedge) {
      has_fall = true;
    }
  }
  for (const auto& s : subs) {
    if (s.negedge_only) {
      has_fall = true;
    }
  }
  const bool has_refresh
      = std::any_of(subs.begin(), subs.end(), [](const auto& s) { return s.refresh_negedge || s.child_refresh_negedge; });
  absl::flat_hash_set<const hhds::Graph*>                        staged_visiting;
  std::function<bool(const std::shared_ptr<hhds::Graph>&, bool)> staged_child_tree;
  staged_child_tree = [&](const std::shared_ptr<hhds::Graph>& pg, bool mixed_edges) {
    if (!pg || !staged_visiting.insert(pg.get()).second) {
      return false;
    }
    bool              staged          = true;
    const std::string child_ref_clock = clock_input_of(pg.get());
    for (const auto node : pg->body().nodes()) {
      const auto op = type_op_of(node);
      if (op == Ntype_op::Memory || (!mixed_edges && op == Ntype_op::Latch)) {
        staged = false;
        break;
      }
      if (is_type_flop(node)) {
        const auto posclk = get_driver(find_sink_pin(node, "posclk"));
        if (!mixed_edges && posclk.is_known_false()) {
          staged = false;
          break;
        }
        const auto clock = get_driver(find_sink_pin(node, "clock_pin"));
        if (clock.is_invalid() || !livehd::graph_util::is_graph_input_pin(clock) || pin_name_of(clock) != child_ref_clock) {
          staged = false;  // derived/secondary clocks cross in a later rung
          break;
        }
        continue;
      }
      if (op != Ntype_op::Sub) {
        continue;
      }
      if (node.subnode_loop()) {
        staged = false;
        break;
      }
      if (auto sio = node.get_subnode_io()) {
        for (const auto& decl : sio->get_input_pin_decls()) {
          if (decl.name != "__valid") {
            continue;
          }
          const auto guard = get_driver(find_sink_pin(node, decl.name));
          if (!mixed_edges && !guard.is_invalid() && (!guard.is_known_true())) {
            staged = false;
          }
          break;
        }
      }
      auto child = node.get_subnode_graph();
      if (!staged || !staged_child_tree(child, mixed_edges)) {
        staged = false;
        break;
      }
    }
    staged_visiting.erase(pg.get());
    return staged;
  };
  const bool staged_bridge_children = std::ranges::all_of(subs, [&](const auto& s) {
    auto child = s.node.get_subnode_graph();
    return !s.loop && child && staged_child_tree(child, false);
  });

  // C++ storage layout by definition node. The occurrence-wide root emitter
  // reaches child state directly, so it must reproduce the exact member names
  // each child definition emits (including collision suffixes). Keep this one
  // deterministic allocator shared by root occurrence access below; it mirrors
  // the category order used above: ports, state, memories, then instances.
  struct Direct_layout {
    absl::flat_hash_map<hhds::Class_index, std::string> member;
  };
  absl::flat_hash_map<const hhds::Graph*, Direct_layout> direct_layout_cache;
  const auto direct_layout = [&](const hhds::Graph* definition) -> const Direct_layout& {
    if (const auto it = direct_layout_cache.find(definition); it != direct_layout_cache.end()) {
      return it->second;
    }
    Direct_layout                    layout;
    absl::flat_hash_set<std::string> used;
    const auto                       reserve_port = [&](std::string_view name) {
      const auto dot = name.find('.');
      used.insert(cpp_id(dot == std::string_view::npos ? name : name.substr(0, dot)));
    };
    if (const auto io = definition->get_io()) {
      for (const auto& decl : io->get_input_pin_decls()) {
        reserve_port(decl.name);
      }
      for (const auto& decl : io->get_output_pin_decls()) {
        reserve_port(decl.name);
      }
    }
    const auto unique = [&](std::string candidate) {
      if (candidate.empty()) {
        candidate = "u";
      }
      std::string name = candidate;
      for (int suffix = 2; !used.insert(name).second; ++suffix) {
        name = absl::StrCat(candidate, "__i", suffix);
      }
      return name;
    };
    for (const auto node : definition->body().nodes()) {
      if (is_type_flop(node) || type_op_of(node) == Ntype_op::Latch) {
        layout.member[node.get_class_index()] = unique(cpp_id(wire_name(node.get_driver_pin(0))));
      }
    }
    for (const auto node : definition->body().nodes()) {
      if (type_op_of(node) == Ntype_op::Memory) {
        layout.member[node.get_class_index()] = unique(cpp_id(default_instance_name(node)));
      }
    }
    for (const auto node : definition->body().nodes()) {
      if (!is_type_sub(node)) {
        continue;
      }
      const auto sub_io = node.get_subnode_io();
      if (!sub_io) {
        continue;
      }
      const std::string name{sub_io->get_name()};
      if (name.empty() || name == livehd::graph_util::lgassert_module_name || name == livehd::graph_util::fproperty_module_name) {
        continue;
      }
      layout.member[node.get_class_index()] = unique(cpp_id(default_instance_name(node)));
    }
    return direct_layout_cache.emplace(definition, std::move(layout)).first->second;
  };
  absl::flat_hash_map<hhds::Occurrence_path, std::string> occurrence_prefix_cache;
  const auto occurrence_prefix = [&](const hhds::Occurrence_node& occurrence) -> const std::string& {
    if (const auto cached = occurrence_prefix_cache.find(occurrence.path()); cached != occurrence_prefix_cache.end()) {
      return cached->second;
    }
    std::string prefix;
    auto*       library = gio ? gio->get_library() : nullptr;
    if (library == nullptr) {
      return occurrence_prefix_cache.emplace(occurrence.path(), std::move(prefix)).first->second;
    }
    for (const auto& step : occurrence.path().steps()) {
      if (step.ordinal) {
        prefix.clear();  // compact loops are excluded from the direct rung
        break;
      }
      const auto parent = library->get_graph(step.subnode.gid);
      if (!parent) {
        prefix.clear();
        break;
      }
      const auto& layout = direct_layout(parent.get());
      const auto  it     = layout.member.find(hhds::Class_index{step.subnode.value});
      if (it == layout.member.end()) {
        prefix.clear();
        break;
      }
      prefix += it->second + ".";
    }
    return occurrence_prefix_cache.emplace(occurrence.path(), std::move(prefix)).first->second;
  };
  const auto occurrence_member = [&](const livehd::sim::Color_plan::Site& site) {
    const auto& layout = direct_layout(site.node.get_graph());
    const auto  it     = layout.member.find(site.node.get_class_index());
    return it == layout.member.end() ? std::string{} : occurrence_prefix(site.node) + it->second;
  };
  const auto occurrence_sub_member = [&](const livehd::sim::Color_plan::Site& site) {
    // An ordinary Sub occurrence's path already includes that Sub step.  State
    // sites live *inside* the path and need occurrence_member() to append their
    // own field, but appending a Sub's local field repeats the instance name
    // (`u.u`).  A root compact-loop control has no path step, so it keeps the
    // local-layout spelling.
    std::string member = occurrence_prefix(site.node);
    if (!member.empty()) {
      member.pop_back();  // trailing '.'
      return member;
    }
    return occurrence_member(site);
  };
  const auto llvm_memory_gate_method_name = [&](const livehd::sim::Color_plan::Site& site) {
    std::string name = absl::StrCat("__llvm_memory_gate_", site.storage_id);
    for (char& c : name) {
      if (!std::isalnum(static_cast<unsigned char>(c))) {
        c = '_';
      }
    }
    return name;
  };
  absl::flat_hash_map<std::pair<hhds::Gid, uint64_t>, Mem> direct_memory_cache;
  const auto direct_memory = [&](const livehd::sim::Color_plan::Site& site) -> const Mem* {
    const auto node = site.node.base_node();
    for (const auto& memory : mems) {
      if (memory.node.get_graph() == node.get_graph() && memory.node.get_class_index() == node.get_class_index()) {
        return &memory;
      }
    }
    const auto key = std::pair{node.get_graph()->get_gid(), static_cast<uint64_t>(node.get_class_index().value)};
    if (const auto it = direct_memory_cache.find(key); it != direct_memory_cache.end()) {
      return &it->second;
    }

    Mem                  memory;
    std::vector<MemPort> ports_by_id;
    memory.node = node;
    for (const auto& msink : node.inp_sorted_pins()) {
      const auto mdrv = msink.get_driver_pin();
      const int  raw  = static_cast<int>(msink.get_port_id());
      const auto name = Ntype::get_sink_name(Ntype_op::Memory, raw);
      const auto port = static_cast<size_t>(raw) / Ntype::Memory_port_stride;
      if (name == "bits") {
        memory.bits = static_cast<int>(const_of(mdrv).to_just_i64());
      } else if (name == "size") {
        memory.size = static_cast<int>(const_of(mdrv).to_just_i64());
      } else if (name == "type") {
        memory.type = static_cast<int>(const_of(mdrv).to_just_i64());
      } else if (name == "fwd") {
        memory.fwd = Dlop::clone(const_of(mdrv));
      } else if (name == "undef") {
        memory.undef = Dlop::clone(const_of(mdrv));
      } else if (name == "update") {
        memory.update = mdrv;
      } else if (name == "update_enable") {
        memory.update_enable = mdrv;
      } else if (name == "reset") {
        memory.reset = mdrv;
      } else if (name == "initial") {
        memory.init = mdrv;
      } else if (name == "wensize") {
        memory.wensize = static_cast<int>(const_of(mdrv).to_just_i64());
      } else {
        if (ports_by_id.size() <= port) {
          ports_by_id.resize(port + 1);
        }
        ports_by_id[port].pid = port;
        if (str_tools::ends_with(name, "clock_pin")) {
          memory.clock = mdrv;
        } else if (str_tools::ends_with(name, "addr")) {
          ports_by_id[port].addr = mdrv;
        } else if (str_tools::ends_with(name, "enable")) {
          ports_by_id[port].enable = mdrv;
        } else if (str_tools::ends_with(name, "din")) {
          ports_by_id[port].din = mdrv;
        } else if (str_tools::ends_with(name, "rdport")) {
          ports_by_id[port].rd = mdrv.is_const() && !const_of(mdrv).is_known_false();
        }
      }
    }
    for (const auto& odrv : node.out_sorted_pins()) {
      if (odrv.get_port_id() == Ntype::Memory_readall_pid) {
        memory.has_read_all = true;
        break;
      }
    }
    memory.bits = std::max(memory.bits, 1);
    if (memory.wensize < 1 || memory.bits % memory.wensize != 0) {
      return nullptr;
    }
    int n_write = 0;
    for (const auto& port : ports_by_id) {
      n_write += !port.addr.is_invalid() && !port.rd;
    }
    int read  = 0;
    int write = 0;
    for (auto& port : ports_by_id) {
      if (port.addr.is_invalid() && port.din.is_invalid() && port.enable.is_invalid()) {
        continue;
      }
      if (port.rd) {
        port.dout_pid = n_write + read;
        port.rdidx    = read++;
      } else {
        port.wridx = write++;
      }
      memory.ports.push_back(port);
    }
    memory.n_wr           = write;
    memory.n_rd           = read;
    const auto row_prefix = [&](const spool_ptr<Dlop>& matrix, int row) {
      if (!matrix) {
        return 0;
      }
      int prefix = 0;
      while (prefix < memory.n_wr && matrix->bit_test(row * memory.n_wr + prefix)) {
        ++prefix;
      }
      for (int bit = prefix; bit < memory.n_wr; ++bit) {
        if (matrix->bit_test(row * memory.n_wr + bit)) {
          return -1;
        }
      }
      return prefix;
    };
    if (!memory.registered()) {
      memory.order     = Mem::Order::fwd;
      memory.n_user_wr = memory.n_wr;
    } else {
      std::vector<int> fwd_upto(static_cast<size_t>(memory.n_rd), 0);
      int              max_fwd   = 0;
      int              max_undef = 0;
      bool             uniform   = true;
      for (int row = 0; row < memory.n_rd; ++row) {
        const int fwd   = row_prefix(memory.fwd, row);
        const int undef = row_prefix(memory.undef, row);
        if (fwd < 0 || undef < 0) {
          return nullptr;
        }
        fwd_upto[static_cast<size_t>(row)]  = fwd;
        max_fwd                             = std::max(max_fwd, fwd);
        max_undef                           = std::max(max_undef, undef);
        uniform                            &= row == 0 || fwd == fwd_upto[0];
      }
      if (max_fwd > 0 && max_undef > 0) {
        return nullptr;
      }
      if (max_undef > 0) {
        memory.order     = Mem::Order::none;
        memory.n_user_wr = max_undef;
      } else if (max_fwd == 0) {
        memory.order     = Mem::Order::old;
        memory.n_user_wr = memory.n_wr;
      } else if (uniform) {
        memory.order     = Mem::Order::fwd;
        memory.n_user_wr = max_fwd;
      } else {
        memory.order     = Mem::Order::program;
        memory.n_user_wr = max_fwd;
        memory.fwd_upto  = std::move(fwd_upto);
      }
    }
    infer_memory_unsign(memory);
    memory.member = direct_layout(node.get_graph()).member.at(node.get_class_index());
    return &direct_memory_cache.emplace(key, std::move(memory)).first->second;
  };
  const auto conditional_occurrence = [&](const hhds::Occurrence_node& occurrence) {
    if (occurrence.path().steps().empty() || gio == nullptr || gio->get_library() == nullptr) {
      return false;
    }
    const auto sub = gio->get_library()->get_node(occurrence.path().steps().back().subnode);
    const auto sio = sub.get_subnode_io();
    if (!sio || !sio->has_input("__valid")) {
      return false;
    }
    const auto guard = get_driver(find_sink_pin(sub, "__valid"));
    if (guard.is_invalid() || (guard.is_known_true())) {
      return false;
    }
    // A definition forwarding its own activation is inside the same region,
    // not a nested condition boundary.
    return !livehd::graph_util::is_graph_input_pin(guard) || pin_name_of(guard) != "__valid";
  };
  // The direct rung consumes the plan's execution slots over the complete
  // ordinary occurrence tree. Unsupported state protocols fail closed here;
  // no state class is silently approximated by the retired module scheduler.
  const bool staged_flat            = staged_bridge_children && mems.empty() && !has_fall && !has_refresh
                                      && std::ranges::all_of(flops, [](const auto& f) { return !f.is_latch && f.posedge; });
  bool       direct_color_supported = color_root;
  if (direct_color_supported) {
    const bool  color_debug      = std::getenv("LIVEHD_SIM_COLOR_DEBUG") != nullptr;
    const auto& hierarchy_clocks = hierarchy_clocks_for_root();
    for (const auto& color : color_plan_->colors()) {
      for (const size_t member : color.members) {
        const auto& version = color_plan_->version_sites()[member];
        const auto& site    = color_plan_->sites()[version.base_site];
        const auto  op      = type_op_of(site.node.base_node());
        if (op == Ntype_op::Sub) {
          bool supported = !occurrence_sub_member(site).empty();
          if (site.kind == livehd::sim::Color_plan::Site_kind::loop_control) {
            supported &= site.node.base_node().is_loop_subnode();
          } else if (site.kind == livehd::sim::Color_plan::Site_kind::instance) {
            bool has_output = false;
            if (const auto sio = site.node.base_node().get_subnode_io()) {
              has_output = std::ranges::any_of(sio->get_output_pin_decls(),
                                               [&](const auto& decl) { return decl.port_id == version.output_port; });
            }
            supported &= version.role == livehd::sim::Color_plan::Version_role::data && has_output;
          } else {
            supported = false;
          }
          if (color_debug && !supported) {
            const std::string graph_name = site.node.get_graph() == nullptr ? "?" : std::string(site.node.get_graph()->get_name());
            const std::string instance_name = livehd::graph_util::has_name(site.node.base_node())
                                                  ? std::string(livehd::graph_util::node_name_of(site.node.base_node()))
                                                  : "?";
            std::string       output_name{"?"};
            if (const auto sio = site.node.base_node().get_subnode_io()) {
              for (const auto& decl : sio->get_output_pin_decls()) {
                if (decl.port_id == version.output_port) {
                  output_name = decl.name;
                  break;
                }
              }
            }
            std::fprintf(stderr,
                         "[color-direct] unsupported sub version role=%u depth=%zu loop=%s graph=%s instance=%s output=p%u:%s\n",
                         static_cast<unsigned>(version.role),
                         site.node.path().steps().size(),
                         site.node.base_node().is_loop_subnode() ? "yes" : "no",
                         graph_name.c_str(),
                         instance_name.c_str(),
                         static_cast<unsigned>(version.output_port),
                         output_name.c_str());
          }
          direct_color_supported &= supported;
          continue;
        }
        if (version.role == livehd::sim::Color_plan::Version_role::state_read
            || version.role == livehd::sim::Color_plan::Version_role::state_update) {
          const bool state_shape_supported
              = (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch || op == Ntype_op::Memory)
                && !occurrence_member(site).empty();
          if (color_debug && !state_shape_supported) {
            std::fprintf(stderr,
                         "[color-direct] unsupported state role=%u op=%u depth=%zu member=%s\n",
                         static_cast<unsigned>(version.role),
                         static_cast<unsigned>(op),
                         site.node.path().steps().size(),
                         occurrence_member(site).empty() ? "no" : "yes");
          }
          direct_color_supported &= state_shape_supported;
          if (op == Ntype_op::Memory) {
            const auto* memory = direct_memory(site);
            if (color_debug && (memory == nullptr || !memory->registered())) {
              std::fprintf(stderr,
                           "[color-direct] unsupported memory depth=%zu found=%s registered=%s\n",
                           site.node.path().steps().size(),
                           memory != nullptr ? "yes" : "no",
                           memory != nullptr && memory->registered() ? "yes" : "no");
            }
            direct_color_supported &= memory != nullptr && memory->registered();
          }
          if (op == Ntype_op::Latch) {
            const auto& local_clocks = local_clocks_for(site.node.get_graph());
            const auto  commit       = livehd::latch_contract::commit_class_of(site.node.base_node(), &local_clocks);
            if (!commit.has_value()) {
              direct_color_supported = false;
            }
          } else if (op != Ntype_op::Memory) {
            const auto clock = livehd::latch_contract::sink_driver_hier(site.node, "clock_pin");
            if (!clock.is_invalid()) {
              const auto& local_clocks = local_clocks_for(site.node.get_graph());
              if (type_op_of(clock.get_master_node().base_node()) == Ntype_op::Clock_cell) {
                const auto reference    = livehd::latch_contract::sink_driver_hier(clock.get_master_node(), "clk_ref");
                const auto root         = livehd::latch_contract::control_root(reference);
                direct_color_supported &= !root.net.is_invalid() && hierarchy_clocks.is_clock(root.net);
              } else if (const auto gate = livehd::latch_contract::clock_op_of(clock.base_pin(), local_clocks)) {
                const auto root         = livehd::latch_contract::control_root(clock);
                direct_color_supported &= gate->div == 1 && !root.net.is_invalid() && hierarchy_clocks.is_clock(root.net);
              } else {
                const auto root         = livehd::latch_contract::control_root(clock);
                direct_color_supported &= !root.net.is_invalid() && hierarchy_clocks.is_clock(root.net);
              }
            }
          }
          continue;
        }
        auto output = site.node.base_node().get_driver_pin(0);
        if (output.is_invalid()) {
          for (const auto& candidate : site.node.base_node().out_pins()) {
            output = candidate;
            break;
          }
        }
        direct_color_supported &= !output.is_invalid();
        if (color_debug && output.is_invalid()) {
          std::fprintf(stderr,
                       "[color-direct] unsupported value role=%u op=%u depth=%zu\n",
                       static_cast<unsigned>(version.role),
                       static_cast<unsigned>(op),
                       site.node.path().steps().size());
        }
      }
    }
  }
  const bool color_runtime_root = direct_color_supported && color_plan_->summary().versioning_complete
                                  && color_plan_->summary().version_dag_acyclic && color_plan_->summary().color_dag_acyclic
                                  && color_plan_->summary().boundary_one_writer && color_plan_->summary().boundary_dominance;

  if (std::getenv("LIVEHD_SIM_COLOR_DEBUG") != nullptr && color_root) {
    std::fprintf(stderr,
                 "[color-direct] module=%s supported=%s runtime=%s versions=%zu colors=%zu\n",
                 gname.c_str(),
                 direct_color_supported ? "yes" : "no",
                 color_runtime_root ? "yes" : "no",
                 color_plan_->version_sites().size(),
                 color_plan_->colors().size());
  }

  if (color_root) {
    hout->append(absl::StrCat("// color-direct eligible=",
                              color_runtime_root ? "true" : "false",
                              " staged=",
                              staged_flat ? "true" : "false",
                              " subs=",
                              subs.size(),
                              " vcd=",
                              vcd_file.empty() ? "false" : "true",
                              " local-shape=",
                              direct_color_supported ? "true" : "false",
                              " versioning=",
                              color_plan_->summary().versioning_complete ? "true" : "false",
                              " version-dag=",
                              color_plan_->summary().version_dag_acyclic ? "true" : "false",
                              " color-dag=",
                              color_plan_->summary().color_dag_acyclic ? "true" : "false",
                              " one-writer=",
                              color_plan_->summary().boundary_one_writer ? "true" : "false",
                              " dominance=",
                              color_plan_->summary().boundary_dominance ? "true" : "false",
                              " random=",
                              color_plan_->summary().runtime_random ? "true" : "false",
                              "\n"));
    if (!color_runtime_root) {
      std::string plan_hint;
      if (!color_plan_->errors().empty()) {
        plan_hint = color_plan_->errors().front();
        // Keep the terminal diagnostic actionable without dumping the full
        // multi-line cycle witness. The complete ring remains in the generated
        // color-plan report.
        //
        // Match "; cycle", not "; cycle-length=": when the residual DFS found
        // no back edge the summary is "; cycle=NONE FOUND …", and anchoring on
        // the length field left the whole blocked-witness list in the hint.
        if (const auto witnesses = plan_hint.find(" witnesses="); witnesses != std::string::npos) {
          if (const auto summary = plan_hint.find("; cycle", witnesses); summary != std::string::npos) {
            plan_hint.erase(witnesses, summary - witnesses);
          }
        }
        if (const auto witness = plan_hint.find(" cycle=\n"); witness != std::string::npos) {
          plan_hint.resize(witness);
        }
        // cycle-boundaries= is one line per hierarchy crossing over the WHOLE
        // ring and survives the cut above by design. On the designs this exists
        // for that ring is millions of hops, so cap what reaches a terminal.
        constexpr size_t kMaxHintBytes = 4096;
        if (plan_hint.size() > kMaxHintBytes) {
          plan_hint.resize(kMaxHintBytes);
          plan_hint += " ... (truncated; see the generated .color-plan.txt report)";
        }
      } else if (!direct_color_supported) {
        plan_hint = "the selected hierarchy contains a state or operation protocol without a direct color kernel";
      } else {
        plan_hint = "inspect the generated .color-plan.txt report";
      }
      livehd::diag::err("inou.cgen.sim", "color-plan-not-lowerable", "unsupported")
          .msg("module `{}` cannot be lowered by the occurrence-wide color scheduler", gname)
          .hint(plan_hint)
          .emit();
      return;
    }
  }

  // The direct emitter visits these relations once while planning generated
  // source. Keeping the indices here avoids rescanning every boundary slot for
  // every color/member (a quadratic code-generation cost on large designs).
  std::vector<std::vector<size_t>>                                    direct_consumed_slots;
  std::vector<std::vector<size_t>>                                    direct_produced_slots;
  std::vector<std::vector<size_t>>                                    direct_state_current_slots;
  std::vector<std::vector<const livehd::sim::Color_plan::Value_use*>> direct_value_uses_by_consumer;
  using Direct_boundary_binding = std::pair<size_t, const livehd::sim::Color_plan::Boundary_consumer*>;
  std::vector<std::vector<Direct_boundary_binding>> direct_boundary_bindings_by_consumer;
  std::vector<std::string>                          direct_slot_storage;
  // The READ spelling of each slot. Keep Slop_u storage as Slop_u at reads so
  // mixed HLOP operations retain the unsigned type and consume its carrier
  // mask-free.
  std::vector<std::string>                          direct_slot_read;
  // Per slot: is its STORAGE the canonical-unsigned Slop_u? The kernel ABI
  // type-puns a slot address through `void**`, so the binding site and the
  // kernel body must name the SAME type -- this is what keeps them in step.
  std::vector<bool>                                 direct_slot_is_u;
  std::vector<std::string>                          direct_input_prev_storage;
  // Root input ports sliced into MANY boundary slots (XiangShan's RenameTable
  // splits a 10,400-bit commit bundle into 3,640 lanes): one whole-port compare
  // gates the per-slot refresh, so an idle wide port costs one memcmp per
  // period instead of one extract+compare per lane. port_id -> prev storage.
  absl::flat_hash_map<uint32_t, std::string>        direct_port_gate_storage;
  std::vector<std::string>                          direct_port_gate_decls;
  // Wide (versioned) inputs of a non-DUT color root: their writers bump
  // `__ver_<field>`, so the refresh compares one counter instead of the bus
  // (an LLVM-mode loop body re-scanned its 7,800-bit invariant every period).
  absl::flat_hash_map<uint32_t, std::string>        direct_port_ver_storage;
  // Slots are pooled into one array per storage TYPE, so the key carries the
  // canonical-unsigned bit as well as the width: `Slop_u<7>` and `Slop<8>` have
  // the same carrier layout but are different C++ types and cannot share an
  // array. With sim.slop_u off the bool is always false and the pooling is
  // exactly what it was.
  std::map<std::pair<uint32_t, bool>, size_t>       direct_slot_width_counts;
  // Previous-input shadows use the same concrete type as __in. Besides making
  // compare-on-write exact, this avoids converting every canonical input back
  // into a lazy same-width Slop solely for dirty detection.
  std::map<std::pair<uint32_t, bool>, size_t>       direct_input_width_counts;
  size_t                                            direct_state_commit_count = 0;
  // Version-site IDs are sparse across reads/data/updates. Give only state
  // updates dense flag storage so the per-cycle clear touches the useful set.
  std::vector<size_t>                               direct_state_commit_flag_of_member;
  std::vector<bool>                                 direct_random_color;
  if (color_runtime_root) {
    direct_consumed_slots.resize(color_plan_->colors().size());
    direct_produced_slots.resize(color_plan_->version_sites().size());
    direct_state_current_slots.resize(color_plan_->sites().size());
    direct_value_uses_by_consumer.resize(color_plan_->version_sites().size());
    direct_boundary_bindings_by_consumer.resize(color_plan_->version_sites().size());
    direct_slot_storage.resize(color_plan_->boundary_slots().size());
    direct_slot_read.resize(color_plan_->boundary_slots().size());
    direct_slot_is_u.assign(color_plan_->boundary_slots().size(), false);
    direct_input_prev_storage.resize(color_plan_->boundary_slots().size());
    direct_state_commit_flag_of_member.assign(color_plan_->version_sites().size(), livehd::sim::Color_plan::invalid_index);
    // A slot is written ONCE per settle and read by every consumer of the
    // boundary, which is precisely the shape lazy Slop masking loses on: the
    // write pays nothing and each read re-masks. Under sim.slop_u an UNSIGNED
    // slot is stored canonical instead, so the mask moves to the single write.
    // (Signed slots keep the lazy Slop: they have a real sign bit, and the lazy
    // contract is the right one for them.)
    const auto slot_is_canonical
        = [&](const livehd::sim::Color_plan::Boundary_slot& s) { return slop_u_ && s.unsign && s.width > 0; };
    // The shared-kernel ABI is a void* type-pun: the cast the kernel emits has
    // to name the SAME type the binding site took the address of. For a
    // color_value slot that object is the runtime array declared right here, so
    // the canonical flag is the whole answer. Every OTHER kind binds a member
    // somebody else declared -- a flop member, an `__in`/`__out` field, a
    // sub-instance's field -- and all of those went through
    // `value_type(bits, unsign)`. This flag used to be written only inside the
    // color_value branch below, so those kinds all reported `Slop<W>` while the
    // binding handed over a `Slop_u<W>`: two unrelated class types aliased
    // through void* (and a real SIZE mismatch at width%64==0, where Slop_u<64>
    // carries a 2-word Slop<65> and Slop<64> is one word).
    //
    // Resolve each kind against the declaration that actually produced the
    // storage, NOT against `slot.unsign`: a current-view state slot can
    // legitimately carry `use.unsign != physical_unsign` while direct_read_expr
    // still hands back the bare member.
    const auto top_io_unsign = [&](hhds::Port_id port, bool want_input) {
      for (const auto& io : ios) {
        if (io.is_input == want_input && io.port_id == static_cast<uint32_t>(port)) {
          return io.unsign;
        }
      }
      return false;
    };
    const auto sub_io_unsign = [&](const livehd::sim::Color_plan::Boundary_slot& s, bool want_input) {
      if (s.owner_site == livehd::sim::Color_plan::invalid_index) {
        return false;
      }
      const auto io = color_plan_->sites()[s.owner_site].node.get_graph()->get_io();
      if (!io) {
        return false;
      }
      if (want_input) {
        for (const auto& decl : io->get_input_pin_decls()) {
          if (decl.port_id == s.public_port) {
            return decl.unsign;
          }
        }
        return false;
      }
      for (const auto& decl : io->get_output_pin_decls()) {
        if (decl.port_id == s.public_port) {
          return decl.unsign;
        }
      }
      return false;
    };
    const auto slot_storage_is_u = [&](const livehd::sim::Color_plan::Boundary_slot& s) {
      if (!slop_u_ || s.width == 0) {
        return false;
      }
      using Bkind = livehd::sim::Color_plan::Boundary_kind;
      switch (s.kind) {
        case Bkind::color_value       : return s.unsign;
        case Bkind::top_input         : return top_io_unsign(s.public_port, true);
        case Bkind::top_output        : return top_io_unsign(s.public_port, false);
        case Bkind::observation_input : return sub_io_unsign(s, true);
        case Bkind::observation_output: return sub_io_unsign(s, false);
        // Both state views bind the SAME declaration: `<member>` for the
        // current value and `<member>_din` for the pending one, and both were
        // declared by value_type(bits, unsign) off the Q pin.
        case Bkind::state_current     :
        case Bkind::state_pending     : {
          if (s.owner_site == livehd::sim::Color_plan::invalid_index) {
            return false;
          }
          const auto pin = color_plan_->sites()[s.owner_site].node.base_node().get_driver_pin(s.producer_port);
          return !pin.is_invalid() && is_unsign(pin);
        }
        default: return false;
      }
    };
    for (size_t slot_index = 0; slot_index < color_plan_->boundary_slots().size(); ++slot_index) {
      const auto& slot             = color_plan_->boundary_slots()[slot_index];
      direct_slot_is_u[slot_index] = slot_storage_is_u(slot);
      if (slot.kind == livehd::sim::Color_plan::Boundary_kind::color_value) {
        const bool   canonical          = slot_is_canonical(slot);
        const size_t position           = direct_slot_width_counts[{slot.width, canonical}]++;
        direct_slot_storage[slot_index] = canonical ? absl::StrCat("__rt.__color_slot_u", slot.width, "[", position, "]")
                                                    : absl::StrCat("__rt.__color_slot_", slot.width, "[", position, "]");
        direct_slot_read[slot_index]    = direct_slot_storage[slot_index];
        I(direct_slot_is_u[slot_index] == canonical);  // slot_storage_is_u agrees on this kind
      }
      if (slot.kind == livehd::sim::Color_plan::Boundary_kind::top_input) {
        const bool   canonical                = slot_is_canonical(slot);
        const size_t position                 = direct_input_width_counts[{slot.width, canonical}]++;
        direct_input_prev_storage[slot_index] = canonical
                                                    ? absl::StrCat("__rt.__color_input_prev_u", slot.width, "[", position, "]")
                                                    : absl::StrCat("__rt.__color_input_prev_", slot.width, "[", position, "]");
      }
      if (slot.producer_version != livehd::sim::Color_plan::invalid_index) {
        I(slot.producer_version < direct_produced_slots.size());
        direct_produced_slots[slot.producer_version].push_back(slot_index);
      }
      if (slot.kind == livehd::sim::Color_plan::Boundary_kind::state_current) {
        I(slot.owner_site != livehd::sim::Color_plan::invalid_index && slot.owner_site < direct_state_current_slots.size());
        direct_state_current_slots[slot.owner_site].push_back(slot_index);
      }
      absl::flat_hash_set<size_t> seen_colors;
      for (const auto& consumer : slot.consumers) {
        if (consumer.version_site != livehd::sim::Color_plan::invalid_index) {
          I(consumer.version_site < direct_boundary_bindings_by_consumer.size());
          direct_boundary_bindings_by_consumer[consumer.version_site].emplace_back(slot_index, &consumer);
        }
        if (consumer.color == livehd::sim::Color_plan::invalid_index || !seen_colors.insert(consumer.color).second) {
          continue;
        }
        I(consumer.color < direct_consumed_slots.size());
        direct_consumed_slots[consumer.color].push_back(slot_index);
      }
    }
    for (const auto& use : color_plan_->value_uses()) {
      I(use.consumer_version < direct_value_uses_by_consumer.size());
      direct_value_uses_by_consumer[use.consumer_version].push_back(&use);
    }
    // Unknown literal bits do not make a color random: sim.unknown_zero
    // replaces them with zero before emitting the C++, and the default random
    // fill is drawn ONCE per literal (a `static const` inside the emitted
    // expression), so the value is constant across periods either way.
    // Keep this classification aligned with Color_plan::runtime_random so
    // stable constant-only colors participate in ordinary dirty gating.
    direct_random_color.assign(color_plan_->colors().size(), false);
    for (size_t color_index = 0; color_index < color_plan_->colors().size(); ++color_index) {
      for (const size_t member : color_plan_->colors()[color_index].members) {
        if (color_plan_->version_sites()[member].role == livehd::sim::Color_plan::Version_role::state_update) {
          I(direct_state_commit_flag_of_member[member] == livehd::sim::Color_plan::invalid_index);
          direct_state_commit_flag_of_member[member] = direct_state_commit_count++;
        }
        const auto& site = color_plan_->sites()[color_plan_->version_sites()[member].base_site];
        if (type_op_of(site.node.base_node()) == Ntype_op::Memory) {
          if (const auto* memory = direct_memory(site); memory != nullptr && memory->order == Mem::Order::none) {
            direct_random_color[color_index] = true;
          }
        }
      }
    }
  }

  // A large occurrence plan can have only a few hundred colors, with thousands
  // of straight-line operations in each. Keeping all cases in the root module
  // .cpp creates one dominant host-compiler action. Partition contiguous color
  // ranges once the evaluator exceeds one target shard; the dispatcher and
  // runtime remain single-copy.
  std::vector<std::pair<size_t, size_t>> direct_color_eval_shards;
  constexpr size_t                       kTargetMembersPerShard = 16384;
  if (color_runtime_root && color_plan_->version_sites().size() > kTargetMembersPerShard) {
    size_t begin   = 0;
    size_t members = 0;
    for (size_t color = 0; color < color_plan_->colors().size(); ++color) {
      const size_t color_members = color_plan_->colors()[color].members.size();
      if (color != begin && (members + color_members > kTargetMembersPerShard || color - begin >= 256)) {
        direct_color_eval_shards.emplace_back(begin, color);
        begin   = color;
        members = 0;
      }
      members += color_members;
    }
    if (begin < color_plan_->colors().size()) {
      direct_color_eval_shards.emplace_back(begin, color_plan_->colors().size());
    }
  }

  // Schedule functions follow execution order and never cross a phase barrier.
  // Their bodies share the evaluator translation units, keeping both the
  // optimizer's function size and the number of repeated header parses bounded.
  std::vector<std::vector<size_t>> direct_run_shards;
  if (!direct_color_eval_shards.empty()) {
    const auto& ordered = color_plan_->colors_in_execution_order();
    for (size_t slot = 0; slot < 5; ++slot) {
      for (const auto color : ordered) {
        if (static_cast<size_t>(color_plan_->colors()[color].slot) != slot) {
          continue;
        }
        if (direct_run_shards.empty() || direct_run_shards.back().size() >= 256
            || color_plan_->colors()[direct_run_shards.back().front()].slot != color_plan_->colors()[color].slot) {
          direct_run_shards.emplace_back();
        }
        direct_run_shards.back().push_back(color);
      }
    }
  }

  // Bound the native compiler's binding-initializer functions as well as the
  // circuit evaluators. Thousands of vector initializers in one function are
  // expensive to optimize even when each circuit color is small.
  std::vector<std::vector<size_t>> direct_binding_shards;
  if (color_runtime_root && !llvm_backend_) {
    std::vector<size_t> binding_colors;
    for (const auto& kernel : color_plan_->kernel_classes()) {
      if (kernel.colors.size() > 1) {
        binding_colors.insert(binding_colors.end(), kernel.colors.begin(), kernel.colors.end());
      }
    }
    if (binding_colors.size() > 512) {
      std::ranges::sort(binding_colors);
      for (size_t begin = 0; begin < binding_colors.size(); begin += 128) {
        direct_binding_shards.emplace_back(binding_colors.begin() + begin,
                                           binding_colors.begin() + std::min(begin + 128, binding_colors.size()));
      }
    }
  }

  struct Direct_commit_shard {
    livehd::sim::Color_plan::Execution_slot slot;
    std::vector<size_t>                     members;
  };
  std::vector<Direct_commit_shard> direct_commit_shards;
  if (color_runtime_root && direct_state_commit_count > 512) {
    // Small shards let clock-gated designs skip inactive state scans while
    // keeping generated translation units cheap to compile.
    constexpr size_t kTargetCommitMembersPerShard = 64;
    for (const auto slot :
         {livehd::sim::Color_plan::Execution_slot::rise_commit, livehd::sim::Color_plan::Execution_slot::fall_commit}) {
      for (size_t member = 0; member < color_plan_->version_sites().size(); ++member) {
        const auto& version = color_plan_->version_sites()[member];
        if (version.role != livehd::sim::Color_plan::Version_role::state_update
            || livehd::sim::Color_plan::commit_slot_of(version) != slot) {
          continue;
        }
        if (direct_commit_shards.empty() || direct_commit_shards.back().slot != slot
            || direct_commit_shards.back().members.size() >= kTargetCommitMembersPerShard) {
          direct_commit_shards.push_back({slot, {}});
        }
        direct_commit_shards.back().members.push_back(member);
      }
    }
  }
  std::vector<size_t> direct_commit_shard_of_member(color_runtime_root ? color_plan_->version_sites().size() : 0,
                                                    livehd::sim::Color_plan::invalid_index);
  std::vector<size_t> direct_commit_shard_bit_of_member(color_runtime_root ? color_plan_->version_sites().size() : 0,
                                                        livehd::sim::Color_plan::invalid_index);
  for (size_t shard = 0; shard < direct_commit_shards.size(); ++shard) {
    for (size_t bit = 0; bit < direct_commit_shards[shard].members.size(); ++bit) {
      const size_t member                       = direct_commit_shards[shard].members[bit];
      direct_commit_shard_of_member[member]     = shard;
      direct_commit_shard_bit_of_member[member] = bit;
    }
  }

  // Each state element carries its Q and, alongside it, the D it will take at
  // the next edge (`_din`) plus the boolean saying whether that edge fires for
  // it (`_cen`). Both are DERIVED: the settle recomputes them from Q and the
  // inputs every period, so they are deliberately absent from
  // dump_state/load_state/design_hash (reset_cycle()'s trailing settle
  // re-derives them after a checkpoint load).
  //
  // They are members rather than settle-locals because the settle and the
  // commit are separate methods: the commit runs with NO combinational value in
  // scope, so anything it needs — the next value, the ICG enable, a secondary
  // clock's edge test — has to have been computed and parked by the settle.
  std::vector<std::string> flop_decls;
  for (const auto& f : flops) {
    const auto type = value_type(f.bits, f.unsign);
    for (const auto& s : f.stages) {
      flop_decls.push_back(absl::StrCat("  ", type, " ", s, "{};  // pipe stage\n"));
      flop_decls.push_back(absl::StrCat("  ", type, " ", s, "_din{};  // ...its next value\n"));
    }
    flop_decls.push_back(absl::StrCat("  ", type, " ", f.member, "{};  // flop\n"));
    flop_decls.push_back(absl::StrCat("  ", type, " ", f.member, "_din{};  // ...its next value\n"));
    if (!f.clock_guards.empty() || !f.sec_clock.is_invalid()) {
      flop_decls.push_back(absl::StrCat("  bool ", f.member, "_cen = true;  // ...and whether its edge fires this period\n"));
    }
  }
  // A huge flop set goes into base structs of bounded size. clang keeps a
  // constructor's initializer count in an 18-bit field, so the implicit
  // constructor of a class with 2^18+ members silently DROPS every initializer
  // past the wrapped count: xs_rob's Verilog side (275,786 members) ran with
  // `__in.clock__tick` false -- it sits past the wrap -- and no flop ever
  // captured. Each base has its own constructor, so the count per class stays
  // far below the field. The flop members only use value types, so a base can
  // hold them before the module's own nested types exist.
  constexpr size_t         kMaxStateMembersPerClass = size_t{1} << 15;
  std::vector<std::string> state_bases;
  if (flop_decls.size() > kMaxStateMembersPerClass) {
    for (size_t begin = 0; begin < flop_decls.size(); begin += kMaxStateMembersPerClass) {
      state_bases.push_back(absl::StrCat(mod, "__state", state_bases.size()));
      hout->append("struct ", state_bases.back(), " {  // flop state chunk of ", mod, "\n");
      for (size_t index = begin; index < std::min(begin + kMaxStateMembersPerClass, flop_decls.size()); ++index) {
        hout->append(flop_decls[index]);
      }
      hout->append("};\n");
    }
    flop_decls.clear();
  }
  std::string base_clause;
  for (const auto& state_base : state_bases) {
    absl::StrAppend(&base_clause, base_clause.empty() ? " : " : ", ", state_base);
  }
  hout->append("struct ", mod, base_clause, " {\n");
  if (color_runtime_root) {
    // The plan's slots, generations, and bindings are body details.
    // Keeping the nested type incomplete here makes a leaf body edit rewrite
    // generated .cpp files only; a parent sees the same module ABI/header.
    hout->append("  struct __Color_runtime;\n  std::shared_ptr<__Color_runtime> __color_runtime;\n");
  }
  for (const auto& decl : flop_decls) {
    hout->append(decl);
  }
  // Previous-value bit per SECONDARY clock net (2f-latch M6), deduped: every
  // flop on the same net shares one, which is also what makes them commit
  // together on that net's edge.
  {
    absl::flat_hash_set<std::string> emitted;
    for (const auto& f : flops) {
      if (f.prev_member.empty() || !emitted.insert(f.prev_member).second) {
        continue;
      }
      hout->append("  bool ", f.prev_member, "{false};  // previous level of a secondary clock net\n");
    }
  }
  for (const auto& m : mems) {
    hout->append(absl::StrCat("  ",
                              mem_type(m),
                              " ",
                              m.member,
                              "{};  // memory (ordering=",
                              m.order == Mem::Order::fwd       ? "fwd"
                              : m.order == Mem::Order::none    ? "none"
                              : m.order == Mem::Order::program ? "program"
                                                               : "old",
                              ")\n"));
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1) {  // sync read: registered dout
        hout->append(absl::StrCat("  ", value_type(m.bits, m.unsign), " ", m.member, "_q", p.rdidx, "{};  // sync read reg\n"));
        if (color_runtime_root) {
          hout->append(
              absl::StrCat("  ", value_type(m.bits, m.unsign), " ", m.member, "_q", p.rdidx, "_din{};  // staged sync read\n"));
        }
      }
    }
  }
  if (color_runtime_root) {
    for (size_t site_index = 0; site_index < color_plan_->sites().size(); ++site_index) {
      const auto& site = color_plan_->sites()[site_index];
      if (type_op_of(site.node.base_node()) != Ntype_op::Memory) {
        continue;
      }
      const auto* memory = direct_memory(site);
      if (memory == nullptr || !memory->has_reset_stage()) {
        continue;
      }
      hout->append("  ",
                   value_type(memory->bits * memory->size, memory->unsign),
                   " __color_whole_",
                   std::to_string(site_index),
                   "_din{};  // staged whole-array update\n");
      hout->append("  bool __color_whole_",
                   std::to_string(site_index),
                   "_cen = false;  // whole-array update/reset fires at the phase barrier\n");
    }
    if (llvm_backend_) {  // only the LLVM object reaches the qualifier through a callback
      for (const auto& site : color_plan_->sites()) {
        if (type_op_of(site.node.base_node()) != Ntype_op::Memory) {
          continue;
        }
        const auto* memory = direct_memory(site);
        if (memory != nullptr && memory->registered() && (!memory->clock_guards.empty() || !memory->tick_field.empty())) {
          hout->append("  bool ", llvm_memory_gate_method_name(site), "();  // LLVM memory edge qualifier\n");
        }
      }
    }
  }
  for (const auto& s : subs) {
    // Use an elaborated type specifier.  A legal RTL instance may have the
    // same name as its module (`foo foo`); after that member declaration the
    // ordinary identifier `foo` hides the class name for later declarations
    // in the same C++ scope.  `struct foo` keeps looking in the tag namespace,
    // so a second specialized occurrence still compiles.
    hout->append(absl::StrCat("  struct ",
                              s.loop ? s.loop_struct : s.callee_struct,
                              " ",
                              s.inst,
                              s.loop ? ";  // compact loop instance\n" : ";  // sub instance\n"));
  }

  // ---- In / Out ----
  // A tuple/struct-packed port flattens to dotted leaves (`io_in.pc`), which is
  // not a legal C++ member name. Rather than mangle the dot away and make every
  // consumer re-derive the grouping, MIRROR the tuple as a nested struct:
  //
  //     struct In {
  //       struct { Slop<32> instruction{}; Slop<1> isValid{}; } io_in{};
  //     };
  //
  // so the RTL path and the C++ path are the same text — `in.io_in.instruction`
  // — and a testbench addressing `acc.io_in.instruction` needs no translation
  // (inou/prp/prp_sim.cpp). Leaves keep declaration order, which is packed
  // order (first member = most significant), so the aggregate can also be
  // read/written as one value. A port with no dot emits exactly as before.
  auto emit_io_block = [&](bool want_input) {
    std::string open_group;  // tuple prefix currently open, "" = none
    for (const auto& io : ios) {
      if (io.is_input != want_input) {
        continue;
      }
      const auto  dot   = io.raw.find('.');
      std::string group = dot == std::string::npos ? std::string{} : io.raw.substr(0, dot);
      if (group != open_group) {
        if (!open_group.empty()) {
          hout->append("    } ", cpp_id(open_group), "{};\n");  // the group is a MEMBER: escape it too
        }
        if (!group.empty()) {
          hout->append("    struct {\n");
        }
        open_group = group;
      }
      if (group.empty()) {
        const auto type = value_type(io.bits, io.unsign);
        if (want_input && io.raw == "__valid") {
          // Hidden activation is true for standalone use. Parent instances
          // overwrite it from the transported call guard before evaluation.
          hout->append(absl::StrCat("    ", type, " ", io.field, " = ", type, "::create_integer(1);\n"));
        } else {
          hout->append("    ", type, " ", io.field, "{};\n");
        }
        if (want_input && versioned_bits(io.bits)) {
          hout->append("    uint32_t ", ver_path(io.field), " = 0;  // change count, see versioned wide inputs\n");
        }
        if (want_input && clock_in_fields.contains(io.field)) {
          hout->append("    bool ", io.field, "__tick = true;  // did this clock port tick? false == the parent gated it off\n");
        }
      } else {
        // nested leaf: the segment after the group prefix (io.field is
        // "<group>.<leaf>", so take what follows its dot)
        hout->append("      ", value_type(io.bits, io.unsign), " ", io.field.substr(io.field.find('.') + 1), "{};\n");
        if (want_input && versioned_bits(io.bits)) {
          hout->append("      uint32_t __ver_", io.field.substr(io.field.find('.') + 1), " = 0;\n");
        }
      }
    }
    if (!open_group.empty()) {
      hout->append("    } ", cpp_id(open_group), "{};\n");  // the group is a MEMBER: escape it too
    }
  };
  hout->append("  struct In {\n");
  emit_io_block(true);
  hout->append("  };\n  struct Out {\n");
  emit_io_block(false);
  hout->append("  };\n");

  // Persistent input latch. The testbench writes inputs through a ref
  // (`acc.x = v` -> __in.x) and advances with `step()`; internal registers are
  // plain members (`acc.total`) and outputs live in __out below. Keeping __in in
  // the instance (not the driver) means a state copy / checkpoint captures the
  // driven inputs too.
  hout->append("  In __in{};\n");
  // CHANGE-GATED EVALUATION. `__gen` counts observable changes to this
  // instance's world: every __in field write that actually changed the value
  // (callers funnel through slop_update) and every state commit that stored a
  // different value. Each eval method remembers the (__gen, sum of child
  // __gen) pair it last ENTERED with; re-entering with the same pair means
  // nothing this method reads has changed AND its last run reached a fixed
  // point (had it committed a change, __gen would have grown past the stored
  // entry value), so the run is skipped. This is what lets an idle or
  // clock-gated cone quiesce to a couple of integer compares per cycle.
  hout->append("  uint64_t __gen = 1;  // observable-change generation (inputs + state commits)\n");
  {
    std::vector<std::string> forward_members;
    forward_members.reserve(forward_stamps.size());
    for (const auto& entry : forward_stamps) {
      forward_members.push_back(entry.first);
    }
    std::ranges::sort(forward_members);  // flat_hash_map order is not reproducible across runs
    for (const auto& member : forward_members) {
      hout->append("  uint32_t ", member, " = 0;  // version of the input last forwarded to this child field\n");
    }
  }
  if (staged_flat && !subs.empty()) {
    hout->append("  uint64_t __color_kids_before = 0;  // child generation snapshot across pre-rise -> commit\n");
  }
  if (!color_storage_only) {
    hout->append("  uint64_t __done_pos = 0, __kids_pos = 0;\n");
    if (has_fall) {
      hout->append("  uint64_t __done_neg = 0, __kids_neg = 0;\n");
    }
    hout->append("  uint64_t __done_settle = 0, __kids_settle = 0;\n");
  }
  // 2f-sim B: the outputs this instance last computed DURING the period --
  // recorded by cycle() before it commits, so it is the value the output drove
  // while the period ran, computed from the state ENTERING the cycle. That is
  // the contract the query engine publishes (09b-simquery "Outputs and state are
  // one commit apart", `"sampled":"during_period"`), so it must stay pre-commit;
  // __out below is the post-commit twin and the two are deliberately different.
  // DERIVED state: deliberately absent from dump_state/load_state and from
  // design_hash — it is recomputed by the next cycle() and must not change the
  // checkpoint layout or its compatibility hash.
  hout->append("  Out __last_out{};\n");
  // The POST-FALL outputs of the CURRENT committed state: what the design drives
  // once this period's color schedule has completed. A testbench read after
  // `step` observes this post-edge value -- exactly what the
  // old peek(__in) returned, but as a plain member a testbench ref can bind ONCE and
  // hold for the whole run instead of an O(total design state) snapshot/restore
  // per read. Also DERIVED: out of dump_state/load_state/design_hash for the
  // same reason as __last_out (reset_cycle() re-settles it after a load).
  hout->append("  Out __out{};\n");

  // ---- VCD trace state (compile.sim.vcd): traces In, Out, and flop state of
  // EVERY module -- the root instance (the one the driver hands a __vcd_path)
  // lazily opens the writer on its first cycle() and __vcd_hier() shares it down
  // the sub-instance tree, so one VCD carries the whole hierarchy under nested
  // scopes. The clock is the one signal NOT traced as an ordinary io port -- it
  // gets the `__vv_clk` waveform driven by `step`; reset and every other input
  // trace normally (reset is just a poked input now).
  //
  // Each traced instance SNAPSHOTS its values (__vs*) inside its own cycle(),
  // pre-commit -- a sub's cycle() runs (and commits its flops) mid-way through
  // the parent's comb walk, so by the parent's dump point the sub's live members
  // are already one edge ahead; the snapshot preserves this-period semantics.
  // The root then walks the hierarchy in timestamp-ordered phases (the writer
  // hard-rejects a past timestamp): clock edge -> [X window] -> settled data. ----
  struct VcdSig {
    std::string var, vname, accessor, snap, prev;
    int         bits;
    bool        posedge;   // dumped at the posedge or negedge slot of the period
    bool        is_input;  // root: poked at the edge; sub: an ordinary comb net
    bool        unsign;    // snapshot uses the same concrete type as its source
  };
  std::vector<VcdSig> vsig;
  if (vcd_on) {
    int  k   = 0;
    auto add = [&](const std::string& nm, int b, const std::string& acc, bool pe = true, bool is_in = false, bool unsign = false) {
      vsig.push_back({absl::StrCat("__vv", k),
                      (b > 1 ? absl::StrCat(nm, "[", b - 1, ":0]") : nm),
                      acc,
                      absl::StrCat("__vs", k),
                      absl::StrCat("__vp", k),
                      b,
                      pe,
                      is_in,
                      unsign});
      ++k;
    };
    for (const auto& io : ios) {
      if (io.is_input && io.field != clk_field) {
        add(io.field, io.bits, absl::StrCat("__in.", io.field), true, true, io.unsign);  // clock gets the dedicated waveform
      }
    }
    for (const auto& io : ios) {
      if (!io.is_input) {
        add(io.field, io.bits, absl::StrCat("__o.", io.field), true, false, io.unsign);
      }
    }
    for (const auto& f : flops) {
      bool vcd_posedge = f.posedge;
      if (f.is_latch) {
        const auto commit = livehd::latch_contract::commit_class_of(f.node, &design_clocks);
        if (commit.has_value() && commit->role == livehd::latch_contract::Net_role::Clock) {
          const auto enable = get_driver(find_sink_pin(f.node, "enable"));
          const auto root   = livehd::latch_contract::control_root(enable);
          vcd_posedge       = root.inverted;  // transparent-low closes on rise; transparent-high on fall
        }
      }
      add(f.member, f.bits, f.member, vcd_posedge, false, f.unsign);
      for (const auto& s : f.stages) {
        add(s, f.bits, s, vcd_posedge, false, f.unsign);
      }
    }
    // shared_ptr (not unique_ptr) so a DUT struct stays COPYABLE: a hierarchical
    // parent's peek() snapshots each sub-instance by value (`auto _pk = sub;`),
    // which a non-copyable member would break. Sub-instances never get a VCD path
    // (only the root instance does), so their __vcd stays null and the shared
    // copy is harmless; the root instance's own peek move-saves __vcd + clears the
    // path so no spurious trace is written during the state-preserving recompute.
    hout->append("  std::shared_ptr<vcd::VCDWriter> __vcd;\n");
    hout->append("  bool __vcd_trace = false;  // registered in a trace (as root or by a parent's __vcd_hier)\n");
    hout->append("  vcd::VarPtr __vv_clk;\n");
    for (const auto& v : vsig) {
      hout->append(absl::StrCat("  vcd::VarPtr ", v.var, ";\n"));
      hout->append(absl::StrCat("  ", value_type(v.bits, v.unsign), " ", v.snap, "{};  // this-period sample (pre-commit)\n"));
      if (vcd_fakedelay) {
        hout->append(
            absl::StrCat("  ", value_type(v.bits, v.unsign), " ", v.prev, "{};  // last dumped (X-window change detection)\n"));
      }
    }
  }

  // Uniquify the clock waveform LABEL against the traced signal names, so a user
  // signal literally named e.g. "clock" can't duplicate the synthetic clock var
  // at VCD registration (vsig is empty unless tracing, so this is a no-op without
  // VCD).
  auto uniquify_label = [&](const std::string& label) {
    auto taken = [&](const std::string& s) {
      for (const auto& v : vsig) {
        if (v.vname == s) {
          return true;
        }
      }
      return false;
    };
    if (!taken(label)) {
      return label;
    }
    for (int i = 0;; ++i) {
      auto cand = absl::StrCat(label, "_vcd", i);
      if (!taken(cand)) {
        return cand;
      }
    }
  };
  const std::string clk_name_baked = uniquify_label(clk_label);

  // Clock waveform config exists only in a VCD build. An ordinary simulator
  // must not carry per-instance strings/counters for a disabled feature.
  if (vcd_on) {
    // A baked FILE path lands only on the --top module: with the machinery now on
    // every module, baking it everywhere would have each sub-instance lazily open
    // the same file as its own root writer.
    std::string vcd_baked = (vcd_file == "1" || vcd_file == "true" || !is_top) ? std::string{} : vcd_file;
    hout->append(absl::StrCat("  std::string __vcd_path = \"", vcd_baked, "\";\n"));
    hout->append("  unsigned __vcd_tick = 0;       // clock periods elapsed (10 VCD time-units each)\n");
    hout->append(absl::StrCat("  std::string __clk_name = \"", clk_name_baked, "\";\n"));
    hout->append("  unsigned __clk_ratio = 1;      // VCD ticks per clock period\n");
  }

  // ---- method declarations, then close the struct (bodies follow in the .cpp) ----
  if (vcd_on) {
    hout->append("  void __vcd_init();\n");
    hout->append("  void __vcd_hier(vcd::VCDWriter* __w, const std::string& __s);\n");
    hout->append("  void __vcd_clk(vcd::VCDWriter* __w, bool __rise);\n");
    hout->append("  void __vcd_dump_in(vcd::VCDWriter* __w);\n");
    hout->append("  void __vcd_snapshot(bool __pos);  // occurrence-wide pre-commit observation barrier\n");
    hout->append("  void __vcd_publish_period();  // root timestamp-ordered publish after the period settles\n");
    if (vcd_fakedelay) {
      hout->append("  void __vcd_dump_x(vcd::VCDWriter* __w, bool __pos, bool __root);\n");
    }
    hout->append("  void __vcd_dump_data(vcd::VCDWriter* __w, bool __pos, bool __root);\n");
  }
  hout->append("  void reset_cycle(bool zero_uninitialized = false);\n");
  // Compact-loop definitions expose phase-specific body kernels so the one
  // native ordinal task can bind and run each state version without expanding
  // the loop. Ordinary hierarchy definitions expose storage only; their logic
  // belongs exclusively to the root occurrence-wide color DAG.
  // PORTS ARE MEMBERS (`__in`): a caller writes `child.__in.<field> = ...`
  // directly and the eval methods take NO argument — no fresh In per call, no
  // by-value struct copy at each boundary, and every instance's checkpoint
  // dumps the inputs it actually saw (the old In-by-value flow left
  // sub-instance `__in` permanently zero). One deliberate semantic shift: a
  // field a call site does not write holds its LAST value, not zero — closer
  // to hardware, and the direction that made the whole-array In-drop loud
  // instead of silently-zero. This is also the substrate for change-gated
  // evaluation (an unchanged `__in` + unadvanced state can skip a settle).
  if (!color_runtime_root && !color_storage_only && staged_flat) {
    hout->append("  void __color_pre_rise();  // evaluate pre-rise colors and park pending state\n");
    hout->append("  void __color_rise_commit();  // state-only rise action; no combinational evaluation\n");
    hout->append("  void __color_post_fall();  // publish the post-fall output versions\n");
  } else if (!color_runtime_root && !color_storage_only) {
    hout->append("  void __compact_pre_rise();  // rise: comb from pre-edge state, then commit posedge state\n");
  }
  if (color_runtime_root) {
    hout->append("  void __color_prepare_runtime();  // allocate runtime storage and bind shared kernels once\n");
    for (size_t shard = 0; shard < direct_binding_shards.size(); ++shard) {
      hout->append("  void __color_bind_part_", std::to_string(shard), "();\n");
    }
    hout->append("  void __color_refresh_inputs();  // publish root inputs into versioned boundary generations\n");
    hout->append("  void __color_reset_settle();  // publish post-fall colors without advancing state\n");
    hout->append("  void __color_run();  // generated topological color schedule\n");
    hout->append("  void __color_eval(std::size_t color);  // indexed body-private color dispatcher\n");
    for (size_t shard = 0; shard < direct_run_shards.size(); ++shard) {
      hout->append("  void __color_run_part_", std::to_string(shard), "();\n");
    }
    for (size_t shard = 0; shard < direct_color_eval_shards.size(); ++shard) {
      hout->append("  void __color_eval_part_", std::to_string(shard), "(std::size_t color);\n");
    }
    hout->append("  void __color_commit(std::size_t slot);  // commit staged state at a phase barrier\n");
    for (size_t shard = 0; shard < direct_commit_shards.size(); ++shard) {
      hout->append("  void __color_commit_part_", std::to_string(shard), "();\n");
    }
  }
  if (has_fall && !color_runtime_root && !color_storage_only) {
    hout->append("  void __compact_fall();  // fall: the negedge cones, re-read post-rise, then commit them\n");
  }
  if (has_refresh && !color_runtime_root && !color_storage_only) {
    hout->append("  void __compact_refresh();  // refresh nested mixed-edge state after post-rise inputs settle\n");
  }
  // One ROOT clock edge is SETTLE -> COMMIT -> SETTLE. cycle()'s body settles
  // the comb cone from the pre-edge state (computing both `o`/__last_out and
  // every next-state) and commits it. The hierarchy root owns the recursive
  // post-edge settle. A child on a parent-level word cycle passes __finish=true
  // too: its post-commit state-only outputs are the boundary values that break
  // that settle ring. Acyclic children pass false and avoid computing the same
  // post-edge outputs once privately and then again in the root walk.
  //
  // The trailing settle is what makes a testbench cell read possible, and it must NOT be
  // hoisted to the front of the next cycle(): committing a next-state that was
  // settled against the PREVIOUS period's `__in` would make every driven input
  // land one cycle late (`acc.reset = clock < 2` would reset cycles 1-2, not
  // 0-1). Both settles are load-bearing and they settle against different state.
  if (!color_storage_only) {
    hout->append("  const Out& ",
                 compact_kernel_ ? "__compact_advance()" : "cycle(bool __finish = true)",
                 " {  // one clock period (inputs already written into __in)\n");
    if (color_runtime_root) {
      if (!compact_kernel_) {
        hout->append("    (void)__finish;  // selected hierarchy roots always own the complete period\n");
      }
      hout->append("    __color_run();\n");
      hout->append("    return __last_out;\n");
    } else {
      if (staged_flat) {
        hout->append("    __color_pre_rise();\n");
        hout->append("    __color_rise_commit();\n");
      } else {
        hout->append("    __compact_pre_rise();\n");
      }
      if (has_fall) {
        hout->append("    __compact_fall();\n");
      }
      hout->append("    return __last_out;  // the during-period outputs the rise recorded\n");
    }
    hout->append("  }\n");
    // The testbench writes __in fields directly (no compare-on-write), so step()
    // force-bumps __gen: the top instance always evaluates; gating lives at the
    // instance boundaries below it.
    if (compact_kernel_) {
      hout->append("  void __compact_publish() { ",
                   color_runtime_root ? "__color_reset_settle" : (staged_flat ? "__color_post_fall" : "__compact_post_fall"),
                   "();");
      if (has_refresh && !color_runtime_root) {
        hout->append(" __compact_refresh();");
      }
      hout->append(" }\n");
    } else {
      hout->append("  void step() { ++__gen; cycle(); }  // drive __in, then advance one clock\n");
    }
  }
  if (!color_runtime_root && !color_storage_only && staged_flat) {
    // __compact_publish() above directly calls the staged post-fall body.
  } else if (!color_runtime_root && !color_storage_only) {
    hout->append("  void __compact_post_fall();  // publish CURRENT committed-state outputs without another advance\n");
  }
  // Editable, name-keyed checkpoint (sim_checkpoint_debug_plan): flops/regs ->
  // the `_r` map keyed by the hierarchical `_p`+member name (pyrope literal),
  // memories -> one `<_dir>/<_p><member>.hex` file each, sub-instances recursed.
  // design_hash folds member names+widths (cross-version warn, never reject).
  hout->append(
      "  void dump_state(const std::string& _p, std::map<std::string, std::string>& _r, const std::string& _dir) const;\n");
  hout->append(
      "  void load_state(const std::string& _p, const std::map<std::string, std::string>& _r, const std::string& _dir);\n");
  hout->append("  std::uint64_t design_hash() const;\n");
  // sim.tune state walkers (sim_profile.md §6.1): one canonical word per stored
  // state element, pre-order through the sub-instances, then -- only when the
  // caller says this instance is the DUT root -- the driven inputs. Declared and
  // defined for EVERY module, whatever the build flavour, so the text is
  // identical across tune vectors and profiling on/off.
  hout->append("  void        __tune_hash(std::uint64_t*& __lhd_to, bool __lhd_root = false) const;\n");
  hout->append("  std::size_t __tune_count(bool __lhd_root = false) const;\n");
  if (executable_root) {
    // The root's support tables and source hasher, defined in <stem>.tune.cpp.
    hout->append("  static const __lhd_tune_support& __tune_support();\n");
    hout->append("  void __tune_sources(std::uint64_t* __lhd_to) const;\n");
  }
  // Observability (lhd sim --list-signals / --probe / --break-when): every scalar
  // signal (flop / pipe stage / sync-read reg / input) by hierarchical name.
  hout->append("  void describe_signals(const std::string& _p, std::vector<hlop::ckpt::Signal>& _v) const;\n");
  hout->append("  void probe_signals(const std::string& _p, std::map<std::string, long>& _m) const;\n");
  // 2f-sim B: the LOSSLESS observation surface the query engine reads. Same walk
  // as probe_signals, but (a) values are full-width canonical hex instead of
  // to_i64_low()'s silent low-64 truncation, and (b) module OUTPUTS are included,
  // served from the last computed Out (recorded at the end of cycle(), so an
  // output read costs nothing — never the O(total state) snapshot/restore of a
  // peek()). `observe_mem` reads ONE committed memory word by member name.
  hout->append("  void observe_signals(const std::string& _p, std::map<std::string, std::string>& _m) const;\n");
  hout->append("  bool observe_mem(const std::string& _n, long _i, std::string& _o) const;\n");
  hout->append("};\n");

  // ---- Dirty bitset ------------------------------------------------------
  // Color activation flags are bits in execution order, 64 per word, not one
  // bool per color: a schedule function can then skip a whole idle 64-color
  // group with one word test (minion walked 11,659 flags per period, 28% of
  // its cycle), and a commit function ORs its marks into a local per word
  // instead of storing the same dirty bytes once per committed flop.
  std::vector<size_t> direct_color_pos(color_plan_ != nullptr ? color_plan_->colors().size() : 0, 0);
  if (color_plan_ != nullptr) {
    const auto& ordered = color_plan_->colors_in_execution_order();
    for (size_t pos = 0; pos < ordered.size(); ++pos) {
      direct_color_pos[ordered[pos]] = pos;
    }
  }
  const size_t direct_dirty_words = (direct_color_pos.size() + 63) / 64;
  const auto   dirty_word         = [&](size_t color) { return direct_color_pos[color] / 64; };
  const auto   dirty_mask = [&](size_t color) { return absl::StrCat("(uint64_t{1} << ", direct_color_pos[color] % 64, ")"); };
  // While a commit function runs, marks accumulate into `__dacc_<word>` locals
  // that the function flushes once; the set records the words it touched.
  std::optional<absl::flat_hash_set<size_t>> dirty_accumulator;
  const auto                                 dirty_mark = [&](size_t color) -> std::string {
    if (dirty_accumulator) {
      dirty_accumulator->insert(dirty_word(color));
      return absl::StrCat("__dacc_", dirty_word(color), " |= ", dirty_mask(color), ";");
    }
    return absl::StrCat("__rt.__color_dirty[", dirty_word(color), "] |= ", dirty_mask(color), ";");
  };
  const auto dirty_test
      = [&](size_t color) { return absl::StrCat("(__rt.__color_dirty[", dirty_word(color), "] & ", dirty_mask(color), ") != 0"); };
  const auto dirty_clear
      = [&](size_t color) { return absl::StrCat("__rt.__color_dirty[", dirty_word(color), "] &= ~", dirty_mask(color), ";"); };

  if (color_runtime_root) {
    const std::string runtime_header_name = fstem + ".color-runtime.hpp";
    auto              runtime_header      = open_out(runtime_header_name);
    runtime_header->append("// Generated simulator color runtime. Do not edit.\n#pragma once\n");
    runtime_header->append("#include \"", fstem, ".hpp\"\n");
    runtime_header->append("\n");
    runtime_header->append("struct ", mod, "::__Color_runtime {\n");
    runtime_header->append(
        "  void* owner = nullptr;\n"
        "  bool __color_input_changed = false;\n"
        "  bool __color_quiescent = false;\n");
    for (const auto& [key, count] : direct_slot_width_counts) {
      const auto& [width, canonical] = key;
      runtime_header->append(canonical
                                 ? absl::StrCat("  std::array<Slop_u<", width, ">, ", count, "> __color_slot_u", width, "{};\n")
                                 : absl::StrCat("  std::array<Slop<", width, ">, ", count, "> __color_slot_", width, "{};\n"));
    }
    for (const auto& [key, count] : direct_input_width_counts) {
      const auto& [width, canonical] = key;
      runtime_header->append(
          canonical ? absl::StrCat("  std::array<Slop_u<", width, ">, ", count, "> __color_input_prev_u", width, "{};\n")
                    : absl::StrCat("  std::array<Slop<", width, ">, ", count, "> __color_input_prev_", width, "{};\n"));
    }
    {
      std::map<uint32_t, size_t> sliced_slots_per_port;
      for (const auto& slot : color_plan_->boundary_slots()) {
        if (slot.kind == livehd::sim::Color_plan::Boundary_kind::top_input && slot.producer_extract_hi > slot.producer_extract_lo) {
          ++sliced_slots_per_port[static_cast<uint32_t>(slot.public_port)];
        }
      }
      for (const auto& [port, count] : sliced_slots_per_port) {
        if (count < 2) {
          continue;
        }
        const Io* io = nullptr;
        for (const auto& candidate : ios) {
          if (candidate.is_input && candidate.port_id == port) {
            io = &candidate;
            break;
          }
        }
        if (io == nullptr || io->bits <= 0 || clock_in_fields.contains(io->field)) {
          continue;
        }
        const auto name = absl::StrCat("__color_port_prev_", direct_port_gate_storage.size());
        direct_port_gate_storage.emplace(port, absl::StrCat("__rt.", name));
        direct_port_gate_decls.push_back(absl::StrCat("  ", value_type(io->bits, io->unsign), " ", name, "{};\n"));
      }
      for (const auto& decl : direct_port_gate_decls) {
        runtime_header->append(decl);
      }
      if (!dut_) {
        for (const auto& io : ios) {
          if (!io.is_input || !versioned_bits(io.bits) || clock_in_fields.contains(io.field)) {
            continue;
          }
          const auto name = absl::StrCat("__color_port_ver_", direct_port_ver_storage.size());
          direct_port_ver_storage.emplace(io.port_id, absl::StrCat("__rt.", name));
          runtime_header->append("  uint32_t ", name, " = 0;  // version of this wide input at its last refresh\n");
        }
      }
    }
    runtime_header->append("  bool __color_state_changed = false;\n");
    for (size_t site = 0; site < color_plan_->sites().size(); ++site) {
      if (color_plan_->sites()[site].kind == livehd::sim::Color_plan::Site_kind::loop_control) {
        runtime_header->append("  bool __loop_advanced_", std::to_string(site), " = false;\n");
      }
    }
    runtime_header->append("  std::array<uint64_t, ",
                           std::to_string(std::max<size_t>(direct_dirty_words, 1)),
                           "> __color_dirty{};  // activation bits in execution order\n");
    if (direct_commit_shards.empty()) {
      runtime_header->append("  std::array<bool, ", std::to_string(direct_state_commit_count), "> __state_commit{};\n");
    } else {
      runtime_header->append("  std::array<uint64_t, ", std::to_string(direct_commit_shards.size()), "> __commit_shard_mask{};\n");
    }
    runtime_header->append("  std::array<void (*)(void*, void**, uint64_t*), ",
                           std::to_string(color_plan_->colors().size()),
                           "> __color_kernel{};\n");
    runtime_header->append("  std::array<std::vector<void*>, ",
                           std::to_string(color_plan_->colors().size()),
                           "> __color_bindings{};\n");
    runtime_header->append("  __Color_runtime() { __color_dirty.fill(~uint64_t{0}); }\n};\n");
    fout->append("#include \"", runtime_header_name, "\"\n");
  }

  // ---- VCD method definitions (source): __vcd_init (root-only entry) plus the
  // recursive registration/dump walkers every module carries. The root's writer
  // is passed down as a plain pointer -- only the root instance OWNS it (shared
  // __vcd stays null on subs, keeping window-off teardown a root-local reset). ----
  if (vcd_on) {
    // `has_clock`: this module has clocked state (or a pass-through clock port),
    // so its scope shows the clock waveform. A clockless module still gets one
    // when it is the ROOT (the testbench tick is its clock) -- registered in
    // __vcd_init, not __vcd_hier, so a clockless SUB scope stays data-only.
    const bool  has_clock = !clk_field.empty() || !flops.empty();
    // The BAKED clock label was uniquified against the traced names at codegen,
    // but a `tick clocks=(name=…)` clause overrides __clk_name at RUN time -- a
    // label matching a traced 1-bit signal would make registration throw. Guard
    // the runtime value against this module's (baked) 1-bit signal names.
    std::string clk_guard;  // C++ bool expr over __cn, "" = no 1-bit names to collide with
    for (const auto& v : vsig) {
      if (v.bits == 1) {
        absl::StrAppend(&clk_guard, clk_guard.empty() ? "" : " || ", "__cn == \"", v.vname, "\"");
      }
    }
    auto emit_clk_reg = [&](const char* writer_expr, const std::string& scope_expr) {
      fout->append(absl::StrCat("  std::string __cn = __clk_name;\n"));
      if (!clk_guard.empty()) {
        fout->append(absl::StrCat("  if (", clk_guard, ") { __cn += \"_vcd0\"; }  // runtime tick-clock label collided\n"));
      }
      fout->append(
          absl::StrCat("  __vv_clk = ", writer_expr, "->register_var(", scope_expr, ", __cn, vcd::VariableType::wire, 1);\n"));
    };
    fout->append("void ", mod, "::__vcd_init() {\n");
    fout->append("  __vcd = std::make_shared<vcd::VCDWriter>(__vcd_path, vcd::makeVCDHeader());\n");
    if (!has_clock) {
      emit_clk_reg("__vcd", absl::StrCat("\"", mod, "\""));
    }
    fout->append(absl::StrCat("  __vcd_hier(__vcd.get(), \"", mod, "\");\n"));
    fout->append("}\n");

    // Register this instance's vars under scope `__s`, then the sub-instances
    // under `__s.<inst>` (nested $scope blocks -- the writer splits on '.').
    fout->append("void ", mod, "::__vcd_hier(vcd::VCDWriter* __w, const std::string& __s) {\n");
    fout->append("  (void)__w; (void)__s;\n");
    fout->append("  __vcd_trace = true;\n");
    if (has_clock) {
      emit_clk_reg("__w", "__s");
    }
    for (const auto& v : vsig) {
      fout->append(
          absl::StrCat("  ", v.var, " = __w->register_var(__s, \"", v.vname, "\", vcd::VariableType::wire, ", v.bits, ");\n"));
    }
    for (const auto& s : subs) {
      // A registered sub is never its own trace root: drop any writer it
      // self-rooted (a baked FILE path without --top) and clear the path so
      // its lazy __vcd_init can't fire again.
      fout->append(absl::StrCat("  ", s.inst, ".__vcd.reset();\n"));
      fout->append(absl::StrCat("  ", s.inst, ".__vcd_path.clear();\n"));
      fout->append(absl::StrCat("  ", s.inst, ".__vcd_hier(__w, __s + \".", s.inst, "\");\n"));
    }
    fout->append("}\n");

    // The clock edge, written into every clocked scope of the subtree.
    fout->append("void ", mod, "::__vcd_clk(vcd::VCDWriter* __w, bool __rise) {\n");
    fout->append("  (void)__w; (void)__rise;\n");
    fout->append("  if (__vv_clk) {\n");
    fout->append("    if (__rise) { __w->change(__vv_clk, \"1\"); } else { __w->change(__vv_clk, \"0\"); }\n");
    fout->append("  }\n");
    for (const auto& s : subs) {
      fout->append(absl::StrCat("  ", s.inst, ".__vcd_clk(__w, __rise);\n"));
    }
    fout->append("}\n");

    // Root-only, at the edge timestamp: the testbench pokes inputs BEFORE the
    // edge, so they change exactly at it (no settle window, no X). Not recursive:
    // a sub's inputs are ordinary comb nets, dumped with the data phase below.
    fout->append("void ", mod, "::__vcd_dump_in(vcd::VCDWriter* __w) {\n");
    fout->append("  (void)__w;\n");
    fout->append("  if (__vcd_trace) {\n");
    for (const auto& v : vsig) {
      if (v.is_input) {
        fout->append(absl::StrCat("    __w->change(", v.var, ", vcd::to_vcd_bits(", v.snap, ", ", v.bits, "));\n"));
      }
    }
    fout->append("  }\n}\n");

    if (vcd_fakedelay) {
      // At the edge timestamp: any signal whose settled value will differ goes X
      // for the settle window ("computing"); an unchanged signal stays clean.
      fout->append("void ", mod, "::__vcd_dump_x(vcd::VCDWriter* __w, bool __pos, bool __root) {\n");
      fout->append("  (void)__w; (void)__pos; (void)__root;\n");
      fout->append("  if (__vcd_trace) {\n");
      fout->append("    if (__pos) {\n");
      for (const auto& v : vsig) {
        if (!v.posedge) {
          continue;
        }
        const char* x = v.bits > 1 ? "bx" : "x";
        if (v.is_input) {
          fout->append(
              absl::StrCat("      if (!__root && !", v.snap, ".same_repr(", v.prev, ")) __w->change(", v.var, ", \"", x, "\");\n"));
        } else {
          fout->append(absl::StrCat("      if (!", v.snap, ".same_repr(", v.prev, ")) __w->change(", v.var, ", \"", x, "\");\n"));
        }
      }
      fout->append("    } else {\n");
      for (const auto& v : vsig) {
        if (v.posedge) {
          continue;
        }
        const char* x = v.bits > 1 ? "bx" : "x";
        fout->append(absl::StrCat("      if (!", v.snap, ".same_repr(", v.prev, ")) __w->change(", v.var, ", \"", x, "\");\n"));
      }
      fout->append("    }\n  }\n");
      for (const auto& s : subs) {
        fout->append(absl::StrCat("  ", s.inst, ".__vcd_dump_x(__w, __pos, false);\n"));
      }
      fout->append("}\n");
    }

    // The settled data of this period's sample (posedge slot, then the negedge
    // slot at the period midpoint). Root inputs were already written at the edge
    // by __vcd_dump_in; as a SUB they are comb nets and dump here instead.
    fout->append("void ", mod, "::__vcd_dump_data(vcd::VCDWriter* __w, bool __pos, bool __root) {\n");
    fout->append("  (void)__w; (void)__pos; (void)__root;\n");
    fout->append("  if (__vcd_trace) {\n");
    fout->append("    if (__pos) {\n");
    for (const auto& v : vsig) {
      if (!v.posedge) {
        continue;
      }
      std::string upd = vcd_fakedelay ? absl::StrCat(" ", v.prev, " = ", v.snap, ";") : std::string{};
      if (v.is_input) {
        fout->append(absl::StrCat("      if (!__root) { __w->change(",
                                  v.var,
                                  ", vcd::to_vcd_bits(",
                                  v.snap,
                                  ", ",
                                  v.bits,
                                  "));",
                                  upd,
                                  " }\n"));
      } else {
        fout->append(absl::StrCat("      __w->change(", v.var, ", vcd::to_vcd_bits(", v.snap, ", ", v.bits, "));", upd, "\n"));
      }
    }
    fout->append("    } else {\n");
    for (const auto& v : vsig) {
      if (v.posedge) {
        continue;
      }
      std::string upd = vcd_fakedelay ? absl::StrCat(" ", v.prev, " = ", v.snap, ";") : std::string{};
      fout->append(absl::StrCat("      __w->change(", v.var, ", vcd::to_vcd_bits(", v.snap, ", ", v.bits, "));", upd, "\n"));
    }
    fout->append("    }\n  }\n");
    for (const auto& s : subs) {
      fout->append(absl::StrCat("  ", s.inst, ".__vcd_dump_data(__w, __pos, false);\n"));
    }
    fout->append("}\n");

    // Direct-color execution no longer enters each child's cycle(), so the
    // pre-commit snapshots are one explicit occurrence-wide barrier. Outputs
    // have already been captured in __last_out by their pre-rise colors.
    fout->append("void ", mod, "::__vcd_snapshot(bool __pos) {\n");
    for (const auto& v : vsig) {
      std::string accessor = v.accessor;
      if (accessor.starts_with("__o.")) {
        accessor.replace(0, 4, "__last_out.");
      }
      fout->append(absl::StrCat("  if (__pos == ", v.posedge ? "true" : "false", ") ", v.snap, " = ", accessor, ";\n"));
    }
    for (const auto& s : subs) {
      fout->append("  ", s.inst, ".__vcd_snapshot(__pos);\n");
    }
    fout->append("}\n");

    fout->append("void ", mod, "::__vcd_publish_period() {\n");
    fout->append("  if (!__vcd && !__vcd_path.empty()) __vcd_init();\n");
    fout->append("  if (__vcd) {\n");
    fout->append("    const unsigned __b = __vcd_tick * 10;\n");
    fout->append("    const unsigned __half = (__clk_ratio > 0 ? __clk_ratio : 1) * 5;\n");
    fout->append("    vcd::global_timestamp = __b;\n");
    fout->append("    __vcd_clk(__vcd.get(), true);\n");
    fout->append("    __vcd_dump_in(__vcd.get());\n");
    if (vcd_fakedelay) {
      fout->append("    __vcd_dump_x(__vcd.get(), true, true);\n");
      fout->append("    vcd::global_timestamp = __b + 3;\n");
    }
    fout->append("    __vcd_dump_data(__vcd.get(), true, true);\n");
    fout->append("    vcd::global_timestamp = __b + __half;\n");
    fout->append("    __vcd_clk(__vcd.get(), false);\n");
    if (vcd_fakedelay) {
      fout->append("    __vcd_dump_x(__vcd.get(), false, true);\n");
      fout->append("    vcd::global_timestamp = __b + __half + 3;\n");
    }
    fout->append("    __vcd_dump_data(__vcd.get(), false, true);\n");
    fout->append("  }\n}\n");
  }

  // ---- reset_cycle: establish each state element's power-on value (using the
  // `initial` pin when present). `zero_uninitialized` is the opt-in
  // sim.init_zero policy: it supplies an initial zero ONLY when neither an
  // initializer nor reset exists. It never changes the reset expression used
  // by eval_rise/eval_negedge. ----
  const auto reset_cycle_mark = fout->mark();
  fout->append("LHD_SIM_COLD void ", mod, "::reset_cycle(bool zero_uninitialized) {\n");
  fout->append("    (void)zero_uninitialized;\n");
  for (const auto& f : flops) {
    auto        init  = get_driver(find_sink_pin(f.node, "initial"));
    auto        reset = get_driver(find_sink_pin(f.node, "reset_pin"));
    const auto  type  = value_type(f.bits, f.unsign);
    const auto  zv    = absl::StrCat(type, "::create_integer(0)");
    const auto  xv    = unknown_value(f.bits, f.unsign);
    std::string rv;
    if (!init.is_invalid()) {
      rv = stored_value_operand(init, f.bits, f.unsign);
    } else if (reset.is_invalid()) {
      rv = absl::StrCat("zero_uninitialized ? ", zv, " : ", xv);
    } else {
      rv = xv;
    }
    for (const auto& s : f.stages) {
      fout->append("    ", s, " = ", rv, ";\n");
    }
    fout->append("    ", f.member, " = ", rv, ";\n");
  }
  {
    absl::flat_hash_set<std::string> emitted;
    for (const auto& f : flops) {
      if (!f.prev_member.empty() && emitted.insert(f.prev_member).second) {
        fout->append("    ", f.prev_member, " = false;\n");
      }
    }
  }
  for (const auto& m : mems) {
    // Power-on contents: apply the comptime `init` bus (ROM / `const`/`mut`
    // array initial value) when present. Otherwise preserve unknown power-on
    // state, except that sim.init_zero supplies zero when there is ALSO no
    // reset. A runtime initializer counts as present but cannot be applied here,
    // so it receives an unspecified seeded value rather than being silently
    // replaced by zero.
    //
    // The unknown fill is PER ENTRY. A whole-array `apply_update(Slop<bits*size>
    // ::unknown(...))` instantiates one Slop as wide as the array -- Slop<262144>
    // for a 4096x64 memory -- so both host-compile time and stack footprint would
    // scale with the array instead of with its entry width. Only the comptime
    // `init` bus, which genuinely IS a whole-array value, keeps that shape.
    const auto mem_type_name = value_type(m.bits, m.unsign);
    const auto mem_unknown   = unknown_value(m.bits, m.unsign);
    if (m.init.is_const()) {
      fout->append(
          absl::StrCat("    ", m.member, ".apply_update(", stored_value_operand(m.init, m.bits * m.size, m.unsign), ");\n"));
    } else if (m.init.is_invalid() && m.reset.is_invalid()) {
      // Uninitialized, reset-free memory powers on ZERO under BOTH policies.
      // This is what cgen_memory_*.v under a 2-state simulator (Verilator)
      // reads back, and mem_wensize_lanes pins it: a wensize lane the test
      // never writes must read 0, which a PRNG fill turned into a
      // seed-dependent value. The flop power-on PRNG rule is about FLOPS and
      // stands unchanged; sim.init_zero still exists to zero
      // those. State WITH a runtime initializer or a reset keeps the seeded
      // unknown fill below -- lhd_sim_init_zero_test pins that reset-only
      // state stays unknown until its reset actually asserts.
      fout->append(absl::StrCat("    ", m.member, ".fill(", mem_type_name, "::create_integer(0));\n"));
    } else {
      fout->append(absl::StrCat("    for (auto& __e : ", m.member, ") __e = ", mem_unknown, ";\n"));
    }
    fout->append(absl::StrCat("    ", m.member, ".clear_pending();\n"));
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1) {
        fout->append(absl::StrCat("    ",
                                  m.member,
                                  "_q",
                                  p.rdidx,
                                  " = zero_uninitialized ? ",
                                  mem_type_name,
                                  "::create_integer(0) : ",
                                  mem_unknown,
                                  ";\n"));
      }
    }
  }
  for (const auto& s : subs) {
    fout->append("    ", s.inst, ".reset_cycle(zero_uninitialized);\n");
  }
  // Leave the design SETTLED at t=0. Every read a testbench makes before its
  // first `step` -- and the docs use that shape (05-assert.md's `assert(x == 0)`
  // ahead of any step) -- goes through __out, which would otherwise be
  // default-zero rather than the outputs the reset state actually drives. Also
  // covers load_state(): __out is derived, absent from the checkpoint, and this
  // is what re-derives it after a restore.
  fout->append("    ++__gen;  // state re-initialized: every gated evaluation must recompute\n");
  if (color_runtime_root) {
    fout->append("    __color_reset_settle();\n");
  } else if (compact_kernel_) {
    fout->append("    __compact_publish();\n");
  }
  fout->append("}\n");
  chunk_cold_region(*fout, reset_cycle_mark);

  // ---- compact-loop body kernels ----
  // Ordinary hierarchy never reaches this emitter: it is storage-only and the
  // root color DAG owns evaluation. A compact loop instead reuses one generated
  // definition body across ordinals. The body below emits its pre-rise/fall and
  // post-fall versions as dedicated compact kernels; the outer loop control
  // color walks each required version once per ordinal without source unrolling.
  //
  // Emitting one body twice (rather than a hand-written second walker) is what
  // keeps the two passes from drifting: the ordering rules here -- the false-loop
  // pre-binds, the memory read/stage interleave, the on-demand operand cones --
  // are subtle enough that a parallel implementation would rot. The settle pass
  // is also restricted to the OUTPUT cone (`settle_cone` below), so it emits far
  // less than the full body: next-state logic is exactly what it does not need.
  //
  // Reset is an ordinary input the testbench drives into `in` (via step()/__in);
  // no special override here. The flop next-state logic reads it through the
  // normal reset_pin path below.
  //
  // Nodes the settle pass must emit: everything backward-reachable from an
  // output driver, stopping at state elements (a flop Q is already a member).
  // Memory and Sub nodes stay IN the set -- a memory read and a sub-instance
  // output are emitted by their own node, not by ensure_ready(), so dropping
  // them would leave the output cone unbound and operand() would quietly
  // substitute a constant 0.
  //
  // In the post-fall version a nested compact kernel publishes combinational
  // outputs only; it must never advance state a second time.
  bool        settle_mode     = false;
  std::string staged_kids_sum = "0ULL";
  for (const auto& s : subs) {
    absl::StrAppend(&staged_kids_sum, " + ", s.inst, ".__gen");
  }
  // The three emissions of one clock period. Rise and Fall are the two halves of
  // the tick; Settle is the trailing comb refresh that keeps `__out` (and any
  // a testbench ref bound to it) current. A module with no negedge state emits no Fall
  // at all, so dino stays exactly one pass per tick.
  enum class Pass { Rise, Fall, Settle };
  absl::flat_hash_set<hhds::Class_index> settle_cone;
  auto                                   build_settle_cone = [&] {
    settle_cone.clear();
    std::vector<hhds::Pin_class> work;
    for (const auto& io : ios) {
      if (io.is_input) {
        continue;
      }
      auto spin = g->get_output_pin(io.raw);
      if (!spin.is_invalid()) {
        work.push_back(get_driver(spin));
      }
    }
    absl::flat_hash_set<pin_key_t> seen;
    while (!work.empty()) {
      auto p = work.back();
      work.pop_back();
      if (p.is_invalid() || p.is_const() || !seen.insert(p.get_class_index()).second) {
        continue;
      }
      auto n = p.get_master_node();
      settle_cone.insert(n.get_class_index());
      if (is_type_register(n)) {
        continue;  // Q is a committed member; its din cone is next-state, not needed
      }
      for (auto e_sink : n.inp_sorted_pins()) {
        for (auto e_drv : e_sink.get_driver_pins()) {
          work.push_back(e_drv);
        }
      }
    }
  };
  build_settle_cone();

  // Every operand port a state element's next-value/guard emission reads.
  static constexpr std::array<const char*, 4> flop_operand_ports{"din", "enable", "reset_pin", "initial"};

  // Nodes the FALL pass must emit: everything backward-reachable from a negedge
  // element's operands, stopping at state (whose Q is the just-committed
  // member). Same shape as `settle_cone` — it is what keeps the fall from
  // re-emitting the whole body when it only needs the negedge cones.
  absl::flat_hash_set<hhds::Class_index> fall_cone;
  if (has_fall) {
    std::vector<hhds::Pin_class> work;
    for (const auto& f : flops) {
      if (f.posedge) {
        continue;
      }
      for (const auto* port : flop_operand_ports) {
        work.push_back(get_driver(find_sink_pin(f.node, port)));
      }
    }
    absl::flat_hash_set<pin_key_t> seen;
    while (!work.empty()) {
      auto pp = work.back();
      work.pop_back();
      if (pp.is_invalid() || pp.is_const() || !seen.insert(pp.get_class_index()).second) {
        continue;
      }
      auto n = pp.get_master_node();
      fall_cone.insert(n.get_class_index());
      if (is_type_register(n)) {
        continue;
      }
      for (auto e_sink : n.inp_sorted_pins()) {
        for (auto e_drv : e_sink.get_driver_pins()) {
          work.push_back(e_drv);
        }
      }
    }
    for (const auto& s : subs) {
      if (s.negedge_only) {
        fall_cone.insert(s.node.get_class_index());
        // ...AND the backward cone of its INPUTS. The fall pass starts from a
        // cleared pin2var, so every value the deferred call needs must be
        // re-emitted by this walk; inserting only the Sub node left its inputs
        // outside the cone, and `ensure_ready` cannot rescue a Memory dout
        // (it returns early for Memory/registers, on the assumption that an
        // unbound one means a real cycle — true in the rise, false here). The
        // symptom was `negedge-operand-unresolved` naming a memory read on a
        // module with no negedge state and no latch of its own: minion's
        // minion_frontend_thread_buffer, whose only negedge-ness is that its
        // clock-gate child is negedge_only.
        std::vector<hhds::Pin_class> sw;
        for (auto e_sink : s.node.inp_sorted_pins()) {
          for (auto e_drv : e_sink.get_driver_pins()) {
            sw.push_back(e_drv);
          }
        }
        while (!sw.empty()) {
          auto pp = sw.back();
          sw.pop_back();
          if (pp.is_invalid() || pp.is_const() || !seen.insert(pp.get_class_index()).second) {
            continue;
          }
          auto n = pp.get_master_node();
          fall_cone.insert(n.get_class_index());
          if (is_type_register(n)) {
            continue;  // state: its q is emitted by its own node, not walked through
          }
          for (auto e_sink : n.inp_sorted_pins()) {
            for (auto e_drv : e_sink.get_driver_pins()) {
              sw.push_back(e_drv);
            }
          }
        }
      }
    }
  }

  // The word-level cycle classification depends only on the module graph.  In
  // particular, it is identical for rise/fall/settle and for every split-output
  // settle body.  Keep it outside emit_period_body: large generated designs can
  // have hundreds of output groups, and recomputing this whole-graph analysis
  // for each group made simulator emission scale with groups * graph-size.
  absl::flat_hash_set<hhds::Node_class> sched_cycle;
  livehd::graph_util::word_level_cycle_nodes(g, /*strict=*/true, sched_cycle, &live_);

  auto emit_period_body = [&](Pass pass_) -> bool {
    const bool settle     = pass_ == Pass::Settle;
    const bool fall       = pass_ == Pass::Fall;
    // Where this method's text begins: the temporary sweeps detach and rewrite
    // exactly this region once the body is closed (see compact_body_temps).
    const auto body_mark  = fout->mark();
    auto       close_body = [&] {  // call on EVERY successful exit (the settle closes early, below)
      auto body = fout->detach_from(body_mark);
      compact_body_temps(body);
      fout->append(body);
    };
    // `settle_mode` means "do NOT advance a child" -- true for the fall as well as
    // the settle. A sub-instance already ran its whole `cycle()` (both halves plus
    // its own trailing settle) during this module's RISE, so all the fall needs
    // from it is its outputs re-evaluated against the inputs the rise just
    // committed: a comb re-settle, not a second advance. That is precisely what
    // the old `negedge-through-instance` refusal existed to prevent, and why it
    // can now be lifted -- before the settle/commit split there was no way to ask
    // a child for its outputs without also stepping it.
    settle_mode = settle || fall;
    if (pass_ != Pass::Rise) {
      // Fresh binding environment. The settle reads the COMMITTED members rather
      // than the temporaries the rise left behind; the fall likewise starts from
      // the state the rise just committed, which is what makes its half-cycle
      // handover fall out of an ordinary walk instead of the invalidate/re-ready
      // dance a single monolithic body needed.
      pin2var.clear();
      canonical_.clear();
      slop_u_values_.clear();
      slop_u_binding_width_.clear();
      preextracted_get_masks_.clear();
      seq_volatile_.clear();
      fout->append("void ",
                   mod,
                   "::",
                   settle && staged_flat ? "__color_post_fall"
                   : settle              ? "__compact_post_fall"
                                         : "__compact_fall",
                   "() {\n");
    } else {
      fout->append("void ", mod, "::", staged_flat ? "__color_pre_rise" : "__compact_pre_rise", "() {\n");
    }
    // ---- CHANGE-GATED evaluation (see the __gen comment in the header). Skip
    // when this method last ENTERED with the same (own __gen, sum of child
    // __gen) — i.e. nothing it reads changed and its last run was a fixed point
    // (a run that committed a change grew __gen past the stored entry value,
    // and a child that advanced grew the sum). Storing the ENTRY values, never
    // the exit ones, is what keeps a mid-flight pipeline running: its own
    // commits keep the stored pair behind until the subtree quiesces. Disabled
    // under VCD (a skipped body would freeze __vcd_tick and the waveform). ----
    std::string gate_done, gate_kids, gate_ksum;
    if (vcd_file.empty() && !env_.nogate) {
      if (pass_ == Pass::Rise) {
        gate_done = "__done_pos";
        gate_kids = "__kids_pos";
      } else if (fall) {
        gate_done = "__done_neg";
        gate_kids = "__kids_neg";
      } else {
        gate_done = "__done_settle";
        gate_kids = "__kids_settle";
      }
      gate_ksum = "0ULL";
      for (const auto& s : subs) {
        absl::StrAppend(&gate_ksum, " + ", s.inst, ".__gen");
      }
      fout->append("    const uint64_t __g0 = __gen;\n");
      fout->append("    const uint64_t __k0 = ", gate_ksum, ";\n");
      fout->append("    if (", gate_done, " == __g0 && ", gate_kids, " == __k0) return;\n");
    }
    // map input ports and flop q outputs into pin2var
    for (const auto& io : ios) {
      if (!io.is_input) {
        continue;
      }
      auto pin = g->get_input_pin(io.raw);
      if (!pin.is_invalid()) {
        pin2var[pin.get_class_index()] = absl::StrCat("__in.", io.field);
        if (io.unsign) {
          mark_slop_u_binding(pin);
        }
      }
    }
    for (const auto& f : flops) {
      auto qpin = f.node.get_driver_pin(0);
      if (!qpin.is_invalid()) {
        pin2var[qpin.get_class_index()] = f.member;
        if (f.unsign) {
          mark_slop_u_binding(qpin);
        }
      }
    }

    // ---- Moore-sub deferral: a Sub on a FALSE loop whose callee has NO
    // combinational input->output path needs no ordering between its inputs and
    // outputs -- its outputs are a pure function of the child's CURRENT state.
    // Pre-bind them via peek() (state-preserving output recompute) before the
    // comb walk; the real cycle(in) call, which only advances child state, is
    // DEFERRED until every comb value is bound (emitted after the walk below).
    // This resolves the DivUnit/ExeUnitImp_2 comb-loop-through-instance class
    // without flattening the stateful callee (state identity/checkpoint names
    // are untouched). ----
    absl::flat_hash_set<hhds::Class_index>                                 moore_deferred;
    std::vector<const Sub*>                                                deferred_moore;
    absl::flat_hash_set<hhds::Class_index>                                 fall_deferred;
    std::vector<const Sub*>                                                deferred_fall;
    // Per-OUTPUT-cone deferral (Stage-2): a MEALY callee (some comb in->out path,
    // so callee_is_moore declines the whole-callee treatment) whose FED-BACK
    // outputs are all pure state reads. Pre-binding just those outputs via peek()
    // un-cycles the schedule; the atomic cycle(in) call then orders normally (its
    // input cone reads only pre-bound values, emitted on demand below). The
    // CSR/NewCSR::MipModule and DivUnit/SRT16 residual class.
    absl::flat_hash_set<hhds::Class_index>                                 mealy_prebound;
    // Memoized per callee def: the set of output pids that are pure current-state
    // reads (a Moore callee: ALL of them). Used by the fed-back walk to decide
    // whether another Sub's output is a boundary or an atomic pass-through.
    absl::flat_hash_map<const hhds::Graph*, absl::flat_hash_set<uint32_t>> state_out_memo;
    auto sub_out_is_state_only = [&](const hhds::Node_class& m, uint32_t pid) -> bool {
      auto cg = m.get_subnode_graph();
      if (!cg) {
        return false;
      }
      auto it = state_out_memo.find(cg.get());
      if (it == state_out_memo.end()) {
        auto                          sio = m.get_subnode_io();
        absl::flat_hash_set<uint32_t> so;
        if (sio) {
          if (callee_is_moore(cg, sio)) {
            for (const auto& od : sio->get_output_pin_decls()) {
              so.insert(static_cast<uint32_t>(od.port_id));
            }
          } else {
            so = callee_state_only_outputs(cg, sio);
          }
        }
        it = state_out_memo.emplace(cg.get(), std::move(so)).first;
      }
      return it->second.contains(pid);
    };
    for (const auto& s : subs) {
      if (s.loop) {
        // A descriptor carry self-edge is the eager chain inside the compact
        // wrapper, not a parent-level combinational ring. A genuinely
        // negedge-only callee still advances in the parent's fall half.
        if (s.negedge_only) {
          fall_deferred.insert(s.node.get_class_index());
          // Deferring is only half of it. Every OTHER fall_deferred sub also
          // gets the `__pre` snapshot + pin2var pre-binding below (it reaches
          // that code with moore_capable true by construction), and skipping it
          // here left the loop's outputs unbound during the rise: a parent cell
          // reading one hits `raw_operand` with no pin2var entry, which sets
          // cycle_unresolved_ and aborts emission with a bogus
          // "combinational-cycle" refusal naming the loop instance — for a
          // design that has no combinational cycle at all.
          auto                                       loop_sio = s.node.get_subnode_io();
          absl::flat_hash_map<uint32_t, std::string> loop_pid2name;
          // The DECL's sign, not the pin's. The member being bound was declared
          // by the callee's own emission from `decl.unsign` (default FALSE =
          // signed), while is_unsign() is attribute-ABSENCE (default TRUE =
          // unsigned). Marking a `Slop<W>` member canonical on the pin's default
          // lets operand()'s free-widening arm reinterpret a data msb as a sign.
          absl::flat_hash_set<uint32_t>              loop_pid_unsigned;
          for (const auto& d : loop_sio->get_output_pin_decls()) {
            loop_pid2name[static_cast<uint32_t>(d.port_id)] = d.name;
            if (d.unsign) {
              loop_pid_unsigned.insert(static_cast<uint32_t>(d.port_id));
            }
          }
          // Only pins that EXIST (walk the out-edges): a declared-but-unread
          // output has no created pin and hhds' name lookup asserts on it.
          for (const auto& odrv : s.node.out_sorted_pins()) {
            auto opin = odrv;
            if (opin.is_invalid() || pin2var.contains(opin.get_class_index())) {
              continue;
            }
            auto lit = loop_pid2name.find(static_cast<uint32_t>(opin.get_port_id()));
            if (lit != loop_pid2name.end()) {
              const auto field = cpp_port_path(lit->second);
              if (settle) {
                pin2var[opin.get_class_index()] = absl::StrCat(s.inst, ".__out.", field);
              } else {
                const auto var = absl::StrCat(s.inst, "__pre_p", static_cast<uint32_t>(opin.get_port_id()));
                fout->append(absl::StrCat("    const auto ",
                                          var,
                                          " = ",
                                          s.inst,
                                          ".__out.",
                                          field,
                                          ";  // negedge-only compact-loop boundary\n"));
                pin2var[opin.get_class_index()] = var;
              }
              if (loop_pid_unsigned.contains(static_cast<uint32_t>(opin.get_port_id()))) {
                mark_slop_u_binding(opin);
              }
            }
          }
        }
        continue;
      }
      auto       fed_back      = sub_false_loop_output_pids(s.node, sub_out_is_state_only);
      const bool moore_capable = s.negedge_only || callee_is_moore(s.node.get_subnode_graph(), s.node.get_subnode_io());
      // A RING THROUGH SEVERAL INSTANCES. `sub_false_loop_output_pids` looks for
      // the instance's OWN output coming back, and it treats a Moore sibling's
      // output as a boundary — correctly. But then a ring of three Moore cells
      // (u0 -> u1 -> u2 -> u0, minion's `intpipe_mul_div_ctl` chain of
      // `prim_phase_pair_lo_hi`) reports `fed_back` EMPTY for every member, so
      // none was deferred, while the graph still had a Sub->Sub->Sub cycle that
      // `forward_class` cannot order. The result was `combinational-loop` on a
      // design whose every hop is through a latch — no comb path at all.
      //
      // A Moore callee's outputs are pure current-state reads, so pre-binding them
      // is always sound; the only reason not to do it unconditionally is the cost
      // of the `__pre` copy. Being ON the schedule's cycle is exactly the
      // condition that makes it worth paying.
      //
      // And the SAME argument holds per-output for a MEALY callee on the cycle:
      // `fed_back` walks stop at a Memory (a state boundary for the DIRECT
      // feedback question), so a ring that threads through a memory's ASYNC read
      // — vpu_ctrl's `mask.ex_mask_rf_q[ex_thread_id]` where the ADDRESS comes
      // from `trans.ex_trans_thread_id_o`, a pure flop read, while `trans`'s own
      // input needs the memory's dout — reports `fed_back` EMPTY for the very
      // instance whose state-only output would break it. Likewise vpu_ml's
      // tensor 3-clique, where every crossing output is a function of registers
      // THROUGH comb logic (`enabled = enabled_int | ...`) and each callee is
      // Mealy only because of unrelated dcache outputs. Pre-binding the
      // state-only outputs of ANY sub the schedule sees on a word-level cycle is
      // sound for exactly the Moore reason, output by output.
      const bool on_cycle      = sched_cycle.contains(s.node);
      if (::getenv("LIVEHD_SIM_SPLIT_DEBUG") != nullptr) {
        std::fprintf(stderr,
                     "[splitdbg] %s: sub %s on_cycle=%d fed_back=%zu negedge=%d moore=%d\n",
                     gname.c_str(),
                     s.inst.c_str(),
                     on_cycle ? 1 : 0,
                     fed_back.size(),
                     s.negedge_only ? 1 : 0,
                     moore_capable ? 1 : 0);
      }
      if (fed_back.empty() && !s.negedge_only && !on_cycle) {
        continue;
      }
      const bool                    moore = moore_capable;
      absl::flat_hash_set<uint32_t> state_only;
      if (s.negedge_only) {
        fall_deferred.insert(s.node.get_class_index());
      } else if (moore) {
        moore_deferred.insert(s.node.get_class_index());
      } else {
        state_only          = callee_state_only_outputs(s.node.get_subnode_graph(), s.node.get_subnode_io());
        bool all_state_only = true;
        for (auto pid : fed_back) {
          if (!state_only.contains(pid)) {
            all_state_only = false;
            break;
          }
        }
        if (!all_state_only) {
          continue;  // genuine comb feedback -> the loud Stage-0 diagnostic below
        }
        if (fed_back.empty() && state_only.empty()) {
          continue;  // on-cycle Mealy with nothing pre-bindable: nothing to gain
        }
        mealy_prebound.insert(s.node.get_class_index());
      }
      // The child's outputs for its CURRENT committed state are exactly what its
      // own trailing settle left in `__out` -- which is what the old
      // `peek({})` recomputed, at the price of snapshotting and restoring the
      // whole child subtree. In the CYCLE pass each demanded FIELD must be
      // snapshotted: the deferred `<inst>.cycle(...)` emitted after the walk
      // refreshes `__out`, and these bindings have to keep reading the pre-call
      // values. Copying the entire Out struct here is unnecessary and can be
      // enormous. In the SETTLE pass
      // nothing advances the child, so bind straight at the member; the only
      // writer there is a Mealy sub's own `__settle`, and every pin bound here is
      // a pure state read, which a settle does not change.
      // Bind only pins that EXIST (enumerated via the instance's out-edges): a
      // declared-but-unread output has no created pin, and hhds' name lookup
      // asserts on it (find_pin "requested pin was not created" -- the DataPath
      // family crash). The lazy out_edges view is iterated read-only.
      auto                                       sio = s.node.get_subnode_io();
      absl::flat_hash_map<uint32_t, std::string> pid2name;
      absl::flat_hash_set<uint32_t>              pid_unsigned;  // decl sign; see the compact-loop note above
      for (const auto& d : sio->get_output_pin_decls()) {
        pid2name[static_cast<uint32_t>(d.port_id)] = d.name;
        if (d.unsign) {
          pid_unsigned.insert(static_cast<uint32_t>(d.port_id));
        }
      }
      for (const auto& odrv : s.node.out_sorted_pins()) {
        auto opin = odrv;
        if (opin.is_invalid() || pin2var.contains(opin.get_class_index())) {
          continue;
        }
        auto pid = static_cast<uint32_t>(opin.get_port_id());
        if (!moore && !state_only.contains(pid)) {
          continue;  // a comb in->out output binds at the (normally ordered) call
        }
        auto it = pid2name.find(pid);
        if (it != pid2name.end()) {
          // cpp_port_path, never cpp_id: a tuple leaf is `io_data.instruction` in
          // the callee's Out struct (a NESTED struct, see emit_io_block), so
          // mangling the dot to `_` here named a member that does not exist —
          // `no member named 'io_data_instruction' in 'StageReg_StageReg::Out'`.
          const auto field = cpp_port_path(it->second);
          if (settle) {
            pin2var[opin.get_class_index()] = absl::StrCat(s.inst, ".__out.", field);
          } else {
            const auto var = absl::StrCat(s.inst, "__pre_p", pid);
            fout->append(absl::StrCat("    const auto ",
                                      var,
                                      " = ",
                                      s.inst,
                                      ".__out.",
                                      field,
                                      s.negedge_only ? ";  // negedge-only sub boundary\n"
                                      : moore        ? ";  // Moore state-output boundary\n"
                                                     : ";  // Mealy state-only output boundary\n"));
            pin2var[opin.get_class_index()] = var;
          }
          if (pid_unsigned.contains(pid)) {
            mark_slop_u_binding(opin);
          }
        }
      }
    }

    // On-demand emission of a combinational input cone. A Memory cell is
    // `loop_last` (a toposort source), so forward_class emits it EARLY -- before a
    // COMPUTED read address / write-forward operand (e.g. `i*2+j`) that a normal
    // comb node would produce later in the order. Binding such an operand at the
    // (early) memory position would read it unresolved. This walks the operand's
    // driver cone and emits any not-yet-bound plain comb node first (its own
    // inputs recursively), so `operand()` resolves. Stops at state elements /
    // atomic Subs / multi-out cells / consts (bound elsewhere, or a genuine cycle
    // the detector below still catches). Direct-input / const addresses (the
    // common case) are already bound, so this is a no-op for them.
    // One atomic Sub call emission, shared by the scheduled walk below and the
    // on-demand path inside ensure_ready (a call can be SCHEDULED after its
    // reader once a false instance-level cycle was dissolved by the
    // Moore-deferral / pre-binding above). The guard is load-bearing: a second
    // emission would call child.cycle() twice and double-advance its state.
    // One memory emission, shared by the scheduled walk below and the on-demand
    // path inside ensure_ready — the same treatment emit_sub_call gets, for the
    // same reason. forward_class emits every loop_last node (Memory, Sub) as a
    // SOURCE in storage order, so whether a memory whose dout feeds another
    // source's input cone was emitted "in time" was pure luck of that order.
    // Emitting it on demand makes the order irrelevant: reads bind dout pins,
    // and a fwd/program-ordered memory's staged writes pull their own operand
    // cones recursively. The guard is load-bearing: reads must bind exactly
    // once, and a re-entry DURING the memory's own emission (a genuine
    // dout -> write cone -> dout cycle) returns without binding, which falls
    // through to the loud unbound-operand diagnostic.
    // Demand-chain trace for the loud refusals below (env-gated): who asked for
    // what — group calls, atomic calls, memory emissions — when a cycle closes.
    std::vector<std::string>               demand_stack;
    absl::flat_hash_set<hhds::Class_index> emitted_mems;
    auto                                   emit_memory = [&](auto&& ensure_fn, const hhds::Node_class& node) -> void {
      if (!emitted_mems.insert(node.get_class_index()).second) {
        return;
      }
      // Emit the read data (read-first: the current array) and register each
      // read port's dout driver pin so downstream nodes resolve. Writes commit
      // at the edge (below). Sync read (type 0) consumers see the dout register.
      for (const auto& m : mems) {
        if (m.node.get_class_index() != node.get_class_index()) {
          continue;
        }
        demand_stack.push_back(absl::StrCat(m.member, ".mem"));
        struct PopM {
          std::vector<std::string>* v;
          ~PopM() { v->pop_back(); }
        } pop_m{&demand_stack};
        // Combinational whole-array: the `update` bus IS the contents this cycle
        // (no clock edge), so scatter it into `member` BEFORE the reads so reads
        // and read_all observe the post-update value. (Registered whole-arrays
        // apply update at the edge below; reads here see committed state.)
        if (m.is_whole() && !m.registered()) {
          ensure_fn(m.update);
          fout->append(
              absl::StrCat("    ", m.member, ".apply_update(", stored_value_operand(m.update, m.bits * m.size, m.unsign), ");\n"));
        } else if (!m.registered() && m.init.is_const()) {
          // Combinational per-port array (`mut t:[..] = <const>`): re-seed the
          // whole array to its comptime init at the START of every cycle. Writes
          // are forwarded to the same-cycle read below and also committed to
          // `member` at the edge, but a comb array has no state, so without this
          // re-seed a per-port write would leak into later cycles' reads.
          fout->append(
              absl::StrCat("    ", m.member, ".apply_update(", stored_value_operand(m.init, m.bits * m.size, m.unsign), ");\n"));
        }
        // Stage the write ports a read may OBSERVE, INTERLEAVED with the reads
        // in program order: before read port r, stage exactly the write ports r
        // resolves against (hlop::Memory_*::read consults the staged slots per
        // its ordering mode). Staging a write any earlier would make the emitted
        // C++ depend on its data, and under ordering="program" the canonical
        // shape `o1 = mem[a]; mem[b] = f(o1); o4 = mem[c]` -- a legal
        // read-modify-write -- would then look like a combinational cycle.
        int  staged        = 0;  // write ports staged so far, by ordinal
        auto stage_through = [&](int upto) {
          // A settle runs AFTER the edge, so a REGISTERED memory's writes for
          // this period are already in the array (mem.tick() committed them);
          // re-staging them would duplicate the write-forwarding and leave
          // pending slots behind. Post-commit, reading the committed array
          // already reflects the write -- also the documented remote-reader rule
          // (08-memories.md: "remote readers always see the last committed
          // state").
          //
          // A COMBINATIONAL array is the opposite: it has no clock and no
          // committed state, its contents ARE the writes made during the pass
          // (which is why the walk re-seeds it to its init above), so the settle
          // must stage them exactly as the cycle pass does. Skipping them left
          // every read returning the re-seed value -- caught by
          // prp-simeq-packed_assign, whose `mut g:[4]u4` is filled by four
          // per-port writes and then runtime-indexed.
          if (settle && m.registered()) {
            staged = upto;
            return;
          }
          if (upto <= staged) {
            return;
          }
          for (const auto& wp : m.ports) {
            if (wp.rd || wp.addr.is_invalid() || wp.din.is_invalid() || wp.wridx < staged || wp.wridx >= upto) {
              continue;
            }
            if (wp.enable.is_known_false()) {
              continue;  // never fires
            }
            ensure_fn(wp.addr);
            if (!wp.enable.is_invalid()) {
              ensure_fn(wp.enable);
            }
            // The gated-clock guards emit_wen folds into the write enable are
            // cones too. Without readying them, `operand()` below degrades an
            // unemitted guard to the UNRESOLVED-CYCLE constant and the module
            // is refused with a bogus back-edge diagnostic.
            for (const auto& gp : m.clock_guards) {
              ensure_fn(gp);
            }
            // ordering="none" never reads the staged DATA -- the collision value
            // is undefined -- so only the address and enable are needed to
            // detect it. Skipping the din keeps a read-modify-write on a "none"
            // memory from becoming a false cycle too.
            std::string din = absl::StrCat(value_type(m.bits, m.unsign), "::create_integer(0)");
            if (m.order != Mem::Order::none) {
              ensure_fn(wp.din);
              din = stored_value_operand(wp.din, m.bits, m.unsign);
            }
            fout->append(absl::StrCat("    ",
                                      m.member,
                                      ".stage_write<",
                                      wp.wridx,
                                      ">(",
                                      emit_wen(m, wp),
                                      ", ",
                                      raw_operand(wp.addr, std::max(1, bits_of(wp.addr))),
                                      ", ",
                                      din,
                                      ");\n"));
          }
          staged = upto;
        };
        // How many write ports read port r resolves against. A sync (latency-1)
        // read resolves at the edge instead, so it stages nothing here.
        auto read_prefix = [&](int rdidx) -> int {
          if (m.type == 1) {
            return 0;
          }
          switch (m.order) {
            case Mem::Order::old    : return 0;
            case Mem::Order::fwd    :
            case Mem::Order::none   : return m.n_user_wr;
            case Mem::Order::program: return m.fwd_upto[static_cast<size_t>(rdidx)];
          }
          return 0;
        };
        // READ-PORT ORDER. Every latency-0 read of a REGISTERED memory returns
        // the same committed contents, so the reads are mutually independent —
        // but they were emitted in `rdidx` order, and each one prefetches its
        // own ADDRESS cone at its own position. When port 0's address is
        // computed from port 3's dout (a perfectly ordinary register-file
        // read-pointer chain: minion's `vpu_tensorfma` and `vpu_ctrl` both do
        // it), port 0 asked for a dout this loop had not emitted yet,
        // `ensure_ready` declined to bind it (a Memory dout is bound by its own
        // emission), and the result was reported as `combinational-loop` on the
        // memory. There is no cycle: only an emission order nobody had chosen.
        //
        // So choose one. Sort the read ports so a port whose address cone reads
        // another port's dout comes AFTER it. A genuine cycle among the ports
        // leaves the order untouched and still fails loudly below.
        std::vector<const MemPort*> rd_order;
        for (const auto& p : m.ports) {
          if (p.rd && !p.addr.is_invalid()) {
            rd_order.push_back(&p);
          }
        }
        // `dout_pid` comes from the SINK-side port descriptors (addr/din/enable),
        // so a read port nothing consumes has no dout driver pin at all. Ask the
        // out edges rather than probing with get_driver_pin: hhds `find_pin`
        // ASSERTS on a port that was never created (only an NDEBUG build hands
        // back an invalid pin), and binding pin2var off an invalid handle would
        // key every such port on the same bogus class index.
        absl::flat_hash_set<uint32_t> live_dout_pids;
        for (const auto& odrv : node.out_sorted_pins()) {
          live_dout_pids.insert(static_cast<uint32_t>(odrv.get_port_id()));
        }
        // Only when every read port stages the SAME write prefix. `stage_through`
        // walks a monotonic counter, so under ordering="program" — the one mode
        // whose prefix varies per port — program order IS the contract and must
        // not be permuted.
        if (m.order != Mem::Order::program && rd_order.size() > 1) {
          absl::flat_hash_map<hhds::Class_index, int> dout2idx;
          for (size_t i = 0; i < rd_order.size(); ++i) {
            if (!live_dout_pids.contains(static_cast<uint32_t>(rd_order[i]->dout_pid))) {
              continue;
            }
            auto dp                        = node.get_driver_pin(static_cast<hhds::Port_id>(rd_order[i]->dout_pid));
            dout2idx[dp.get_class_index()] = static_cast<int>(i);
          }
          // deps[i] = read ports whose dout appears in port i's address cone
          std::vector<std::vector<int>> deps(rd_order.size());
          for (size_t i = 0; i < rd_order.size(); ++i) {
            absl::flat_hash_set<hhds::Class_index> seen_p;
            std::vector<hhds::Pin_class>           work{rd_order[i]->addr};
            while (!work.empty()) {
              auto pp = work.back();
              work.pop_back();
              if (pp.is_invalid() || pp.is_const() || !seen_p.insert(pp.get_class_index()).second) {
                continue;
              }
              if (auto it = dout2idx.find(pp.get_class_index()); it != dout2idx.end()) {
                if (it->second != static_cast<int>(i)) {
                  deps[i].push_back(it->second);
                }
                continue;  // the dout itself is the boundary
              }
              auto pn = pp.get_master_node();
              if (is_type_register(pn) || type_op_of(pn) == Ntype_op::Sub || type_op_of(pn) == Ntype_op::Memory) {
                continue;
              }
              for (const auto& psink : pn.inp_sorted_pins()) {
                const auto pdrv = psink.get_driver_pin();
                work.push_back(pdrv);
              }
            }
          }
          std::vector<int> indeg(rd_order.size(), 0);
          for (size_t i = 0; i < deps.size(); ++i) {
            indeg[i] = static_cast<int>(deps[i].size());
          }
          std::vector<const MemPort*> sorted_ports;
          std::vector<bool>           done(rd_order.size(), false);
          for (size_t pass = 0; pass < rd_order.size(); ++pass) {
            bool progress = false;
            for (size_t i = 0; i < rd_order.size(); ++i) {
              if (done[i]) {
                continue;
              }
              bool ready = true;
              for (const int d : deps[i]) {
                if (!done[static_cast<size_t>(d)]) {
                  ready = false;
                  break;
                }
              }
              if (ready) {
                done[i] = true;
                sorted_ports.push_back(rd_order[i]);
                progress = true;
              }
            }
            if (!progress) {
              break;  // a genuine cycle among the ports: keep the original order
            }
          }
          if (sorted_ports.size() == rd_order.size()) {
            rd_order = std::move(sorted_ports);
          }
        }
        for (const auto* pp_ : rd_order) {
          const auto&     p        = *pp_;
          // No readers -> no dout pin to bind (see live_dout_pids above). The
          // read itself is still emitted: `stage_through` is what orders the
          // OTHER ports' writes, so skipping the port would move them.
          const bool      has_dout = live_dout_pids.contains(static_cast<uint32_t>(p.dout_pid));
          hhds::Pin_class dout;
          if (has_dout) {
            dout = node.get_driver_pin(static_cast<hhds::Port_id>(p.dout_pid));
          }
          if (m.type == 1) {
            if (!has_dout) {
              continue;  // registered read with no reader: nothing but the binding
            }
            pin2var[dout.get_class_index()] = absl::StrCat(m.member, "_q", std::to_string(p.rdidx));
            seq_volatile_.insert(dout.get_class_index());  // slop_update'd mid-sequential-section
            if (m.unsign) {
              mark_slop_u_binding(dout);
            }
          } else {
            // Latency-0 read: the memory resolves its own ordering mode against
            // the writes staged above, exactly as the cgen_memory wrapper's
            // UNDEF-then-FWD-then-stored chain does (and at latency 1 the same
            // resolved value is what the read register latches, at the edge).
            stage_through(read_prefix(p.rdidx));  // program-order staging, see above
            ensure_fn(p.addr);                    // computed read address emitted before this early (loop_last) memory node
            int  ab  = std::max(1, bits_of(p.addr));
            auto var = absl::StrCat("cg_", std::to_string(tmp_cnt++));
            fout->append(absl::StrCat("    ",
                                      value_type(m.bits, m.unsign),
                                      " ",
                                      var,
                                      " = ",
                                      m.member,
                                      ".read<",
                                      p.rdidx,
                                      ">(",
                                      operand(p.addr, ab),
                                      ");  // mem read\n"));
            if (!has_dout) {
              continue;  // value computed for its staging side effect only
            }
            pin2var[dout.get_class_index()] = var;
            if (m.unsign) {
              mark_slop_u_binding(dout);
            }
          }
        }
        // Async whole-array read: pack the current `member` into one bus.
        // `has_read_all` IS "this reserved pid has out edges", so the pin exists.
        if (m.has_read_all) {
          auto ra  = node.get_driver_pin(static_cast<hhds::Port_id>(Ntype::Memory_readall_pid));
          auto var = absl::StrCat("cg_", std::to_string(tmp_cnt++));
          fout->append(absl::StrCat("    auto ", var, " = ", m.member, ".read_all();  // read_all\n"));
          pin2var[ra.get_class_index()] = var;
          if (m.unsign) {
            mark_slop_u_binding(ra);
          }
        }
        break;
      }
    };

    absl::flat_hash_set<hhds::Class_index> emitted_subs;
    auto                                   sub_input_driver = [&](const Sub& s, std::string_view name, hhds::Port_id pid) {
      auto sink = find_sink_pin(s.node, name);
      if (!s.loop) {
        return get_driver(sink);
      }
      if (s.loop->index_input && pid == *s.loop->index_input) {
        return hhds::Pin_class{};  // occurrence-supplied by the wrapper
      }
      if (sink.is_invalid()) {
        // find_sink_pin returns an INVALID pin for a declared callee input with
        // no materialized pin (the non-loop path gets this guard for free from
        // get_driver). A compact loop node stores no external edge for any
        // descriptor-provided role or unbound input, and get_driver_pins()
        // asserts on a null graph_.
        return hhds::Pin_class{};
      }
      for (auto e_drv : sink.get_driver_pins()) {
        if (e_drv.get_master_node() != s.node) {
          return e_drv;  // carry initial value or invariant
        }
      }
      return hhds::Pin_class{};
    };
    // A child's clock input still carries the ROOT clock as a data member; a
    // recognized Clock_cell's enables travel separately through `<port>__tick`
    // below.  Asking operand() for the cell output is both unnecessary and
    // forbidden (a Clock_cell has timing semantics, not a data value).
    auto child_clock_data_driver = [&](const Sub& s, uint32_t port_id, const hhds::Pin_class& drv) {
      auto cdef = s.node.get_subnode_graph();
      if (!cdef || !clock_guard_ports(cdef, port_cache).contains(port_id)) {
        return drv;
      }
      auto cone = livehd::latch_contract::clock_op_of(drv, design_clocks);
      if (cone && !cone->clock.is_invalid() && !cone->clock_inverted && cone->div == 1) {
        return cone->clock;
      }
      return drv;  // unsupported timing shapes keep the existing loud refusal
    };

    // Runtime skip for a conditionally activated child. The hidden __valid
    // actual is the complete caller/path guard. Reset is discovered from the
    // same structural interface analysis used by activation Clock_cell
    // insertion, including active-low polarity; if a reset cone is not fully
    // reducible to declared ports, return no condition and visit the child
    // unconditionally (safe, only less efficient).
    auto sub_run_condition = [&](auto&& ensure_fn, const Sub& s) -> std::string {
      if (s.loop) {
        return {};  // the compact wrapper applies its per-ordinal activation/reset guard
      }
      auto cdef = s.node.get_subnode_graph();
      auto sio  = s.node.get_subnode_io();
      if (!cdef || !sio) {
        return {};
      }

      hhds::Pin_class active;
      for (const auto& d : sio->get_input_pin_decls()) {
        if (d.name == "__valid") {
          active = sub_input_driver(s, d.name, d.port_id);
          break;
        }
      }
      if (active.is_invalid() || (active.is_known_true())) {
        return {};
      }
      // The caller already skipped this whole definition when its own
      // top-level activation was false. Repeating that guard on every
      // unconditional child is wasted work, and for group-scheduled callees it
      // can put a recursively demanded group's declarations inside another
      // group's lexical `if` even though later consumers need them outside.
      // A local path condition is an And/Mux cone and therefore does not reduce
      // to this graph-input root; it still receives the runtime guard below.
      const auto active_root = livehd::latch_contract::control_root(active).net;
      if (!active_root.is_invalid() && livehd::graph_util::is_graph_input_pin(active_root)
          && pin_name_of(active_root) == "__valid") {
        return {};
      }
      ensure_fn(active);
      if (!active.is_const() && !pin2var.contains(active.get_class_index())) {
        return {};  // unresolved feedback: the ordinary call path diagnoses it
      }
      std::string cond = emit_known_true(operand(active, 1));

      const auto& resets = reset_guard_ports(cdef, port_cache);
      if (!resets.complete || resets.ports.size() > 1) {
        return {};  // rule 16: no skip when reset cannot be represented exactly
      }
      if (!resets.ports.empty()) {
        const auto&     rp = resets.ports.front();
        hhds::Pin_class reset;
        for (const auto& d : sio->get_input_pin_decls()) {
          if (static_cast<uint32_t>(d.port_id) == rp.port_id) {
            reset = sub_input_driver(s, d.name, d.port_id);
            break;
          }
        }
        if (reset.is_invalid()) {
          return {};
        }
        ensure_fn(reset);
        if (!reset.is_const() && !pin2var.contains(reset.get_class_index())) {
          return {};
        }
        absl::StrAppend(&cond, " || (", operand(reset, 1), rp.active_low ? ").is_known_false()" : ").is_known_true()");
      }
      return cond;
    };

    // A GATED CLOCK CROSSING INTO THE CHILD. If this port carries the child's
    // clock and we are driving it with a recognized gate, the child cannot see
    // that: inside it the port is an ordinary graph input, so it would commit
    // every period with our gate as dead code. Tell it via the `<port>__tick`
    // guard word. The guard field is defaulted true, so an UNGATED port is
    // untouched — but ONLY write it when the child actually DECLARED the guard
    // (it really clocks state on this port); writing a field the callee never
    // emitted is a compile error, and a gate output used as ordinary DATA by
    // the child is legal and common. SHARED by every call path that fills a
    // child's inputs: emit_sub_call AND the deferred-Moore / deferred-fall
    // drains — the drains used to skip it, so a tick-guarded Moore or
    // negedge-only callee committed on gated-off periods (found by the
    // adversarial review of the ports-as-members redesign; the fresh-In flow
    // had the same hole, defaulting the guard true on every call).
    // `bind_only` runs the cone-BINDING half alone: a caller about to open a
    // runtime-activation `if` needs every declaration ensure_fn emits to land
    // at method scope, never inside that nested C++ block.
    auto emit_child_tick = [&](auto&&                 ensure_fn,
                               const Sub&             s,
                               std::string_view       port_name,
                               uint32_t               port_id,
                               const hhds::Pin_class& drv,
                               bool                   bind_only = false) -> void {
      const bool child_wants_tick = [&] {
        auto cdef = s.node.get_subnode_graph();
        if (!cdef) {
          return false;
        }
        // The RECURSIVE guard-port set: covers state clocked on the port
        // directly, through a gate inside the child, and ports the child
        // merely forwards into ITS children — the stateless-wrapper levels
        // of a multi-boundary gate chain (tests/sim/gate_through_wrapper).
        return clock_guard_ports(cdef, port_cache).contains(port_id);
      }();
      if (!child_wants_tick) {
        return;
      }
      std::string     cond;
      hhds::Pin_class root = drv;
      if (auto cone = livehd::latch_contract::clock_op_of(drv, design_clocks); cone) {
        if (!cone->enables.empty() && !cone->clock_inverted && cone->div == 1) {
          for (const auto& gp : cone->enables) {
            ensure_fn(gp);
            absl::StrAppend(&cond, cond.empty() ? "" : " && ", emit_known_true(operand(gp, 1)));
          }
          root = cone->clock;
        } else if (!cone->enables.empty()) {
          root = {};  // inverted / divided gate: same named limits as the local fold — no guard word
        } else {
          root = cone->clock;
        }
      } else {
        root = livehd::latch_contract::control_root(drv).net;
      }
      // FORWARD my own guard word: when the (root of the) net feeding the
      // child's clock port is one of MY guarded input ports, whatever my
      // parent says about that port composes into what I tell the child.
      if (!root.is_invalid() && livehd::graph_util::is_graph_input_pin(root)) {
        for (const auto& io : ios) {
          if (io.is_input && io.raw == pin_name_of(root) && clock_in_fields.contains(io.field)) {
            absl::StrAppend(&cond, cond.empty() ? "" : " && ", "__in.", io.field, "__tick");
            break;
          }
        }
      }
      if (!cond.empty() && !bind_only) {
        fout->append(absl::StrCat("    ",
                                  s.inst,
                                  ".__gen += slop_update(",
                                  s.inst,
                                  ".__in.",
                                  cpp_port_path(port_name),
                                  "__tick, ",
                                  cond,
                                  ");  // gated clock into the child: it commits only when the gate is open\n"));
      }
    };

    auto emit_sub_call = [&](auto&& ensure_fn, const hhds::Node_class& node) -> void {
      if (!emitted_subs.insert(node.get_class_index()).second) {
        return;
      }
      for (const auto& s : subs) {
        if (s.node.get_class_index() != node.get_class_index()) {
          continue;
        }
        auto sio = node.get_subnode_io();
        if (!sio) {
          break;
        }
        demand_stack.push_back(absl::StrCat(s.inst, ".atomic"));
        struct PopA {
          std::vector<std::string>* v;
          ~PopA() { v->pop_back(); }
        } pop_a{&demand_stack};
        const std::string run_condition = sub_run_condition(ensure_fn, s);
        const bool        runtime_skip  = !run_condition.empty();
        // A negedge-only child advances ONLY its fall half here (eval_negedge +
        // a re-settle), never `cycle()`, so there is no `<inst>__o` snapshot to
        // name: like settle mode, its outputs bind straight at `__out`, which
        // the re-settle just refreshed. This IS reachable with `settle_mode`
        // false for a fall-only child, and reading `<inst>__o` there would name
        // an undeclared identifier in the generated C++.
        const bool        fall_advance  = fall && s.negedge_only;
        const bool        out_is_member = settle_mode || fall_advance;
        if (runtime_skip && !out_is_member) {
          // cycle() returns the pre-edge observation. Keep it outside the if so
          // downstream caller muxes can read a stable (masked) value when the
          // child is inactive.
          fout->append(absl::StrCat("    const auto* ", s.inst, "__o = &", s.inst, ".__out;\n"));
        }
        if (runtime_skip) {
          // BIND the input cones before the guard opens. ensure_fn DECLARES
          // what it binds at the current output position (`Slop<W> cg_N`, and a
          // demanded nested instance's own `<inst>__o`) while pin2var keeps
          // naming it for the rest of the method, so a cone first reached
          // inside the `if` would be scoped to that C++ block and out of scope
          // at its later consumers — the generated file would not compile.
          // It also keeps a demanded nested instance's advance unconditional:
          // emitted_subs marks it done, so an advance left under a false guard
          // is simply lost. Exactly the drivers the fill loop below asks for,
          // so nothing extra is demanded.
          for (const auto& d : sio->get_input_pin_decls()) {
            if (s.loop && s.loop->index_input && d.port_id == *s.loop->index_input) {
              continue;
            }
            auto pre_drv = sub_input_driver(s, d.name, d.port_id);
            ensure_fn(child_clock_data_driver(s, static_cast<uint32_t>(d.port_id), pre_drv));
            emit_child_tick(ensure_fn, s, d.name, static_cast<uint32_t>(d.port_id), pre_drv, /*bind_only=*/true);
          }
          fout->append(absl::StrCat("    if (", run_condition, ") {  // conditional activation (reset keeps it open)\n"));
        }
        // Ports are members: write straight into the child's __in — no local In,
        // no by-value copy at the call.
        for (const auto& d : sio->get_input_pin_decls()) {
          if (s.loop && s.loop->index_input && d.port_id == *s.loop->index_input) {
            continue;  // the wrapper supplies first + ordinal*step
          }
          auto drv       = sub_input_driver(s, d.name, d.port_id);
          int  wb        = d.bits > 0 ? static_cast<int>(d.bits) : 1;
          auto value_drv = child_clock_data_driver(s, static_cast<uint32_t>(d.port_id), drv);
          // Emit any pending operand cone on demand (conservative: stops at
          // state elements / consts; another atomic Sub recurses through this
          // same helper). A genuinely cyclic cone stays unbound and falls
          // through to the loud Stage-0 diagnostic below.
          ensure_fn(value_drv);
          // Stage 0: a valid, non-const driver feeding this instance input that is
          // not yet bound is a combinational cycle threading THROUGH this atomic Sub
          // call (the false-loop-through-instance case). Report it precisely.
          if (!value_drv.is_invalid() && !value_drv.is_const() && !pin2var.contains(value_drv.get_class_index())
              && !cycle_reported_) {
            livehd::diag::err("inou.cgen.sim", "comb-loop-through-instance", "unsupported")
                .msg(
                    "combinational loop through instance `{}` ({}::{}): input `{}` is fed by logic that depends on "
                    "this instance's own output",
                    s.inst,
                    gname,
                    s.callee_struct,
                    d.name)
                .hint(
                    "a sub-instance is simulated atomically (all inputs -> all outputs), so an output that feeds back "
                    "into one of its inputs forms a cycle the single-pass schedule cannot break; restructure so the "
                    "cone feeding this input is computed before the call, split the sub by output cone, or flatten "
                    "this instance for sim")
                .emit();
            cycle_reported_ = true;
          }
          emit_in_binding("    ",
                          s.inst,
                          cpp_port_path(d.name),
                          wb,
                          bind_operand(value_drv, wb, d.unsign),
                          forward_io_of(value_drv, wb));
          emit_child_tick(ensure_fn, s, d.name, static_cast<uint32_t>(d.port_id), drv);
        }
        // In settle mode the child is re-settled against the inputs just rebuilt
        // from the parent's COMMITTED state, and its outputs are read from its
        // own __out member. cycle() would advance it a second time in the period.
        if (fall_advance) {
          fout->append(absl::StrCat("    ", s.inst, ".__compact_fall();\n"));
          fout->append(absl::StrCat("    ", s.inst, ".__compact_publish();\n"));
        } else if (settle_mode) {
          fout->append(absl::StrCat("    ", s.inst, ".__compact_publish();\n"));
        } else if (staged_flat) {
          // This parent was admitted only when every child is an ordinary,
          // acyclic staged definition on the same reference rise. Evaluate
          // its pre-rise colors in the parent's window and bind the preserved
          // pre-rise observation directly. The recursive commit happens only
          // after the whole parent cone has parked its pending values.
          fout->append(absl::StrCat("    ", s.inst, ".__color_pre_rise();\n"));
          fout->append(absl::StrCat("    const auto& ", s.inst, "__o = ", s.inst, ".__last_out;\n"));
        } else if (runtime_skip) {
          fout->append(absl::StrCat("    ", s.inst, "__o = &", s.inst, ".__compact_advance();\n"));
        } else {
          fout->append(absl::StrCat("    const auto& ", s.inst, "__o = ", s.inst, ".__compact_advance();\n"));
        }
        if (runtime_skip) {
          fout->append("    }\n");
        }
        const std::string sub_out = out_is_member  ? absl::StrCat(s.inst, ".__out.")
                                    : runtime_skip ? absl::StrCat(s.inst, "__o->")
                                                   : absl::StrCat(s.inst, "__o.");
        for (const auto& d : sio->get_output_pin_decls()) {
          auto opin = find_driver_pin(node, d.name);
          if (!opin.is_invalid()) {
            pin2var[opin.get_class_index()] = absl::StrCat(sub_out, cpp_port_path(d.name));
            if (d.unsign) {
              mark_slop_u_binding(opin);
            }
          }
        }
        break;
      }
    };

    // Bind one combinational node's value. SINGLE-FANOUT nodes bind as a
    // parenthesized EXPRESSION instead of a named temp — the consumer inlines
    // the whole tree, the intermediate Slop is never materialized, and the
    // conversions that existed only to LAND the value in a typed variable go
    // with it (the host compiler then keeps the tree in registers). Everything
    // pasted is pure, every referenced name is an SSA temp / member / __in
    // field that is never reassigned within the pass, and pin2var is
    // pass-local, so an expression string means the same thing at any later
    // paste point — the same reasoning that lets `inst.__out.f` bindings work.
    // The one class of name that DOES get rewritten mid-emission is a
    // latency-1 memory read register (see seq_volatile_), so a cone reading one
    // materializes instead; that also ends the taint for its own consumers,
    // which is why testing the DIRECT drivers is enough.
    //
    // A node with fan-out > 1 (or a huge tree, or under LIVEHD_SIM_NOINLINE for
    // debuggability) still materializes, with `= expr` so the hlop cross-width
    // ctor's EXPLICIT keyword keeps its job: a node_expr that does not land at
    // the declared width fails the build instead of silently truncating and
    // sign-extending. `sub_width_expr_` opts a declaration into brace-init for
    // the one arm that deliberately returns a narrower expression (the Get_mask
    // raw pass-through), where the value is an unsigned canonical whose top
    // stored bit is clear, so the ctor's sign-extend equals the zext.
    const bool forest_ok = !env_.noinline;
    auto       bind_comb = [&](const hhds::Node_class& n, const hhds::Pin_class& dp, const char* tag) {
      sub_width_expr_              = false;
      const int   wb               = wbits_of(dp);
      const bool  unsigned_result  = proven_unsigned_result(n, dp);
      const bool  canonical_result = proven_canonical_unsigned_result(n, dp);
      const bool  use_u            = slop_u_ && unsigned_result;
      // Slop<W> is signed. A literal uW result therefore evaluates in a
      // Slop<W+1> carrier before landing as Slop_u<W>; this is the sole
      // headroom bit and is not part of the graph hint.
      const int   eval_bits        = unsigned_result ? wb + 1 : wb;
      std::string ex               = node_expr(n, eval_bits);

      const auto unsigned_landing = [&]() {
        if (sub_width_expr_) {
          return absl::StrCat("(", ex, ").zext_to_u<", wb, ">()");
        }
        if (debug_) {
          return absl::StrCat("Slop_u<", wb, ">::land(", ex, ")");
        }
        if (slop_u_expr_) {
          return ex;
        }
        if (canonical_result) {
          return absl::StrCat("Slop_u<", wb, ">::from_proven(", ex, ")");
        }
        return absl::StrCat("Slop_u<", wb, ">{", ex, "}");
      };

      bool frozen = false;  // reads a name the sequential section rewrites
      for (const auto& isnk : n.inp_sorted_pins()) {
        const auto idrv = isnk.get_driver_pin();
        if (!idrv.is_invalid() && seq_volatile_.contains(idrv.get_class_index())) {
          frozen = true;
          break;
        }
      }
      if (forest_ok && !frozen && ex.size() <= 4000) {
        int nout = 0;
        for (const auto& oe : n.out_edges()) {
          (void)oe;
          if (++nout > 1) {
            break;
          }
        }
        if (nout == 1) {
          if (use_u) {
            pin2var[dp.get_class_index()] = absl::StrCat("(", unsigned_landing(), ")");
            slop_u_values_.insert(dp.get_class_index());
          } else {
            pin2var[dp.get_class_index()] = absl::StrCat("(", ex, ")");
          }
          canonical_.insert(dp.get_class_index());
          return;
        }
      }
      auto var = absl::StrCat("cg_", std::to_string(tmp_cnt++));
      if (use_u) {
        const auto landing = unsigned_landing();
        fout->append(absl::StrCat("    Slop_u<", wb, "> ", var, " = ", landing, ";  // ", op_name(type_op_of(n)), tag, "\n"));
        slop_u_values_.insert(dp.get_class_index());
      } else {
        const char* open = sub_width_expr_ ? "{" : " = ";
        const char* shut = sub_width_expr_ ? "};" : ";";
        fout->append(absl::StrCat("    Slop<", eval_bits, "> ", var, open, ex, shut, "  // ", op_name(type_op_of(n)), tag, "\n"));
      }
      pin2var[dp.get_class_index()] = var;
      canonical_.insert(dp.get_class_index());  // node_expr result: canonical at its declared width
    };

    absl::flat_hash_set<pin_key_t> prefetch_seen;
    auto                           ensure_ready_impl = [&](auto&& self, const hhds::Pin_class& drv) -> void {
      if (drv.is_invalid() || drv.is_const()) {
        return;
      }
      if (pin2var.contains(drv.get_class_index())) {
        return;
      }
      if (!prefetch_seen.insert(drv.get_class_index()).second) {
        // Already walked this pin (or a combinational-cycle back-edge re-entered
        // it): stop instead of recursing forever -- an unbound cycle member is
        // left for operand() to report as the loud combinational-loop diagnostic
        // (unbounded self-recursion here stack-overflowed on BusyTable_1/Dispatch,
        // any comb cycle reaching a Memory operand; repro sim_loop_mem_prefetch).
        if (::getenv("LIVEHD_SIM_SPLIT_DEBUG") != nullptr && !demand_stack.empty()) {
          std::string chain;
          for (const auto& d2 : demand_stack) {
            chain += d2 + " -> ";
          }
          std::fprintf(stderr,
                       "[splitdbg] RE-ENTRY: %s%s (the cycle-closing demand)\n",
                       chain.c_str(),
                       debug_name(drv.get_master_node()).c_str());
        }
        return;
      }
      auto n   = drv.get_master_node();
      auto nop = type_op_of(n);
      if (::getenv("LIVEHD_SIM_SPLIT_DEBUG2") != nullptr && !demand_stack.empty()) {
        std::fprintf(stderr, "[splitdbg]   walk %s (%s)\n", debug_name(n).c_str(), op_name(nop));
      }
      if (nop == Ntype_op::Sub) {
        // A deferred-Moore instance's outputs are pre-bound (peek) -- nothing to
        // emit. Any other atomic call is emitted on demand, its own input cones
        // first (prefetch_seen above already broke re-entry on a real cycle).
        if (!moore_deferred.contains(n.get_class_index())) {
          emit_sub_call([&](const hhds::Pin_class& p) { self(self, p); }, n);
        }
        return;
      }
      if (nop == Ntype_op::Memory) {
        // Emit the memory on demand, like an atomic Sub call above. If this is a
        // RE-ENTRY during the memory's own emission (dout -> write cone -> dout,
        // a genuine cycle) the guard returns without binding and the caller's
        // operand() raises the loud diagnostic.
        emit_memory([&](const hhds::Pin_class& p) { self(self, p); }, n);
        return;
      }
      if (is_type_register(n)) {
        return;  // bound at its own emission; if still unbound it is a real cycle
      }
      if (Ntype::has_multiple_driver_pins(nop) || !n.has_out_edges()) {
        return;
      }
      for (const auto& isnk : n.inp_sorted_pins()) {
        const auto idrv = isnk.get_driver_pin();
        self(self, idrv);
      }
      auto dp = n.get_driver_pin(0);
      if (pin2var.contains(dp.get_class_index())) {
        return;
      }
      bind_comb(n, dp, " (mem-operand prefetch)");
    };
    auto ensure_ready = [&](const hhds::Pin_class& drv) { ensure_ready_impl(ensure_ready_impl, drv); };

    // combinational SSA bindings, in dependency order
    for (auto node : g->body().nodes(hhds::Node_order::forward)) {
      auto op = type_op_of(node);
      // The settle pass only needs the OUTPUT cone. Everything else in this walk
      // is next-state logic -- on a real design the large majority of it -- so
      // this filter is what keeps the second emission from doubling the generated
      // C++ (and with it the host clang++ time, which already dominates `lhd sim`).
      if (settle && !settle_cone.contains(node.get_class_index())) {
        continue;
      }
      // Same idea for the fall half: it needs only the cones feeding negedge
      // state, re-read from the members the rise just committed.
      if (fall && !fall_cone.contains(node.get_class_index())) {
        continue;
      }
      if (op == Ntype_op::Memory) {
        emit_memory(ensure_ready, node);
        continue;
      }
      if (op == Ntype_op::Sub) {
        // Instantiate the callee directly: build its In from this Sub's input
        // drivers, call child.cycle(), and register each output driver pin to the
        // returned struct field (no intermediate wires).
        if (fall_deferred.contains(node.get_class_index())) {
          for (const auto& s : subs) {
            if (s.node.get_class_index() == node.get_class_index()) {
              deferred_fall.push_back(&s);
              break;
            }
          }
          continue;
        }
        if (moore_deferred.contains(node.get_class_index())) {
          // outputs were pre-bound from peek(); the state-advancing cycle(in)
          // call is emitted after the walk, when its input operands are bound
          for (const auto& s : subs) {
            if (s.node.get_class_index() == node.get_class_index()) {
              deferred_moore.push_back(&s);
              break;
            }
          }
          continue;
        }
        emit_sub_call(ensure_ready, node);
        continue;
      }
      if (op == Ntype_op::Clock_cell) {
        // Timing-only. Local state folds this cell into its commit guard, and a
        // child clock port receives the root clock plus `<port>__tick` above.
        // Emitting the cell as ordinary combinational data would either fatal
        // in node_expr() or, worse, discard its enable.
        continue;
      }
      if (Ntype::has_multiple_driver_pins(op)) {
        continue;
      }
      if (!node.has_out_edges() || is_type_register(node)) {
        continue;
      }
      if (!live_.contains(node.get_class_index())) {
        // Nothing real consumes this value: lnast.tolg's packed-wire rewrites
        // can strand whole Or-trees
        // (ImmediateGenerator: 400 of 935 emitted values were dead). A skipped
        // dead ROOT (no out edges) would otherwise leave its entire operand
        // tree emitted-but-unused (-Wunused-variable noise, wasted compiles).
        continue;
      }
      auto dpin = node.get_driver_pin(0);
      if (pin2var.contains(dpin.get_class_index())) {
        continue;  // already emitted via a memory-operand prefetch
      }
      // forward_class built its order on the RAW graph; once a false
      // instance-level cycle has been dissolved (Moore-deferral / state-only
      // pre-binding above), a comb node can still be SCHEDULED before one of
      // its operands. Emit pending operand cones on demand; a genuinely cyclic
      // operand stays unbound (ensure_ready stops on re-entry) and operand()
      // below reports it as the loud Stage-0 diagnostic.
      // PLURAL reader: `node` may be a compact-loop Sub, whose carry-in sink is
      // the one sanctioned two-driver pin (the external SEED plus a self edge
      // from its own carry-out). The singular get_driver_pin() returns only one
      // of the two, so when the self edge came back first the SEED was never
      // made ready -- it never entered pin2var, and operand() then emitted it as
      // `0 /*UNRESOLVED-CYCLE*/`. That is order-dependent by construction, which
      // is why matched_filter's `cs` seed bound correctly while `xs` silently
      // became 0 and the whole correlator computed zeros.
      for (const auto& sink : node.inp_sorted_pins()) {
        for (const auto& drv : sink.get_driver_pins()) {
          ensure_ready(drv);
        }
      }
      bind_comb(node, dpin, "");
    }

    // Deferred Moore-sub calls: every comb value is bound now; the child call
    // only advances the child's state (its outputs were pre-bound from __out).
    // Inputs are bound from the instance's EXISTING sink edges (an unconnected
    // declared input has no created pin -- hhds asserts on a name lookup -- and
    // the In{} zero-init already models it as 0).
    //
    // Nothing to do in the settle pass: it never advances a child, and the
    // pre-binding above already pointed these outputs straight at `<inst>.__out`.
    for (const auto* sp : (pass_ == Pass::Rise) ? deferred_moore : std::vector<const Sub*>{}) {
      const auto& s    = *sp;
      auto        dsio = s.node.get_subnode_io();
      if (!dsio) {
        continue;
      }
      absl::flat_hash_map<uint32_t, const void*>                 bound_pids;  // pid -> decl (dedupe)
      absl::flat_hash_map<uint32_t, std::pair<std::string, int>> pid2in;
      absl::flat_hash_map<uint32_t, bool>                        pid2unsign;
      for (const auto& d : dsio->get_input_pin_decls()) {
        pid2in[static_cast<uint32_t>(d.port_id)]     = {d.name, d.bits > 0 ? static_cast<int>(d.bits) : 1};
        pid2unsign[static_cast<uint32_t>(d.port_id)] = d.unsign;
      }
      const std::string run_condition = sub_run_condition(ensure_ready, s);
      if (!run_condition.empty()) {
        // Bind the input cones BEFORE the guard opens — same reason as
        // emit_sub_call: a `cg_N` declared inside the block is out of scope at
        // the later consumers pin2var still names it for. Same pids, same
        // dedupe as the fill loop below, so nothing extra is demanded.
        absl::flat_hash_set<uint32_t> pre_seen;
        for (auto e_sink : s.node.inp_sorted_pins()) {
          for (auto e_drv : e_sink.get_driver_pins()) {
            auto pid = static_cast<uint32_t>(e_sink.get_port_id());
            auto it  = pid2in.find(pid);
            if (it == pid2in.end() || !pre_seen.insert(pid).second) {
              continue;
            }
            ensure_ready(child_clock_data_driver(s, pid, e_drv));
            emit_child_tick(ensure_ready, s, it->second.first, pid, e_drv, /*bind_only=*/true);
          }
        }
        fout->append(absl::StrCat("    if (", run_condition, ") {  // conditional activation (reset keeps it open)\n"));
      }
      for (auto e_sink : s.node.inp_sorted_pins()) {
        for (auto e_drv : e_sink.get_driver_pins()) {
          auto pid = static_cast<uint32_t>(e_sink.get_port_id());
          auto it  = pid2in.find(pid);
          if (it == pid2in.end() || !bound_pids.emplace(pid, nullptr).second) {
            continue;
          }
          // cpp_port_path for the same reason as the __pre read above: In mirrors a
          // tuple port as a nested struct, so the leaf is `io_data.instruction`.
          auto value_drv = child_clock_data_driver(s, pid, e_drv);
          ensure_ready(value_drv);
          emit_in_binding("    ",
                          s.inst,
                          cpp_port_path(it->second.first),
                          it->second.second,
                          bind_operand(value_drv, it->second.second, pid2unsign[pid]),
                          forward_io_of(value_drv, it->second.second));
          emit_child_tick(ensure_ready, s, it->second.first, pid, e_drv);
        }
      }
      fout->append(absl::StrCat("    ", s.inst, ".__compact_advance();  // deferred compact-kernel state advance\n"));
      if (!run_condition.empty()) {
        fout->append("    }\n");
      }
    }

    // Stage 0: if a combinational cycle survivor was hit anywhere above but the
    // precise Sub-input site did not already name it (a pure-cell comb loop with no
    // Sub on it), fail loudly with the best label we captured rather than emit a
    // silently-wrong sim that substitutes 0.
    if (cycle_unresolved_ && !cycle_reported_) {
      livehd::diag::err("inou.cgen.sim", "combinational-loop", "unsupported")
          .msg(
              "module `{}` has a combinational loop the single-pass sim schedule cannot order: {} was read before it "
              "could be computed",
              gname,
              cycle_first_label_.empty() ? std::string{"a value"} : cycle_first_label_)
          .hint(
              "inou.cgen.sim emits one sequential `cycle()` per module; a combinational cycle (a real loop, or a false "
              "loop through an atomic Sub instance) has no valid emission order")
          .emit();
      cycle_reported_ = true;
    }

    // flop next-state. Each stage/q: reset ? rstval : (enable ? value_in : hold),
    // mirroring cgen's always block. value_in = din for stage0, the previous
    // stage otherwise.
    //
    // TWO-PHASE TICK (todo/livehd/2f-latch M5). One tick is one clock PERIOD, and
    // a period contains a RISE then a FALL. Posedge state is evaluated here (from
    // pre-tick state) and committed below; NEGEDGE state is evaluated afterwards,
    // in the second phase, so it observes the POST-RISE value of anything the
    // posedge half just committed. That is the half-cycle transfer: a posedge
    // stage feeding a negedge stage on the same clock hands over WITHIN one
    // period, which the old single-phase commit turned into a full-cycle lag.
    //
    // A design with no negedge flop skips the fall re-evaluation; latches still
    // use the low/high window pair and memories remain in the rise phase.
    // Park the commit enable, for the same reason `_din` is parked: the ICG guard
    // operands and a secondary clock's level are combinational, and the commit
    // method cannot read them.
    //
    // The edge test compares against the PRE-TICK level, so EVERY flop's `_cen` --
    // negedge ones included -- is emitted in the RISE pass, before `prev_member`
    // is advanced. That is what preserves the rule both phases depend on: updating
    // the prev bit between the phases made every secondary negedge test compare
    // `cur` against itself and miss the edge permanently.
    auto emit_commit_enable = [&](const Flop& f) {
      if (f.clock_guards.empty() && f.sec_clock.is_invalid()) {
        return;  // ungated, on the reference clock: commits every tick
      }
      if (f.gate_fall && pass_ == Pass::Rise) {
        return;  // fall-gated: its guards are sampled POST-rise, in the fall pass
      }
      // The guard and secondary-clock cones are NOT in flop_operand_ports, so no
      // caller readies them on the flop's behalf. Doing it here covers every
      // path into `_cen` — an unready cone would otherwise reach operand() as
      // `0 /*UNRESOLVED-CYCLE*/` and the flop would silently never load.
      for (const auto& gp : f.clock_guards) {
        ensure_ready(gp);
      }
      ensure_ready(f.sec_clock);
      std::string cen;
      for (const auto& gp : f.clock_guards) {
        absl::StrAppend(&cen, cen.empty() ? "" : " && ", emit_known_true(operand(gp, 1)));
      }
      if (!f.sec_clock.is_invalid()) {
        const std::string cur = emit_known_true(operand(f.sec_clock, 1));
        absl::StrAppend(
            &cen,
            cen.empty() ? "" : " && ",
            f.posedge ? absl::StrCat("(", cur, " && !", f.prev_member, ")") : absl::StrCat("(!", cur, " && ", f.prev_member, ")"));
      }
      fout->append("    ", f.member, "_cen = ", cen, ";\n");
    };

    // `with_cen == false` emits ONLY the next value and leaves `_cen` alone. The
    // fall pass needs exactly that: its `_din` is computed from the POST-rise
    // state, but its commit enable was already computed in the rise pass.
    auto emit_flop_next = [&](const Flop& f, bool latch_low = false, bool latch_high = false, bool with_cen = true) {
      auto din      = get_driver(find_sink_pin(f.node, "din"));
      auto rstp     = get_driver(find_sink_pin(f.node, "reset_pin"));
      auto initp    = get_driver(find_sink_pin(f.node, "initial"));
      auto enp      = get_driver(find_sink_pin(f.node, "enable"));
      bool negreset = false;
      if (auto np = get_driver(find_sink_pin(f.node, "negreset")); np.is_const()) {
        negreset = !const_of(np).is_known_false();
      }
      const std::string rstval = initp.is_invalid() ? absl::StrCat(value_type(f.bits, f.unsign), "::create_integer(0)")
                                                    : stored_value_operand(initp, f.bits, f.unsign);
      std::string       rtest;  // C++ bool: reset asserted (empty = no reset)
      bool              reset_always = false;
      if (!rstp.is_invalid()) {
        if (rstp.is_const()) {
          reset_always = !const_of(rstp).is_known_false();
        } else {
          rtest = absl::StrCat(raw_operand(rstp, 1), negreset ? ".is_known_false()" : ".is_known_true()");
        }
      }
      std::string etest;  // C++ bool: write enabled (empty = always)
      if (!enp.is_invalid() && !enp.is_const()) {
        // `neg_enable` = an active-LOW latch gate: transparent while enable == 0.
        // is_known_false() rather than !is_known_true(), so an UNKNOWN enable
        // fails closed (holds) on both polarities instead of writing on X.
        etest = absl::StrCat(raw_operand(enp, 1), f.neg_enable ? ".is_known_false()" : ".is_known_true()");
      } else if (!enp.is_invalid() && f.neg_enable && enp.is_const()) {
        // A CONSTANT active-low gate folds here: const 0 => permanently
        // transparent (write every tick), anything else => never writes.
        if (!const_of(enp).is_known_false()) {
          etest = "false";
        }
      }
      auto next_of = [&](const std::string& value_in, const std::string& hold) -> std::string {
        if (reset_always) {
          return rstval;
        }
        std::string core = etest.empty() ? value_in : absl::StrCat("(", etest, " ? ", value_in, " : ", hold, ")");
        return rtest.empty() ? core : absl::StrCat("(", rtest, " ? ", rstval, " : ", core, ")");
      };
      // `_din` is a MEMBER (the commit runs in a separate method and has no
      // combinational value in scope, so the next value has to be parked here);
      // `_low`, the latch's low-window staging, stays a settle-local because it is
      // produced and consumed entirely inside this pass.
      auto        next_name  = [&](const std::string& signal) { return absl::StrCat(signal, latch_low ? "_low" : "_din"); };
      auto        hold_name  = [&](const std::string& signal) { return latch_high ? absl::StrCat(signal, "_low") : signal; };
      const char* decl       = latch_low ? "auto " : "";
      // LAZY GUARDED NEXT-STATE: a gated flop whose commit will not fire this
      // period does not need its `_din` computed at all — and with single-use
      // forestation the din EXPRESSION carries the flop's whole exclusive comb
      // chain, so a closed gate skips that entire cone. The guard wraps the
      // COMPUTE-phase assignment only; the two-phase structure stays (every din
      // still reads PRE-commit neighbor state — sinking the expression into the
      // commit itself would read post-commit values). `_cen` therefore emits
      // BEFORE the din block here instead of after. Latch windows keep their
      // unconditional evaluation (transparency semantics), and VCD builds keep
      // the eager form (an X-window dump reads dins the gate would skip).
      const bool  lazy_guard = !f.is_latch && !latch_low && !latch_high && vcd_file.empty() && !env_.nolazy
                               && (!f.clock_guards.empty() || !f.sec_clock.is_invalid() || !f.tick_field.empty());
      std::string gcond;
      if (lazy_guard) {
        if (with_cen) {
          emit_commit_enable(f);  // readies its own guard/secondary-clock cones
        }
        if (!f.clock_guards.empty() || !f.sec_clock.is_invalid()) {
          gcond = absl::StrCat(f.member, "_cen");
        }
        if (!f.tick_field.empty()) {
          absl::StrAppend(&gcond, gcond.empty() ? "" : " && ", "__in.", f.tick_field, "__tick");
        }
        fout->append("    if (", gcond, ") {  // gated: skip the whole next-state cone when the edge cannot fire\n");
      }
      const std::string din_expr = din.is_invalid() ? f.member : stored_value_operand(din, f.bits, f.unsign);
      if (f.depth <= 1) {
        fout->append("    ", decl, next_name(f.member), " = ", next_of(din_expr, hold_name(f.member)), ";\n");
      } else {
        fout->append("    ", decl, next_name(f.stages[0]), " = ", next_of(din_expr, hold_name(f.stages[0])), ";\n");
        for (size_t i = 1; i < f.stages.size(); ++i) {
          fout->append("    ", decl, next_name(f.stages[i]), " = ", next_of(f.stages[i - 1], hold_name(f.stages[i])), ";\n");
        }
        fout->append("    ", decl, next_name(f.member), " = ", next_of(f.stages.back(), hold_name(f.member)), ";\n");
      }
      if (lazy_guard) {
        fout->append("    }\n");
      } else if (with_cen && !latch_low) {
        emit_commit_enable(f);
      }
    };

    // Every driver pin emit_flop_next() will dereference. PHASE 2 drops the
    // bindings of everything reachable from a posedge Q, so each of these cones
    // has to be re-readied there — readying only `din` left a latch-qualified
    // enable unbound and operand() substituted a silent constant 0 into the
    // commit guard. (`negreset` is absent: it is only ever read as a constant.)

    // Commit one state element's next value under its ICG gate and/or its
    // secondary clock's edge. BOTH phases use this: a gated or secondary-clocked
    // flop needs the same guard whichever edge it commits on.
    // Pure member moves: everything combinational this needs was parked by the
    // settle (see emit_flop_next). That is what lets the commit be its own method.
    //
    // ICG fold (2f-latch M5): a gated flop commits only in ticks where its gate's
    // enable terms are true; without it the gate is DEAD CODE and the flop loads
    // every tick regardless — a silently wrong waveform. SECONDARY clock (M6): a
    // flop on a different net commits only on a detected EDGE of it, which is why
    // the shared prev bit is updated once per tick rather than per phase — two
    // clocks whose edges land in the same tick then both sample pre-edge values,
    // which is what IEEE 1800 requires for coincident edges. Both are folded into
    // `_cen`.
    auto commit_state = [&](const auto& f) {
      const bool        cen     = !f.clock_guards.empty() || !f.sec_clock.is_invalid();
      const bool        guarded = cen || !f.tick_field.empty();
      const std::string ind     = guarded ? "      " : "    ";
      if (guarded) {
        std::string cond;
        if (cen) {
          cond = absl::StrCat(f.member, "_cen");
        }
        if (!f.tick_field.empty()) {
          // The clock arrived on a PORT, and the parent says whether it ticked.
          absl::StrAppend(&cond, cond.empty() ? "" : " && ", "__in.", f.tick_field, "__tick");
        }
        fout->append("    if (", cond, ") {  // commit only when this element's clock edge actually fired\n");
      }
      // Compare-on-write commits: __gen advances only when the stored value
      // actually changed — a quiesced pipeline (same state, same din) stops
      // bumping and the change-gated settles above it go idle.
      for (const auto& s : f.stages) {
        fout->append(ind, "__gen += slop_update(", s, ", ", s, "_din);\n");
      }
      fout->append(ind,
                   "__gen += slop_update(",
                   f.member,
                   ", ",
                   f.member,
                   f.posedge ? "_din);\n" : "_din);  // negedge: commits at the FALL\n");
      if (guarded) {
        fout->append("    }\n");
      }
    };

    // NOTE the visited set: this walk recurses through the in-pin walk, and
    // without dedup a reconvergent cone re-walks every diamond on every path —
    // measured EXPONENTIAL on minion's intpipe_top (the emission sat for 25+ minutes
    // inside this lambda; every ICG enable latch's window invalidates a cone
    // full of shared CSR decode logic). Erasing a binding twice is idempotent,
    // so visiting each node once is exactly equivalent and linear.
    absl::flat_hash_set<hhds::Class_index> invalidate_seen;
    auto                                   invalidate_upstream_impl = [&](auto&& self, const hhds::Pin_class& p) -> void {
      if (p.is_invalid() || p.is_const() || livehd::graph_util::is_graph_input_pin(p)) {
        return;
      }
      auto n = p.get_master_node();
      if (!invalidate_seen.insert(n.get_class_index()).second) {
        return;
      }
      if (is_type_register(n) || type_op_of(n) == Ntype_op::Memory || type_op_of(n) == Ntype_op::Sub) {
        return;
      }
      auto dp = n.get_driver_pin(0);
      if (!dp.is_invalid()) {
        pin2var.erase(dp.get_class_index());
        slop_u_values_.erase(dp.get_class_index());
        prefetch_seen.erase(dp.get_class_index());
      }
      for (const auto& sink : n.inp_sorted_pins()) {
        const auto drv = sink.get_driver_pin();
        self(self, drv);
      }
    };
    auto invalidate_upstream
        = [&](auto&& /*self: kept for the callers' recursive-call signature*/, const hhds::Pin_class& p) -> void {
      invalidate_seen.clear();  // per-invocation: each window's invalidation is its own wave
      invalidate_upstream_impl(invalidate_upstream_impl, p);
    };
    auto invalidate_downstream = [&](const hhds::Pin_class& root) {
      std::vector<hhds::Pin_class>           work{root};
      absl::flat_hash_set<hhds::Class_index> seen;
      while (!work.empty()) {
        auto p = work.back();
        work.pop_back();
        if (p.is_invalid() || !seen.insert(p.get_class_index()).second) {
          continue;
        }
        for (const auto& e : p.out_edges()) {
          auto n = e.sink.get_master_node();
          if (is_type_register(n) || type_op_of(n) == Ntype_op::Memory || type_op_of(n) == Ntype_op::Sub
              || Ntype::has_multiple_driver_pins(type_op_of(n))) {
            continue;
          }
          auto dp = n.get_driver_pin(0);
          if (!dp.is_invalid()) {
            pin2var.erase(dp.get_class_index());
            slop_u_values_.erase(dp.get_class_index());
            prefetch_seen.erase(dp.get_class_index());
            work.push_back(dp);
          }
        }
      }
    };

    // PHASE 0/1 (low settle, then the rise).  An active-low latch is still
    // transparent immediately before the rising edge, so a posedge consumer at
    // that same edge samples the latch's just-settled value, not its previous
    // committed member.  Stage every latch first, then temporarily bind the Q of
    // active-low latches to its staged value while building posedge next-state.
    // This is the L1/coincident-edge rule and is equivalent to Verilog's latch
    // transparency before the active event without requiring an event queue.
    //
    // The SETTLE pass skips this whole block. It computes next-state, and a settle
    // has no next-state to compute; more importantly the windows rebind the
    // reference clock pin to constants and invalidate operand cones around each
    // one, which would corrupt the committed-state bindings the settle depends on.
    // Skipping it also leaves every latch Q bound to its plain member (the
    // end-of-period committed value), which is the right post-edge reading -- the
    // `<q>_low` transparency binding is a within-period concern.
    bool              any_negedge    = false;
    const std::string ref_clock_name = clock_input_of(g);
    const auto        ref_clock_pin  = ref_clock_name.empty() ? hhds::Pin_class{} : g->get_input_pin(ref_clock_name);
    // Low window: force the synthetic reference clock to zero and stage every
    // latch into `<q>_low`.  This works for both explicit active-low cells and a
    // source-level `if !clk` gate (whose cell polarity remains active-high).
    for (const auto& f : flops) {
      if (pass_ != Pass::Rise || !f.is_latch) {
        continue;
      }
      for (const auto* port : flop_operand_ports) {
        invalidate_upstream(invalidate_upstream, get_driver(find_sink_pin(f.node, port)));
      }
      if (!ref_clock_pin.is_invalid()) {
        pin2var[ref_clock_pin.get_class_index()]
            = slop_u_ && is_unsign(ref_clock_pin) ? "Slop_u<1>::create_integer(0)" : "Slop<1>::create_integer(0)";
      }
      for (const auto* port : flop_operand_ports) {
        ensure_ready(get_driver(find_sink_pin(f.node, port)));
      }
      emit_flop_next(f, true, false);
    }
    // High window: rebuild with the reference clock at one.  The low-window
    // result is the hold value, so a latch open in either half retains the last
    // transparent value and closes with the correct end-of-period state.
    for (const auto& f : flops) {
      if (pass_ != Pass::Rise || !f.is_latch) {
        continue;
      }
      for (const auto* port : flop_operand_ports) {
        invalidate_upstream(invalidate_upstream, get_driver(find_sink_pin(f.node, port)));
      }
      if (!ref_clock_pin.is_invalid()) {
        pin2var[ref_clock_pin.get_class_index()]
            = slop_u_ && is_unsign(ref_clock_pin) ? "Slop_u<1>::create_integer(1)" : "Slop<1>::create_integer(1)";
      }
      // A latch Q read DURING THE HIGH WINDOW is that latch's post-rise value —
      // its staged `_din`, already emitted by this same loop for every latch
      // EARLIER in flop order — not the pre-rise member. The member binding
      // made a gated enable chain (`if (clk and e1l) { e2l = e2 }`, the ICG
      // enable-latch cascade) sample with the PREVIOUS period's gate value
      // (tests/sim/chained_clock_gates_mixed_phase.prp). A latch LATER in flop
      // order keeps the member binding, exactly as before.
      for (const auto& f2 : flops) {
        if (&f2 == &f) {
          break;
        }
        if (!f2.is_latch) {
          continue;
        }
        auto q2 = f2.node.get_driver_pin(0);
        if (!q2.is_invalid()) {
          pin2var[q2.get_class_index()] = absl::StrCat(f2.member, "_din");
          if (f2.unsign) {
            mark_slop_u_binding(q2);
          }
        }
      }
      for (const auto* port : flop_operand_ports) {
        ensure_ready(get_driver(find_sink_pin(f.node, port)));
      }
      emit_flop_next(f, false, true);
    }
    if (pass_ == Pass::Rise && !ref_clock_pin.is_invalid()) {
      pin2var[ref_clock_pin.get_class_index()] = absl::StrCat("__in.", cpp_port_path(ref_clock_name));
      // The `__in` field's type came from io.unsign (decl, default SIGNED), so
      // that is the authority here -- not is_unsign() on the pin, which reports
      // unsigned by attribute ABSENCE. The live front ends do call set_unsign()
      // on a clock port, but a hand-built or library GraphIO (abc_map's
      // blackbox_io, for one) does not.
      for (const auto& io : ios) {
        if (io.is_input && io.raw == ref_clock_name) {
          if (io.unsign) {
            mark_slop_u_binding(ref_clock_pin);
          }
          break;
        }
      }
    }
    for (const auto& f : flops) {
      if (pass_ == Pass::Rise && f.is_latch) {
        auto qpin = f.node.get_driver_pin(0);
        if (!qpin.is_invalid()) {
          invalidate_downstream(qpin);
          pin2var[qpin.get_class_index()] = absl::StrCat(f.member, "_low");
          if (f.unsign) {
            mark_slop_u_binding(qpin);
          }
        }
      }
    }
    for (const auto& f : flops) {
      if (pass_ != Pass::Rise) {
        break;  // the rise owns posedge next-state; the fall emits its own below
      }
      if (!f.is_latch && f.posedge) {
        for (const auto* port : flop_operand_ports) {
          ensure_ready(get_driver(find_sink_pin(f.node, port)));
        }
        emit_flop_next(f);
      } else if (!f.is_latch && !f.posedge) {
        any_negedge = true;
        // Its `_din` belongs to the fall pass (post-rise state), but its `_cen`
        // belongs HERE — see emit_commit_enable: every phase's edge test must read
        // the same pre-tick secondary-clock level, and the prev bit advances at
        // the end of this pass. (emit_commit_enable readies the guard and
        // secondary-clock cones itself.)
        emit_commit_enable(f);
      }
    }

    // Outputs from the state this pass reads. In the CYCLE pass that is the
    // PRE-edge state, and the result is the returned `o`/`__last_out` (what the
    // output drove during the period). In the SETTLE pass it is the committed
    // state, and the result lands directly in the `__out` member a testbench read binds.
    // (The FALL pass drives no outputs: `__last_out` is the during-period value
    // the rise recorded, and `__out` is refreshed by the trailing settle.)
    // `__o` (not `o`): every other identifier this emitter mints is `__`-prefixed
    // precisely so a source-derived struct member can never shadow it. A bare `o`
    // broke that — a sub instance named `o` (the LHS variable of `mut o =
    // packer(…)`) put a local `Out o` on top of the member, and every `o.<...>`
    // in the pass after it resolved to the output struct.
    const std::string out_dst = settle ? "    __out." : "    __o.";
    if (!fall) {
      if (!settle) {
        fout->append("    Out __o;\n");
      }
      for (const auto& io : ios) {
        if (io.is_input) {
          continue;
        }
        auto spin = g->get_output_pin(io.raw);
        auto drv  = spin.is_invalid() ? hhds::Pin_class{} : get_driver(spin);
        if (drv.is_invalid()) {
          fout->append("    // output ", io.field, " is undriven\n");
        } else {
          // ensure_ready FIRST. This loop runs AFTER the latch windows above have
          // called `invalidate_downstream(qpin)`, which erases the pin2var binding
          // of every comb node transitively downstream of a latch Q so the
          // post-window value gets re-emitted. A module OUTPUT driven by such a
          // node — `assign clk_o = clk_i & en_latch` in minion's own prim_clk_gate
          // is the canonical one — then read an erased binding, and `operand()`
          // reported that as `negedge-operand-unresolved: a combinational-cycle
          // back-edge`. It is neither: there is no cycle and, for prim_clk_gate,
          // no negedge state at all (the module has no fall pass). Every other
          // operand() site after the invalidation already re-readies.
          ensure_ready(drv);
          fout->append(out_dst, io.field, " = ", operand(drv, io.bits), ";\n");
        }
      }
    }

    // The settle pass ends here: everything below is state advance (VCD sample,
    // memory commit, flop commit, the fall phase) and must happen exactly once
    // per period.
    if (settle) {
      if (!gate_done.empty()) {
        // Same store-the-entry-pair epilogue as the far tail (settle bodies —
        // full and per-group — close HERE, which is why the settle gate was
        // dead code until this store existed), including the transitive bump.
        fout->append("    if ((", gate_ksum, ") != __k0) ++__gen;\n");
        fout->append("    ", gate_done, " = __g0;\n");
        fout->append("    ", gate_kids, " = __k0;\n");
      }
      fout->append("}\n");
      close_body();
      return true;
    }

    // Everything from here to the posedge commit is the RISE half: it advances
    // state and must happen exactly once per period.
    if (!fall) {
      // VCD sample + dump (pre-commit, current-state values at this cycle's
      // timestamp). EVERY traced instance snapshots its values here -- a sub's
      // cycle() runs mid-way through its parent's comb walk, so by the parent's
      // dump point the sub's live flops are already one edge ahead; the snapshot
      // preserves this-period semantics. Only the ROOT (the instance holding the
      // writer; peek() clears path+writer to suppress dumping) then walks the
      // hierarchy in timestamp-ORDERED phases (the writer hard-rejects any past
      // timestamp, so per-instance inline dumping cannot interleave correctly).
      //
      // One cycle() call advances one clock period of `__clk_ratio` ticks (10 VCD
      // time-units each). With the settle window (compile.sim.vcdfakedelay, the
      // default), edges and data are spread out so a waveform viewer shows
      // clock-edge -> data causality:
      //   base          : clock rises 0->1; root inputs take this period's pokes;
      //                   any about-to-change data goes X ("computing")
      //   base + 3      : posedge-sourced data settles (just after the rising edge)
      //   base + half   : clock falls 1->0   (half = ratio*5, the period midpoint)
      //   base + half+3 : negedge-sourced data settles (just after the falling edge)
      // Without it (vcdfakedelay=false) data lands exactly ON its clock edge: no X,
      // no +3 offset (the traditional, smaller trace). change() only writes when a
      // value differs from the previous timestamp.
      if (vcd_on) {
        // UNCONDITIONAL (not gated on __vcd_trace): on the very first traced cycle
        // the root's lazy __vcd_init below has not run yet -- and every sub already
        // cycled earlier in this comb walk -- so a gated snapshot would dump
        // default-zero values for the whole first period (the reset cycle of a lec
        // witness, or the opening cycle of a --vcd-from window).
        for (const auto& v : vsig) {
          fout->append(absl::StrCat("    ", v.snap, " = ", v.accessor, ";\n"));
        }
        fout->append("    if (!__vcd && !__vcd_path.empty()) __vcd_init();\n");
        fout->append("    if (__vcd) {\n");
        fout->append("      const unsigned __b    = __vcd_tick * 10;\n");
        fout->append("      const unsigned __half = (__clk_ratio > 0 ? __clk_ratio : 1) * 5;\n");
        // rising clock edge, in every clocked scope; root inputs change AT the edge
        fout->append("      vcd::global_timestamp = __b;\n");
        fout->append("      __vcd_clk(__vcd.get(), true);\n");
        fout->append("      __vcd_dump_in(__vcd.get());\n");
        if (vcd_fakedelay) {
          fout->append("      __vcd_dump_x(__vcd.get(), true, true);\n");
          fout->append("      vcd::global_timestamp = __b + 3;\n");
        }
        fout->append("      __vcd_dump_data(__vcd.get(), true, true);\n");
        // falling clock edge at the period midpoint, then the negedge-sourced data
        // (the calls recurse the whole subtree; scopes with none write nothing)
        fout->append("      vcd::global_timestamp = __b + __half;\n");
        fout->append("      __vcd_clk(__vcd.get(), false);\n");
        if (vcd_fakedelay) {
          fout->append("      __vcd_dump_x(__vcd.get(), false, true);\n");
          fout->append("      vcd::global_timestamp = __b + __half + 3;\n");
        }
        fout->append("      __vcd_dump_data(__vcd.get(), false, true);\n");
        fout->append("    }\n");
      }
      // A VCD build advances every module's trace clock even while its window
      // is closed. A non-VCD build has neither the counter nor this hot update.
      if (vcd_on) {
        fout->append("    __vcd_tick += (__clk_ratio > 0 ? __clk_ratio : 1);\n");
      }

      if (staged_flat) {
        // The pre-rise output is an observation version, not a value the commit
        // is allowed to reconstruct after overwriting Q. Close the evaluation
        // color here; everything below is a state action over parked members.
        fout->append("    __last_out = __o;  // preserve the pre-rise observation version\n");
        if (!gate_done.empty()) {
          fout->append("    ", gate_done, " = __g0;\n");
          fout->append("    ", gate_kids, " = __k0;\n");
        }
        if (!subs.empty()) {
          fout->append("    __color_kids_before = ", staged_kids_sum, ";\n");
        }
        fout->append("}\n");
        fout->append("void ", mod, "::__color_rise_commit() {\n");
        for (const auto& s : subs) {
          fout->append("    ", s.inst, ".__color_rise_commit();\n");
        }
        if (!subs.empty()) {
          fout->append("    if ((", staged_kids_sum, ") != __color_kids_before) ++__gen;\n");
        }
      }

      // Memories are posedge state.  Stage and commit them while every operand is
      // still the PRE-RISE binding, alongside the posedge flops below.  The old
      // placement after phase 2 was both semantically late and unsafe: phase 2 had
      // erased cones reachable from a posedge Q, so a write enable/data/address in
      // one of those cones degraded to `0 /*UNRESOLVED-CYCLE*/` and the write was
      // silently dropped.
      for (const auto& m : mems) {
        if (m.is_whole() && !m.registered()) {
          continue;  // combinational whole-array: contents applied in the combinational section
        }
        // LAZY WRITE STAGING: evaluate the (cheap, shared) write-enable first; a
        // known-false enable skips the address and data cones entirely — with
        // forestation those are whole inlined expressions, so a gated / idle write
        // port costs one boolean test.
        //
        // The skip is NOT free on its own: `stage_write` opens with `p.clear()`,
        // which is what CANCELS a write this port already staged eagerly from the
        // combinational section (stage_through, for a latency-0 read's program-order
        // resolution). Skipping the call would leave that entry live and `tick()`
        // would commit it. `clear_pending()` up front restores the invariant the
        // eager form got for free — every visited port's pending state is
        // re-established from THIS cycle's enable — so the skip is once again
        // behavior-identical. The ports the loop skips below (reads, invalid
        // addr/din, const-false enable) are exactly the ones stage_through skips
        // too, so nothing that was meant to survive is dropped.
        const bool lazy_wr = vcd_file.empty() && !env_.nolazy;
        bool       cleared = false;
        for (const auto& p : m.ports) {
          if (p.rd || p.addr.is_invalid() || p.din.is_invalid()) {
            continue;
          }
          if (p.enable.is_known_false()) {
            continue;
          }
          if (lazy_wr) {
            if (!cleared) {
              fout->append(absl::StrCat("    ", m.member, ".clear_pending();  // a skipped stage_write must still cancel\n"));
              cleared = true;
            }
            fout->append(absl::StrCat("    { auto _w = ",
                                      emit_wen(m, p),
                                      "; if (!_w.is_known_false()) ",
                                      m.member,
                                      ".stage_write<",
                                      p.wridx,
                                      ">(_w, ",
                                      operand(p.addr, std::max(1, bits_of(p.addr))),
                                      ", ",
                                      stored_value_operand(p.din, m.bits, m.unsign),
                                      "); }\n"));
          } else {
            fout->append(absl::StrCat("    ",
                                      m.member,
                                      ".stage_write<",
                                      p.wridx,
                                      ">(",
                                      emit_wen(m, p),
                                      ", ",
                                      operand(p.addr, std::max(1, bits_of(p.addr))),
                                      ", ",
                                      stored_value_operand(p.din, m.bits, m.unsign),
                                      ");\n"));
          }
        }
        for (const auto& p : m.ports) {
          if (p.rd && m.type == 1 && !p.addr.is_invalid()) {
            const int         ab   = std::max(1, bits_of(p.addr));
            // A latency-1 read is a REGISTER on the array's clock, so it takes the
            // same gate the writes take. Latching it unconditionally made the read
            // data follow the address through periods the gate holds — the silent
            // half of the memory clock-gate fold.
            const std::string cond = mem_gate_cond(m);
            fout->append(absl::StrCat("    ",
                                      cond.empty() ? "" : absl::StrCat("if (", cond, ") "),
                                      "__gen += slop_update(",
                                      m.member,
                                      "_q",
                                      p.rdidx,
                                      ", ",
                                      m.member,
                                      ".read<",
                                      p.rdidx,
                                      ">(",
                                      operand(p.addr, ab),
                                      "));  // sync read: the RESOLVED value\n"));
          }
        }
        std::string whole_close;
        if (m.has_reset_stage()) {
          const int W = m.bits * m.size;
          // A reset reloads the `initial` contents in one cycle and cancels the
          // staged per-port writes (tolg already gates their enables with
          // !reset). For a per-port memory this is the only whole-array arm:
          // the `else` below then holds nothing.
          if (!m.reset.is_invalid()) {
            const std::string initbus = m.init.is_invalid() ? absl::StrCat(value_type(W, m.unsign), "::create_integer(0)")
                                                            : stored_value_operand(m.init, W, m.unsign);
            fout->append(absl::StrCat("    if ((",
                                      raw_operand(m.reset, 1),
                                      ").is_known_true()) { __gen += ",
                                      m.member,
                                      ".apply_update(",
                                      initbus,
                                      "); ",
                                      m.member,
                                      ".clear_pending(); } else {\n"));
            whole_close = "    }\n";
          }
        }
        if (m.is_whole()) {
          const int   W = m.bits * m.size;
          // The WHOLE-ARRAY update bus takes the same gated-clock guard the
          // per-port writes get through emit_wen: without it a clock-gated whole
          // array re-applies its update every tick with the gate as dead code,
          // which is the same silent miscompile one level up.
          std::string ucond;
          if (!m.update_enable.is_invalid()) {
            absl::StrAppend(&ucond, emit_known_true(raw_operand(m.update_enable, 1)));
          }
          if (const std::string gc = mem_gate_cond(m); !gc.empty()) {
            absl::StrAppend(&ucond, ucond.empty() ? "" : " && ", gc);
          }
          const std::string ue = ucond.empty() ? "" : absl::StrCat("if (", ucond, ") ");
          fout->append(absl::StrCat("    ",
                                    ue,
                                    "__gen += ",
                                    m.member,
                                    ".apply_update(",
                                    stored_value_operand(m.update, W, m.unsign),
                                    ");\n"));
        }
        fout->append(whole_close);
        fout->append(absl::StrCat("    __gen += ", m.member, ".tick();\n"));
      }

      // commit flops (+ pipe stages) at the clock edge (PHASE 1: the rise)
      for (const auto& f : flops) {
        if (!f.posedge) {
          continue;  // negedge state commits in phase 2, below
        }
        commit_state(f);
      }
    }  // end of the rise half

    // THE FALL half: negedge state, evaluated AFTER the rise committed.
    //
    // This is now its own method with a fresh binding environment, so the old
    // invalidate/re-ready dance is gone: every flop Q was bound in the prologue to
    // its just-committed member, and the walk above re-emitted the fall cone from
    // there. Data crossing a SUB-INSTANCE is no longer refused either — the child
    // ran its whole cycle() during the rise, so `settle_mode` has the walk ask it
    // for a comb re-settle (`sub.__settle`) rather than a second advance, which is
    // exactly what the `negedge-through-instance` refusal existed to prevent.
    if (fall) {
      (void)any_negedge;
      // A negedge-only child samples after the parent's rise.  Its current-state
      // outputs were pre-bound before the walk, but its state-advancing call is
      // delayed until now so inputs driven by parent posedge Qs are rebuilt from
      // the just-committed members.  A local negedge consumer must see the child's
      // POST-fall state in this same period, and it does so for free: the child's
      // own cycle() ends with its trailing settle, so `<inst>.__out` already holds
      // the outputs of the state it just committed. This used to need a second
      // full peek() (a whole-subtree snapshot/restore) after the call.
      // Bind the member directly -- unlike the pre-bind above, nothing runs after
      // this point that would advance the child again, so no copy is needed.
      for (const auto* sp : deferred_fall) {
        const auto& s   = *sp;
        auto        sio = s.node.get_subnode_io();
        if (!sio) {
          continue;
        }
        const std::string run_condition = sub_run_condition(ensure_ready, s);
        if (!run_condition.empty()) {
          // Bind the input cones BEFORE the guard opens — same reason as
          // emit_sub_call: a `cg_N` declared inside the block is out of scope
          // at the later consumers pin2var still names it for.
          for (const auto& d : sio->get_input_pin_decls()) {
            auto pre_drv = sub_input_driver(s, d.name, d.port_id);
            ensure_ready(child_clock_data_driver(s, static_cast<uint32_t>(d.port_id), pre_drv));
            emit_child_tick(ensure_ready, s, d.name, static_cast<uint32_t>(d.port_id), pre_drv, /*bind_only=*/true);
          }
          fout->append(absl::StrCat("    if (", run_condition, ") {  // conditional activation (reset keeps it open)\n"));
        }
        // Ports are members: rebuild the child's __in from the just-committed
        // parent state, then advance it (no local In, no by-value copy).
        for (const auto& d : sio->get_input_pin_decls()) {
          auto drv       = sub_input_driver(s, d.name, d.port_id);
          int  wb        = d.bits > 0 ? static_cast<int>(d.bits) : 1;
          auto value_drv = child_clock_data_driver(s, static_cast<uint32_t>(d.port_id), drv);
          ensure_ready(value_drv);
          emit_in_binding("    ",
                          s.inst,
                          cpp_port_path(d.name),
                          wb,
                          bind_operand(value_drv, wb, d.unsign),
                          forward_io_of(value_drv, wb));
          emit_child_tick(ensure_ready, s, d.name, static_cast<uint32_t>(d.port_id), drv);
        }
        fout->append(absl::StrCat("    ", s.inst, ".__compact_advance();\n"));
        if (!run_condition.empty()) {
          fout->append("    }\n");
        }
        for (const auto& d : sio->get_output_pin_decls()) {
          auto opin = find_driver_pin(s.node, d.name);
          if (!opin.is_invalid()) {
            pin2var[opin.get_class_index()] = absl::StrCat(s.inst, ".__out.", cpp_port_path(d.name));
            if (d.unsign) {
              mark_slop_u_binding(opin);
            }
          }
        }
      }
      // Re-ready EVERY cone the negedge emission below will read: the four
      // operand ports of emit_flop_next(), plus the ICG guards and secondary
      // clock that commit_state() reads. Readying only `din` is not enough — a
      // reg-file whose write strobe is a transparent latch AND-ed with posedge
      // state leaves `enable` in the erased set, and operand() then substitutes a
      // constant 0 into the commit guard, so the flop silently never loads.
      // ensure_ready() no-ops on invalid and const pins, so absent ports are free.
      for (const auto& f : flops) {
        if (f.posedge) {
          continue;
        }
        for (const auto* port : flop_operand_ports) {
          ensure_ready(get_driver(find_sink_pin(f.node, port)));
        }
        for (const auto& gp : f.clock_guards) {
          ensure_ready(gp);
        }
        ensure_ready(f.sec_clock);
      }
      for (const auto& f : flops) {
        if (f.posedge) {
          continue;
        }
        // `_din` only: `_cen` was computed in the rise pass, against the pre-tick
        // secondary-clock level both phases must observe. EXCEPTION: a
        // fall-GATED flop (gate_fall) samples its ICG guards at the fall — the
        // guard latches were transparent during the high phase, so only the
        // post-rise values are the ones its silicon enable latch pinned; its
        // `_cen` is therefore computed HERE, from the re-readied cones above.
        if (f.gate_fall) {
          emit_commit_enable(f);
        }
        emit_flop_next(f, /*latch_low=*/false, /*latch_high=*/false, /*with_cen=*/false);
      }
      for (const auto& f : flops) {
        if (f.posedge) {
          continue;
        }
        commit_state(f);
      }
    }

    // Secondary-clock levels for the NEXT tick's edge detection. Both phases'
    // guards must observe the same PRE-TICK level -- updating this between them
    // made every secondary negedge test compare `cur` against itself and miss the
    // edge permanently. That still holds now the phases are separate methods: the
    // fall reads no level at all (emit_commit_enable parked every `_cen`, negedge
    // ones included, back in the rise), so advancing the bit at the end of the
    // RISE is safe and, unlike the fall, always runs.
    if (!fall) {
      absl::flat_hash_set<std::string> done;
      for (const auto& f : flops) {
        if (f.prev_member.empty() || !done.insert(f.prev_member).second) {
          continue;
        }
        fout->append("    ", f.prev_member, " = (", operand(f.sec_clock, 1), ").is_known_true();\n");
      }
    }

    // Fail closed on a PHASE-2 substitution. Stage 0 above already cleared this
    // graph of real combinational cycles, so an operand that is STILL unbound
    // here is not a loop: it is a negedge cone that phase 2 dropped and did not
    // re-ready, and operand() has quietly put a 0 in its place. Emitting that
    // would be a silently wrong sim (the failure mode the whole fail-closed pass
    // exists to prevent), so refuse instead.
    if (cycle_unresolved_ && !cycle_reported_) {
      livehd::diag::err("inou.cgen.sim", "negedge-operand-unresolved", "internal")
          .msg("module `{}` has a NEGEDGE state element whose operand {} could not be re-resolved after the rise",
               gname,
               cycle_first_label_.empty() ? std::string{"(unnamed)"} : cycle_first_label_)
          .hint(
              "the two-phase tick drops the pin2var bindings reachable from a posedge Q and re-emits them via "
              "ensure_ready(); every pin the negedge emission reads (din, enable, reset_pin, initial, the ICG "
              "clock_guards and any secondary clock) must be re-readied — tracked as todo/livehd/2f-latch M5")
          .emit();
      cycle_reported_ = true;
      return false;
    }

    if (!fall && !staged_flat) {
      // The during-period outputs, recorded before any commit -- the value the
      // query engine publishes as `"sampled":"during_period"`.
      fout->append("    __last_out = __o;  // 2f-sim B: free output observation for the query engine\n");
    }
    if (!gate_done.empty() && !staged_flat) {
      // TRANSITIVE change propagation: my direct-kids sum only sees children's
      // OWN __gen, and a stateless wrapper never bumps its own — so a
      // grandchild's still-moving pipeline would be invisible to MY parent and
      // it would gate this whole subtree off mid-flight (minion_top skipped
      // while core_top's grandchildren still retired instructions). If the
      // subtree advanced during this run, reflect it in MY __gen; the wave
      // bubbles one level per run and dies out one run after the deepest
      // mover quiesces.
      fout->append("    if ((", gate_ksum, ") != __k0) ++__gen;\n");
      fout->append("    ", gate_done, " = __g0;\n");
      fout->append("    ", gate_kids, " = __k0;\n");
    }
    fout->append("}\n");
    close_body();
    return true;
  };
  if (!color_runtime_root && !color_storage_only) {
    if (!emit_period_body(Pass::Rise)) {
      return;
    }
    if (has_fall && !emit_period_body(Pass::Fall)) {
      return;
    }
  }
  if (!color_runtime_root && !color_storage_only) {
    if (!emit_period_body(Pass::Settle)) {
      return;
    }
  }

  // The ordinary trailing settle above has rebuilt every nested input from the
  // parent's committed post-rise state. Refresh only mixed-edge descendants:
  // their normal atomic cycle sampled the negedge before those inputs existed.
  // Do not re-settle this module afterward; its externally observed output must
  // retain the established during-period phase, while the corrected nested
  // state is ready for the next rise.
  if (has_refresh && !color_runtime_root && !color_storage_only) {
    fout->append("void ", mod, "::__compact_refresh() {\n");
    for (const auto& s : subs) {
      if (s.refresh_negedge) {
        fout->append("    ", s.inst, ".__compact_fall();\n");
        fout->append("    ", s.inst, ".__compact_publish();\n");
      }
      if (s.child_refresh_negedge) {
        fout->append("    ", s.inst, ".__compact_refresh();\n");
        fout->append("    ", s.inst, ".__compact_publish();\n");
      }
    }
    fout->append("}\n");
  }

  if (runtime_support_on) {
    const auto debug_members_mark = fout->mark();
    // ---- dump_state: flops/regs -> the `_r` map (by hierarchical name, pyrope
    // literal); memories -> one editable `<_dir>/<_p><member>.hex` each; recurse
    // into sub-instances. Mirrors the reset_cycle/peek member walk. ----
    fout->append("LHD_SIM_COLD void ",
                 mod,
                 "::dump_state(const std::string& _p, std::map<std::string, std::string>& _r, const std::string& _dir) const {\n");
    for (const auto& f : flops) {
      for (const auto& s : f.stages) {
        fout->append(absl::StrCat("  _r[_p + \"", s, "\"] = ", s, ".to_pyrope();\n"));
      }
      fout->append(absl::StrCat("  _r[_p + \"", f.member, "\"] = ", f.member, ".to_pyrope();\n"));
    }
    {
      absl::flat_hash_set<std::string> emitted;
      for (const auto& f : flops) {
        if (!f.prev_member.empty() && emitted.insert(f.prev_member).second) {
          fout->append(absl::StrCat("  _r[_p + \"", f.prev_member, "\"] = ", f.prev_member, " ? \"true\" : \"false\";\n"));
        }
      }
    }
    for (const auto& m : mems) {
      fout->append(absl::StrCat("  hlop::ckpt::write_mem_hex(_dir + \"/\" + _p + \"", m.member, ".hex\", ", m.member, ");\n"));
      for (const auto& p : m.ports) {
        if (p.rd && m.type == 1) {
          fout->append(absl::StrCat("  _r[_p + \"", m.member, "_q", p.rdidx, "\"] = ", m.member, "_q", p.rdidx, ".to_pyrope();\n"));
        }
      }
    }
    for (const auto& s : subs) {
      fout->append(absl::StrCat("  ", s.inst, ".dump_state(_p + \"", s.inst, ".\", _r, _dir);\n"));
    }
    // The persistent input latch __in is state too: a poke-once-and-hold input must
    // survive a restart (the testbench may not re-poke it after the checkpoint cycle).
    for (const auto& io : ios) {
      if (io.is_input) {
        fout->append(absl::StrCat("  _r[_p + \"__in.", io.field, "\"] = __in.", io.field, ".to_pyrope();\n"));
      }
    }
    fout->append("}\n");

    // ---- load_state: set each flop/reg from the map IF PRESENT (a missing key
    // keeps the reset value -> cross-version reload tolerance); memories from the
    // hex file if present; recurse into sub-instances. ----
    fout->append("LHD_SIM_COLD void ",
                 mod,
                 "::load_state(const std::string& _p, const std::map<std::string, std::string>& _r, const std::string& _dir) {\n");
    for (const auto& f : flops) {
      const auto type = value_type(f.bits, f.unsign);
      for (const auto& s : f.stages) {
        fout->append(absl::StrCat("  if (auto _it = _r.find(_p + \"",
                                  s,
                                  "\"); _it != _r.end()) ",
                                  s,
                                  " = ",
                                  type,
                                  "::from_pyrope(_it->second);\n"));
      }
      fout->append(absl::StrCat("  if (auto _it = _r.find(_p + \"",
                                f.member,
                                "\"); _it != _r.end()) ",
                                f.member,
                                " = ",
                                type,
                                "::from_pyrope(_it->second);\n"));
    }
    {
      absl::flat_hash_set<std::string> emitted;
      for (const auto& f : flops) {
        if (!f.prev_member.empty() && emitted.insert(f.prev_member).second) {
          fout->append(absl::StrCat("  if (auto _it = _r.find(_p + \"",
                                    f.prev_member,
                                    "\"); _it != _r.end()) ",
                                    f.prev_member,
                                    " = Slop<1>::from_pyrope(_it->second).is_known_true();\n"));
        }
      }
    }
    for (const auto& m : mems) {
      const auto type = value_type(m.bits, m.unsign);
      fout->append(absl::StrCat("  hlop::ckpt::read_mem_hex(_dir + \"/\" + _p + \"", m.member, ".hex\", ", m.member, ");\n"));
      for (const auto& p : m.ports) {
        if (p.rd && m.type == 1) {
          fout->append(absl::StrCat("  if (auto _it = _r.find(_p + \"",
                                    m.member,
                                    "_q",
                                    p.rdidx,
                                    "\"); _it != _r.end()) ",
                                    m.member,
                                    "_q",
                                    p.rdidx,
                                    " = ",
                                    type,
                                    "::from_pyrope(_it->second);\n"));
        }
      }
    }
    for (const auto& s : subs) {
      fout->append(absl::StrCat("  ", s.inst, ".load_state(_p + \"", s.inst, ".\", _r, _dir);\n"));
    }
    for (const auto& io : ios) {
      if (io.is_input) {
        const auto type = value_type(io.bits, io.unsign);
        fout->append(absl::StrCat("  if (auto _it = _r.find(_p + \"__in.",
                                  io.field,
                                  "\"); _it != _r.end()) { __in.",
                                  io.field,
                                  " = ",
                                  type,
                                  "::from_pyrope(_it->second); }\n"));
        // Bump the version OUTSIDE the key-present guard. `<inst>.load_state`
        // just above has already overwritten this child subtree's own `__in`
        // from the checkpoint, while the parent's `__fwd_*` forward stamps (and
        // a compact wrapper's `__bcast_ver_*`) only ever invalidate when the
        // version MOVES. A checkpoint that carries the child's key but not the
        // parent's -- the cross-version reload this function is built to
        // tolerate -- would otherwise leave the parent believing it had already
        // forwarded the value the child no longer holds, and the gate would skip
        // that bind forever. `++__gen` below cannot cover this: it invalidates
        // gated EVALUATION, not the per-field forward stamps.
        if (versioned_bits(io.bits)) {
          fout->append(absl::StrCat("  ++__in.", ver_path(io.field), ";  // restored: re-arm every version-gated forward\n"));
        }
      }
    }
    fout->append("  ++__gen;  // loaded state: every gated evaluation must recompute\n");
    if (color_runtime_root) {
      fout->append("  __color_runtime.reset();  // discard serial dirty/input caches after checkpoint restore\n");
    }
    fout->append("}\n");

    // ---- design_hash: FNV-fold of every member name+width (+ mem size + sub
    // callee + recursion). Stamped into meta.json; a mismatch on load is a WARNING
    // (editable checkpoints are cross-version on purpose), never a hard reject. ----
    fout->append("LHD_SIM_COLD std::uint64_t ", mod, "::design_hash() const {\n");
    fout->append("  std::uint64_t _h = hlop::ckpt::kFnvOffset;\n");
    if (color_root) {
      // Keep checkpoint compatibility diagnostic-only, but make the diagnostic
      // exact enough to explain a stale execution contract. The schedule digest
      // deliberately excludes the observation-name suffix; the full binding
      // digest retains it so renamed/direct observation slots still warn.
      const std::string report      = color_plan_->report();
      const size_t      observation = report.find("observation-map begin");
      const auto        schedule
          = observation == std::string::npos ? std::string_view{report} : std::string_view{report}.substr(0, observation);
      const uint64_t schedule_digest = fnv1a_str(livehd::hash_util::kFnv1a64_offset, schedule);
      const uint64_t binding_digest  = fnv1a_str(livehd::hash_util::kFnv1a64_offset, report);
      fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a(_h, ",
                                cpp_string_literal("sim-color-boundary-abi-v2"),
                                ");\n",
                                "  _h = hlop::ckpt::fnv1a(_h, ",
                                cpp_string_literal(kSimGenVersion),
                                ");\n",
                                "  _h = hlop::ckpt::fnv1a_u64(_h, ",
                                schedule_digest,
                                "ULL);\n",
                                "  _h = hlop::ckpt::fnv1a_u64(_h, ",
                                binding_digest,
                                "ULL);\n"));
    }
    for (const auto& f : flops) {
      for (const auto& s : f.stages) {
        fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a(_h, \"", s, "\"), ", f.bits, ");\n"));
      }
      fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a(_h, \"", f.member, "\"), ", f.bits, ");\n"));
    }
    {
      absl::flat_hash_set<std::string> emitted;
      for (const auto& f : flops) {
        if (!f.prev_member.empty() && emitted.insert(f.prev_member).second) {
          fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a(_h, \"", f.prev_member, "\"), 1);\n"));
        }
      }
    }
    for (const auto& m : mems) {
      // Ordering + wensize are part of the memory's identity: a checkpoint taken
      // under one same-cycle mode restores into a different machine under another,
      // and without this the hash would not budge.
      fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a(_h, \"",
                                m.member,
                                "\"), ",
                                m.bits,
                                "), ",
                                m.size,
                                ");\n"));
      fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a_u64(_h, ",
                                static_cast<int>(m.order),
                                "), ",
                                m.wensize,
                                ");\n"));
      for (const auto& p : m.ports) {
        if (p.rd && m.type == 1) {
          fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a(_h, \"",
                                    m.member,
                                    "_q",
                                    p.rdidx,
                                    "\"), ",
                                    m.bits,
                                    ");\n"));
        }
      }
    }
    for (const auto& s : subs) {
      fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a(_h, \"",
                                s.inst,
                                "\"); _h = hlop::ckpt::fnv1a_u64(_h, ",
                                s.inst,
                                ".design_hash());\n"));
    }
    // Interface (every I/O port name+width+direction) so a port change warns.
    for (const auto& io : ios) {
      fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a(_h, \"",
                                io.field,
                                "\"), ",
                                io.bits,
                                "), ",
                                io.is_input ? 1 : 2,
                                ");\n"));
    }
    // A coarse logic fingerprint: the cell count (computed at codegen). It is not a
    // full structural hash, but it makes a logic-only change (added/removed cells —
    // same state layout) flip the hash, so a stale checkpoint still warns.
    {
      size_t _ncells = 0;
      for ([[maybe_unused]] auto node : g->body().nodes()) {
        ++_ncells;
      }
      fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(_h, ", _ncells, ");\n"));
    }
    fout->append("  return _h;\n}\n");

    // ---- describe_signals / probe_signals: the observable scalar state by
    // hierarchical name (flops, pipe stages, sync-read regs, inputs; whole memories
    // and combinational outputs are excluded). describe_* lists name+bits+kind for
    // --list-signals; probe_* reads the current values for --probe / --break-when. ----
    fout->append("LHD_SIM_COLD void ", mod, "::describe_signals(const std::string& _p, std::vector<hlop::ckpt::Signal>& _v) const {\n");
    for (const auto& f : flops) {
      for (const auto& s : f.stages) {
        fout->append(absl::StrCat("  _v.push_back({_p + \"", s, "\", ", f.bits, ", \"pipe\"});\n"));
      }
      fout->append(absl::StrCat("  _v.push_back({_p + \"", f.member, "\", ", f.bits, ", \"flop\"});\n"));
    }
    for (const auto& m : mems) {
      for (const auto& p : m.ports) {
        if (p.rd && m.type == 1) {
          fout->append(absl::StrCat("  _v.push_back({_p + \"", m.member, "_q", p.rdidx, "\", ", m.bits, ", \"memrd\"});\n"));
        }
      }
    }
    for (const auto& io : ios) {
      if (io.is_input) {
        fout->append(absl::StrCat("  _v.push_back({_p + \"__in.", io.field, "\", ", io.bits, ", \"input\"});\n"));
      }
    }
    for (const auto& s : subs) {
      fout->append(absl::StrCat("  ", s.inst, ".describe_signals(_p + \"", s.inst, ".\", _v);\n"));
    }
    fout->append("}\n");

    fout->append("LHD_SIM_COLD void ", mod, "::probe_signals(const std::string& _p, std::map<std::string, long>& _m) const {\n");
    for (const auto& f : flops) {
      for (const auto& s : f.stages) {
        fout->append(absl::StrCat("  _m[_p + \"", s, "\"] = ", s, ".to_i64_low();\n"));
      }
      fout->append(absl::StrCat("  _m[_p + \"", f.member, "\"] = ", f.member, ".to_i64_low();\n"));
    }
    for (const auto& m : mems) {
      for (const auto& p : m.ports) {
        if (p.rd && m.type == 1) {
          fout->append(
              absl::StrCat("  _m[_p + \"", m.member, "_q", p.rdidx, "\"] = ", m.member, "_q", p.rdidx, ".to_i64_low();\n"));
        }
      }
    }
    for (const auto& io : ios) {
      if (io.is_input) {
        fout->append(absl::StrCat("  _m[_p + \"__in.", io.field, "\"] = __in.", io.field, ".to_i64_low();\n"));
      }
    }
    for (const auto& s : subs) {
      fout->append(absl::StrCat("  ", s.inst, ".probe_signals(_p + \"", s.inst, ".\", _m);\n"));
    }
    fout->append("}\n");

    // ---- observe_signals / observe_mem: the LOSSLESS query surface (2f-sim B) ----
    // Same hierarchical walk as probe_signals, with the two things the query engine
    // needs that the legacy probe map cannot express:
    //   * FULL-WIDTH values. `to_i64_low()` returns the raw low limb, so every
    //     signal wider than 64 bits was silently truncated in --probe rows and in
    //     --break-when comparisons. `to_hex(digits)` renders the whole vector, and
    //     the digit count is pinned to ceil(bits/4) so the text is fixed-width and
    //     a reader can recover the value without knowing the sign convention.
    //   * OUTPUTS, served from __last_out (recorded by cycle()). Inputs keep their
    //     historical `__in.` spelling here; the catalog publishes the clean port
    //     name and carries this one as an alias.
    fout->append("LHD_SIM_COLD void ", mod, "::observe_signals(const std::string& _p, std::map<std::string, std::string>& _m) const {\n");
    const auto hexdigits = [](int bits) { return std::to_string((std::max(1, bits) + 3) / 4); };
    for (const auto& f : flops) {
      for (const auto& s : f.stages) {
        fout->append(absl::StrCat("  _m[_p + \"", s, "\"] = ", s, ".to_hex(", hexdigits(f.bits), ");\n"));
      }
      fout->append(absl::StrCat("  _m[_p + \"", f.member, "\"] = ", f.member, ".to_hex(", hexdigits(f.bits), ");\n"));
    }
    for (const auto& m : mems) {
      for (const auto& p : m.ports) {
        if (p.rd && m.type == 1) {
          fout->append(absl::StrCat("  _m[_p + \"",
                                    m.member,
                                    "_q",
                                    p.rdidx,
                                    "\"] = ",
                                    m.member,
                                    "_q",
                                    p.rdidx,
                                    ".to_hex(",
                                    hexdigits(m.bits),
                                    ");\n"));
        }
      }
    }
    for (const auto& io : ios) {
      if (io.is_input) {
        // Keyed by the CLEAN port name, which is what the catalog publishes and
        // therefore what a query asks for. probe_signals keeps the historical
        // `__in.<field>` spelling (pinned by lhd_sim_observe_test.sh) and the
        // catalog carries it as an alias, but observe_signals is new and only the
        // query engine reads it, so it has no legacy shape to preserve. Keying it
        // the old way made every INPUT unqueryable: the catalog offered `acc.din`
        // while the stream only held `acc.__in.din`, so a perfectly valid name
        // came back "not observable".
        fout->append(absl::StrCat("  _m[_p + \"", io.field, "\"] = __in.", io.field, ".to_hex(", hexdigits(io.bits), ");\n"));
      } else {
        fout->append(absl::StrCat("  _m[_p + \"", io.field, "\"] = __last_out.", io.field, ".to_hex(", hexdigits(io.bits), ");\n"));
      }
    }
    for (const auto& s : subs) {
      fout->append(absl::StrCat("  ", s.inst, ".observe_signals(_p + \"", s.inst, ".\", _m);\n"));
    }
    fout->append("}\n");

    // One COMMITTED memory word by member name. `_n` is relative to this instance
    // ("mem" here, "sub.mem" one level down), so the walk mirrors the catalog's
    // dotted names. Staged same-cycle writes are transient between ticks by
    // construction, so what is read here is exactly the entry array.
    fout->append("LHD_SIM_COLD bool ", mod, "::observe_mem(const std::string& _n, long _i, std::string& _o) const {\n");
    for (const auto& m : mems) {
      fout->append(absl::StrCat("  if (_n == \"", m.member, "\") {\n"));
      fout->append(absl::StrCat("    if (_i < 0 || _i >= ", m.size, ") { return false; }\n"));
      fout->append(absl::StrCat("    _o = ", m.member, "[static_cast<size_t>(_i)].to_hex(", hexdigits(m.bits), ");\n"));
      fout->append("    return true;\n  }\n");
    }
    for (const auto& s : subs) {
      if (s.loop) {
        fout->append(absl::StrCat("  if (_n.compare(0, ", s.inst.size() + 1, ", \"", s.inst, "[\") == 0) {\n"));
        fout->append(absl::StrCat("    return ", s.inst, ".observe_mem(_n.substr(", s.inst.size(), "), _i, _o);\n  }\n"));
      } else {
        fout->append(absl::StrCat("  if (_n.compare(0, ", s.inst.size() + 1, ", \"", s.inst, ".\") == 0) {\n"));
        fout->append(absl::StrCat("    return ", s.inst, ".observe_mem(_n.substr(", s.inst.size() + 1, "), _i, _o);\n  }\n"));
      }
    }
    fout->append("  return false;\n}\n");
    chunk_cold_region(*fout, debug_members_mark);
  }

  // ---- sim.tune state walkers (sim_profile.md §6.1) -------------------------
  // `__tune_hash` writes one canonical word (__lhd_tune_h) per STORED state
  // element: dump_state's element set, in dump_state's order -- flop pipeline
  // stages then Q (latches included), the de-duplicated secondary-clock
  // `__clkprev_*` bits, and per memory the memory then its sync-read `_q<n>`
  // registers -- then recurses into each sub-instance (a compact loop walks its
  // stored lanes), then, only when `__lhd_root`, this module's `__in` input
  // fields in port order except the reference clock (the cycle model never
  // toggles it, and a word that changed every cycle would make no pair look
  // quiescent). `__tune_count(root)` is exactly the number of words written.
  //
  // Everything DERIVED is out: `_din`/`_cen`, root-only `_q<n>_din`,
  // `__color_whole_*`, `__out`/`__last_out`, `__gen`/`__done_*`/`__kids_*`,
  // `__fwd_*`/`__ver_*`/`*__tick`, the whole `__Color_runtime`, and a non-root
  // `__in` (observation-only slots, stale in performance builds). What remains
  // depends on the module's own state layout, sub list and ports -- never on
  // the tune vector, the plan, profiling, or the build flavour -- so the text is
  // byte-identical across all of them, and the walkers read stored state only.
  //
  // Emitted for EVERY module, outside the runtime_support gate: the profiler
  // and the end-of-test digest run in lean builds too.
  {
    const auto tune_mark = fout->mark();
    size_t     own_words = 0;
    fout->append("LHD_SIM_COLD void ", mod, "::__tune_hash(std::uint64_t*& __lhd_to, bool __lhd_root) const {\n");
    const auto put = [&](std::string_view member) {
      fout->append("  *__lhd_to++ = __lhd_tune_h(", member, ");\n");
      ++own_words;
    };
    for (const auto& f : flops) {
      for (const auto& st : f.stages) {
        put(st);
      }
      put(f.member);
    }
    {
      absl::flat_hash_set<std::string> emitted;
      for (const auto& f : flops) {
        if (!f.prev_member.empty() && emitted.insert(f.prev_member).second) {
          put(f.prev_member);
        }
      }
    }
    for (const auto& m : mems) {
      put(m.member);
      for (const auto& p : m.ports) {
        if (p.rd && m.type == 1) {
          put(absl::StrCat(m.member, "_q", p.rdidx));
        }
      }
    }
    for (const auto& s : subs) {
      fout->append("  ", s.inst, ".__tune_hash(__lhd_to);\n");
    }
    size_t in_words = 0;
    // Always emitted, possibly empty, so `__lhd_root` is always used.
    fout->append("  if (__lhd_root) {\n");
    for (const auto& io : ios) {
      if (io.is_input && io.field != clk_field) {
        fout->append("    *__lhd_to++ = __lhd_tune_h(__in.", io.field, ");\n");
        ++in_words;
      }
    }
    fout->append("  }\n}\n");

    fout->append("LHD_SIM_COLD std::size_t ", mod, "::__tune_count(bool __lhd_root) const {\n");
    fout->append("  std::size_t __lhd_n = ",
                 std::to_string(own_words),
                 "u + (__lhd_root ? ",
                 std::to_string(in_words),
                 "u : 0u);\n");
    for (const auto& s : subs) {
      fout->append("  __lhd_n += ", s.inst, ".__tune_count();\n");
    }
    fout->append("  return __lhd_n;\n}\n");
    chunk_cold_region(*fout, tune_mark, kTuneColdChunkStatements);
  }

  if (color_runtime_root) {
    // THE occurrence in-edge reader for everything below. The nested pin walk
    // is term for term what Occurrence_node::inp_edges() produced -- that
    // reader was itself `for (sink : inp_sorted_pins()) for (edge :
    // sink.inp_edges())` -- so a caller may still compare this SIZE against a
    // definition list and pair the two POSITIONALLY.
    //
    // Both readers are load-bearing. inp_sorted_pins() yields the node-as-pin
    // (port 0) FIRST and then ascending port order; the raw inp_pins() omits
    // port 0, which is where a banked cell's first operand lives, and imposes
    // no order. get_driver_pins() is PLURAL because a sink pin really can
    // resolve to several drivers -- a compact loop's carry-in reaches the
    // previous ordinal's output AND the inactive-carry bypass -- and each of
    // those was its own edge, so each must stay its own element here.
    const auto occurrence_sorted_inputs = [](const hhds::Occurrence_node& node) {
      std::vector<hhds::Occurrence_edge> inputs;
      for (const auto& sink : node.inp_sorted_pins()) {
        for (const auto& driver : sink.get_driver_pins()) {
          inputs.emplace_back(driver, sink);
        }
      }
      return inputs;
    };
    // The FIRST driver resolved onto sink port `port`, or an invalid pin. This
    // is the shape of a walk that matched a sink port and then broke out: port
    // ids are unique per pin, so the single matching sink pin is the whole
    // search, and an unresolvable one simply contributes no driver.
    const auto occurrence_driver_on_port = [](const hhds::Occurrence_node& node, hhds::Port_id port) -> hhds::Occurrence_pin {
      for (const auto& sink : node.inp_sorted_pins()) {
        if (sink.get_port_id() != port) {
          continue;
        }
        for (const auto& driver : sink.get_driver_pins()) {
          return driver;
        }
      }
      return {};
    };
    const auto find_site_output = [&](size_t site_index, hhds::Port_id port) {
      const auto node = color_plan_->sites()[site_index].node.base_node();
      for (const auto& output : node.out_pins()) {
        if (output.get_port_id() == port) {
          return output;
        }
      }
      return node.get_driver_pin(port);
    };
    absl::flat_hash_map<hhds::Graph*, absl::flat_hash_map<hhds::Class_index, const Flop*>> local_flop_by_node;
    for (const auto& flop : flops) {
      local_flop_by_node[flop.node.get_graph()].emplace(flop.node.get_class_index(), &flop);
    }
    const auto find_local_flop = [&](size_t site_index) -> const Flop* {
      const auto node     = color_plan_->sites()[site_index].node.base_node();
      const auto graph_it = local_flop_by_node.find(node.get_graph());
      if (graph_it == local_flop_by_node.end()) {
        return nullptr;
      }
      const auto node_it = graph_it->second.find(node.get_class_index());
      return node_it == graph_it->second.end() ? nullptr : node_it->second;
    };
    const auto find_local_mem = [&](size_t site_index) -> const Mem* { return direct_memory(color_plan_->sites()[site_index]); };
    const auto whole_pending  = [&](size_t site_index) { return absl::StrCat("__color_whole_", site_index); };
    const auto output_field   = [&](hhds::Port_id port) -> std::string {
      for (const auto& io : ios) {
        if (!io.is_input && io.port_id == static_cast<uint32_t>(port)) {
          return io.field;
        }
      }
      return {};
    };
    const auto input_field = [&](hhds::Port_id port) -> std::string {
      for (const auto& io : ios) {
        if (io.is_input && io.port_id == static_cast<uint32_t>(port)) {
          return io.field;
        }
      }
      return {};
    };
    absl::flat_hash_map<hhds::Occurrence_index, size_t> direct_site_index;
    direct_site_index.reserve(color_plan_->sites().size());
    std::vector<size_t>                                             direct_conditional_sites;
    absl::flat_hash_map<hhds::Occurrence_path, std::vector<size_t>> direct_latch_sites_by_path;
    for (size_t site_index = 0; site_index < color_plan_->sites().size(); ++site_index) {
      const auto& site = color_plan_->sites()[site_index];
      direct_site_index.emplace(site.node.get_occurrence_index(), site_index);
      if (site.kind == livehd::sim::Color_plan::Site_kind::conditional_control) {
        direct_conditional_sites.push_back(site_index);
      }
      if (type_op_of(site.node.base_node()) == Ntype_op::Latch) {
        direct_latch_sites_by_path[site.node.path()].push_back(site_index);
      }
    }
    std::vector<size_t> direct_version_color(color_plan_->version_sites().size(), livehd::sim::Color_plan::invalid_index);
    std::vector<size_t> direct_state_update(color_plan_->sites().size(), livehd::sim::Color_plan::invalid_index);
    std::vector<std::vector<size_t>> direct_site_colors(color_plan_->sites().size());
    for (size_t color_index = 0; color_index < color_plan_->colors().size(); ++color_index) {
      absl::flat_hash_set<size_t> color_sites;
      for (const size_t version : color_plan_->colors()[color_index].members) {
        direct_version_color[version] = color_index;
        const auto& version_site      = color_plan_->version_sites()[version];
        color_sites.insert(version_site.base_site);
        if (version_site.role == livehd::sim::Color_plan::Version_role::state_update) {
          direct_state_update[version_site.base_site] = version;
        }
      }
      for (const size_t site : color_sites) {
        direct_site_colors[site].push_back(color_index);
      }
    }
    const auto& direct_hierarchy_clocks = hierarchy_clocks_for_root();
    const auto  state_update_version    = [&](size_t site_index) { return direct_state_update[site_index]; };
    std::function<std::optional<hhds::Occurrence_pin>(const hhds::Occurrence_pin&, const hhds::Pin_class&, int)>
        resolve_occurrence_pin;
    resolve_occurrence_pin =
        [&](const hhds::Occurrence_pin& root_pin, const hhds::Pin_class& target, int depth) -> std::optional<hhds::Occurrence_pin> {
      if (root_pin.is_invalid() || target.is_invalid() || depth > 32) {
        return std::nullopt;
      }
      if (root_pin.base_pin().get_definition_index() == target.get_definition_index()) {
        return root_pin;
      }
      const auto occurrence_node = root_pin.get_master_node();
      const auto base_node       = root_pin.base_pin().get_master_node();
      // Both sides are (sink, driver) sequences in ascending sink-port order,
      // indexable so they can be paired positionally.
      const auto occurrence_in   = occurrence_sorted_inputs(occurrence_node);
      const auto base_in         = livehd::graph_util::inp_sink_drivers(base_node);
      if (occurrence_in.size() != base_in.size()) {
        return std::nullopt;
      }
      for (size_t input = 0; input < base_in.size(); ++input) {
        if (base_in[input].driver.get_definition_index() == target.get_definition_index()) {
          return occurrence_in[input].driver;
        }
        if (base_in[input].driver.is_invalid() || base_in[input].driver.is_const()
            || livehd::graph_util::is_graph_input_pin(base_in[input].driver)) {
          continue;
        }
        if (auto found = resolve_occurrence_pin(occurrence_in[input].driver, target, depth + 1)) {
          return found;
        }
      }
      return std::nullopt;
    };
    // A recognized boolean sub-expression: the emitted C++ text plus TWO
    // widths, both of which have to travel with it.
    //
    //   bits -- Slop_arg<T>::bits of the C++ TYPE of `text`. Uniform unsigned
    //           materialization makes a 1-bit unsigned port / state member a
    //           `Slop_u<1>`, whose carrier is a `Slop<2>`, so it counts as TWO
    //           Slop-bits. `Slop<W>::or_op`/`xor_op` assert `W >= every
    //           operand's Slop_arg bits`, which is exactly why a hard-coded
    //           `Slop<1>` result stopped compiling.
    //   mag  -- the RTL width of the VALUE. NOT the same number: an `or_op`
    //           over two `Slop_u<1>`s has type `Slop<2>` yet still carries a
    //           1-bit value, and that is the width `Not` must complement at.
    //           A LEAF can also be far wider than one bit, because the
    //           `Get_mask`/`Sext`/`Set_mask` arms below hand their operand
    //           through unchanged.
    //
    // INVARIANT every producer below maintains: bits <= mag + 1. `Not` relies
    // on it -- `Slop_u<mag>`'s carrier is `Slop<mag + 1>`, which must be at
    // least as wide as the operand being complemented.
    //
    // SECOND, SILENT REQUIREMENT: `mag` must be EXACTLY the width the value is
    // observed at, never an upper bound. `Slop_u<mag>::not_op` masks at it, so
    // a too-SMALL mag is a build error (the invariant above catches it) while a
    // too-LARGE one still satisfies the invariant and silently complements
    // phantom bits to 1. That is why the const leaf uses the literal container
    // width rather than Dlop's signed bit count, and why a narrowing pass-
    // through arm reports "not representable" instead of keeping its operand.
    struct Bool_expr {
      std::string text;      // empty == this cone is not representable
      int         bits = 1;  // Slop_arg<T>::bits of `text`
      int         mag  = 1;  // RTL width of the value `text` holds
    };
    // Ports, state members and a latch's `_din` are all declared through
    // value_type(), so the C++ type is `Slop_u<w>` exactly when sim.slop_u is
    // on and the pin is stamped unsigned.
    const auto bool_pin_expr = [&](std::string text, const hhds::Pin_class& p) {
      const int  w      = std::max(wbits_of(p), 1);
      const bool unsign = slop_u_ && !p.is_invalid() && is_unsign(p);
      return Bool_expr{std::move(text), unsign ? w + 1 : w, w};
    };
    const auto bool_input_expr = [&](hhds::Port_id port) -> Bool_expr {
      for (const auto& io : ios) {
        if (io.is_input && io.port_id == static_cast<uint32_t>(port)) {
          const bool unsign = slop_u_ && io.unsign;
          return Bool_expr{absl::StrCat("__in.", io.field), unsign ? io.bits + 1 : io.bits, io.bits};
        }
      }
      return {};
    };
    // operand() materializes a literal AT the width it is asked for, so the
    // C++ type is Slop<1>; the VALUE keeps the literal's own width.
    //
    // `mag` is the LITERAL container width (Dlop::get_payload_bits), NOT the
    // signed carrier, which is one wider for every non-negative literal (3
    // stamps 3 bits, container 2). A const pin carries no `bits` attr, so
    // wbits_of() floors at 1 and contributes nothing -- the literal alone
    // decides, and an inflated width is a silent wrong value under
    // `Slop_u<mag>::not_op`, whose extra bit always complements to 1.
    const auto bool_const_expr = [&](const hhds::Occurrence_pin& pin) {
      const auto  cpin  = pin.base_pin();
      const auto& value = const_of(cpin);
      const int   mag   = std::max({wbits_of(cpin), static_cast<int>(value.get_payload_bits()), 1});
      return Bool_expr{operand(cpin, 1), 1, mag};
    };
    // `~x` used as a boolean has to be complemented at the VALUE's width and
    // masked back to it: the member `Slop<W>::not_op()` exists only on Slop
    // (Slop_u has no member form at all) and complements the whole carrier
    // word, so a canonical 1 would come back as ...1110 -- still "true".
    const auto bool_not_expr = [](const Bool_expr& value) {
      const int m = std::max(value.mag, 1);
      return Bool_expr{absl::StrCat("Slop_u<", m, ">::not_op(", value.text, ")"), m + 1, m};
    };
    // Bitwise reductions run at the WIDEST operand carrier: that satisfies
    // or_op/xor_op's width check and stops and_op from reading a wide operand
    // at the result's (narrower) word count.
    const auto bool_bit_expr = [](const char* method, const std::vector<Bool_expr>& inputs) {
      Bool_expr result = inputs.front();
      for (size_t input = 1; input < inputs.size(); ++input) {
        const int width = std::max(result.bits, inputs[input].bits);
        const int value = std::max(result.mag, inputs[input].mag);
        result
            = Bool_expr{absl::StrCat("Slop<", width, ">::", method, "(", result.text, ", ", inputs[input].text, ")"), width, value};
      }
      return result;
    };
    // An EQ cell produces a 1-bit 0/1, so name that type: `Slop_u<1>::eq_op`
    // compares at max(operand carriers) -- never truncating a wide operand the
    // way a `Slop<1>` result does -- and lands a canonical single bit, which is
    // what keeps the bits <= mag + 1 invariant true for a `Not` above it.
    const auto bool_eq_expr = [](const Bool_expr& lhs, const Bool_expr& rhs) {
      return Bool_expr{absl::StrCat("Slop_u<1>::eq_op(", lhs.text, ", ", rhs.text, ")"), 2, 1};
    };
    std::function<Bool_expr(const hhds::Occurrence_pin&, int)> occurrence_bool_value;
    occurrence_bool_value = [&](const hhds::Occurrence_pin& pin, int depth) -> Bool_expr {
      if (pin.is_invalid() || depth > 32) {
        return {};
      }
      if (pin.is_const()) {
        return bool_const_expr(pin);
      }
      if (livehd::graph_util::is_graph_input_pin(pin) && pin.get_graph() == g) {
        return bool_input_expr(pin.get_port_id());
      }
      if (const auto it = direct_site_index.find(pin.get_master_node().get_occurrence_index());
          it != direct_site_index.end() && color_plan_->sites()[it->second].kind == livehd::sim::Color_plan::Site_kind::state) {
        return bool_pin_expr(occurrence_member(color_plan_->sites()[it->second]), pin.base_pin());
      }
      const auto node = pin.get_master_node();
      const auto op   = type_op_of(node.base_node());
      if (op == Ntype_op::Clock_cell) {
        // Read the enable off the OCCURRENCE's own sink port, never by ordinal
        // against the definition list -- hhds does not guarantee one occurrence
        // driver per definition input (an unresolvable driver contributes
        // none). The `return` is what the old edge walk's `return` was, so
        // splitting one loop into two changes nothing here.
        //
        // Do not hoist either range into a named local and keep an ITERATOR
        // into it: an OccurrencePinRange owns its shared_ptr<vector<>> and its
        // iterator does not, so an iterator outliving the temporary is a
        // use-after-free (this arm shipped that bug once).
        for (const auto& sink : node.inp_sorted_pins()) {
          if (Ntype::get_sink_name(Ntype_op::Clock_cell, static_cast<int>(sink.get_port_id())) != "en") {
            continue;
          }
          for (const auto& driver : sink.get_driver_pins()) {
            return occurrence_bool_value(driver, depth + 1);
          }
        }
        return Bool_expr{"Slop<1>::create_integer(1)", 1, 1};
      }
      std::vector<Bool_expr> inputs;
      // One entry per resolved DRIVER, which is one per in-edge: the bit-op
      // fold below reads inputs[0]/inputs[1], so a sink with two drivers has to
      // contribute both, exactly as the edge walk did.
      for (const auto& sink : node.inp_sorted_pins()) {
        for (const auto& driver : sink.get_driver_pins()) {
          inputs.push_back(occurrence_bool_value(driver, depth + 1));
          if (inputs.back().text.empty()) {
            return {};
          }
        }
      }
      if (inputs.empty()) {
        return {};
      }
      if (op == Ntype_op::Not) {
        return bool_not_expr(inputs.front());
      }
      if (op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Xor) {
        return bool_bit_expr(op == Ntype_op::And ? "and_op" : op == Ntype_op::Or ? "or_op" : "xor_op", inputs);
      }
      if (op == Ntype_op::EQ && inputs.size() >= 2) {
        return bool_eq_expr(inputs[0], inputs[1]);
      }
      if ((op == Ntype_op::Get_mask || op == Ntype_op::Sext) && !inputs.empty()) {
        // A pass-through keeps the OPERAND's text, so it can only keep the
        // OPERAND's C++ type -- but `mag` must be the width THIS node's value is
        // observed at, because `Slop_u<mag>::not_op` above masks at it. A
        // NARROWING node cannot be spelled by handing its operand through: the
        // mask/truncation would vanish from the text while the complement masked
        // at the narrower width. That was survivable while `Not` emitted the
        // unmasked member form; it is an observably wrong guard now. Report such
        // a cone as not representable and let the caller evaluate the region
        // unconditionally, which is always safe.
        const int node_w = std::max(wbits_of(pin.base_pin()), 1);
        auto      value  = inputs.front();
        if (node_w < value.mag) {
          return {};
        }
        value.mag = node_w;
        return value;
      }
      return {};
    };
    const auto occurrence_bool_expr
        = [&](const hhds::Occurrence_pin& pin, int depth) { return occurrence_bool_value(pin, depth).text; };

    // Memory clocks are timing metadata and intentionally do not enter the
    // color data ABI.  The reference backend nevertheless folds every decoded
    // ICG enable (and a hierarchical clock-port tick) into the write enable.
    // Keep that policy at the owner callback boundary: the LLVM object still
    // computes exact-width data/enables, while this tiny generated method reads
    // the structurally resolved clock qualifier from the owning occurrence.
    struct Llvm_memory_gate {
      std::string method;
      std::string expression;
      bool        complete = true;
    };
    std::vector<Llvm_memory_gate> llvm_memory_gates(color_plan_->sites().size());
    for (size_t site_index = 0; llvm_backend_ && site_index < color_plan_->sites().size(); ++site_index) {
      const auto& site = color_plan_->sites()[site_index];
      if (type_op_of(site.node.base_node()) != Ntype_op::Memory) {
        continue;
      }
      const auto* memory = direct_memory(site);
      if (memory == nullptr || !memory->registered() || (memory->clock_guards.empty() && memory->tick_field.empty())) {
        continue;
      }
      auto& gate                   = llvm_memory_gates[site_index];
      gate.method                  = llvm_memory_gate_method_name(site);
      const auto occurrence_inputs = occurrence_sorted_inputs(site.node);
      const auto definition_inputs = livehd::graph_util::inp_sink_drivers(site.node.base_node());
      if (occurrence_inputs.size() != definition_inputs.size()) {
        gate.complete = false;
        continue;
      }
      for (const auto& guard : memory->clock_guards) {
        std::optional<hhds::Occurrence_pin> resolved_guard;
        for (size_t input = 0; input < definition_inputs.size(); ++input) {
          const auto sink_name
              = Ntype::get_sink_name(Ntype_op::Memory, static_cast<int>(definition_inputs[input].sink.get_port_id()));
          if (!str_tools::ends_with(sink_name, "clock_pin")) {
            continue;
          }
          resolved_guard = resolve_occurrence_pin(occurrence_inputs[input].driver, guard, 0);
          if (resolved_guard.has_value()) {
            break;
          }
        }
        if (!resolved_guard.has_value()) {
          gate.complete = false;
          break;
        }
        const auto value = occurrence_bool_expr(*resolved_guard, 0);
        if (value.empty()) {
          gate.complete = false;
          break;
        }
        absl::StrAppend(&gate.expression, gate.expression.empty() ? "" : " && ", emit_known_true(value));
      }
      if (gate.complete && !memory->tick_field.empty()) {
        absl::StrAppend(&gate.expression,
                        gate.expression.empty() ? "" : " && ",
                        occurrence_prefix(site.node),
                        "__in.",
                        memory->tick_field,
                        "__tick");
      }
      if (gate.expression.empty()) {
        gate.complete = false;
      }
      if (gate.complete) {
        fout->append(absl::StrCat("bool ", mod, "::", gate.method, "() { return ", gate.expression, "; }\n"));
      }
    }
    std::function<Bool_expr(const hhds::Occurrence_pin&, const hhds::Pin_class&, livehd::sim::Color_plan::Execution_slot, int)>
        occurrence_guard_value;
    occurrence_guard_value = [&](const hhds::Occurrence_pin&             pin,
                                 const hhds::Pin_class&                  clock_root,
                                 livehd::sim::Color_plan::Execution_slot target_slot,
                                 int                                     depth) -> Bool_expr {
      if (pin.is_invalid() || depth > 32) {
        return {};
      }
      if (pin.is_const()) {
        return bool_const_expr(pin);
      }
      if (livehd::graph_util::is_graph_input_pin(pin) && pin.get_graph() == g) {
        if ((!clock_root.is_invalid() && pin.base_pin().get_definition_index() == clock_root.get_definition_index())
            || direct_hierarchy_clocks.is_clock(pin.base_pin())) {
          return Bool_expr{"Slop<1>::create_integer(1)", 1, 1};
        }
        return bool_input_expr(pin.get_port_id());
      }
      if (const auto it = direct_site_index.find(pin.get_master_node().get_occurrence_index());
          it != direct_site_index.end() && type_op_of(color_plan_->sites()[it->second].node.base_node()) == Ntype_op::Latch) {
        // Clock-enable latches are sampled at their closing edge. The planner
        // adds the matching same-edge state-update dependency, so the pending
        // member is ready before this guard is evaluated.
        return bool_pin_expr(occurrence_member(color_plan_->sites()[it->second]) + "_din", pin.base_pin());
      }
      const auto node = pin.get_master_node();
      const auto op   = type_op_of(node.base_node());
      if (op == Ntype_op::Clock_cell) {
        // Same contract as occurrence_bool_value above: match the enable by the
        // occurrence's OWN sink port, never by ordinal against the definition
        // list, and never keep an iterator into a range temporary.
        for (const auto& sink : node.inp_sorted_pins()) {
          if (Ntype::get_sink_name(Ntype_op::Clock_cell, static_cast<int>(sink.get_port_id())) != "en") {
            continue;
          }
          for (const auto& driver : sink.get_driver_pins()) {
            return occurrence_guard_value(driver, clock_root, target_slot, depth + 1);
          }
        }
        return Bool_expr{"Slop<1>::create_integer(1)", 1, 1};
      }
      std::vector<Bool_expr> inputs;
      // One entry per resolved DRIVER, which is one per in-edge: the bit-op
      // fold below reads inputs[0]/inputs[1], so a sink with two drivers has to
      // contribute both, exactly as the edge walk did.
      for (const auto& sink : node.inp_sorted_pins()) {
        for (const auto& driver : sink.get_driver_pins()) {
          inputs.push_back(occurrence_guard_value(driver, clock_root, target_slot, depth + 1));
          if (inputs.back().text.empty()) {
            return {};
          }
        }
      }
      if (inputs.empty()) {
        return {};
      }
      if (op == Ntype_op::Not) {
        return bool_not_expr(inputs.front());
      }
      if (op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Xor) {
        return bool_bit_expr(op == Ntype_op::And ? "and_op" : op == Ntype_op::Or ? "or_op" : "xor_op", inputs);
      }
      if (op == Ntype_op::EQ && inputs.size() >= 2) {
        return bool_eq_expr(inputs[0], inputs[1]);
      }
      if ((op == Ntype_op::Get_mask || op == Ntype_op::Sext || op == Ntype_op::Set_mask) && !inputs.empty()) {
        // A pass-through keeps the OPERAND's text, so it can only keep the
        // OPERAND's C++ type -- but `mag` must be the width THIS node's value is
        // observed at, because `Slop_u<mag>::not_op` above masks at it. A
        // NARROWING node cannot be spelled by handing its operand through: the
        // mask/truncation would vanish from the text while the complement masked
        // at the narrower width. That was survivable while `Not` emitted the
        // unmasked member form; it is an observably wrong guard now. Report such
        // a cone as not representable and let the caller evaluate the region
        // unconditionally, which is always safe.
        const int node_w = std::max(wbits_of(pin.base_pin()), 1);
        auto      value  = inputs.front();
        if (node_w < value.mag) {
          return {};
        }
        value.mag = node_w;
        return value;
      }
      return {};
    };
    const auto occurrence_guard_expr = [&](const hhds::Occurrence_pin&             pin,
                                           const hhds::Pin_class&                  clock_root,
                                           livehd::sim::Color_plan::Execution_slot target_slot,
                                           int depth) { return occurrence_guard_value(pin, clock_root, target_slot, depth).text; };
    const auto conditional_activation_expr = [&](const hhds::Occurrence_node& occurrence) {
      std::string activation;
      const auto& occurrence_steps = occurrence.path().steps();
      for (const size_t candidate_index : direct_conditional_sites) {
        const auto& candidate       = color_plan_->sites()[candidate_index];
        const auto& candidate_steps = candidate.node.path().steps();
        if (occurrence_steps.size() < candidate_steps.size()) {
          continue;
        }
        bool prefix = true;
        for (size_t step = 0; step < candidate_steps.size(); ++step) {
          if (occurrence_steps[step] != candidate_steps[step]) {
            prefix = false;
            break;
          }
        }
        if (!prefix) {
          continue;
        }
        hhds::Occurrence_pin guard;
        const auto           valid_sink = candidate.node.get_sink_pin("__valid");
        if (!valid_sink.is_invalid()) {
          guard = occurrence_driver_on_port(candidate.node, valid_sink.get_port_id());
        }
        const auto guard_value = occurrence_bool_expr(guard, 0);
        if (guard_value.empty()) {
          continue;
        }
        const auto test = emit_known_true(guard_value);
        activation      = activation.empty() ? test : absl::StrCat("(", activation, " && ", test, ")");
      }
      return activation;
    };
    // Conditional colors share one predicate and execution slot. Keep that
    // grouping explicit so the serial schedule tests the guard once around the
    // corresponding ordered region.
    struct Direct_condition_phase {
      size_t              control_site = livehd::sim::Color_plan::invalid_index;
      uint8_t             slot         = 0;
      std::string         predicate;
      std::vector<size_t> colors;
    };
    const auto conditional_run_expr = [&](const livehd::sim::Color_plan::Site& control) -> std::string {
      hhds::Occurrence_pin guard;
      const auto           valid_sink = control.node.get_sink_pin("__valid");
      if (!valid_sink.is_invalid()) {
        guard = occurrence_driver_on_port(control.node, valid_sink.get_port_id());
      }
      const auto guard_value = occurrence_bool_expr(guard, 0);
      if (guard_value.empty()) {
        return {};
      }
      std::string predicate = emit_known_true(guard_value);

      const auto child = control.node.get_subnode_graph();
      const auto sio   = control.node.get_subnode_io();
      if (!child || !sio) {
        return {};
      }
      const auto& resets = reset_guard_ports(child, port_cache);
      if (!resets.complete || resets.ports.size() > 1) {
        return {};  // exact-representability fallback: evaluate unconditionally
      }
      if (resets.ports.empty()) {
        return predicate;
      }

      const auto& reset_port  = resets.ports.front();
      // Matched on the sink port the reset arrives at. Resolve it by WALKING
      // the sorted sink pins rather than asking get_sink_pin(port_id): hhds
      // `find_pin` asserts on a port that was never created, and a reset port
      // a child declares need not have a materialized pin on this call site.
      const auto  reset       = occurrence_driver_on_port(control.node, static_cast<hhds::Port_id>(reset_port.port_id));
      const auto  reset_value = occurrence_bool_expr(reset, 0);
      if (reset_value.empty()) {
        return {};
      }
      absl::StrAppend(&predicate, " || (", reset_value, reset_port.active_low ? ").is_known_false()" : ").is_known_true()");
      return predicate;
    };

    std::vector<std::string> direct_condition_predicate(color_plan_->sites().size());
    for (size_t site_index = 0; site_index < color_plan_->sites().size(); ++site_index) {
      if (color_plan_->sites()[site_index].kind == livehd::sim::Color_plan::Site_kind::conditional_control) {
        direct_condition_predicate[site_index] = conditional_run_expr(color_plan_->sites()[site_index]);
      }
    }
    // A nested conditional region cannot be an independent outer-DAG join:
    // its colors also belong to the enclosing region's selected branch. Cache
    // the outermost representable owner per occurrence path instead of doing a
    // sites-by-sites prefix scan (quadratic on Minion's occurrence plan).
    const auto parent_occurrence_prefix = [](std::string prefix) {
      if (!prefix.empty() && prefix.back() == '.') {
        prefix.pop_back();
      }
      const auto separator = prefix.rfind('.');
      return separator == std::string::npos ? std::string{} : prefix.substr(0, separator + 1);
    };
    absl::flat_hash_map<std::string, size_t> condition_by_path;
    for (size_t site_index = 0; site_index < color_plan_->sites().size(); ++site_index) {
      if (!direct_condition_predicate[site_index].empty()) {
        condition_by_path.try_emplace(occurrence_prefix(color_plan_->sites()[site_index].node), site_index);
      }
    }
    for (size_t site_index = 0; site_index < color_plan_->sites().size(); ++site_index) {
      if (direct_condition_predicate[site_index].empty()) {
        continue;
      }
      const std::string path = occurrence_prefix(color_plan_->sites()[site_index].node);
      if (path.empty()) {
        continue;
      }
      std::string prefix = parent_occurrence_prefix(path);
      while (true) {
        if (condition_by_path.contains(prefix)) {
          direct_condition_predicate[site_index].clear();
          break;
        }
        if (prefix.empty()) {
          break;
        }
        prefix = parent_occurrence_prefix(std::move(prefix));
      }
    }
    condition_by_path.clear();
    for (size_t site_index = 0; site_index < color_plan_->sites().size(); ++site_index) {
      if (!direct_condition_predicate[site_index].empty()) {
        condition_by_path.try_emplace(occurrence_prefix(color_plan_->sites()[site_index].node), site_index);
      }
    }
    absl::flat_hash_map<std::string, size_t> condition_owner_cache;
    const auto                               condition_owner = [&](const livehd::sim::Color_plan::Site& site) {
      const std::string path = occurrence_prefix(site.node);
      if (const auto cached = condition_owner_cache.find(path); cached != condition_owner_cache.end()) {
        return cached->second;
      }
      std::string prefix = path;
      while (true) {
        if (const auto found = condition_by_path.find(prefix); found != condition_by_path.end()) {
          condition_owner_cache.emplace(path, found->second);
          return found->second;
        }
        if (prefix.empty()) {
          break;
        }
        prefix = parent_occurrence_prefix(std::move(prefix));
      }
      condition_owner_cache.emplace(path, livehd::sim::Color_plan::invalid_index);
      return livehd::sim::Color_plan::invalid_index;
    };
    std::vector<size_t> direct_color_condition(color_plan_->colors().size(), livehd::sim::Color_plan::invalid_index);
    for (size_t color_index = 0; color_index < color_plan_->colors().size(); ++color_index) {
      size_t owner             = livehd::sim::Color_plan::invalid_index;
      bool   owner_initialized = false;
      for (const size_t version_index : color_plan_->colors()[color_index].members) {
        const auto&  member_site  = color_plan_->sites()[color_plan_->version_sites()[version_index].base_site];
        const size_t member_owner = condition_owner(member_site);
        if (!owner_initialized) {
          owner             = member_owner;
          owner_initialized = true;
        } else if (owner != member_owner) {
          owner = livehd::sim::Color_plan::invalid_index;
          break;  // a coarsened color crossing the control boundary runs unconditionally
        }
      }
      direct_color_condition[color_index] = owner;
    }
    std::vector<Direct_condition_phase> direct_condition_phases;
    std::vector<size_t> direct_color_condition_phase(color_plan_->colors().size(), livehd::sim::Color_plan::invalid_index);
    absl::flat_hash_map<std::pair<size_t, uint8_t>, size_t> condition_phase_index;
    for (size_t color_index = 0; color_index < color_plan_->colors().size(); ++color_index) {
      const size_t owner = direct_color_condition[color_index];
      if (owner == livehd::sim::Color_plan::invalid_index) {
        continue;
      }
      const auto slot                 = static_cast<uint8_t>(color_plan_->colors()[color_index].slot);
      const auto [phase_it, inserted] = condition_phase_index.try_emplace({owner, slot}, direct_condition_phases.size());
      const size_t phase              = phase_it->second;
      if (inserted) {
        direct_condition_phases.push_back({owner, slot, direct_condition_predicate[owner], {}});
      }
      direct_condition_phases[phase].colors.push_back(color_index);
      direct_color_condition_phase[color_index] = phase;
    }
    const auto observation_output = [&](const livehd::sim::Color_plan::Boundary_slot& slot) -> std::string {
      if (slot.owner_site == livehd::sim::Color_plan::invalid_index) {
        return {};
      }
      const auto& site = color_plan_->sites()[slot.owner_site];
      const auto  io   = site.node.get_graph()->get_io();
      if (!io) {
        return {};
      }
      for (const auto& decl : io->get_output_pin_decls()) {
        if (decl.port_id != slot.public_port) {
          continue;
        }
        const char* surface = slot.version == livehd::sim::Color_plan::State_version::pre_rise ? "__last_out." : "__out.";
        return occurrence_prefix(site.node) + surface + cpp_port_path(decl.name);
      }
      return {};
    };
    const auto observation_input = [&](const livehd::sim::Color_plan::Boundary_slot& slot) -> std::string {
      if (slot.owner_site == livehd::sim::Color_plan::invalid_index) {
        return {};
      }
      const auto& site = color_plan_->sites()[slot.owner_site];
      const auto  io   = site.node.get_graph()->get_io();
      if (!io) {
        return {};
      }
      for (const auto& decl : io->get_input_pin_decls()) {
        if (decl.port_id == slot.public_port) {
          return occurrence_prefix(site.node) + "__in." + cpp_port_path(decl.name);
        }
      }
      return {};
    };

    struct Direct_kernel_read {
      size_t                                                                               slot_index = 0;
      size_t                                                                               rank       = 0;
      livehd::sim::Color_plan::Boundary_consumer                                           consumer;
      std::tuple<size_t, uint8_t, hhds::Port_id, uint32_t, uint32_t, uint32_t, bool, bool> key;
    };
    struct Direct_kernel_write {
      size_t                                                                                   slot_index = 0;
      size_t                                                                                   rank       = 0;
      size_t                                                                                   version    = 0;
      std::tuple<size_t, uint8_t, hhds::Port_id, uint32_t, uint32_t, uint32_t, uint32_t, bool> key;
    };
    struct Direct_kernel_abi {
      std::vector<Direct_kernel_read>  reads;
      std::vector<Direct_kernel_write> writes;
    };
    // A state update needs Q as the hold value even though Q is not a physical
    // Flop/Latch sink. Keep it explicit in the packed ABI instead of inventing
    // a graph edge or relying on the output buffer's previous contents.
    constexpr uint32_t direct_state_current_input = std::numeric_limits<uint32_t>::max();
    const auto         range_extract_expr = [&](std::string_view value, uint32_t width, bool canonical, uint32_t lo, uint32_t hi) {
      if (slop_u_ && canonical) {
        return absl::StrCat("Slop_u<", width, ">::get_mask_op_opt(", value, ", ", lo, ", ", hi, ")");
      }
      return absl::StrCat("Slop<", width + 1, ">::get_mask_op_opt(", value, ", ", lo, ", ", hi, ")");
    };
    const auto direct_read_expr = [&](const livehd::sim::Color_plan::Boundary_slot& slot, size_t slot_index) {
      if (!slot.literal.empty()) {
        auto literal = slot.literal;
        if (unknown_zero_) {
          std::replace(literal.begin(), literal.end(), '?', '0');
        }
        return sim_const_expr(literal, std::to_string(slot.width));
      }
      if (slot.kind == livehd::sim::Color_plan::Boundary_kind::color_value) {
        return direct_slot_read[slot_index];
      }
      if (slot.kind == livehd::sim::Color_plan::Boundary_kind::state_current) {
        const auto& state = color_plan_->sites()[slot.owner_site];
        auto        value = occurrence_member(state);
        const auto  pin   = state.node.base_node().get_driver_pin(slot.producer_port);
        const int   width = std::max(wbits_of(pin), 1);
        if (slot.producer_extract_hi > slot.producer_extract_lo) {
          return range_extract_expr(value, slot.width, slot.unsign, slot.producer_extract_lo, slot.producer_extract_hi);
        }
        if (slot.producer_shift != 0) {
          return absl::StrCat("Slop<", slot.width, ">::shl_op(", value, ", ", slot.producer_shift, ")");
        }
        if (static_cast<uint32_t>(width) != slot.width) {
          return slot.unsign ? append_zext(value, static_cast<int>(slot.width))
                             : absl::StrCat("Slop<", slot.width, ">{", value, "}");
        }
        return value;
      }
      if (slot.kind == livehd::sim::Color_plan::Boundary_kind::top_input) {
        const auto field = input_field(slot.public_port);
        if (field.empty()) {
          return std::string{};
        }
        const auto value = "__in." + field;
        if (slot.producer_extract_hi > slot.producer_extract_lo) {
          return range_extract_expr(value, slot.width, slot.unsign, slot.producer_extract_lo, slot.producer_extract_hi);
        }
        return value;
      }
      if (slot.kind == livehd::sim::Color_plan::Boundary_kind::observation_input) {
        return observation_input(slot);
      }
      if (slot.kind == livehd::sim::Color_plan::Boundary_kind::observation_output) {
        return observation_output(slot);
      }
      return std::string{};
    };
    const auto direct_write_expr = [&](const livehd::sim::Color_plan::Boundary_slot& slot, size_t slot_index) {
      if (slot.kind == livehd::sim::Color_plan::Boundary_kind::color_value) {
        return direct_slot_storage[slot_index];
      }
      if (slot.kind == livehd::sim::Color_plan::Boundary_kind::top_output) {
        const auto field = output_field(slot.public_port);
        if (field.empty()) {
          return std::string{};
        }
        const char* surface = slot.version == livehd::sim::Color_plan::State_version::pre_rise ? "__last_out." : "__out.";
        return surface + field;
      }
      if (slot.kind == livehd::sim::Color_plan::Boundary_kind::observation_output) {
        return observation_output(slot);
      }
      if (slot.kind == livehd::sim::Color_plan::Boundary_kind::observation_input) {
        return observation_input(slot);
      }
      if (slot.kind == livehd::sim::Color_plan::Boundary_kind::state_pending) {
        if (slot.owner_site == livehd::sim::Color_plan::invalid_index) {
          return std::string{};
        }
        return occurrence_member(color_plan_->sites()[slot.owner_site]) + "_din";
      }
      return std::string{};
    };
    const auto direct_consumer_expr = [&](const livehd::sim::Color_plan::Boundary_slot&     slot,
                                          size_t                                            slot_index,
                                          const livehd::sim::Color_plan::Boundary_consumer& consumer) {
      auto value = direct_read_expr(slot, slot_index);
      if (consumer.preextracted) {
        return value;
      }
      if (value.empty() || slot.width == consumer.width) {
        return value;
      }
      // Do not narrow a color-boundary carrier immediately before a low-bit
      // Get_mask that performs the same narrowing itself. The old sequence
      // became `slot.zext_to<K>().zext_to<K,W>()` after node lowering. Reading
      // the stored carrier is exact when the selected range [0,me) lies inside
      // the consumer's boundary width: every bit the skipped conversion could
      // have cleared is outside the mask and therefore unobservable.
      const auto& consumer_version = color_plan_->version_sites()[consumer.version_site];
      const auto  consumer_node    = color_plan_->sites()[consumer_version.base_site].node.base_node();
      if (type_op_of(consumer_node) == Ntype_op::Get_mask && consumer.port == Ntype::get_sink_pid(Ntype_op::Get_mask, "a")) {
        for (auto edge_sink : consumer_node.inp_sorted_pins()) {
          for (auto edge_drv : edge_sink.get_driver_pins()) {
            if (edge_sink.get_port_id() != Ntype::get_sink_pid(Ntype_op::Get_mask, "mask") || !edge_drv.is_const()) {
              continue;
            }
            const auto& mask = const_of(edge_drv);
            if (!mask.is_negative() && !mask.has_unknowns()) {
              const auto [mb, me] = mask.get_mask_range();
              if (mb == 0 && me > 0 && static_cast<uint32_t>(me) <= consumer.width) {
                return value;
              }
            }
            break;
          }
        }
      }
      return slot.unsign ? append_zext(value, static_cast<int>(consumer.width))
                         : absl::StrCat("Slop<", consumer.width, ">{", value, "}");
    };
    const auto emit_serial_dirty_consumers = [&](size_t slot_index, std::string_view indent) {
      if (!tune_dirty()) {
        return;
      }
      absl::flat_hash_set<size_t> emitted;
      for (const auto& consumer : color_plan_->boundary_slots()[slot_index].consumers) {
        if (consumer.color == livehd::sim::Color_plan::invalid_index || !emitted.insert(consumer.color).second) {
          continue;
        }
        fout->append(indent, dirty_mark(consumer.color), "\n");
      }
    };
    const auto emit_serial_dirty_site_colors = [&](size_t site_index, std::string_view indent) {
      if (!tune_dirty()) {
        return;
      }
      for (const size_t color_index : direct_site_colors[site_index]) {
        fout->append(indent, dirty_mark(color_index), "\n");
      }
    };
    const auto build_direct_kernel_abi = [&](size_t color_index) {
      Direct_kernel_abi                   abi;
      absl::flat_hash_map<size_t, size_t> rank_of;
      rank_of.reserve(color_plan_->canonical_members()[color_index].size());
      for (size_t rank = 0; rank < color_plan_->canonical_members()[color_index].size(); ++rank) {
        rank_of.emplace(color_plan_->canonical_members()[color_index][rank], rank);
      }
      for (const size_t slot_index : direct_consumed_slots[color_index]) {
        const auto& slot = color_plan_->boundary_slots()[slot_index];
        if (direct_read_expr(slot, slot_index).empty()) {
          continue;
        }
        for (const auto& consumer : slot.consumers) {
          if (consumer.color != color_index || consumer.version_site == livehd::sim::Color_plan::invalid_index) {
            continue;
          }
          if (color_plan_->version_sites()[consumer.version_site].role == livehd::sim::Color_plan::Version_role::state_read
              && !std::ranges::any_of(direct_produced_slots[consumer.version_site], [&](const size_t produced_slot) {
                   return !direct_write_expr(color_plan_->boundary_slots()[produced_slot], produced_slot).empty();
                 })) {
            continue;  // state-read versions are ABI sources, not executable consumers
          }
          const auto rank_it = rank_of.find(consumer.version_site);
          I(rank_it != rank_of.end());
          const size_t rank = rank_it->second;
          abi.reads.push_back({
              slot_index,
              rank,
              consumer,
              {rank,
                static_cast<uint8_t>(slot.kind),
                consumer.port,
                consumer.input,
                slot.width,
                consumer.width,
                slot.unsign,
                consumer.preextracted}
          });
        }
      }
      for (size_t rank = 0; rank < color_plan_->canonical_members()[color_index].size(); ++rank) {
        const size_t version_index = color_plan_->canonical_members()[color_index][rank];
        const auto&  version       = color_plan_->version_sites()[version_index];
        if (version.role != livehd::sim::Color_plan::Version_role::state_update) {
          continue;
        }
        const auto& site = color_plan_->sites()[version.base_site];
        const auto  q    = site.node.base_node().get_driver_pin(0);
        if (q.is_invalid()) {
          continue;
        }
        for (const size_t slot_index : direct_state_current_slots[version.base_site]) {
          const auto& slot = color_plan_->boundary_slots()[slot_index];
          if (slot.producer_port != q.get_port_id() || slot.producer_shift != 0
              || slot.producer_extract_hi > slot.producer_extract_lo) {
            continue;
          }
          livehd::sim::Color_plan::Boundary_consumer consumer{version_index,
                                                              color_index,
                                                              q.get_port_id(),
                                                              direct_state_current_input,
                                                              slot.width,
                                                              false};
          abi.reads.push_back({
              slot_index,
              rank,
              consumer,
              {rank,
                static_cast<uint8_t>(slot.kind),
                consumer.port,
                consumer.input,
                slot.width,
                consumer.width,
                slot.unsign,
                consumer.preextracted}
          });
          break;
        }
      }
      for (size_t rank = 0; rank < color_plan_->canonical_members()[color_index].size(); ++rank) {
        const size_t version = color_plan_->canonical_members()[color_index][rank];
        for (const size_t slot_index : direct_produced_slots[version]) {
          const auto& slot = color_plan_->boundary_slots()[slot_index];
          if (direct_write_expr(slot, slot_index).empty()) {
            continue;
          }
          abi.writes.push_back({
              slot_index,
              rank,
              version,
              {rank,
                static_cast<uint8_t>(slot.kind),
                slot.producer_port,
                slot.producer_shift,
                slot.producer_extract_lo,
                slot.producer_extract_hi,
                slot.width,
                slot.unsign}
          });
        }
      }
      std::ranges::sort(abi.reads, {}, &Direct_kernel_read::key);
      std::ranges::sort(abi.writes, {}, &Direct_kernel_write::key);
      return abi;
    };
    // Bind a member's inputs that the plan resolved to a producer in the SAME
    // color. These reads go through wiring the planner does not schedule (a
    // packed Or/SHL/Concat spelling, an erased GraphIO boundary), so no
    // boundary slot carries them: the consumer takes the producer's local value
    // with the use's extract/extend/shift applied. Shared by the inline
    // evaluator and the canonical kernel emitter. The kernel used to skip it,
    // which left such an operand unbound, and operand() silently put a 0 in its
    // place (br_credit_sender_vc: `get_mask(0 /*UNRESOLVED-CYCLE*/, 1, 2)`).
    const auto bind_internal_value_uses = [&](size_t                                          member,
                                              size_t                                          color_index,
                                              const hhds::Node_class&                         node,
                                              const absl::flat_hash_map<size_t, std::string>& version_value,
                                              const absl::flat_hash_set<size_t>&              version_slop_u) {
      for (const auto* use : direct_value_uses_by_consumer[member]) {
        if (use->top_input || use->producer_version == livehd::sim::Color_plan::invalid_index
            || direct_version_color[use->producer_version] != color_index) {
          continue;
        }
        const auto value_it = version_value.find(use->producer_version);
        I(value_it != version_value.end() && !value_it->second.empty());
        std::string value              = value_it->second;
        bool        value_is_u         = version_slop_u.contains(use->producer_version);
        // `use->consumer_width` is the value width AFTER any erased GraphIO
        // boundary, while version_value names the actual producer
        // temporary. A
        // fused hierarchy color can therefore hold a Slop<4> child-internal
        // packed expression whose public unsigned-u2 boundary is Slop<3>.
        // Canonicalize the materialized producer to the consumer ABI before
        // the next ordinary operation; otherwise fusion bypasses the module
        // boundary and HLOP correctly rejects (for example) Slop<3>::or over
        // that Slop<4> temporary.
        uint32_t    materialized_width = use->width;
        const auto& producer_version   = color_plan_->version_sites()[use->producer_version];
        const auto  producer_output    = find_site_output(producer_version.base_site, producer_version.output_port);
        if (!producer_output.is_invalid()) {
          materialized_width = static_cast<uint32_t>(std::max(1, wbits_of(producer_output)));
        }
        if (use->preextracted) {
          value      = range_extract_expr(value, use->width, use->unsign, use->producer_extract_lo, use->producer_extract_hi);
          value_is_u = slop_u_ && use->unsign;
          materialized_width = use->width;
        }
        if (materialized_width != use->consumer_width) {
          value      = use->unsign ? append_zext(value, static_cast<int>(use->consumer_width))
                                   : absl::StrCat("Slop<", use->consumer_width, ">{", value, "}");
          value_is_u = false;
        }
        if (use->producer_shift != 0) {
          value      = absl::StrCat("Slop<", use->consumer_width, ">::shl_op(", value, ", ", use->producer_shift, ")");
          value_is_u = false;
        }
        uint32_t input_index = 0;
        for (auto edge_sink : node.inp_sorted_pins()) {
          for (auto edge_drv : edge_sink.get_driver_pins()) {
            if (input_index++ != use->consumer_input) {
              continue;
            }
            I(edge_sink.get_port_id() == use->consumer_port);
            pin2var[edge_drv.get_class_index()] = std::move(value);
            if (use->preextracted) {
              preextracted_get_masks_.insert(node.get_class_index());
            }
            if (!use->unsign || use->width <= use->consumer_width) {
              canonical_.insert(edge_drv.get_class_index());
            }
            if (value_is_u) {
              slop_u_values_.insert(edge_drv.get_class_index());
            }
            break;
          }
        }
      }
    };
    const auto kernel_read_is_addressable = [&](const Direct_kernel_read& read) {
      const auto& slot = color_plan_->boundary_slots()[read.slot_index];
      if (slot.kind == livehd::sim::Color_plan::Boundary_kind::color_value) {
        return true;
      }
      if (slot.producer_extract_hi > slot.producer_extract_lo || slot.producer_shift != 0) {
        return false;  // direct_read_expr is a computed temporary, not storage whose address can enter the ABI
      }
      if (slot.kind == livehd::sim::Color_plan::Boundary_kind::top_input) {
        for (const auto& io : ios) {
          if (io.is_input && io.port_id == static_cast<uint32_t>(slot.public_port)) {
            return static_cast<uint32_t>(io.bits) == slot.width;
          }
        }
        return false;
      }
      if (slot.kind == livehd::sim::Color_plan::Boundary_kind::state_current) {
        const auto pin = color_plan_->sites()[slot.owner_site].node.base_node().get_driver_pin(slot.producer_port);
        return !pin.is_invalid() && static_cast<uint32_t>(std::max(1, wbits_of(pin))) == slot.width;
      }
      return false;
    };
    const auto kernel_name = [&](std::string_view signature) {
      std::string name = absl::StrCat("__lhd_color_kernel_", llvm_backend_ ? cpp_id(fstem) : mod, "_");
      for (const char c : signature) {
        name += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
      }
      return name;
    };
    const auto kernel_file_stem = [&](std::string_view signature) {
      std::string name = absl::StrCat(fstem, ".color-kernel-");
      for (const char c : signature) {
        name += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
      }
      return name;
    };
    const auto kernel_instance_name = [&](size_t color_index, std::string_view signature) {
      auto name = kernel_name(llvm_backend_ ? "body" : signature);
      if (llvm_backend_) {
        absl::StrAppend(&name, "_color_", color_index);
      }
      return name;
    };
    const auto kernel_instance_file_stem = [&](size_t color_index, std::string_view signature) {
      // LLVM emits one body per color, so the color index already identifies
      // it. Repeating a 128-bit signature exhausted NAME_MAX on loop bodies.
      auto name = kernel_file_stem(llvm_backend_ ? "body" : signature);
      if (llvm_backend_) {
        absl::StrAppend(&name, "-color-", color_index);
      }
      return name;
    };
    std::map<std::string, size_t> llvm_data_eligibility;
    std::map<std::string, size_t> llvm_state_eligibility;
    std::map<std::string, size_t> llvm_color_rejections;
    const bool                    llvm_state_debug = std::getenv("LIVEHD_SIM_LLVM_STATE_DEBUG") != nullptr;
    const auto                    pure_data_member = [&](const size_t member) {
      const auto reject_data = [&](std::string_view reason) {
        if (llvm_state_debug) {
          ++llvm_data_eligibility[std::string(reason)];
        }
        return false;
      };
      const auto& version = color_plan_->version_sites()[member];
      const auto& site    = color_plan_->sites()[version.base_site];
      const auto  op      = type_op_of(site.node.base_node());
      if (version.role != livehd::sim::Color_plan::Version_role::data) {
        return reject_data("role");
      }
      if (op == Ntype_op::Clock_cell || op == Ntype_op::Sub) {
        return reject_data("operation");
      }
      const auto definition_inputs = livehd::graph_util::inp_sink_drivers(site.node.base_node());
      const auto occurrence_inputs = occurrence_sorted_inputs(site.node);
      if (definition_inputs.size() != occurrence_inputs.size()) {
        return reject_data("input-shape");
      }
      if (op != Ntype_op::Memory) {
        const auto& local_clocks = local_clocks_for(site.node.get_graph());
        for (size_t input = 0; input < definition_inputs.size(); ++input) {
          if (livehd::graph_util::is_graph_input_pin(definition_inputs[input].driver)
              && local_clocks.is_clock(definition_inputs[input].driver)) {
            return reject_data("clock-input");  // clock levels are scheduler slot values, not bound ABI inputs
          }
        }
      }
      if (llvm_state_debug) {
        ++llvm_data_eligibility["eligible"];
      }
      return true;
    };
    const auto pure_data_color
        = [&](size_t color_index) { return std::ranges::all_of(color_plan_->colors()[color_index].members, pure_data_member); };
    const auto llvm_state_update_member = [&](const size_t member) {
      const auto& version      = color_plan_->version_sites()[member];
      const auto& site         = color_plan_->sites()[version.base_site];
      const auto  node         = site.node.base_node();
      const auto  op           = type_op_of(node);
      const auto  reject_state = [&](std::string_view reason) {
        if (llvm_state_debug) {
          ++llvm_state_eligibility[std::string(reason)];
          std::fprintf(stderr,
                       "[llvm-state-reject] module=%s member=%zu structural=%s storage=%s op=%s reason=%.*s\n",
                       gname.c_str(),
                       member,
                       version.structural_id.c_str(),
                       site.storage_id.c_str(),
                       op_name(op),
                       static_cast<int>(reason.size()),
                       reason.data());
        }
        return false;
      };
      if (version.role != livehd::sim::Color_plan::Version_role::state_update) {
        return reject_state("not-update");
      }
      if (op == Ntype_op::Memory) {
        const auto* memory = direct_memory(site);
        if (memory == nullptr) {
          return reject_state("memory-description");
        }
        if (memory->registered() && (!memory->clock_guards.empty() || !memory->tick_field.empty())
            && (version.base_site >= llvm_memory_gates.size() || !llvm_memory_gates[version.base_site].complete)) {
          return reject_state("memory-clock-guard");
        }
        if (llvm_state_debug) {
          ++llvm_state_eligibility["eligible-memory"];
        }
        return true;
      }
      if (op != Ntype_op::Flop && op != Ntype_op::Latch) {
        return reject_state("non-scalar");
      }
      // The color scheduler already wraps every conditional control region in
      // its resolved __valid predicate. A state-update kernel is therefore not
      // invoked while its occurrence is inactive, so no second data-ABI guard
      // is needed here.
      if (const auto pipe_min = get_driver(find_sink_pin(node, "pipe_min"));
          !pipe_min.is_invalid() && (!pipe_min.is_const() || const_of(pipe_min).to_just_i64() != 1)) {
        return reject_state("pipeline");
      }
      if (op == Ntype_op::Flop) {
        const auto definition_inputs = livehd::graph_util::inp_sink_drivers(node);
        const auto occurrence_inputs = occurrence_sorted_inputs(site.node);
        if (definition_inputs.size() != occurrence_inputs.size()) {
          return reject_state("input-shape");
        }
        for (size_t input = 0; input < definition_inputs.size(); ++input) {
          if (definition_inputs[input].sink.get_port_id() != Ntype::get_sink_pid(op, "clock_pin")) {
            continue;
          }
          const auto local_root = livehd::latch_contract::control_root(definition_inputs[input].driver);
          if (local_root.inverted || local_root.net.is_invalid() || !livehd::graph_util::is_graph_input_pin(local_root.net)
              || pin_name_of(local_root.net) != clock_input_of(site.node.get_graph())) {
            return reject_state("local-clock");
          }
          const auto resolved_root = livehd::latch_contract::control_root(occurrence_inputs[input].driver);
          if (resolved_root.inverted || resolved_root.net.is_invalid()
              || !livehd::graph_util::is_graph_input_pin(resolved_root.net.base_pin()) || resolved_root.net.get_graph() != g
              || pin_name_of(resolved_root.net.base_pin()) != clock_input_of(g)) {
            return reject_state("resolved-clock");
          }
          break;
        }
      }
      if (op == Ntype_op::Latch) {
        // The reference emitter derives a latch's COMMIT predicate from its
        // clock provenance -- a Clock_cell activation, folded ICG guards, a
        // secondary clock's detected edge. None of that reaches the LLVM
        // object, whose commit is value-change only, so a gated latch would
        // commit on every period. The Flop arm above rejects those shapes
        // through control_root; a Latch has no such check, so make it explicit.
        const auto* local_latch = find_local_flop(version.base_site);
        if (local_latch == nullptr || !local_latch->clock_guards.empty() || !local_latch->sec_clock.is_invalid()) {
          return reject_state("latch-clock");
        }
        if (const auto latch_clock = get_driver(find_sink_pin(node, "clock_pin"));
            !latch_clock.is_invalid() && type_op_of(latch_clock.get_master_node()) == Ntype_op::Clock_cell) {
          return reject_state("latch-clock-cell");
        }
      }
      const bool current = std::ranges::any_of(direct_state_current_slots[version.base_site], [&](const size_t slot_index) {
        const auto& slot = color_plan_->boundary_slots()[slot_index];
        return slot.producer_shift == 0 && slot.producer_extract_hi == slot.producer_extract_lo
               && !direct_read_expr(slot, slot_index).empty();
      });
      if (llvm_state_debug) {
        ++llvm_state_eligibility[current ? "eligible" : "current-state"];
      }
      return current;
    };
    const auto llvm_kernel_color = [&](size_t color_index) {
      bool eligible = true;
      for (const size_t member : color_plan_->colors()[color_index].members) {
        if (color_plan_->version_sites()[member].role == livehd::sim::Color_plan::Version_role::state_read) {
          continue;  // its consumers read a state-current boundary ABI slot
        }
        if (color_plan_->version_sites()[member].role == livehd::sim::Color_plan::Version_role::state_update) {
          const bool state = llvm_state_update_member(member);
          if (llvm_state_debug && !state) {
            const auto& rejected_version = color_plan_->version_sites()[member];
            const auto  rejected_op      = type_op_of(color_plan_->sites()[rejected_version.base_site].node.base_node());
            std::fprintf(stderr,
                         "[llvm-member-reject] module=%s color=%zu member=%zu role=state-update op=%s\n",
                         gname.c_str(),
                         color_index,
                         member,
                         op_name(rejected_op));
          }
          eligible &= state;
        } else {
          const bool pure  = pure_data_member(member);
          eligible        &= pure;
          if (llvm_state_debug && !pure) {
            const auto& rejected_version = color_plan_->version_sites()[member];
            const auto  rejected_op      = type_op_of(color_plan_->sites()[rejected_version.base_site].node.base_node());
            ++llvm_color_rejections[std::string(op_name(rejected_op))];
            std::fprintf(stderr,
                         "[llvm-member-reject] module=%s color=%zu member=%zu role=%u op=%s\n",
                         gname.c_str(),
                         color_index,
                         member,
                         static_cast<unsigned>(rejected_version.role),
                         op_name(rejected_op));
          }
        }
      }
      return eligible;
    };
    const auto single_occurrence_body_color = [&](size_t color_index) {
      const auto& members = color_plan_->colors()[color_index].members;
      if (members.empty()) {
        return true;
      }
      const auto& path = color_plan_->sites()[color_plan_->version_sites()[members.front()].base_site].node.path();
      return std::ranges::all_of(members, [&](size_t member) {
        return color_plan_->sites()[color_plan_->version_sites()[member].base_site].node.path() == path;
      });
    };
    // Extra invariants that only the SHARED C++ kernel body needs. The LLVM
    // backend emits one object per color from that color's own occurrence, so
    // it may lower a Memory member (its storage is reached through generated
    // owner callbacks) and an occurrence-specific constant. A shared canonical
    // TU can do neither: it is emitted once from the representative and has no
    // module members in scope at all.
    const auto shared_cpp_body_color = [&](size_t color_index) {
      return std::ranges::all_of(color_plan_->colors()[color_index].members, [&](const size_t member) {
        const auto& version = color_plan_->version_sites()[member];
        const auto& site    = color_plan_->sites()[version.base_site];
        if (type_op_of(site.node.base_node()) == Ntype_op::Memory) {
          return false;
        }
        const auto definition_inputs = livehd::graph_util::inp_sink_drivers(site.node.base_node());
        const auto occurrence_inputs = occurrence_sorted_inputs(site.node);
        if (definition_inputs.size() != occurrence_inputs.size()) {
          return false;
        }
        for (size_t input = 0; input < definition_inputs.size(); ++input) {
          if (occurrence_inputs[input].driver.is_const() && !definition_inputs[input].driver.is_const()) {
            return false;  // occurrence-specific parameter: not part of the boundary ABI
          }
        }
        return true;
      });
    };
    std::vector<const livehd::sim::Color_plan::Kernel_class*> direct_kernel(color_plan_->colors().size(), nullptr);
    std::vector<Direct_kernel_abi>                            direct_abi(color_plan_->colors().size());
    for (const auto& kernel : color_plan_->kernel_classes()) {
      // The shared C++ kernel only earns its own translation unit when a class
      // is REUSED, and a standalone TU can lower only what it can see (no module
      // members, hence no Memory and no occurrence-specific constant). The LLVM
      // backend has neither restriction: it emits one object per color from that
      // color's own occurrence. Decide this HERE rather than at emission: the
      // File_output below flushes on destruction, so a class rejected after it
      // exists leaves a comment-only .cpp in the sim directory (and a declared
      // but undefined kernel symbol in the kernels header), and its name is
      // already in `expected_kernel_files` so the stale-file pruner keeps it.
      if (!llvm_backend_ && (kernel.colors.size() < 2 || !std::ranges::all_of(kernel.colors, [&](const size_t color_index) {
                               return pure_data_color(color_index) && single_occurrence_body_color(color_index)
                                      && shared_cpp_body_color(color_index);
                             }))) {
        continue;
      }
      if (!llvm_kernel_color(kernel.representative)) {
        if (llvm_state_debug) {
          std::fprintf(stderr,
                       "[llvm-kernel-reject] module=%s color=%zu reason=representative\n",
                       gname.c_str(),
                       kernel.representative);
        }
        continue;
      }
      auto representative_abi = build_direct_kernel_abi(kernel.representative);
      bool verified           = true;
      for (const size_t color_index : kernel.colors) {
        if (!llvm_kernel_color(color_index)) {
          if (llvm_state_debug) {
            std::fprintf(stderr, "[llvm-kernel-reject] module=%s color=%zu reason=member\n", gname.c_str(), color_index);
          }
          verified = false;
          break;
        }
        auto abi = build_direct_kernel_abi(color_index);
        if (abi.reads.size() != representative_abi.reads.size() || abi.writes.size() != representative_abi.writes.size()) {
          if (llvm_state_debug) {
            std::fprintf(stderr,
                         "[llvm-kernel-reject] module=%s color=%zu reason=abi-size reads=%zu/%zu writes=%zu/%zu\n",
                         gname.c_str(),
                         color_index,
                         abi.reads.size(),
                         representative_abi.reads.size(),
                         abi.writes.size(),
                         representative_abi.writes.size());
          }
          verified = false;
          break;
        }
        if (!llvm_backend_ && !std::ranges::all_of(abi.reads, kernel_read_is_addressable)) {
          verified = false;  // the shared TU binds storage by address; a computed read has none
          break;
        }
        for (size_t i = 0; i < abi.reads.size(); ++i) {
          verified &= abi.reads[i].key == representative_abi.reads[i].key;
        }
        for (size_t i = 0; i < abi.writes.size(); ++i) {
          verified &= abi.writes[i].key == representative_abi.writes[i].key;
        }
        if (!verified) {
          if (llvm_state_debug) {
            std::fprintf(stderr, "[llvm-kernel-reject] module=%s color=%zu reason=abi-key\n", gname.c_str(), color_index);
          }
          break;
        }
        direct_abi[color_index] = std::move(abi);
      }
      if (!verified) {
        for (const size_t color_index : kernel.colors) {
          direct_abi[color_index] = {};
        }
        continue;
      }
      for (const size_t color_index : kernel.colors) {
        direct_kernel[color_index] = &kernel;
        if (llvm_state_debug) {
          std::fprintf(stderr,
                       "[llvm-kernel] module=%s color=%zu reads=%zu writes=%zu\n",
                       gname.c_str(),
                       color_index,
                       direct_abi[color_index].reads.size(),
                       direct_abi[color_index].writes.size());
        }
      }
    }
    if (llvm_state_debug && !llvm_state_eligibility.empty()) {
      std::fprintf(stderr, "[llvm-state] module=%s", gname.c_str());
      for (const auto& [reason, count] : llvm_state_eligibility) {
        std::fprintf(stderr, " %s=%zu", reason.c_str(), count);
      }
      std::fputc('\n', stderr);
      if (!llvm_color_rejections.empty()) {
        std::fprintf(stderr, "[llvm-color-reject] module=%s", gname.c_str());
        for (const auto& [reason, count] : llvm_color_rejections) {
          std::fprintf(stderr, " %s=%zu", reason.c_str(), count);
        }
        std::fputc('\n', stderr);
      }
      if (!llvm_data_eligibility.empty()) {
        std::fprintf(stderr, "[llvm-data] module=%s", gname.c_str());
        for (const auto& [reason, count] : llvm_data_eligibility) {
          std::fprintf(stderr, " %s=%zu", reason.c_str(), count);
        }
        std::fputc('\n', stderr);
      }
    }
    std::vector<bool>        llvm_inline_kernel(color_plan_->colors().size(), false);
    std::vector<bool>        llvm_scalar_kernel(color_plan_->colors().size(), false);
    std::vector<std::string> llvm_inline_raw_name(color_plan_->colors().size());

    // One digest-named TU per verified canonical class. Occurrence-specific
    // storage is supplied only through the ordered boundary ABI below.
    const std::string kernel_header_name = fstem + ".color-kernels.hpp";
    auto              kernel_header      = open_out(kernel_header_name);
    kernel_header->append("// Generated canonical simulator kernels. Do not edit.\n#pragma once\n#include <cstdint>\n");
    fout->append("#include \"", kernel_header_name, "\"\n");
    absl::flat_hash_set<std::string> expected_kernel_files;
    absl::flat_hash_set<std::string> expected_llvm_objects;
    for (size_t color_index = 0; color_index < direct_kernel.size(); ++color_index) {
      const auto* kernel = direct_kernel[color_index];
      if (kernel != nullptr) {
        // LLVM adapters are emitted at their typed occurrence in the module
        // evaluator.  Keeping a tiny C++ TU beside every native object made
        // the host build parse slop.hpp hundreds of redundant times.
        if (!llvm_backend_) {
          expected_kernel_files.insert(kernel_instance_file_stem(color_index, kernel->signature) + ".cpp");
        }
        expected_llvm_objects.insert(kernel_instance_file_stem(color_index, kernel->signature) + ".llvm.o");
      }
    }
    if (!odir.empty()) {
      std::error_code   ec;
      const std::string prefix = fstem + ".color-kernel-";
      for (const auto& entry : std::filesystem::directory_iterator(std::string(odir), ec)) {
        const auto filename = entry.path().filename().string();
        if (!entry.is_regular_file() || !filename.starts_with(prefix)) {
          continue;
        }
        if ((filename.ends_with(".cpp") && expected_kernel_files.contains(filename))
            || (filename.ends_with(".llvm.o") && llvm_backend_ && expected_llvm_objects.contains(filename))) {
          continue;
        }
        if (!filename.ends_with(".cpp") && !filename.ends_with(".llvm.o")) {
          continue;
        }
        std::filesystem::remove(entry.path(), ec);
      }
    }
    absl::flat_hash_set<std::string> emitted_kernels;
    absl::flat_hash_set<std::string> emitted_llvm_memory_helpers;
    absl::flat_hash_set<std::string> emitted_llvm_constant_helpers;
    const auto llvm_memory_helper_name = [&](const livehd::sim::Color_plan::Site& site, std::string_view operation) {
      std::string name = absl::StrCat("__lhd_llvm_mem_", mod, "_", site.storage_id, "_", operation);
      for (char& c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c))) {
          c = '_';
        }
      }
      return name;
    };
    const auto emit_llvm_memory_read_helper
        = [&](const livehd::sim::Color_plan::Site& site, const MemPort& port, Cgen_llvm::Value address) {
            const auto symbol = llvm_memory_helper_name(site, absl::StrCat("read_", port.rdidx));
            if (emitted_llvm_memory_helpers.insert(symbol).second) {
              fout->append(absl::StrCat("extern \"C\" void ",
                                        symbol,
                                        "(void* __owner, const std::uint64_t* __addr_words, std::uint64_t* __out_words) {\n",
                                        "  auto& __self = *static_cast<",
                                        mod,
                                        "*>(__owner);\n",
                                        "  const auto __addr = ",
                                        address.unsign ? "Slop_u<" : "Slop<",
                                        address.width,
                                        ">::from_packed_words(__addr_words);\n",
                                        "  const auto __value = __self.",
                                        occurrence_member(site),
                                        ".read(",
                                        port.rdidx,
                                        ", __addr);\n",
                                        "  __value.copy_packed_words(__out_words);\n",
                                        "}\n"));
            }
            return symbol;
          };
    const auto emit_llvm_memory_read_all_helper = [&](const livehd::sim::Color_plan::Site& site) {
      const auto symbol = llvm_memory_helper_name(site, "read_all");
      if (emitted_llvm_memory_helpers.insert(symbol).second) {
        fout->append(absl::StrCat("extern \"C\" void ",
                                  symbol,
                                  "(void* __owner, std::uint64_t* __out_words) {\n",
                                  "  auto& __self = *static_cast<",
                                  mod,
                                  "*>(__owner);\n",
                                  "  const auto __value = __self.",
                                  occurrence_member(site),
                                  ".read_all();\n",
                                  "  __value.copy_packed_words(__out_words);\n",
                                  "}\n"));
      }
      return symbol;
    };
    const auto emit_llvm_memory_apply_helper = [&](const livehd::sim::Color_plan::Site& site, const Mem& memory) {
      const auto symbol = llvm_memory_helper_name(site, "apply");
      if (emitted_llvm_memory_helpers.insert(symbol).second) {
        fout->append(absl::StrCat("extern \"C\" void ",
                                  symbol,
                                  "(void* __owner, const std::uint64_t* __data_words) {\n",
                                  "  auto& __self = *static_cast<",
                                  mod,
                                  "*>(__owner);\n",
                                  "  const auto __data = ",
                                  value_type(memory.bits * memory.size, memory.unsign),
                                  "::from_packed_words(__data_words);\n",
                                  "  __self.",
                                  occurrence_member(site),
                                  ".apply_update(__data);\n",
                                  "}\n"));
      }
      return symbol;
    };
    const auto emit_llvm_memory_stage_whole_helper
        = [&](size_t site_index, const livehd::sim::Color_plan::Site& site, const Mem& memory) {
            const auto symbol = llvm_memory_helper_name(site, "stage_whole");
            if (emitted_llvm_memory_helpers.insert(symbol).second) {
              const auto pending = whole_pending(site_index);
              fout->append(absl::StrCat("extern \"C\" void ",
                                        symbol,
                                        "(void* __owner, const std::uint64_t* __enable_words, const std::uint64_t* __force_words, "
                                        "const std::uint64_t* __data_words) {\n",
                                        "  auto& __self = *static_cast<",
                                        mod,
                                        "*>(__owner);\n",
                                        "  __self.",
                                        pending,
                                        "_din = ",
                                        value_type(memory.bits * memory.size, memory.unsign),
                                        "::from_packed_words(__data_words);\n",
                                        "  __self.",
                                        pending,
                                        "_cen = Slop<1>::from_packed_words(__force_words).is_known_true() || "
                                        "(Slop<1>::from_packed_words(__enable_words).is_known_true()",
                                        llvm_memory_gates[site_index].method.empty()
                                            ? std::string{}
                                            : absl::StrCat(" && __self.", llvm_memory_gates[site_index].method, "()"),
                                        ");\n",
                                        "}\n"));
            }
            return symbol;
          };
    const auto emit_llvm_memory_clear_helper = [&](const livehd::sim::Color_plan::Site& site) {
      const auto symbol = llvm_memory_helper_name(site, "clear");
      if (emitted_llvm_memory_helpers.insert(symbol).second) {
        fout->append(absl::StrCat("extern \"C\" void ",
                                  symbol,
                                  "(void* __owner) {\n",
                                  "  auto& __self = *static_cast<",
                                  mod,
                                  "*>(__owner);\n",
                                  "  __self.",
                                  occurrence_member(site),
                                  ".clear_pending();\n",
                                  "}\n"));
      }
      return symbol;
    };
    const auto emit_llvm_memory_stage_helper = [&](size_t                               site_index,
                                                   const livehd::sim::Color_plan::Site& site,
                                                   const Mem&                           memory,
                                                   const MemPort&                       port,
                                                   Cgen_llvm::Value                     enable,
                                                   Cgen_llvm::Value                     address) {
      const auto symbol = llvm_memory_helper_name(site, absl::StrCat("stage_", port.wridx));
      if (emitted_llvm_memory_helpers.insert(symbol).second) {
        fout->append(absl::StrCat("extern \"C\" void ",
                                  symbol,
                                  "(void* __owner, const std::uint64_t* __wen_words, const std::uint64_t* __addr_words, const "
                                  "std::uint64_t* __din_words) {\n",
                                  "  auto& __self = *static_cast<",
                                  mod,
                                  "*>(__owner);\n",
                                  "  const auto __wen = ",
                                  enable.unsign ? "Slop_u<" : "Slop<",
                                  enable.width,
                                  ">::from_packed_words(__wen_words);\n",
                                  "  const auto __addr = ",
                                  address.unsign ? "Slop_u<" : "Slop<",
                                  address.width,
                                  ">::from_packed_words(__addr_words);\n",
                                  "  const auto __din = ",
                                  value_type(memory.bits, memory.unsign),
                                  "::from_packed_words(__din_words);\n",
                                  "  const auto __effective_wen = ",
                                  llvm_memory_gates[site_index].method.empty() ? std::string{"__wen"}
                                                                               : absl::StrCat("__self.",
                                                                                              llvm_memory_gates[site_index].method,
                                                                                              "() ? __wen : ",
                                                                                              enable.unsign ? "Slop_u<" : "Slop<",
                                                                                              enable.width,
                                                                                              ">::create_integer(0)"),
                                  ";\n",
                                  "  __self.",
                                  occurrence_member(site),
                                  ".stage_write(",
                                  port.wridx,
                                  ", __effective_wen, __addr, __din);\n",
                                  "}\n"));
      }
      return symbol;
    };
    for (size_t color_index = 0; color_index < direct_kernel.size(); ++color_index) {
      const auto* kernel       = direct_kernel[color_index];
      const auto  emission_key = llvm_backend_ ? absl::StrCat(kernel == nullptr ? "" : kernel->signature, ":", color_index)
                                               : std::string(kernel == nullptr ? "" : kernel->signature);
      if (kernel == nullptr || !emitted_kernels.insert(emission_key).second) {
        continue;
      }
      const size_t representative = llvm_backend_ ? color_index : kernel->representative;
      const auto&  abi            = direct_abi[representative];
      const auto   instance_name  = kernel_instance_name(color_index, kernel->signature);
      const auto   instance_stem  = kernel_instance_file_stem(color_index, kernel->signature);
      if (!llvm_backend_) {
        kernel_header->append("void ", instance_name, "(void*, void**, std::uint64_t*);\n");
      }
      const std::string            kernel_path = instance_stem + ".cpp";
      // LLVM kernels are native objects with an inline adapter; they never
      // need a C++ kernel translation unit.  Opening one here used to record a
      // phantom .cpp in gen_digests.json before aborting and deleting it.  That
      // made every otherwise-warm LLVM generation fail the artifact-complete
      // check and bypass the early module deduplication.
      std::shared_ptr<File_output> kernel_out;
      if (!llvm_backend_) {
        kernel_out = open_out(kernel_path);
      }
      const auto slot_is_u
          = [&](size_t slot_index) { return slot_index < direct_slot_is_u.size() && direct_slot_is_u[slot_index]; };
      // The ABI is a void* type-pun: the cast has to name the type the binding
      // site took the address of. `direct_slot_is_u` is the ONE authority that
      // resolves that per kind against the declaration that produced the
      // storage; `slot.unsign` is the USE's sign and legitimately differs (a
      // current-view state slot is the documented case), which aliases two
      // unrelated class types -- and at width%64==0 two different SIZES.
      const auto slot_type = [&](size_t slot_index, uint32_t width) {
        return slot_is_u(slot_index) ? absl::StrCat("Slop_u<", width, ">") : absl::StrCat("Slop<", width, ">");
      };
      // Single-node scalar regions pay off in modules with enough independent
      // colors for Slop's repeated exact-width wrapper work to dominate (the
      // matched-filter shape). On small control-heavy modules (DINO), keeping
      // those cases in the already-inline Slop evaluator is faster. Multi-node
      // scalar regions still benefit directly from LLVM folding.
      constexpr size_t llvm_scalar_module_threshold = 64;
      const bool       llvm_scalar_abi
          = abi.writes.size() == 1 && color_plan_->boundary_slots()[abi.writes.front().slot_index].width <= 64
            && std::ranges::all_of(
                abi.reads,
                [&](const Direct_kernel_read& read) { return color_plan_->boundary_slots()[read.slot_index].width <= 64; })
            && (color_plan_->colors()[representative].members.size() > 1
                || color_plan_->colors().size() >= llvm_scalar_module_threshold);
      // The C++ adapter is ABI glue only. Values are packed into little-endian
      // limbs at the boundary; every operation remains an exact-width LLVM iN.
      const std::string llvm_raw_name = instance_name + "_llvm";
      const std::string llvm_object   = instance_stem + ".llvm.o";
      std::string       llvm_rejection;
      const auto        emit_llvm_object = [&]() -> bool {
        // The LLVM kernel has no C++ translation unit to record the reason in
        // (see above), so keep the fallback explanation reachable the way the
        // other LLVM eligibility tallies are.
        const auto reject = [&](std::string_view reason) {
          llvm_rejection = reason;
          if (llvm_state_debug) {
            std::fprintf(stderr,
                         "sim llvm: color %zu falls back to Slop: %.*s\n",
                         color_index,
                         static_cast<int>(reason.size()),
                         reason.data());
          }
          return false;
        };
        if (!llvm_backend_) {
          return false;  // the reference backend: the shared C++ body below IS the kernel, not a fallback
        }
        // sim.tune.backend selects the implementation, not a profitability hint.
        // The packed ABI handles wide and multi-output kernels; the scalar ABI
        // is only a calling-convention optimization for the same LLVM backend.
        for (const auto& read : abi.reads) {
          const auto& slot = color_plan_->boundary_slots()[read.slot_index];
          if (slot.width == 0 || read.consumer.width == 0) {
            return reject("boundary read has a zero width");
          }
        }
        for (const auto& write : abi.writes) {
          const auto& slot = color_plan_->boundary_slots()[write.slot_index];
          if (direct_write_expr(slot, write.slot_index).empty() || slot.width == 0) {
            return reject("boundary write is not a direct value");
          }
        }

        std::vector<std::pair<uint32_t, bool>> input_types;
        input_types.reserve(abi.reads.size());
        for (const auto& read : abi.reads) {
          const auto& slot = color_plan_->boundary_slots()[read.slot_index];
          input_types.emplace_back(slot.width, slot.unsign);
        }
        Cgen_llvm                                       llvm_kernel(llvm_raw_name, input_types, llvm_scalar_abi);
        absl::flat_hash_map<uint64_t, Cgen_llvm::Value> llvm_inputs;
        absl::flat_hash_map<size_t, Cgen_llvm::Value>   llvm_outputs;
        absl::flat_hash_set<size_t>                     llvm_preextracted_get_masks;
        const auto input_key = [](size_t version, uint32_t input) { return (static_cast<uint64_t>(version) << 32) | input; };
        for (size_t i = 0; i < abi.reads.size(); ++i) {
          const auto& read             = abi.reads[i];
          const auto& slot             = color_plan_->boundary_slots()[read.slot_index];
          const auto& consumer_version = color_plan_->version_sites()[read.consumer.version_site];
          const auto  consumer_node    = color_plan_->sites()[consumer_version.base_site].node.base_node();
          uint32_t    consumer_input   = 0;
          bool        bound            = false;
          if (consumer_version.role == livehd::sim::Color_plan::Version_role::state_read
              || read.consumer.input == direct_state_current_input) {
            auto value = llvm_kernel.input(i);
            if (read.consumer.width != slot.width) {
              value = llvm_kernel.resize(value, read.consumer.width, slot.unsign);
            }
            llvm_inputs[input_key(read.consumer.version_site, read.consumer.input)] = value;
            bound                                                                   = true;
          }
          for (auto edge_sink : consumer_node.inp_sorted_pins()) {
            for (auto edge_drv : edge_sink.get_driver_pins()) {
              if (bound) {
                break;
              }
              if (consumer_input++ != read.consumer.input) {
                continue;
              }
              if (edge_sink.get_port_id() != read.consumer.port) {
                return reject("boundary consumer port does not match its planned input");
              }
              auto value = llvm_kernel.input(i);
              if (read.consumer.width != slot.width) {
                value = llvm_kernel.resize(value, read.consumer.width, slot.unsign);
              }
              llvm_inputs[input_key(read.consumer.version_site, read.consumer.input)] = value;
              if (read.consumer.preextracted) {
                llvm_preextracted_get_masks.insert(read.consumer.version_site);
              }
              bound = true;
              break;
            }
          }
          if (!bound) {
            return reject("boundary consumer input was not found");
          }
        }

        auto                        members = color_plan_->colors()[representative].members;
        absl::flat_hash_set<size_t> member_set(members.begin(), members.end());
        absl::flat_hash_map<uint64_t, std::vector<const livehd::sim::Color_plan::Value_use*>> internal_uses;
        for (const auto& use : color_plan_->value_uses()) {
          if (use.consumer_version == livehd::sim::Color_plan::invalid_index
              || use.producer_version == livehd::sim::Color_plan::invalid_index || !member_set.contains(use.consumer_version)
              || !member_set.contains(use.producer_version)) {
            continue;
          }
          internal_uses[input_key(use.consumer_version, use.consumer_input)].push_back(&use);
        }
        const auto select_internal_use = [&](uint64_t key, const hhds::Occurrence_edge& edge) {
          const auto uses_it = internal_uses.find(key);
          if (uses_it == internal_uses.end() || uses_it->second.empty()) {
            return static_cast<const livehd::sim::Color_plan::Value_use*>(nullptr);
          }
          if (uses_it->second.size() == 1) {
            return uses_it->second.front();
          }
          const auto driver_occurrence = edge.driver.get_master_node().get_occurrence_index();
          for (const auto* use : uses_it->second) {
            const auto& producer_version = color_plan_->version_sites()[use->producer_version];
            const auto& producer_site    = color_plan_->sites()[producer_version.base_site];
            if (producer_version.output_port == edge.driver.get_port_id()
                && producer_site.node.get_occurrence_index() == driver_occurrence) {
              return use;
            }
          }
          return static_cast<const livehd::sim::Color_plan::Value_use*>(nullptr);
        };
        std::ranges::sort(members, [&](size_t a, size_t b) {
          const auto& av = color_plan_->version_sites()[a];
          const auto& bv = color_plan_->version_sites()[b];
          return std::tie(av.execution_order, av.structural_id) < std::tie(bv.execution_order, bv.structural_id);
        });
        for (const size_t member : members) {
          const auto& version = color_plan_->version_sites()[member];
          const bool  state_read_with_output
              = version.role == livehd::sim::Color_plan::Version_role::state_read
                && std::ranges::any_of(abi.writes, [&](const Direct_kernel_write& write) { return write.version == member; });
          if (version.role == livehd::sim::Color_plan::Version_role::state_read && !state_read_with_output) {
            continue;
          }
          const auto&     occurrence = color_plan_->sites()[version.base_site].node;
          const auto      node       = occurrence.base_node();
          hhds::Pin_class output;
          for (const auto& candidate : node.out_pins()) {
            if (candidate.get_port_id() == version.output_port) {
              output = candidate;
              break;
            }
          }
          if (output.is_invalid()) {
            output = node.get_driver_pin(0);
          }
          if (output.is_invalid()) {
            return reject("member has no output pin");
          }
          uint32_t result_width = static_cast<uint32_t>(wbits_of(output));
          if (type_op_of(node) == Ntype_op::Memory) {
            const auto* memory = find_local_mem(version.base_site);
            if (memory == nullptr || memory->bits <= 0 || memory->size <= 0) {
              return reject("memory member has an invalid storage width");
            }
            const uint64_t memory_result_width = version.output_port == Ntype::Memory_readall_pid
                                                     ? static_cast<uint64_t>(memory->bits) * memory->size
                                                     : static_cast<uint64_t>(memory->bits);
            if (memory_result_width > std::numeric_limits<uint32_t>::max()) {
              return reject("memory member result is too wide for LLVM");
            }
            result_width = static_cast<uint32_t>(memory_result_width);
          }
          if (result_width == 0) {
            return reject("member result has a zero width");
          }
          if (version.role == livehd::sim::Color_plan::Version_role::state_read) {
            I(state_read_with_output);
            Cgen_llvm::Value state_value;
            for (const auto& read : abi.reads) {
              if (read.consumer.version_site != member) {
                continue;
              }
              const auto input_it = llvm_inputs.find(input_key(member, read.consumer.input));
              if (input_it != llvm_inputs.end()) {
                state_value = input_it->second;
                break;
              }
            }
            if (state_value.width == 0) {
              return reject("state-read output has no boundary input");
            }
            llvm_outputs[member] = llvm_kernel.resize(state_value, result_width, state_value.unsign);
            for (size_t write_index = 0; write_index < abi.writes.size(); ++write_index) {
              const auto& write = abi.writes[write_index];
              if (write.version != member) {
                continue;
              }
              const auto& slot         = color_plan_->boundary_slots()[write.slot_index];
              auto        source_value = llvm_kernel.resize(llvm_outputs[member], slot.width, slot.unsign);
              std::string error;
              if (!llvm_kernel.add_output(write_index, source_value, error)) {
                return reject("LLVM state-read output construction failed");
              }
            }
            continue;
          }
          const bool result_unsign = proven_unsigned_result(node, output);
          // The node's (sink, driver) inputs, indexed positionally below
          // (edges[0] value, edges[1] mask, ...) and by the ABI input ordinal,
          // so it has to be the same sequence the rest of the direct emitter
          // counts: one element per resolved DRIVER, sink pins in ascending
          // port order with the node-as-pin first. The operand order is the pin
          // order, with nothing to sort.
          const auto edges         = occurrence_sorted_inputs(occurrence);
          const auto constant_of   = [&](const hhds::Occurrence_pin& pin) -> Cgen_llvm::Value {
            if (pin.is_const()) {
              const auto& constant = const_of(pin);
              if (!constant.is_numeric()) {  // a Boolean const is the u1 `1`/`0`, minted with its tag
                return {};
              }
              const auto width
                  = static_cast<uint32_t>(std::max({wbits_of(pin.base_pin()), static_cast<int>(constant.get_signed_bits()), 1}));
              // Unknown constants are resolved by the shared runtime literal
              // cache after the driver sets its seed. LLVM must not draw them
              // during setup, or silently select the Slop circuit backend.
              if (constant.has_unknowns() && !unknown_zero_) {
                const auto literal = sim_const_text(constant, false);
                const auto symbol = absl::StrCat("__lhd_llvm_constant_", mod, "_", width, "_", livehd::hash_util::fnv1a64(literal));
                if (emitted_llvm_constant_helpers.insert(symbol).second) {
                  fout->append("extern \"C\" void ",
                               symbol,
                               "(void*, std::uint64_t* __words) {\n  (",
                               sim_const_expr(literal, std::to_string(width)),
                               ").copy_packed_words(__words);\n}\n");
                }
                return llvm_kernel.external_read_all(symbol, width, is_unsign(pin));
              }
              auto simulated = constant;
              if (constant.has_unknowns()) {
                const auto parsed = Dlop::from_pyrope(sim_const_text(constant, unknown_zero_));
                if (!parsed || !parsed->is_numeric() || parsed->has_unknowns()) {
                  return {};
                }
                simulated = *parsed;
              }
              std::vector<uint64_t> words((static_cast<size_t>(width) + 63) / 64, 0);
              for (uint32_t bit = 0; bit < width; ++bit) {
                if (simulated.bit_test(static_cast<int>(bit))) {
                  words[bit / 64] |= uint64_t{1} << (bit % 64);
                }
              }
              return llvm_kernel.constant_words(width, words, is_unsign(pin));
            }
            return {};
          };
          absl::flat_hash_set<hhds::Port_id> memory_required_ports;
          if (type_op_of(node) == Ntype_op::Memory) {
            const auto* memory = find_local_mem(version.base_site);
            if (memory == nullptr) {
              return reject("memory member has no storage description");
            }
            // Decide from the exact sink, not merely from the driver identity:
            // one source commonly feeds several Memory attributes (including
            // metadata/timing fields), and marking every same-driver edge as
            // required creates fake ABI holes for those non-data inputs.
            for (const auto& dsink : node.inp_sorted_pins()) {
              const auto ddrv = dsink.get_driver_pin();
              if (ddrv.is_invalid() || ddrv.is_const()) {
                continue;
              }
              const auto     sink       = dsink.get_port_id();
              const auto     name       = Ntype::get_sink_name(Ntype_op::Memory, static_cast<int>(sink));
              // `ports` IS indexed by port group -- a linear search over a
              // duplicated `pid` field also matches the default-constructed
              // gap entries, which all report group 0.
              const size_t   port_group = static_cast<size_t>(sink) / Ntype::Memory_port_stride;
              const MemPort* port       = port_group < memory->ports.size() ? &memory->ports[port_group] : nullptr;
              bool           required   = false;
              if (version.role == livehd::sim::Color_plan::Version_role::state_update) {
                required  = (memory->is_whole() && (name == "update" || name == "update_enable"))
                            || (memory->has_reset_stage() && (name == "initial" || name == "reset"));
                required |= port != nullptr && !port->rd
                            && (str_tools::ends_with(name, "addr") || str_tools::ends_with(name, "din")
                                || (str_tools::ends_with(name, "enable") && name != "update_enable"));
              } else {
                required  = memory->is_whole() && !memory->registered() && name == "update";
                required |= port != nullptr && port->rd && static_cast<hhds::Port_id>(port->dout_pid) == version.output_port
                            && str_tools::ends_with(name, "addr");
              }
              if (required) {
                memory_required_ports.insert(sink);
              }
            }
          }
          std::vector<Cgen_llvm::Value> operands;
          operands.reserve(edges.size());
          for (size_t input = 0; input < edges.size(); ++input) {
            const auto& edge  = edges[input];
            auto        value = constant_of(edge.driver);
            if (type_op_of(node) == Ntype_op::Get_mask && llvm_preextracted_get_masks.contains(member) && input == 1) {
              value = llvm_kernel.constant(1, 0, true);  // the boundary already applied this mask
            }
            if (!edge.driver.is_const()) {
              const auto  key               = input_key(member, static_cast<uint32_t>(input));
              const auto* use_ptr           = select_internal_use(key, edge);
              bool        bound_to_internal = false;
              if (use_ptr != nullptr) {
                const auto& use       = *use_ptr;
                const auto  output_it = llvm_outputs.find(use.producer_version);
                if (output_it != llvm_outputs.end()) {
                  bound_to_internal = true;
                  value             = output_it->second;
                  if (use.preextracted) {
                    const auto work_width = std::max(value.width, use.producer_extract_hi);
                    value                 = llvm_kernel.resize(value, work_width, value.unsign);
                    if (use.producer_extract_lo != 0) {
                      const auto amount = llvm_kernel.constant(work_width, use.producer_extract_lo, true);
                      value             = llvm_kernel.binary(Cgen_llvm::Binary_op::lshr, value, amount, work_width, true);
                    }
                    value = llvm_kernel.resize(value, use.width, true);
                    llvm_preextracted_get_masks.insert(member);
                  }
                  if (value.width != use.consumer_width) {
                    value = llvm_kernel.resize(value, use.consumer_width, use.unsign);
                  }
                  if (use.producer_shift != 0) {
                    const auto amount = llvm_kernel.constant(use.consumer_width, use.producer_shift, true);
                    value = llvm_kernel.binary(Cgen_llvm::Binary_op::shl, value, amount, use.consumer_width, use.unsign);
                  }
                }
              }
              if (!bound_to_internal) {
                const auto input_it = llvm_inputs.find(key);
                if (input_it != llvm_inputs.end()) {
                  value = input_it->second;
                }
              }
            }
            if (value.width == 0 && type_op_of(node) == Ntype_op::Memory) {
              const bool required = memory_required_ports.contains(edge.sink.get_port_id());
              if (!required) {
                const auto width = static_cast<uint32_t>(std::max(wbits_of(edge.driver.base_pin()), 1));
                value            = llvm_kernel.constant(width, 0, true);
              }
            }
            if (value.width == 0) {
              const auto  key          = input_key(member, static_cast<uint32_t>(input));
              const auto* producer_use = select_internal_use(key, edge);
              std::string available_reads;
              for (const auto& read : abi.reads) {
                if (read.consumer.version_site != member) {
                  continue;
                }
                absl::StrAppend(&available_reads,
                                available_reads.empty() ? "" : ",",
                                read.consumer.input,
                                "/p",
                                read.consumer.port,
                                "/slot",
                                read.slot_index);
              }
              std::string planned_bindings;
              for (const auto& [slot_index, consumer] : direct_boundary_bindings_by_consumer[member]) {
                const auto& slot = color_plan_->boundary_slots()[slot_index];
                absl::StrAppend(&planned_bindings,
                                planned_bindings.empty() ? "" : ",",
                                consumer->input,
                                "/p",
                                consumer->port,
                                "/slot",
                                slot_index,
                                "/kind",
                                static_cast<int>(slot.kind),
                                direct_read_expr(slot, slot_index).empty() ? "/unreadable" : "/readable");
              }
              return reject(absl::StrCat(
                  "member ",
                  member,
                  " (`",
                  version.structural_id,
                  "`, ",
                  op_name(type_op_of(node)),
                  ") input ",
                  input,
                  " has no LLVM value; internal producer=",
                  producer_use == nullptr ? "none" : std::to_string(producer_use->producer_version),
                  producer_use != nullptr && !llvm_outputs.contains(producer_use->producer_version) ? " (not emitted yet)" : "",
                  "; ABI reads=[",
                  available_reads,
                  "]",
                  "; planned bindings=[",
                  planned_bindings,
                  "]",
                  "; driver=",
                  debug_name(edge.driver.get_master_node()),
                  edge.driver.is_const() ? absl::StrCat("; constant=", const_of(edge.driver).to_pyrope()) : std::string{},
                  edge.driver.is_invalid() ? "; undriven" : ""));
            }
            operands.push_back(value);
          }

          Cgen_llvm::Value result;
          const auto       fold = [&](Cgen_llvm::Binary_op operation) {
            if (operands.empty()) {
              return llvm_kernel.constant(result_width, 0, result_unsign);
            }
            auto value = operands.front();
            if (operands.size() == 1) {
              return llvm_kernel.resize(value, result_width, result_unsign);
            }
            for (size_t i = 1; i < operands.size(); ++i) {
              value = llvm_kernel.binary(operation, value, operands[i], result_width, result_unsign);
            }
            return value;
          };
          const auto operand_at = [&](std::string_view name) -> Cgen_llvm::Value {
            const auto port = Ntype::get_sink_pid(type_op_of(node), name);
            for (size_t input = 0; input < edges.size(); ++input) {
              if (edges[input].sink.get_port_id() == port) {
                return operands[input];
              }
            }
            return {};
          };
          const auto memory_operand = [&](std::optional<size_t> port_group, std::string_view field) -> Cgen_llvm::Value {
            for (size_t input = 0; input < edges.size(); ++input) {
              const auto sink = edges[input].sink.get_port_id();
              const auto name = Ntype::get_sink_name(Ntype_op::Memory, static_cast<int>(sink));
              if (port_group.has_value() && static_cast<size_t>(sink) / Ntype::Memory_port_stride != *port_group) {
                continue;
              }
              if ((!port_group.has_value() && name == field) || (port_group.has_value() && str_tools::ends_with(name, field))) {
                return operands[input];
              }
            }
            return {};
          };
          switch (type_op_of(node)) {
            case Ntype_op::Memory: {
              const auto* memory = find_local_mem(version.base_site);
              if (memory == nullptr) {
                return reject("memory callback has no storage description");
              }
              if (version.role == livehd::sim::Color_plan::Version_role::state_update) {
                if (memory->has_reset_stage()) {
                  const auto whole_width = static_cast<uint32_t>(memory->bits * memory->size);
                  // A per-port memory with a reset has no bulk update: its only
                  // whole-array arm is the reset reload (enable stays 0, the
                  // reset is the `force`).
                  auto       update      = llvm_kernel.constant(whole_width, 0, memory->unsign);
                  auto       enable      = llvm_kernel.constant(1, memory->is_whole() ? 1 : 0, true);
                  auto       force       = llvm_kernel.constant(1, 0, true);
                  if (memory->is_whole()) {
                    update = memory_operand(std::nullopt, "update");
                    if (update.width == 0) {
                      return reject("whole-array memory update has no data value");
                    }
                    update = llvm_kernel.resize(update, whole_width, memory->unsign);
                    if (!memory->update_enable.is_invalid()) {
                      auto update_enable = memory_operand(std::nullopt, "update_enable");
                      if (update_enable.width == 0) {
                        return reject("whole-array memory update has no enable value");
                      }
                      enable = llvm_kernel.reduce_or(update_enable, 1, true);
                    }
                  }
                  if (!memory->reset.is_invalid()) {
                    auto reset = memory_operand(std::nullopt, "reset");
                    if (reset.width == 0) {
                      return reject("whole-array memory update has no reset value");
                    }
                    reset     = llvm_kernel.reduce_or(reset, 1, true);
                    force     = reset;
                    auto init = memory->init.is_invalid() ? llvm_kernel.constant(whole_width, 0, memory->unsign)
                                                          : memory_operand(std::nullopt, "initial");
                    if (init.width == 0) {
                      return reject("whole-array memory update has no initial value");
                    }
                    init   = llvm_kernel.resize(init, whole_width, memory->unsign);
                    update = llvm_kernel.mux(reset, update, init, whole_width, memory->unsign);
                  }
                  const auto symbol
                      = emit_llvm_memory_stage_whole_helper(version.base_site, color_plan_->sites()[version.base_site], *memory);
                  if (!llvm_kernel.external_stage_whole(symbol, enable, force, update)) {
                    return reject("whole-array memory stage callback construction failed");
                  }
                }
                if (!llvm_kernel.external_clear(emit_llvm_memory_clear_helper(color_plan_->sites()[version.base_site]))) {
                  return reject("memory clear callback construction failed");
                }
                for (const auto& port : memory->ports) {
                  if (port.rd || port.addr.is_invalid() || port.din.is_invalid()) {
                    continue;
                  }
                  auto enable = memory_operand(port.pid, "enable");
                  if (enable.width == 0) {
                    // An absent enable pin means EVERY lane writes, which at
                    // wensize > 1 is all-ones -- not 1, which would leave every
                    // lane above the lowest permanently un-written (emit_wen
                    // spells this `Slop<wensize>::create_integer(-1)`).
                    const auto            wen_bits = static_cast<uint32_t>(std::max(memory->wensize, 1));
                    std::vector<uint64_t> all_lanes((static_cast<size_t>(wen_bits) + 63) / 64, ~uint64_t{0});
                    enable = llvm_kernel.constant_words(wen_bits, all_lanes, true);
                  }
                  auto address = memory_operand(port.pid, "addr");
                  auto data    = memory_operand(port.pid, "din");
                  if (address.width == 0 || data.width == 0) {
                    return reject("memory write callback has no address or data value");
                  }
                  data              = llvm_kernel.resize(data, static_cast<uint32_t>(memory->bits), memory->unsign);
                  const auto symbol = emit_llvm_memory_stage_helper(version.base_site,
                                                                    color_plan_->sites()[version.base_site],
                                                                    *memory,
                                                                    port,
                                                                    enable,
                                                                    address);
                  if (!llvm_kernel.external_stage_write(symbol, enable, address, data)) {
                    return reject("memory write callback construction failed");
                  }
                }
                result = llvm_kernel.constant(result_width, 0, result_unsign);
                break;
              }
              if (memory->is_whole() && !memory->registered()) {
                auto update = memory_operand(std::nullopt, "update");
                if (update.width == 0) {
                  return reject("combinational whole-array memory has no update value");
                }
                update = llvm_kernel.resize(update, static_cast<uint32_t>(memory->bits * memory->size), memory->unsign);
                if (!llvm_kernel.external_apply(emit_llvm_memory_apply_helper(color_plan_->sites()[version.base_site], *memory),
                                                update)) {
                  return reject("whole-array memory apply callback construction failed");
                }
              }
              if (memory->has_read_all && version.output_port == Ntype::Memory_readall_pid) {
                result = llvm_kernel.external_read_all(emit_llvm_memory_read_all_helper(color_plan_->sites()[version.base_site]),
                                                       result_width,
                                                       result_unsign);
                break;
              }
              if (memory->type == 1) {
                return reject("registered memory reads remain external state values");
              }
              const MemPort* read_port = nullptr;
              for (const auto& port : memory->ports) {
                if (port.rd && static_cast<hhds::Port_id>(port.dout_pid) == version.output_port) {
                  read_port = &port;
                  break;
                }
              }
              if (read_port == nullptr || read_port->addr.is_invalid()) {
                return reject("memory read callback has no matching port");
              }
              auto address = memory_operand(read_port->pid, "addr");
              if (address.width == 0) {
                return reject("memory read callback has no address value");
              }
              const auto symbol = emit_llvm_memory_read_helper(color_plan_->sites()[version.base_site], *read_port, address);
              result            = llvm_kernel.external_read(symbol, address, result_width, result_unsign);
              break;
            }
            case Ntype_op::Flop :
            case Ntype_op::Latch: {
              if (version.role != livehd::sim::Color_plan::Version_role::state_update) {
                return reject("register cell is not a state-update version");
              }
              const auto current_it = llvm_inputs.find(input_key(member, direct_state_current_input));
              if (current_it == llvm_inputs.end()) {
                return reject("register update has no current-state ABI input");
              }
              auto current = llvm_kernel.resize(current_it->second, result_width, result_unsign);
              auto din     = operand_at("din");
              din          = din.width == 0 ? current : llvm_kernel.resize(din, result_width, result_unsign);

              auto enable = operand_at("enable");
              if (enable.width == 0) {
                enable = llvm_kernel.constant(1, 1, true);
              } else {
                enable = llvm_kernel.reduce_or(enable, 1, true);
              }
              bool neg_enable = false;
              if (type_op_of(node) == Ntype_op::Latch) {
                const auto polarity = get_driver(find_sink_pin(node, "posclk"));
                neg_enable          = polarity.is_known_false();
              }
              if (neg_enable) {
                enable = llvm_kernel.unary_not(enable, 1, true);
              }
              result = llvm_kernel.mux(enable, current, din, result_width, result_unsign);

              auto reset = operand_at("reset_pin");
              if (reset.width != 0) {
                reset                   = llvm_kernel.reduce_or(reset, 1, true);
                const auto negreset_pin = get_driver(find_sink_pin(node, "negreset"));
                if (!get_driver(find_sink_pin(node, "reset_pin")).is_const() && negreset_pin.is_const()
                    && !negreset_pin.is_known_false()) {
                  reset = llvm_kernel.unary_not(reset, 1, true);
                }
                auto initial = operand_at("initial");
                initial      = initial.width == 0 ? llvm_kernel.constant(result_width, 0, result_unsign)
                                                  : llvm_kernel.resize(initial, result_width, result_unsign);
                result       = llvm_kernel.mux(reset, result, initial, result_width, result_unsign);
              }
              break;
            }
            case Ntype_op::Sum: {
              result = llvm_kernel.constant(result_width, 0, result_unsign);
              for (size_t i = 0; i < operands.size(); ++i) {
                const auto operation = Ntype::sink_bank(Ntype_op::Sum, edges[i].sink.get_port_id()) == 0
                                           ? Cgen_llvm::Binary_op::add
                                           : Cgen_llvm::Binary_op::sub;
                result               = llvm_kernel.binary(operation, result, operands[i], result_width, result_unsign);
              }
              break;
            }
            case Ntype_op::Mult: result = fold(Cgen_llvm::Binary_op::mul); break;
            case Ntype_op::Div :
            case Ntype_op::Rem:
              if (operands.size() < 2) {
                result = operands.empty() ? llvm_kernel.constant(result_width, 0, result_unsign)
                                          : llvm_kernel.resize(operands[0], result_width, result_unsign);
              } else {
                const auto operation  = type_op_of(node) == Ntype_op::Div ? Cgen_llvm::Binary_op::div : Cgen_llvm::Binary_op::rem;
                uint32_t   work_width = std::max({operands[0].width, operands[1].width, result_width});
                if (!operands[0].unsign || !operands[1].unsign) {
                  ++work_width;
                }
                result = llvm_kernel.binary(operation, operands[0], operands[1], work_width, result_unsign);
                result = llvm_kernel.resize(result, result_width, result_unsign);
              }
              break;
            case Ntype_op::And: result = fold(Cgen_llvm::Binary_op::bit_and); break;
            case Ntype_op::Or : result = fold(Cgen_llvm::Binary_op::bit_or); break;
            case Ntype_op::Xor: result = fold(Cgen_llvm::Binary_op::bit_xor); break;
            case Ntype_op::Rxor    :
            case Ntype_op::Popcount: {
              if (operands.size() != 2) {
                return reject("counted reduction does not have two operands");
              }
              result = llvm_kernel.count_bits(operands[0],
                                              livehd::graph_util::reduction_count(node),
                                              result_width,
                                              type_op_of(node) == Ntype_op::Rxor);
              break;
            }
            case Ntype_op::Ror: {
              result = llvm_kernel.constant(result_width, 0, true);
              for (const auto operand : operands) {
                result = llvm_kernel.binary(Cgen_llvm::Binary_op::bit_or,
                                            result,
                                            llvm_kernel.reduce_or(operand, result_width, true),
                                            result_width,
                                            true);
              }
              break;
            }
            case Ntype_op::Not:
              if (operands.size() != 1) {
                return reject("not operation does not have one operand");
              }
              result = llvm_kernel.unary_not(operands[0], result_width, result_unsign);
              break;
            case Ntype_op::EQ:
            case Ntype_op::LT:
            case Ntype_op::GT: {
              if (operands.size() != 2) {
                return reject("comparison does not have two operands");
              }
              const auto operation = type_op_of(node) == Ntype_op::EQ   ? Cgen_llvm::Binary_op::eq
                                     : type_op_of(node) == Ntype_op::LT ? Cgen_llvm::Binary_op::lt
                                                                        : Cgen_llvm::Binary_op::gt;
              result               = llvm_kernel.binary(operation, operands[0], operands[1], result_width, true);
              break;
            }
            case Ntype_op::SHL:
            case Ntype_op::SRA: {
              if (operands.size() != 2) {
                return reject("shift does not have two operands");
              }
              // `x >> n` read only through low lanes (see low_lane_readers_width):
              // two word loads + a funnel shift instead of a variable shift of
              // the whole value -- the i7800 `lshr` was the hottest LLVM kernel.
              if (type_op_of(node) == Ntype_op::SRA && !observation_on && operands[0].unsign && !edges[1].driver.is_const()) {
                const int wmax = low_lane_readers_width(node.get_driver_pin(0));
                if (wmax > 0 && wmax <= 64) {
                  result = llvm_kernel.dynamic_extract(operands[0],
                                                       operands[1],
                                                       static_cast<uint32_t>(wmax),
                                                       result_width,
                                                       result_unsign);
                  break;
                }
              }
              const auto operation = type_op_of(node) == Ntype_op::SHL ? Cgen_llvm::Binary_op::shl
                                     : operands[0].unsign              ? Cgen_llvm::Binary_op::lshr
                                                                       : Cgen_llvm::Binary_op::ashr;
              result               = llvm_kernel.binary(operation, operands[0], operands[1], result_width, result_unsign);
              break;
            }
            case Ntype_op::Get_mask: {
              if (operands.empty()) {
                return reject("get-mask has no operand");
              }
              // The color planner can move a constant slice to the boundary.
              // Its ABI value is already packed at bit zero, so this node is
              // only the landing conversion in that case.
              if (llvm_preextracted_get_masks.contains(member)) {
                auto packed = llvm_kernel.resize(operands[0], operands[0].width, true);
                result      = llvm_kernel.resize(packed, result_width, result_unsign);
                break;
              }
              if (operands.size() == 1) {
                auto packed = llvm_kernel.resize(operands[0], operands[0].width, true);
                result      = llvm_kernel.resize(packed, result_width, result_unsign);
                break;
              }
              if (operands.size() != 2 || !edges[1].driver.is_const()) {
                return reject("get-mask is not a constant slice");
              }
              const auto& mask   = const_of(edges[1].driver);
              auto        packed = llvm_kernel.resize(operands[0], operands[0].width, true);
              if (livehd::graph_util::is_whole_value_mask(mask)) {  // to-unsigned
                result = llvm_kernel.resize(packed, result_width, result_unsign);
                break;
              }
              const auto window = livehd::graph_util::mask_window_of(mask);
              if (!window) {
                return reject("get-mask is not a constant window");
              }
              const auto [lo, hi] = *window;
              if (hi > static_cast<int64_t>(operands[0].width) && !operands[0].unsign) {
                // Positions above a SIGNED operand's width are its sign bits;
                // the Slop backend replicates them, the JIT path would not.
                return reject("get-mask range reaches past a signed operand");
              }
              const auto work_width = static_cast<uint32_t>(std::max<int64_t>(operands[0].width, hi));
              packed                = llvm_kernel.resize(packed, work_width, true);
              if (lo != 0) {
                const auto amount = llvm_kernel.constant(work_width, static_cast<uint64_t>(lo), true);
                packed            = llvm_kernel.binary(Cgen_llvm::Binary_op::lshr, packed, amount, work_width, true);
              }
              packed = llvm_kernel.resize(packed, static_cast<uint32_t>(hi - lo), true);
              result = llvm_kernel.resize(packed, result_width, result_unsign);
              break;
            }
            case Ntype_op::Set_mask: {
              if (operands.size() != 3 || !edges[1].driver.is_const()) {
                return reject("set-mask is not a constant slice");
              }
              // graph/cell.hpp: ONE window, or -1 == "replace everything", i.e.
              // the whole result. The -1 spelling used to fall back to the Slop
              // backend for the entire color.
              const auto& mask = const_of(edges[1].driver);
              auto        window
                  = livehd::graph_util::is_whole_value_mask(mask)
                        ? std::optional<std::pair<int, int>>{{0, static_cast<int>(result_width)}}
                        : livehd::graph_util::mask_window_of(mask);
              if (!window) {
                return reject("set-mask is not a constant window");
              }
              const auto [lo, hi] = *window;
              if (hi > static_cast<int64_t>(result_width)) {
                return reject("set-mask range is outside its result");
              }
              result = llvm_kernel.bitfield_insert(operands[0],
                                                   operands[2],
                                                   static_cast<uint32_t>(lo),
                                                   static_cast<uint32_t>(hi),
                                                   result_width,
                                                   result_unsign);
              break;
            }
            case Ntype_op::Concat: {
              const auto lanes = livehd::graph_util::concat_lanes(node);
              const auto total = livehd::graph_util::concat_total_width(lanes);
              if (lanes.empty() || total <= 0) {
                return reject("concat result has no lanes");
              }
              auto assembled = llvm_kernel.constant(static_cast<uint32_t>(total), 0, true);
              for (size_t lane_index = 0; lane_index < lanes.size(); ++lane_index) {
                const auto& lane = lanes[lane_index];
                if (lane.width <= 0) {
                  return reject("concat lane has a zero width");
                }
                Cgen_llvm::Value lane_value;
                for (size_t input = 0; input < edges.size(); ++input) {
                  if (edges[input].sink.get_port_id() == static_cast<hhds::Port_id>(2 * lane_index)) {
                    lane_value = operands[input];
                    break;
                  }
                }
                if (lane_value.width == 0) {
                  return reject("concat lane has no LLVM value");
                }
                lane_value        = llvm_kernel.resize(lane_value, static_cast<uint32_t>(lane.width), true);
                const auto amount = llvm_kernel.constant(static_cast<uint32_t>(total), static_cast<uint64_t>(lane.width), true);
                assembled  = llvm_kernel.binary(Cgen_llvm::Binary_op::shl, assembled, amount, static_cast<uint32_t>(total), true);
                lane_value = llvm_kernel.resize(lane_value, static_cast<uint32_t>(total), true);
                assembled
                    = llvm_kernel.binary(Cgen_llvm::Binary_op::bit_or, assembled, lane_value, static_cast<uint32_t>(total), true);
              }
              result = llvm_kernel.resize(assembled, result_width, result_unsign);
              break;
            }
            case Ntype_op::Sext: {
              if (operands.empty()) {
                result = llvm_kernel.constant(result_width, 0, result_unsign);
                break;
              }
              uint32_t sign_bit = result_width - 1;
              if (operands.size() > 1) {
                if (!edges[1].driver.is_const()) {
                  return reject("sign-extension width is not constant");
                }
                const auto& width = const_of(edges[1].driver);
                if (!width.is_just_i64() || width.to_just_i64() <= 0 || static_cast<uint64_t>(width.to_just_i64()) > result_width) {
                  return reject("sign-extension width is outside its result");
                }
                sign_bit = static_cast<uint32_t>(width.to_just_i64() - 1);
              }
              result = llvm_kernel.sign_extend_from(operands[0], sign_bit, result_width, result_unsign);
              break;
            }
            case Ntype_op::Hotmux: {
              result = llvm_kernel.hotmux(operands, result_width, result_unsign);
              break;
            }
            case Ntype_op::Mux: {
              if (operands.size() < 2) {
                return reject("mux has no data arms");
              }
              std::vector<Cgen_llvm::Value> arms(operands.begin() + 1, operands.end());
              if (arms.size() == 2) {
                result = llvm_kernel.mux(operands[0], arms[0], arms[1], result_width, result_unsign);
              } else {
                result = llvm_kernel.indexed_mux(operands[0], arms, result_width, result_unsign);
              }
              break;
            }
            case Ntype_op::LUT: {
              const auto table_attr = node.attr(livehd::attrs::lut);
              if (!table_attr.has() || operands.empty()) {
                return reject("lut has no serialized table or address inputs");
              }
              const auto table_value = Dlop::unserialize(table_attr.get());
              if (!table_value || !table_value->is_integer() || table_value->has_unknowns()) {
                return reject("lut table is not a known integer");
              }
              const auto            table_width = static_cast<uint32_t>(std::max(table_value->get_signed_bits(), 1));
              std::vector<uint64_t> table_words((static_cast<size_t>(table_width) + 63) / 64, 0);
              for (uint32_t bit = 0; bit < table_width; ++bit) {
                if (table_value->bit_test(static_cast<int>(bit))) {
                  table_words[bit / 64] |= uint64_t{1} << (bit % 64);
                }
              }
              const auto table = llvm_kernel.constant_words(table_width, table_words, true);

              uint64_t address_width_64 = 0;
              for (const auto operand : operands) {
                address_width_64 += operand.width;
              }
              if (address_width_64 == 0 || address_width_64 > std::numeric_limits<uint32_t>::max()) {
                return reject("lut address width is invalid");
              }
              const auto address_width = static_cast<uint32_t>(address_width_64);
              auto       address       = llvm_kernel.constant(address_width, 0, true);
              uint32_t   offset        = 0;
              for (const auto operand : operands) {
                auto lane = llvm_kernel.resize(operand, address_width, true);
                if (offset != 0) {
                  const auto amount = llvm_kernel.constant(address_width, offset, true);
                  lane              = llvm_kernel.binary(Cgen_llvm::Binary_op::shl, lane, amount, address_width, true);
                }
                address  = llvm_kernel.binary(Cgen_llvm::Binary_op::bit_or, address, lane, address_width, true);
                offset  += operand.width;
              }
              result = llvm_kernel.lut(table, address, result_width, result_unsign);
              break;
            }
            case Ntype_op::AttrSet:
            case Ntype_op::IO:
              result = operands.empty() ? llvm_kernel.constant(result_width, 0, result_unsign)
                                        : llvm_kernel.resize(operands[0], result_width, result_unsign);
              break;
            default: return reject("member operation is not implemented by cgen_llvm");
          }
          if (result.width == 0) {
            return reject("member lowering produced no LLVM value");
          }
          llvm_outputs[member] = result;

          for (size_t write_index = 0; write_index < abi.writes.size(); ++write_index) {
            const auto& write = abi.writes[write_index];
            if (write.version != member) {
              continue;
            }
            const auto& slot      = color_plan_->boundary_slots()[write.slot_index];
            auto        source_it = llvm_outputs.find(member);
            if (source_it == llvm_outputs.end()) {
              return reject("boundary producer has no LLVM value");
            }
            auto source_value = source_it->second;
            if (slot.producer_extract_hi > slot.producer_extract_lo) {
              const auto work_width = std::max(source_value.width, slot.producer_extract_hi);
              source_value          = llvm_kernel.resize(source_value, work_width, source_value.unsign);
              if (slot.producer_extract_lo != 0) {
                auto amount  = llvm_kernel.constant(work_width, slot.producer_extract_lo, true);
                source_value = llvm_kernel.binary(Cgen_llvm::Binary_op::lshr, source_value, amount, work_width, true);
              }
              source_value = llvm_kernel.resize(source_value, slot.producer_extract_hi - slot.producer_extract_lo, true);
            }
            source_value = llvm_kernel.resize(source_value, slot.width, slot.unsign);
            if (slot.producer_shift != 0) {
              auto amount  = llvm_kernel.constant(slot.width, slot.producer_shift, true);
              source_value = llvm_kernel.binary(Cgen_llvm::Binary_op::shl, source_value, amount, slot.width, slot.unsign);
            }
            std::string error;
            if (!llvm_kernel.add_output(write_index, source_value, error)) {
              return reject("LLVM output construction failed");
            }
          }
        }
        std::string error;
        const auto  object_path = odir.empty() ? llvm_object : absl::StrCat(odir, "/", llvm_object);
        if (!llvm_kernel.write_object(object_path, error)) {
          return reject(error);
        }
        // Objects are link inputs, so they belong in the module's artifact
        // manifest exactly like the generated .cpp files: a warm run that finds
        // one missing must miss cold rather than skip and then fail to link.
        note_emitted(llvm_object);
        return true;
      };
      const bool llvm_emitted = emit_llvm_object();
      if (!llvm_emitted && !odir.empty()) {
        std::error_code ec;
        std::filesystem::remove(absl::StrCat(odir, "/", llvm_object), ec);
      }
      if (llvm_emitted) {
        if (llvm_scalar_abi) {
          kernel_header->append("extern \"C\" std::uint64_t ", llvm_raw_name, "(");
          for (size_t input = 0; input < abi.reads.size(); ++input) {
            kernel_header->append(input == 0 ? "std::uint64_t" : ", std::uint64_t");
          }
          kernel_header->append(abi.reads.empty() ? "void*);\n" : ", void*);\n");
        } else {
          kernel_header->append("extern \"C\" void ",
                                llvm_raw_name,
                                "(const std::uint64_t*, std::uint64_t*, std::uint64_t*, void*);\n");
        }
        // The evaluator already has the typed expressions for this exact
        // occurrence, so always inline the packing adapter.  A standalone
        // wrapper repeats the same work behind a void** ABI and adds one host
        // C++ compile action per LLVM object.
        llvm_inline_kernel[color_index]   = true;
        llvm_scalar_kernel[color_index]   = llvm_scalar_abi;
        llvm_inline_raw_name[color_index] = llvm_raw_name;
        direct_kernel[color_index]        = nullptr;
        continue;
      }
      if (llvm_backend_) {
        livehd::diag::err("inou.cgen.sim", "llvm-kernel", "unsupported")
            .msg("cannot emit LLVM color kernel '{}' in '{}': {}", color_index, gname, llvm_rejection)
            .hint("sim.tune.backend=llvm does not substitute a Slop circuit kernel; select sim.tune.backend=slop explicitly if needed")
            .fatal();
      }
      // Reaching here means the shared C++ body is emittable: the class-selection
      // loop above already required every color to be reused, pure-data,
      // single-occurrence, standalone-lowerable and address-bound.
      kernel_out->append(
          "// Generated canonical simulator kernel. Do not edit.\n"
          "#include <cassert>\n#include <cstddef>\n#include <cstdint>\n");
      kernel_out->append("#include \"slop.hpp\"\n\n");
      kernel_out->append(kUnknownLiteralHelper);  // standalone TU: it does not see the module header
      kernel_out->append("void ", instance_name, "([[maybe_unused]] void* __owner, void** __bind, std::uint64_t* __changed) {\n");
      // The pointed-to TYPE has to match what the binding site took the address
      // of, byte for byte -- this ABI is a void* type-pun, so a mismatch is
      // undefined behaviour rather than a compile error.
      for (size_t i = 0; i < abi.reads.size(); ++i) {
        const auto& slot  = color_plan_->boundary_slots()[abi.reads[i].slot_index];
        const auto  stype = slot_type(abi.reads[i].slot_index, slot.width);
        if (abi.reads[i].consumer.width == slot.width) {
          // Same width: bind the slot in its own type, Slop_u included. The
          // registration below records a same-width unsigned read in
          // slop_u_values_, so the body treats it as a Slop_u (W bits in a
          // W+1 carrier). A `->zext_to<W>()` here handed it a plain Slop<W>
          // instead, whose top bit is a SIGN: the next widening turned a u3 5
          // into -3 (br_credit_sender_vc's credit counters computed 21+5=18).
          kernel_out->append(
              absl::StrCat("  [[maybe_unused]] const auto& __k_in_", i, " = *static_cast<const ", stype, "*>(__bind[", i, "]);\n"));
        } else {
          // A width change goes through zext_to, exactly as the inline read
          // does; a narrowing unsigned read is outside canonical_, so the
          // consumer re-applies its own conversion.
          kernel_out->append(absl::StrCat("  [[maybe_unused]] const auto __k_in_",
                                          i,
                                          " = static_cast<const ",
                                          stype,
                                          "*>(__bind[",
                                          i,
                                          "])->zext_to<",
                                          abi.reads[i].consumer.width,
                                          ">();\n"));
        }
      }
      for (size_t i = 0; i < abi.writes.size(); ++i) {
        const auto& slot = color_plan_->boundary_slots()[abi.writes[i].slot_index];
        kernel_out->append(absl::StrCat("  [[maybe_unused]] auto& __k_out_",
                                        i,
                                        " = *static_cast<",
                                        slot_type(abi.writes[i].slot_index, slot.width),
                                        "*>(__bind[",
                                        abi.reads.size() + i,
                                        "]);\n"));
      }
      pin2var.clear();
      canonical_.clear();
      slop_u_values_.clear();
      slop_u_binding_width_.clear();
      preextracted_get_masks_.clear();
      seq_volatile_.clear();
      // Bind ABI read `i` into the expression tables. The key is the consumer's
      // DEFINITION driver pin, and one kernel can hold several members that read
      // the SAME definition pin through DIFFERENT boundary slots: the lanes of
      // one packed GraphIO input, each feeding its own preextracted Get_mask
      // (minion's vpu_rf `wr_data_i#[0..=31]` / `wr_data_i#[32..=63]`), or a
      // preextracted bit next to the whole value. Binding every read once, up
      // front, let the LAST read win the shared key, so the other member
      // silently computed from its sibling's lane (the vpu_rf write port B
      // stored port A's `?` half) -- only in vectors whose coarsening shares
      // that pure-data pair as one kernel (minion: dirty=on fence=16), so the
      // tune vector changed simulated values.
      // The inline evaluator rebinds per member; the member loop below
      // rebinds each member's own reads right before emitting it.
      absl::flat_hash_map<size_t, std::vector<size_t>> reads_of_member;
      const auto                                       bind_kernel_read = [&](size_t i) {
        const auto& read             = abi.reads[i];
        const auto& slot             = color_plan_->boundary_slots()[read.slot_index];
        const auto& consumer_version = color_plan_->version_sites()[read.consumer.version_site];
        const auto  consumer_node    = color_plan_->sites()[consumer_version.base_site].node.base_node();
        uint32_t    consumer_input   = 0;
        for (auto edge_sink : consumer_node.inp_sorted_pins()) {
          for (auto edge_drv : edge_sink.get_driver_pins()) {
            if (consumer_input++ != read.consumer.input) {
              continue;
            }
            I(edge_sink.get_port_id() == read.consumer.port);
            const auto key = edge_drv.get_class_index();
            pin2var[key]   = absl::StrCat("__k_in_", i);
            if (read.consumer.preextracted) {
              preextracted_get_masks_.insert(consumer_node.get_class_index());
            }
            if (slot.kind == livehd::sim::Color_plan::Boundary_kind::color_value
                && (!slot.unsign || slot.width <= read.consumer.width)) {
              canonical_.insert(key);
            } else {
              canonical_.erase(key);
            }
            if (slot.kind == livehd::sim::Color_plan::Boundary_kind::color_value && direct_slot_is_u[read.slot_index]
                && slot.width == read.consumer.width) {
              slop_u_values_.insert(key);
            } else {
              slop_u_values_.erase(key);
            }
            return;
          }
        }
      };
      for (size_t i = 0; i < abi.reads.size(); ++i) {
        reads_of_member[abi.reads[i].consumer.version_site].push_back(i);
        bind_kernel_read(i);
      }
      auto members = color_plan_->colors()[representative].members;
      std::ranges::sort(members, [&](size_t a, size_t b) {
        const auto& av = color_plan_->version_sites()[a];
        const auto& bv = color_plan_->version_sites()[b];
        return std::tie(av.execution_order, av.structural_id) < std::tie(bv.execution_order, bv.structural_id);
      });
      const auto                                    kernel_mark = kernel_out->mark();
      size_t                                        temporary   = 0;
      absl::flat_hash_map<std::string, std::string> extraction_cse;
      absl::flat_hash_map<size_t, std::string>      kernel_version_value;
      absl::flat_hash_set<size_t>                   kernel_version_slop_u;
      const size_t                                  unresolved_before = unresolved_operands_;
      for (const size_t member : members) {
        const auto&     version = color_plan_->version_sites()[member];
        const auto      node    = color_plan_->sites()[version.base_site].node.base_node();
        hhds::Pin_class output;
        for (const auto& candidate : node.out_pins()) {
          output = candidate;
          break;
        }
        if (output.is_invalid()) {
          output = node.get_driver_pin(0);
        }
        I(!output.is_invalid());
        if (const auto own_reads = reads_of_member.find(member); own_reads != reads_of_member.end()) {
          preextracted_get_masks_.erase(node.get_class_index());
          for (const size_t i : own_reads->second) {
            bind_kernel_read(i);  // this member's lanes, not a sibling's (see bind_kernel_read)
          }
        }
        bind_internal_value_uses(member, representative, node, kernel_version_value, kernel_version_slop_u);
        sub_width_expr_             = false;
        const int  width            = wbits_of(output);
        const bool unsigned_result  = proven_unsigned_result(node, output);
        const bool canonical_result = proven_canonical_unsigned_result(node, output);
        const bool use_u            = slop_u_ && unsigned_result;
        const int  eval_width       = unsigned_result ? width + 1 : width;
        const auto expr             = node_expr(node, eval_width);
        const bool expr_is_u        = slop_u_expr_;
        const bool cse_extract
            = type_op_of(node) == Ntype_op::Get_mask && !preextracted_get_masks_.contains(node.get_class_index());
        std::string landing;
        if (use_u) {
          landing = sub_width_expr_    ? absl::StrCat("(", expr, ").zext_to_u<", width, ">()")
                    : debug_           ? absl::StrCat("Slop_u<", width, ">::land(", expr, ")")
                    : expr_is_u        ? expr
                    : canonical_result ? absl::StrCat("Slop_u<", width, ">::from_proven(", expr, ")")
                                       : absl::StrCat("Slop_u<", width, ">{", expr, "}");
        } else {
          landing = sub_width_expr_ ? absl::StrCat("Slop<", eval_width, ">{", expr, "}") : expr;
        }
        const auto  cse_key = absl::StrCat(use_u ? "u" : "s", ":", eval_width, ":", landing);
        auto        cse_it  = cse_extract ? extraction_cse.find(cse_key) : extraction_cse.end();
        std::string temp;
        if (cse_it != extraction_cse.end()) {
          temp = cse_it->second;
        } else {
          temp = absl::StrCat("__k_tmp_", temporary++);
          kernel_out->append(
              absl::StrCat("  ", use_u ? "Slop_u<" : "Slop<", use_u ? width : eval_width, "> ", temp, " = ", landing, ";\n"));
          if (cse_extract) {
            extraction_cse.emplace(cse_key, temp);
          }
        }
        if (use_u) {
          slop_u_values_.insert(output.get_class_index());
          kernel_version_slop_u.insert(member);
        }
        kernel_version_value.insert_or_assign(member, temp);
        pin2var[output.get_class_index()] = temp;
        canonical_.insert(output.get_class_index());
        for (size_t write_index = 0; write_index < abi.writes.size(); ++write_index) {
          const auto& write = abi.writes[write_index];
          if (write.version != member) {
            continue;
          }
          const auto& slot   = color_plan_->boundary_slots()[write.slot_index];
          const auto  source = find_site_output(version.base_site, slot.producer_port);
          I(!source.is_invalid());
          // The raw path is only value-correct for an UNSIGNED source. The
          // destination is a `Slop_u<W>&`, whose converting ctor ZERO-extends;
          // handing it a narrower SIGNED temp therefore drops the sign bits,
          // while the serial emitter for the same slot spells `Slop<W>{...}`
          // and sign-extends. A signed source must take operand().
          // The shift also forces the concrete carrier: `Slop<W>::shl_op` runs
          // input_width_check, and a bare `Slop_u<W>` counts as W+1 Slop-bits,
          // so shifting one would static_assert in the generated kernel.
          const bool  has_extract = slot.producer_extract_hi > slot.producer_extract_lo;
          const bool  raw_ok      = !has_extract && slot_is_u(write.slot_index) && slot.producer_shift == 0 && is_unsign(source);
          std::string source_expr;
          if (has_extract) {
            source_expr = range_extract_expr(stored_operand(source, std::max(wbits_of(source), 1)),
                                             slot.width,
                                             slot_is_u(write.slot_index),
                                             slot.producer_extract_lo,
                                             slot.producer_extract_hi);
          } else {
            source_expr = raw_ok ? raw_operand(source, slot.width + 1) : operand(source, slot.width);
          }
          if (!has_extract && slot.producer_shift != 0) {
            source_expr = absl::StrCat("Slop<", slot.width, ">::shl_op(", source_expr, ", ", slot.producer_shift, ")");
          }
          if (slot.kind == livehd::sim::Color_plan::Boundary_kind::color_value && tune_dirty()) {
            kernel_out->append(absl::StrCat("  if (slop_update(__k_out_",
                                            write_index,
                                            ", ",
                                            source_expr,
                                            ")) __changed[",
                                            write_index / 64,
                                            "] |= std::uint64_t{1} << ",
                                            write_index % 64,
                                            ";\n"));
          } else {
            kernel_out->append(absl::StrCat("  __k_out_", write_index, " = ", source_expr, ";\n"));
          }
        }
      }
      {
        auto body = kernel_out->detach_from(kernel_mark);
        compact_body_temps(body);
        kernel_out->append(body);
      }
      kernel_out->append("}\n");
      // A canonical kernel sees only its ABI reads, its own members and the
      // internal uses bound above. Any operand still unbound was replaced by a
      // silent 0: on the color root the cycle flag is noise (register reads
      // set it), so fail closed on the count this body added instead.
      if (unresolved_operands_ != unresolved_before) {
        livehd::diag::err("inou.cgen.sim", "kernel-operand-unbound", "internal")
            .msg("module `{}`: shared color kernel `{}` reads {} operand(s) with no binding ({}); the kernel would read 0",
                 gname,
                 instance_name,
                 unresolved_operands_ - unresolved_before,
                 cycle_first_label_.empty() ? std::string{"(unnamed)"} : cycle_first_label_)
            .hint("every value a kernel member reads must be an ABI boundary read, a member of the same color, or an "
                  "internal value use of one")
            .emit();
        cycle_reported_ = true;
      }
    }

    std::vector<std::shared_ptr<File_output>> color_eval_outputs;
    if (!direct_color_eval_shards.empty()) {
      absl::flat_hash_set<std::string> expected_color_eval_files;
      for (size_t shard = 0; shard < direct_color_eval_shards.size(); ++shard) {
        expected_color_eval_files.insert(absl::StrCat(fstem, ".color-eval-", shard, ".cpp"));
      }
      if (!odir.empty()) {
        std::error_code   ec;
        const std::string prefix = fstem + ".color-eval-";
        for (const auto& entry : std::filesystem::directory_iterator(std::string(odir), ec)) {
          const auto filename = entry.path().filename().string();
          if (!entry.is_regular_file() || !filename.starts_with(prefix) || !filename.ends_with(".cpp")
              || expected_color_eval_files.contains(filename)) {
            continue;
          }
          std::filesystem::remove(entry.path(), ec);
        }
      }
      color_eval_outputs.reserve(direct_color_eval_shards.size());
      for (size_t shard = 0; shard < direct_color_eval_shards.size(); ++shard) {
        const std::string filename = absl::StrCat(fstem, ".color-eval-", shard, ".cpp");
        auto              out      = open_out(filename);
        out->append("// Generated simulator color evaluator shard. Do not edit.\n");
        out->append("#include \"", fstem, ".color-runtime.hpp\"\n");
        out->append("#include \"", kernel_header_name, "\"\n");
        out->append("#include <cassert>\n#include <cstddef>\n");
        out->append("\n");
        out->append("void ", mod, "::__color_eval_part_", std::to_string(shard), "(std::size_t __color_index) {\n");
        out->append("  assert(__color_runtime);\n  [[maybe_unused]] auto& __rt = *__color_runtime;\n  switch (__color_index) {\n");
        color_eval_outputs.push_back(std::move(out));
      }
    } else if (!odir.empty()) {
      std::error_code   ec;
      const std::string prefix = fstem + ".color-eval-";
      for (const auto& entry : std::filesystem::directory_iterator(std::string(odir), ec)) {
        const auto filename = entry.path().filename().string();
        if (entry.is_regular_file() && filename.starts_with(prefix) && filename.ends_with(".cpp")) {
          std::filesystem::remove(entry.path(), ec);
        }
      }
    }

    // Emit one body-private indexed dispatcher over all planned colors. This
    // keeps the generated module header independent of the plan's color count.
    // This direct-kernel shape
    // admits ordinary rising-edge state throughout an acyclic occurrence tree;
    // latch, memory, derived/gated-clock, compact-loop, conditional, and random
    // designs do not select color_runtime_root above and therefore cannot
    // silently enter an incomplete lowering. Every cross-color value lands in
    // its producer-owned slot; state reads bind the occurrence's existing Q
    // member and state updates write its existing `_din` member before commit.
    // ONE definition of "raise this state member's commit flag", shared by every
    // site that dispatches a color kernel: the indexed __color_eval below, the
    // phase-order direct calls, and the inlined-ABI color bodies. They used to
    // spell the shard/flag choice three times, and the memory rule below existed
    // in only one of them.
    const auto emit_commit_flag_at = [&](std::string_view indent, size_t member) {
      I(member < direct_state_commit_flag_of_member.size()
        && direct_state_commit_flag_of_member[member] != livehd::sim::Color_plan::invalid_index);
      if (member < direct_commit_shard_of_member.size()
          && direct_commit_shard_of_member[member] != livehd::sim::Color_plan::invalid_index) {
        fout->append(indent,
                     "__rt.__commit_shard_mask[",
                     std::to_string(direct_commit_shard_of_member[member]),
                     "] |= std::uint64_t{1} << ",
                     std::to_string(direct_commit_shard_bit_of_member[member]),
                     ";\n");
      } else {
        fout->append(indent, "__rt.__state_commit[", std::to_string(direct_state_commit_flag_of_member[member]), "] = true;\n");
      }
    };
    // A memory state update stages through generated owner callbacks, so no
    // boundary write carries its changed bit: its commit is UNCONDITIONAL.
    const auto kernel_memory_commit_members = [&](size_t color_index) {
      std::vector<size_t> members;
      for (const size_t member : color_plan_->colors()[color_index].members) {
        const auto& version = color_plan_->version_sites()[member];
        if (version.role == livehd::sim::Color_plan::Version_role::state_update
            && type_op_of(color_plan_->sites()[version.base_site].node.base_node()) == Ntype_op::Memory) {
          members.push_back(member);
        }
      }
      return members;
    };
    const auto emit_direct_kernel_call = [&](size_t color, const std::string& call_indent) {
      const bool kernel_has_state_update = std::ranges::any_of(color_plan_->colors()[color].members, [&](const size_t member) {
        return color_plan_->version_sites()[member].role == livehd::sim::Color_plan::Version_role::state_update;
      });
      if (tune_dirty() || kernel_has_state_update) {
        const size_t changed_words = std::max<size_t>(1, (direct_abi[color].writes.size() + 63) / 64);
        fout->append(absl::StrCat(call_indent,
                                  "uint64_t __changed_",
                                  color,
                                  "[",
                                  changed_words,
                                  "]{};\n",
                                  call_indent,
                                  "__rt.__color_kernel[",
                                  color,
                                  "](__rt.owner, __rt.__color_bindings[",
                                  color,
                                  "].data(), __changed_",
                                  color,
                                  ");\n"));
        for (size_t write_index = 0; write_index < direct_abi[color].writes.size(); ++write_index) {
          const auto& write      = direct_abi[color].writes[write_index];
          const auto& write_slot = color_plan_->boundary_slots()[write.slot_index];
          if (write_slot.kind != livehd::sim::Color_plan::Boundary_kind::color_value
              && write_slot.kind != livehd::sim::Color_plan::Boundary_kind::state_pending) {
            continue;
          }
          fout->append(absl::StrCat(call_indent,
                                    "if ((__changed_",
                                    color,
                                    "[",
                                    write_index / 64,
                                    "] & (uint64_t{1} << ",
                                    write_index % 64,
                                    ")) != 0) {\n"));
          if (write_slot.kind == livehd::sim::Color_plan::Boundary_kind::state_pending) {
            emit_commit_flag_at(call_indent + "  ", write.version);
          } else if (tune_dirty()) {
            emit_serial_dirty_consumers(write.slot_index, call_indent + "  ");
          }
          fout->append(call_indent, "}\n");
        }
        for (const size_t member : kernel_memory_commit_members(color)) {
          emit_commit_flag_at(call_indent, member);
        }
      } else {
        fout->append(absl::StrCat(call_indent,
                                  "uint64_t __changed_",
                                  color,
                                  "[",
                                  std::max<size_t>(1, (direct_abi[color].writes.size() + 63) / 64),
                                  "]{};\n",
                                  call_indent,
                                  "__rt.__color_kernel[",
                                  color,
                                  "](__rt.owner, __rt.__color_bindings[",
                                  color,
                                  "].data(), __changed_",
                                  color,
                                  ");\n"));
      }
    };
    fout->append("void ", mod, "::__color_eval(std::size_t __color_index) {\n");
    fout->append("  assert(__color_runtime);\n  [[maybe_unused]] auto& __rt = *__color_runtime;\n");
    if (direct_color_eval_shards.empty()) {
      fout->append("  switch (__color_index) {\n");
    } else {
      fout->append("  switch (__color_index) {\n");
      for (size_t shard = 0; shard < direct_color_eval_shards.size(); ++shard) {
        const auto [begin, end] = direct_color_eval_shards[shard];
        for (size_t color = begin; color < end; ++color) {
          fout->append("  case ",
                       std::to_string(color),
                       ": __color_eval_part_",
                       std::to_string(shard),
                       "(__color_index); return;\n");
        }
      }
      fout->append("  default: assert(false && \"invalid color index\"); return;\n  }\n}\n");
    }
    const auto root_fout              = fout;
    size_t     color_eval_shard       = 0;
    const auto emit_state_commit_flag = [&](size_t member, std::string_view predicate) {
      I(member < direct_state_commit_flag_of_member.size()
        && direct_state_commit_flag_of_member[member] != livehd::sim::Color_plan::invalid_index);
      const size_t commit_flag = direct_state_commit_flag_of_member[member];
      const bool   conditional = predicate != "true";
      if (conditional) {
        fout->append("  if (", predicate, ") {\n");
      }
      const std::string indent = conditional ? "    " : "  ";
      if (member < direct_commit_shard_of_member.size()
          && direct_commit_shard_of_member[member] != livehd::sim::Color_plan::invalid_index) {
        fout->append(indent,
                     "__rt.__commit_shard_mask[",
                     std::to_string(direct_commit_shard_of_member[member]),
                     "] |= uint64_t{1} << ",
                     std::to_string(direct_commit_shard_bit_of_member[member]),
                     ";\n");
      } else {
        fout->append(indent, "__rt.__state_commit[", std::to_string(commit_flag), "] = true;\n");
      }
      if (conditional) {
        fout->append("  }\n");
      }
    };
    std::vector<std::vector<size_t>> direct_incoming_versions(color_plan_->version_sites().size());
    for (const auto& edge : color_plan_->version_dependencies()) {
      I(edge.producer < direct_incoming_versions.size() && edge.consumer < direct_incoming_versions.size());
      direct_incoming_versions[edge.consumer].push_back(edge.producer);
    }
    // A same-slot latch update whose staged `_din` another state update READS
    // (an ICG enable folded into a flop's commit guard, or a flop whose D is a
    // latch closing on the same edge -- the planner's latch-update ->
    // state-update edges) must keep `_din` current EVERY period: the reader
    // binds `<latch>_din` by name, not through a boundary slot. Inside one
    // color the activation-domain analysis below guarantees that (the reader's
    // guard names the `_din`, or its domain widens its producer). A reader in
    // ANOTHER color is invisible to that per-color analysis, so the latch kept
    // its own guarded `if (enable) { _din = ...; }` form and the reader saw a
    // stale `_din` whenever the enable was low -- after reset_cycle that is the
    // default-constructed 0, not the reset/drawn Q. Whether the two share a
    // color is decided by the coarsener's live-word budget, so the values
    // depended on sim.tune.live_words
    // (tests/sim/conditional_internal_icg_call.prp at live_words=1). Treat such
    // a reader like any other cross-color consumer: the latch's `_din` is an
    // external write, so its update runs in the unconditional domain (its hold
    // mux keeps a de-asserted enable holding) exactly as the same-color case
    // already does.
    std::vector<bool> direct_pending_read_across_colors(color_plan_->version_sites().size(), false);
    for (const auto& edge : color_plan_->version_dependencies()) {
      const auto& producer = color_plan_->version_sites()[edge.producer];
      const auto& consumer = color_plan_->version_sites()[edge.consumer];
      if (producer.role != livehd::sim::Color_plan::Version_role::state_update
          || consumer.role != livehd::sim::Color_plan::Version_role::state_update
          || direct_version_color[edge.producer] == direct_version_color[edge.consumer]
          || type_op_of(color_plan_->sites()[producer.base_site].node.base_node()) != Ntype_op::Latch) {
        continue;
      }
      direct_pending_read_across_colors[edge.producer] = true;
    }
    // ...and under dirty gating (sim.tune.dirty=on) such a reader needs a DIRTY
    // edge as well. A by-name `_din` read goes through no boundary slot, so no
    // `emit_serial_dirty_consumers` ever marks the reader's color: when only
    // the latch's inputs changed (an ICG enable toggling under constant data)
    // the latch's color ran, rewrote `_din`, and the gated flop's color stayed
    // idle -- the flop never committed, while dirty=off (every color runs) and
    // a budget that fused the two colors both advanced it. Values depended on
    // sim.tune.live_words x sim.tune.dirty (review round 2, codegen-fix:R2-1).
    //
    // The marks are emitted only for the colors that actually NAME
    // `<latch>_din` (the D-driver binding, a normalized commit guard, a folded
    // ICG guard), not for every latch-update -> state-update version edge: the
    // ancestor-latch trie edges (sim_color_plan) tie a root latch to every
    // same-slot flop of the design, and marking all of those would re-run most
    // of the design whenever an enable toggles. Which colors name the `_din` is
    // known only once they are emitted, so every latch `_din` write site carries
    // a placeholder (`latch_din_placeholder`), each emitted color body is
    // scanned for `_din` names (`record_latch_din_readers`), and the
    // placeholders are resolved after the last color (`resolve_latch_din_marks`).
    // A mark fires only when the write CHANGED `_din` (a snapshot compare):
    // a reader that ran after the previous change already saw this value, and
    // reset/load re-dirty every color.
    absl::flat_hash_map<std::string, size_t> latch_din_member;  // "<latch>_din" -> the latch's state_update member
    for (size_t member = 0; member < color_plan_->version_sites().size(); ++member) {
      const auto& version = color_plan_->version_sites()[member];
      if (version.role != livehd::sim::Color_plan::Version_role::state_update
          || type_op_of(color_plan_->sites()[version.base_site].node.base_node()) != Ntype_op::Latch) {
        continue;
      }
      const auto name = occurrence_member(color_plan_->sites()[version.base_site]);
      if (!name.empty()) {
        latch_din_member.emplace(name + "_din", member);
      }
    }
    // Every latch `_din` a text names, as that latch's state_update member.
    // Occurrence members are dotted paths (`u_a.u_b.held`), so a name is the
    // longest `[A-Za-z0-9_.]` run ending in `_din`, and every dotted suffix of
    // it is tried too: over-reporting a reader only costs a spurious mark,
    // missing one is the miscompile this guards against.
    const auto latch_dins_named = [&](std::string_view text, const auto& on_member) {
      if (latch_din_member.empty()) {
        return;
      }
      static constexpr std::string_view kDin = "_din";
      const auto identifier_char             = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
      for (size_t at = text.find(kDin); at != std::string_view::npos; at = text.find(kDin, at + kDin.size())) {
        const size_t end = at + kDin.size();
        if (end < text.size() && identifier_char(text[end])) {
          continue;
        }
        size_t begin = at;
        while (begin > 0 && (identifier_char(text[begin - 1]) || text[begin - 1] == '.')) {
          --begin;
        }
        for (size_t start = begin; start < at;) {
          if (const auto it = latch_din_member.find(text.substr(start, end - start)); it != latch_din_member.end()) {
            on_member(it->second);
          }
          const auto dot = text.find('.', start);
          if (dot == std::string_view::npos || dot >= at) {
            break;
          }
          start = dot + 1;
        }
      }
    };
    std::vector<std::vector<size_t>> latch_din_readers;  // latch member -> OTHER colors naming its `_din` (tune_dirty only)
    if (tune_dirty()) {
      latch_din_readers.resize(color_plan_->version_sites().size());
    }
    bool       latch_din_placeholders   = false;
    const auto record_latch_din_readers = [&](std::string_view text, size_t color_index) {
      if (!tune_dirty()) {
        return;
      }
      latch_dins_named(text, [&](size_t member) {
        if (direct_version_color[member] == color_index) {
          return;  // the latch's own color: its domain analysis orders the read
        }
        auto& readers = latch_din_readers[member];
        if (std::ranges::find(readers, color_index) == readers.end()) {
          readers.push_back(color_index);
        }
      });
    };
    // The same hole for a state's CURRENT value. A commit guard resolved
    // through an occurrence edge (a gated clock handed to a child as a
    // SECONDARY clock port: `icg_guards` + occurrence_bool_value) names the
    // enable latch's -- or any flop's -- member directly, with no state-current
    // boundary slot behind it, so the state's commit marked the slot consumers
    // and never the gated flop's color. When the ICG latch opened under
    // constant data, the capture color stayed idle and the flop never loaded
    // (minion's txfma f3..f5 pipeline flops on `ctrl_f<N>_clk`: they kept their
    // power-on draws under dirty=on and loaded under dirty=off). Record every
    // color whose activation or reset predicate names a flop/latch Q that none
    // of that state's current slots already delivers, and have the state's
    // commit mark it (emit_commit_member). Commits are emitted after every
    // color, so the reader sets are complete by then.
    absl::flat_hash_map<std::string, size_t> state_q_site;           // flop/latch occurrence member -> site
    std::vector<std::vector<size_t>>         state_q_guard_readers;  // site -> colors naming its Q in a guard
    if (tune_dirty()) {
      state_q_guard_readers.resize(color_plan_->sites().size());
      for (size_t site_index = 0; site_index < color_plan_->sites().size(); ++site_index) {
        const auto& site = color_plan_->sites()[site_index];
        if (site.kind != livehd::sim::Color_plan::Site_kind::state || type_op_of(site.node.base_node()) == Ntype_op::Memory
            || state_update_version(site_index) == livehd::sim::Color_plan::invalid_index) {
          continue;
        }
        if (const auto name = occurrence_member(site); !name.empty()) {
          state_q_site.emplace(name, site_index);
        }
      }
    }
    const auto record_state_q_guard_readers = [&](std::string_view text, size_t color_index) {
      if (state_q_site.empty()) {
        return;
      }
      const auto identifier_char = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
      const auto note            = [&](size_t site_index) {
        for (const size_t slot_index : direct_state_current_slots[site_index]) {
          for (const auto& consumer : color_plan_->boundary_slots()[slot_index].consumers) {
            if (consumer.color == color_index) {
              return;  // already a planned reader: emit_serial_dirty_consumers marks it
            }
          }
        }
        auto& readers = state_q_guard_readers[site_index];
        if (std::ranges::find(readers, color_index) == readers.end()) {
          readers.push_back(color_index);
        }
      };
      // A member is a dotted path (`u_a.u_b.q`), so take each maximal
      // `[A-Za-z0-9_.]` run and try it and every dotted prefix of it (a method
      // call such as `q.is_known_true()` extends the run past the name).
      for (size_t at = 0; at < text.size();) {
        if (!identifier_char(text[at])) {
          ++at;
          continue;
        }
        size_t end = at;
        while (end < text.size() && (identifier_char(text[end]) || text[end] == '.')) {
          ++end;
        }
        for (size_t stop = end; stop > at;) {
          if (const auto it = state_q_site.find(text.substr(at, stop - at)); it != state_q_site.end()) {
            note(it->second);
          }
          const auto dot = text.rfind('.', stop - 1);
          if (dot == std::string_view::npos || dot < at) {
            break;
          }
          stop = dot;
        }
        at = end;
      }
    };
    // `kind` 'S'/'M' bracket the latch's normal `_din` writes (snapshot before,
    // compare-and-mark after), 's'/'m' its reset slow path. Resolved (or
    // deleted, when no other color names the `_din`) by resolve_latch_din_marks.
    const auto latch_din_placeholder = [&](char kind, size_t member) {
      I(tune_dirty());
      latch_din_placeholders = true;
      return absl::StrCat("  /*__lhd_din:", std::string_view(&kind, 1), ":", member, "*/\n");
    };
    const auto emit_latch_din_placeholder = [&](char kind, size_t member) {
      if (!tune_dirty() || member >= latch_din_readers.size()) {
        return;
      }
      const auto& version = color_plan_->version_sites()[member];
      if (version.role != livehd::sim::Color_plan::Version_role::state_update
          || type_op_of(color_plan_->sites()[version.base_site].node.base_node()) != Ntype_op::Latch) {
        return;
      }
      fout->append(latch_din_placeholder(kind, member));
    };
    std::vector<std::pair<std::shared_ptr<File_output>, size_t>> latch_din_patch_marks;
    latch_din_patch_marks.emplace_back(root_fout, root_fout->mark());
    for (const auto& out : color_eval_outputs) {
      latch_din_patch_marks.emplace_back(out, out->mark());
    }
    const auto resolve_latch_din_marks = [&] {
      if (!latch_din_placeholders) {
        return;
      }
      absl::flat_hash_map<size_t, std::string> din_name;
      for (const auto& [name, member] : latch_din_member) {
        din_name.emplace(member, name);
      }
      static constexpr std::string_view kOpen = "/*__lhd_din:";
      for (const auto& [out, mark] : latch_din_patch_marks) {
        auto text = out->detach_from(mark);
        if (text.find(kOpen) == std::string::npos) {
          out->append(text);
          continue;
        }
        std::string patched;
        patched.reserve(text.size());
        size_t copied = 0;
        for (size_t at = text.find(kOpen); at != std::string::npos; at = text.find(kOpen, copied)) {
          const size_t line_begin = text.rfind('\n', at) == std::string::npos ? 0 : text.rfind('\n', at) + 1;
          const size_t close      = text.find("*/", at);
          I(close != std::string::npos);
          size_t line_end     = text.find('\n', close);
          line_end            = line_end == std::string::npos ? text.size() : line_end + 1;
          const char   kind   = text[at + kOpen.size()];
          const size_t member = std::stoul(text.substr(at + kOpen.size() + 2, close - (at + kOpen.size() + 2)));
          I(member < latch_din_readers.size());
          patched.append(text, copied, line_begin - copied);
          copied             = line_end;
          const auto name_it = din_name.find(member);
          if (member >= latch_din_readers.size() || latch_din_readers[member].empty() || name_it == din_name.end()) {
            continue;  // no other color names this `_din`: nothing to mark
          }
          auto readers = latch_din_readers[member];
          std::ranges::sort(readers);
          const auto& name     = name_it->second;
          const auto  snapshot = absl::StrCat(kind == 'S' || kind == 'M' ? "__lhd_din_prev_" : "__lhd_din_rprev_", member);
          if (kind == 'S' || kind == 's') {
            absl::StrAppend(&patched, "  const auto ", snapshot, " = ", name, ";\n");
            continue;
          }
          I(kind == 'M' || kind == 'm');
          absl::StrAppend(&patched, "  if (!", name, ".identical(", snapshot, ")) {  // cross-color `_din` readers\n");
          for (const size_t reader : readers) {
            absl::StrAppend(&patched, "    __rt.__color_dirty[", dirty_word(reader), "] |= ", dirty_mask(reader), ";\n");
          }
          patched.append("  }\n");
        }
        patched.append(text, copied, std::string::npos);
        out->append(patched);
      }
    };
    // Without dirty gating, a state capture in an always-run domain of an
    // always-run color raises its commit flag every period. Such a member
    // commits unconditionally instead: no flag store, no flag test.
    std::vector<bool> direct_always_commit(color_plan_->version_sites().size(), false);
    for (size_t color_index = 0; color_index < color_plan_->colors().size(); ++color_index) {
      const auto& color = color_plan_->colors()[color_index];
      if (!color_eval_outputs.empty()) {
        while (color_eval_shard + 1 < direct_color_eval_shards.size()
               && color_index >= direct_color_eval_shards[color_eval_shard].second) {
          ++color_eval_shard;
        }
        fout = color_eval_outputs[color_eval_shard];
      }
      fout->append("  case ", std::to_string(color_index), ": {\n");
      if (direct_kernel[color_index] != nullptr) {
        emit_direct_kernel_call(color_index, "    ");
        fout->append("    return;\n  }\n");
        continue;
      }
      // One color body is one straight-line region: sweep its temporaries the
      // same way a period body is swept (dead ones dropped, single-use ones
      // folded into the next statement).
      const auto case_fout  = fout;
      const auto case_mark  = case_fout->mark();
      const auto close_case = [&] {
        auto body = case_fout->detach_from(case_mark);
        compact_body_temps(body);
        record_latch_din_readers(body, color_index);
        case_fout->append(body);
      };
      pin2var.clear();
      canonical_.clear();
      slop_u_values_.clear();
      slop_u_binding_width_.clear();
      preextracted_get_masks_.clear();
      seq_volatile_.clear();

      if (llvm_inline_kernel[color_index]) {
        const auto& abi        = direct_abi[color_index];
        const auto  llvm_words = [](uint32_t width) { return (static_cast<size_t>(width) + 63) / 64; };
        if (llvm_scalar_kernel[color_index]) {
          I(abi.writes.size() == 1);
          for (size_t input = 0; input < abi.reads.size(); ++input) {
            const auto& read = abi.reads[input];
            const auto& slot = color_plan_->boundary_slots()[read.slot_index];
            const auto  expr = direct_read_expr(slot, read.slot_index);
            I(!expr.empty() && slot.width <= 64);
            fout->append(absl::StrCat("  std::uint64_t __llvm_input_",
                                      input,
                                      ";\n  (",
                                      expr,
                                      ").copy_packed_words(&__llvm_input_",
                                      input,
                                      ");\n"));
          }
          const auto& write    = abi.writes.front();
          const auto& slot     = color_plan_->boundary_slots()[write.slot_index];
          auto        old_expr = direct_write_expr(slot, write.slot_index);
          if (slot.kind == livehd::sim::Color_plan::Boundary_kind::state_pending) {
            for (const auto& read : abi.reads) {
              if (read.consumer.version_site == write.version && read.consumer.input == direct_state_current_input) {
                old_expr = direct_read_expr(color_plan_->boundary_slots()[read.slot_index], read.slot_index);
                break;
              }
            }
          }
          I(!old_expr.empty() && slot.width <= 64);
          if (slot.kind == livehd::sim::Color_plan::Boundary_kind::state_pending) {
            emit_latch_din_placeholder('S', write.version);  // a latch `_din`: cross-color readers' dirty marks
          }
          fout->append("  std::uint64_t __llvm_old;\n  (", old_expr, ").copy_packed_words(&__llvm_old);\n");
          if (slot.width < 64) {
            fout->append("  __llvm_old &= UINT64_C(", std::to_string((uint64_t{1} << slot.width) - 1), ");\n");
          }
          fout->append("  const std::uint64_t __llvm_value = ", llvm_inline_raw_name[color_index], "(");
          for (size_t input = 0; input < abi.reads.size(); ++input) {
            fout->append(input == 0 ? "__llvm_input_" : ", __llvm_input_", std::to_string(input));
          }
          fout->append(abi.reads.empty() ? "this);\n" : ", this);\n");
          fout->append("  const bool __llvm_value_changed = __llvm_value != __llvm_old;\n");
          const auto expr      = direct_write_expr(slot, write.slot_index);
          const bool canonical = direct_slot_is_u[write.slot_index];
          fout->append(absl::StrCat("  ",
                                    expr,
                                    " = ",
                                    canonical ? "Slop_u<" : "Slop<",
                                    slot.width,
                                    ">::from_packed_words(&__llvm_value);\n"));
          if (tune_dirty() && slot.kind == livehd::sim::Color_plan::Boundary_kind::color_value) {
            fout->append("  if (__llvm_value_changed) {\n");
            emit_serial_dirty_consumers(write.slot_index, "    ");
            fout->append("  }\n");
          }
          if (slot.kind == livehd::sim::Color_plan::Boundary_kind::state_pending) {
            emit_state_commit_flag(write.version, "__llvm_value_changed");
            emit_latch_din_placeholder('M', write.version);
          }
          for (const size_t member : kernel_memory_commit_members(color_index)) {
            emit_commit_flag_at("  ", member);
          }
          close_case();
          fout->append("    return;\n  }\n");
          continue;
        }
        size_t input_words = 0;
        for (const auto& read : abi.reads) {
          input_words += llvm_words(color_plan_->boundary_slots()[read.slot_index].width);
        }
        size_t output_words = 0;
        for (const auto& write : abi.writes) {
          output_words += llvm_words(color_plan_->boundary_slots()[write.slot_index].width);
        }
        fout->append("  std::uint64_t __llvm_inputs[", std::to_string(std::max<size_t>(input_words, 1)), "]{};\n");
        size_t input_word = 0;
        for (size_t input = 0; input < abi.reads.size(); ++input) {
          const auto& read = abi.reads[input];
          const auto& slot = color_plan_->boundary_slots()[read.slot_index];
          const auto  expr = direct_read_expr(slot, read.slot_index);
          I(!expr.empty());
          fout->append("  (", expr, ").copy_packed_words(__llvm_inputs + ", std::to_string(input_word), ");\n");
          input_word += llvm_words(slot.width);
        }
        fout->append("  std::uint64_t __llvm_outputs[", std::to_string(std::max<size_t>(output_words, 1)), "]{};\n");
        size_t output_word = 0;
        for (size_t output = 0; output < abi.writes.size(); ++output) {
          const auto& write = abi.writes[output];
          const auto& slot  = color_plan_->boundary_slots()[write.slot_index];
          auto        expr  = direct_write_expr(slot, write.slot_index);
          if (slot.kind == livehd::sim::Color_plan::Boundary_kind::state_pending) {
            for (const auto& read : abi.reads) {
              if (read.consumer.version_site == write.version && read.consumer.input == direct_state_current_input) {
                expr = direct_read_expr(color_plan_->boundary_slots()[read.slot_index], read.slot_index);
                break;
              }
            }
          }
          I(!expr.empty());
          fout->append("  (", expr, ").copy_packed_words(__llvm_outputs + ", std::to_string(output_word), ");\n");
          output_word += llvm_words(slot.width);
        }
        const size_t changed_words = std::max<size_t>(1, (abi.writes.size() + 63) / 64);
        fout->append("  std::uint64_t __llvm_changed[",
                     std::to_string(changed_words),
                     "]{};\n  ",
                     llvm_inline_raw_name[color_index],
                     "(__llvm_inputs, __llvm_outputs, __llvm_changed, this);\n");
        for (const auto& write : abi.writes) {  // snapshot every latch `_din` before the first store below
          if (color_plan_->boundary_slots()[write.slot_index].kind == livehd::sim::Color_plan::Boundary_kind::state_pending) {
            emit_latch_din_placeholder('S', write.version);
          }
        }
        output_word = 0;
        for (size_t output = 0; output < abi.writes.size(); ++output) {
          const auto& write     = abi.writes[output];
          const auto& slot      = color_plan_->boundary_slots()[write.slot_index];
          const auto  expr      = direct_write_expr(slot, write.slot_index);
          const bool  canonical = direct_slot_is_u[write.slot_index];  // the DECLARED storage type, not the use's sign
          fout->append(absl::StrCat("  ",
                                    expr,
                                    " = ",
                                    canonical ? "Slop_u<" : "Slop<",
                                    slot.width,
                                    ">::from_packed_words(__llvm_outputs + ",
                                    output_word,
                                    ");\n"));
          output_word += llvm_words(slot.width);
          if (tune_dirty() && slot.kind == livehd::sim::Color_plan::Boundary_kind::color_value) {
            fout->append("  if ((__llvm_changed[",
                         std::to_string(output / 64),
                         "] & (std::uint64_t{1} << ",
                         std::to_string(output % 64),
                         ")) != 0) {\n");
            emit_serial_dirty_consumers(write.slot_index, "    ");
            fout->append("  }\n");
          }
          if (slot.kind == livehd::sim::Color_plan::Boundary_kind::state_pending) {
            emit_state_commit_flag(
                write.version,
                absl::StrCat("(__llvm_changed[", output / 64, "] & (std::uint64_t{1} << ", output % 64, ")) != 0"));
            emit_latch_din_placeholder('M', write.version);
          }
        }
        for (const size_t member : kernel_memory_commit_members(color_index)) {
          emit_commit_flag_at("  ", member);  // staged through owner callbacks: no changed bit carries it
        }
        close_case();
        fout->append("    return;\n  }\n");
        continue;
      }

      auto members = color.members;
      std::ranges::sort(members, [&](size_t a, size_t b) {
        const auto& av = color_plan_->version_sites()[a];
        const auto& bv = color_plan_->version_sites()[b];
        return std::tie(av.execution_order, av.structural_id) < std::tie(bv.execution_order, bv.structural_id);
      });
      absl::flat_hash_map<size_t, std::string> local_version_value;
      absl::flat_hash_set<size_t>              local_version_slop_u;
      struct Direct_member_body {
        size_t      member = livehd::sim::Color_plan::invalid_index;
        std::string text;
        std::string activation;
      };
      struct Direct_reset_body {
        std::string predicate;
        std::string text;
        size_t      member = livehd::sim::Color_plan::invalid_index;  // the state_update whose reset this is
      };
      std::vector<Direct_member_body>                                  direct_member_bodies;
      std::vector<Direct_reset_body>                                   direct_reset_bodies;
      absl::flat_hash_set<size_t>                                      force_always_members;
      absl::flat_hash_map<std::string, std::pair<std::string, size_t>> extraction_cse;
      local_version_value.reserve(members.size());
      direct_member_bodies.reserve(members.size());
      // AND two optional predicates: an empty side contributes nothing. Every
      // guard/enable/activation conjunction below funnels through this so the
      // spelling (and the parenthesization the string matchers rely on) is one.
      const auto combine_activation = [](std::string lhs, std::string_view rhs) {
        if (is_constant_true_guard(lhs)) {
          lhs.clear();
        }
        if (is_constant_true_guard(rhs)) {
          rhs = {};
        }
        if (lhs.empty()) {
          return std::string(rhs);
        }
        if (rhs.empty()) {
          return lhs;
        }
        return absl::StrCat("(", lhs, " && ", rhs, ")");
      };
      size_t                           temporary = 0;
      // A compact loop instance with K carried outputs has K version-sites in
      // possibly different colors (one per output port). Its state-advance
      // action runs once per period, while bindings are shared once per color: each `__compact_advance()` COMMITS every lane's
      // child state, so a second walk in the same clock stepped a two-carry tap chain twice per cycle (matched_filter: `xs`/`cs`
      // carries).
      // The walk (and the `__in` bindings) are emitted in the body of the FIRST
      // sibling only; every later sibling just names `__last_out.<port>`. So the
      // owner's body is a producer of each sibling, and the activation-domain
      // pass below must see that edge (compact_loop_owner_of: sibling -> owner).
      absl::flat_hash_map<std::string, size_t> compact_loop_controlled;  // "<sub>#pre|#post" -> owner member
      absl::flat_hash_map<size_t, size_t>      compact_loop_owner_of;
      for (const size_t member : members) {
        const auto& version = color_plan_->version_sites()[member];
        const auto& site    = color_plan_->sites()[version.base_site];
        const auto  node    = site.node.base_node();
        const auto  op      = type_op_of(node);
        std::string member_value;
        std::string member_activation;
        bool        member_value_is_u   = false;
        const auto  member_body_mark    = fout->mark();
        const auto  capture_member_body = [&] {
          direct_member_bodies.push_back(Direct_member_body{member, fout->detach_from(member_body_mark), member_activation});
        };

        // Class_index is definition-local, and the same definition can occur
        // many times in one hierarchy-crossing color. Rebuild the expression
        // bindings for exactly this occurrence member instead of allowing a
        // sibling's numerically-equal pin index to leak across the fused body.
        pin2var.clear();
        canonical_.clear();
        slop_u_values_.clear();
        preextracted_get_masks_.clear();
        seq_volatile_.clear();
        for (const auto& [slot_index, consumer_ptr] : direct_boundary_bindings_by_consumer[member]) {
          const auto& slot     = color_plan_->boundary_slots()[slot_index];
          const auto& consumer = *consumer_ptr;
          std::string slot_expr;
          if (slot.kind == livehd::sim::Color_plan::Boundary_kind::color_value
              || slot.kind == livehd::sim::Color_plan::Boundary_kind::state_current) {
            slot_expr = direct_read_expr(slot, slot_index);
          } else if (slot.kind == livehd::sim::Color_plan::Boundary_kind::top_input) {
            const auto field = input_field(slot.public_port);
            if (!field.empty()) {
              slot_expr = "__in." + field;
            }
          }
          if (slot_expr.empty()) {
            continue;
          }
          if (consumer.color != color_index || version.role == livehd::sim::Color_plan::Version_role::state_read) {
            continue;
          }
          const auto consumer_expr     = direct_consumer_expr(slot, slot_index, consumer);
          const auto definition_inputs = livehd::graph_util::inp_sink_drivers(node);
          uint32_t   consumer_input    = 0;
          for (const auto& edge : definition_inputs) {
            if (consumer_input++ != consumer.input) {
              continue;
            }
            I(edge.sink.get_port_id() == consumer.port);
            pin2var[edge.driver.get_class_index()] = consumer_expr;
            if (consumer.preextracted) {
              preextracted_get_masks_.insert(node.get_class_index());
            }
            const bool exact_u
                = slot.width == consumer.width && slop_u_ && slot.unsign
                  && ((slot.kind == livehd::sim::Color_plan::Boundary_kind::color_value && direct_slot_is_u[slot_index])
                      || slot.kind == livehd::sim::Color_plan::Boundary_kind::top_input
                      || slot.kind == livehd::sim::Color_plan::Boundary_kind::state_current);
            if ((slot.kind == livehd::sim::Color_plan::Boundary_kind::color_value
                 || slot.kind == livehd::sim::Color_plan::Boundary_kind::top_input
                 || slot.kind == livehd::sim::Color_plan::Boundary_kind::state_current)
                && (!slot.unsign || slot.width <= consumer.width)) {
              canonical_.insert(edge.driver.get_class_index());
            }
            if (exact_u) {
              // direct_consumer_expr returned the Slop_u slot itself (rather
              // than a width-adjusted Slop), or an unsigned input/state member.
              // Preserve that concrete type so raw_operand can feed it straight
              // to HLOP's mixed operands.
              slop_u_values_.insert(edge.driver.get_class_index());
            }
            break;
          }
        }
        bind_internal_value_uses(member, color_index, node, local_version_value, local_version_slop_u);
        // Bind only the definition's REFERENCE clock. The old spelling walked
        // every input declaration of the member's module for every occurrence
        // version, even though clock_input_of() had already identified the one
        // candidate. On Minion that was another versions x module-inputs scan.
        // Preserve the transported activation before replacing a definition's
        // reference clock with its scheduler phase level. Conditional children
        // use this input as an enable, not as the implicit root clock.
        std::string transported_clock;
        if (version.role == livehd::sim::Color_plan::Version_role::state_update) {
          const auto clock = get_driver(find_sink_pin(node, "clock_pin"));
          if (const auto binding = pin2var.find(clock.get_class_index()); binding != pin2var.end()) {
            transported_clock = binding->second;
          }
        }
        const auto* member_flop     = find_local_flop(version.base_site);
        const bool  secondary_state = member_flop != nullptr && !member_flop->sec_clock.is_invalid();
        const auto  reference_clock = clock_input_of(node.get_graph());
        if (!secondary_state && !reference_clock.empty()) {
          const auto input = node.get_graph()->get_input_pin(reference_clock);
          if (!input.is_invalid() && local_clocks_for(node.get_graph()).is_clock(input)) {
            const bool clock_high            = color.slot == livehd::sim::Color_plan::Execution_slot::post_rise_eval
                                               || color.slot == livehd::sim::Color_plan::Execution_slot::fall_commit;
            pin2var[input.get_class_index()] = absl::StrCat(slop_u_ && is_unsign(input) ? "Slop_u<1>" : "Slop<1>",
                                                            "::create_integer(",
                                                            clock_high ? "1" : "0",
                                                            ")");
            canonical_.insert(input.get_class_index());
            mark_slop_u_binding(input);
          }
        }
        if (op == Ntype_op::Sub) {
          const auto sio = node.get_subnode_io();
          I(sio != nullptr);
          const auto sub_member = occurrence_sub_member(site);
          I(!sub_member.empty());
          bool owns_loop_walk = false;
          if (site.kind == livehd::sim::Color_plan::Site_kind::loop_control) {
            const auto [owner, inserted] = compact_loop_controlled.try_emplace(
                absl::StrCat(sub_member, version.version == livehd::sim::Color_plan::State_version::pre_rise ? "#pre" : "#post"),
                member);
            owns_loop_walk = inserted;
            if (!inserted) {
              compact_loop_owner_of.emplace(member, owner->second);
            }
          }
          if (owns_loop_walk) {
            const auto loop = node.subnode_loop();
            I(loop.has_value());
            for (const auto& decl : sio->get_input_pin_decls()) {
              if (loop->index_input && decl.port_id == *loop->index_input) {
                continue;  // supplied as first + ordinal*step by the native wrapper
              }
              const auto sink = find_sink_pin(node, decl.name);
              if (sink.is_invalid()) {
                continue;
              }
              hhds::Pin_class driver;
              for (auto edge_drv : sink.get_driver_pins()) {
                if (edge_drv.get_master_node() != node) {
                  driver = edge_drv;
                  break;
                }
              }
              if (driver.is_invalid()) {
                continue;  // descriptor-only carry/activation input
              }
              const int width = decl.bits > 0 ? static_cast<int>(decl.bits) : 1;
              emit_in_binding("  ",
                              sub_member,
                              cpp_port_path(decl.name),
                              width,
                              bind_operand(driver, width, decl.unsign),
                              forward_io_of(driver, width));
              if (const auto child = node.get_subnode_graph();
                  child && clock_guard_ports(child, port_cache).contains(static_cast<uint32_t>(decl.port_id))) {
                const auto root_pin = livehd::latch_contract::control_root(driver).net;
                I(!root_pin.is_invalid() && livehd::graph_util::is_graph_input_pin(root_pin));
                const auto field = input_field(root_pin.get_port_id());
                I(!field.empty());
                fout->append(absl::StrCat("  ",
                                          sub_member,
                                          ".__gen += slop_update(",
                                          sub_member,
                                          ".__in.",
                                          cpp_port_path(decl.name),
                                          "__tick, __in.",
                                          field,
                                          "__tick);\n"));
              }
            }
            if (version.version == livehd::sim::Color_plan::State_version::pre_rise) {
              // The walk COMMITS every lane's child state, and that state is
              // opaque to this plan (no state site, no `__state_commit` slot):
              // without the check below a design whose only state lives inside
              // a rolled loop looked quiescent as soon as its inputs held
              // still, and the pipeline froze mid-drain (matched_filter froze
              // at the cycle its input went to zero). The wrapper's `__gen`
              // moves when any lane's child committed a change (__sync_kids),
              // so a moved generation is exactly "state changed": keep the
              // period live and re-dirty every color that holds this instance.
              fout->append("  if (!__rt.__loop_advanced_", std::to_string(version.base_site), ") {\n");
              fout->append("  __rt.__loop_advanced_", std::to_string(version.base_site), " = true;\n");
              fout->append("  const uint64_t __loop_gen_before = ", sub_member, ".__gen;\n");
              fout->append("  ", sub_member, ".__compact_advance();  // compact-loop control: one native ordinal walk\n");
              fout->append("  if (", sub_member, ".__gen != __loop_gen_before) {\n    __rt.__color_state_changed = true;\n");
              emit_serial_dirty_site_colors(version.base_site, "    ");
              fout->append("  } }\n");
            } else {
              fout->append("  ", sub_member, ".__compact_publish();  // compact-loop post-edge version: one native ordinal walk\n");
            }
          }
          const char* surface = version.version == livehd::sim::Color_plan::State_version::pre_rise ? ".__last_out." : ".__out.";
          for (const auto& decl : sio->get_output_pin_decls()) {
            const auto output = find_driver_pin(node, decl.name);
            if (!output.is_invalid()) {
              const auto value                  = sub_member + surface + cpp_port_path(decl.name);
              pin2var[output.get_class_index()] = value;
              canonical_.insert(output.get_class_index());
              if (decl.unsign) {
                mark_slop_u_binding(output);
              }
              if (decl.port_id == version.output_port) {
                member_value = value;
              }
            }
          }
        } else {
          // Expression lowering works on definition pins, while the color plan
          // works on resolved occurrence edges. A child GraphIO input driven by
          // a parent literal therefore needs an explicit definition-pin binding;
          // otherwise a raw Class_index collision with a root input can silently
          // substitute the wrong signal. Non-constant cross-boundary sources are
          // already bound through the exact Boundary_consumer records above.
          const auto occurrence_inputs = occurrence_sorted_inputs(site.node);
          const auto definition_inputs = livehd::graph_util::inp_sink_drivers(node);
          // Pair the two sequences by SINK pin, never by ordinal. hhds resolves
          // every definition driver across the instance boundary
          // (Hierarchy_view_state::pin_in_edges -> resolve_driver), and a
          // driver it cannot resolve contributes NO occurrence driver -- so
          // "one occurrence driver per definition input" is not an invariant
          // hhds guarantees. Measured on lhdsuite's minion_top: a node with 83
          // definition inputs whose occurrence side holds 82. The old ordinal
          // walk then ran one past occurrence_inputs.end() and dereferenced
          // unconstructed storage (SIGSEGV under -c opt; the I() that guarded
          // it is compiled out by -DNDEBUG, so release builds had no check at
          // all), and whenever a drop happened to be balanced by an expansion
          // it would silently bind every later input to the WRONG parent
          // driver. The bucketing below is exactly why the occurrence side is
          // read with the PLURAL get_driver_pins(): one sink pin legitimately
          // resolving to several drivers is the case it has to SEE, not one to
          // silently truncate.
          absl::flat_hash_map<hhds::Class_index, absl::InlinedVector<hhds::Occurrence_pin, 1>> occurrence_by_sink;
          for (const auto& occurrence_edge : occurrence_inputs) {
            occurrence_by_sink[occurrence_edge.sink.get_class_index()].emplace_back(occurrence_edge.driver);
          }
          absl::flat_hash_map<hhds::Class_index, size_t> definition_by_sink;
          for (size_t input = 0; input < definition_inputs.size(); ++input) {
            ++definition_by_sink[definition_inputs[input].sink.get_class_index()];
          }
          absl::flat_hash_map<hhds::Class_index, size_t> consumed_by_sink;
          for (size_t input = 0; input < definition_inputs.size(); ++input) {
            const auto sink_key      = definition_inputs[input].sink.get_class_index();
            const auto occurrence_it = occurrence_by_sink.find(sink_key);
            const auto sink_ordinal  = consumed_by_sink[sink_key]++;
            if (occurrence_it == occurrence_by_sink.end() || occurrence_it->second.size() != definition_by_sink[sink_key]
                || sink_ordinal >= occurrence_it->second.size()) {
              continue;  // no unambiguous occurrence driver for this definition input
            }
            const auto& occurrence_driver = occurrence_it->second[sink_ordinal];
            if (version.role == livehd::sim::Color_plan::Version_role::state_update) {
              const auto producer_it = direct_site_index.find(occurrence_driver.get_master_node().get_occurrence_index());
              if (producer_it != direct_site_index.end()
                  && type_op_of(color_plan_->sites()[producer_it->second].node.base_node()) == Ntype_op::Latch) {
                const size_t producer_update = state_update_version(producer_it->second);
                if (producer_update != livehd::sim::Color_plan::invalid_index
                    && color_plan_->version_sites()[producer_update].slot == version.slot) {
                  pin2var[definition_inputs[input].driver.get_class_index()]
                      = occurrence_member(color_plan_->sites()[producer_it->second]) + "_din";
                  canonical_.insert(definition_inputs[input].driver.get_class_index());
                  continue;
                }
              }
            }
            if (!occurrence_driver.is_const() || definition_inputs[input].driver.is_const()) {
              continue;
            }
            const auto width = std::max<int32_t>(1, wbits_of(definition_inputs[input].driver));
            pin2var[definition_inputs[input].driver.get_class_index()] = operand(occurrence_driver.base_pin(), width);
          }
          if (version.role == livehd::sim::Color_plan::Version_role::state_read) {
            const auto qpin                 = node.get_driver_pin(0);
            member_value                    = occurrence_member(site);
            pin2var[qpin.get_class_index()] = member_value;
            // Read the hint off the PIN, not off find_local_flop(): `flops` is
            // collected from the ROOT graph only and local_flop_by_node is keyed
            // by the flop's own graph, so a register inside a sub-definition
            // never matched and every hierarchical state read was treated as
            // signed -- while its member had already been declared `Slop_u<W>`
            // by the sub's own emission. The state_update sibling below and the
            // consumer-side binding both read the hint directly; this producer
            // path was the odd one out. Memory is excluded: a Memory site also
            // reaches here and its occurrence_member is an array object, not a
            // Slop.
            if (op != Ntype_op::Memory && is_unsign(qpin)) {
              mark_slop_u_binding(qpin);
              member_value_is_u = true;
            }
          }
          if (version.role == livehd::sim::Color_plan::Version_role::state_update) {
            const auto* local_flop = find_local_flop(version.base_site);
            const auto  state      = occurrence_member(site);
            if (op == Ntype_op::Memory) {
              const auto* memory = find_local_mem(version.base_site);
              I(memory != nullptr && !state.empty());
              if (memory->has_reset_stage() && !memory->is_whole()) {
                // Per-port memory with a reset: the whole-array stage is the
                // reset reload of the `initial` contents, nothing else.
                const int         width   = memory->bits * memory->size;
                const std::string pending = whole_pending(version.base_site);
                const std::string init    = memory->init.is_invalid()
                                                ? absl::StrCat(value_type(width, memory->unsign), "::create_integer(0)")
                                                : stored_value_operand(memory->init, width, memory->unsign);
                fout->append("  ", pending, "_din = ", init, ";\n");
                fout->append(absl::StrCat("  ", pending, "_cen = (", raw_operand(memory->reset, 1), ").is_known_true();\n"));
                emit_state_commit_flag(member, "true");
              } else if (memory->is_whole()) {
                const int         width   = memory->bits * memory->size;
                const std::string pending = whole_pending(version.base_site);
                const std::string update  = stored_value_operand(memory->update, width, memory->unsign);
                const std::string init    = memory->init.is_invalid()
                                                ? absl::StrCat(value_type(width, memory->unsign), "::create_integer(0)")
                                                : stored_value_operand(memory->init, width, memory->unsign);
                std::string       enable;
                if (!memory->update_enable.is_invalid()) {
                  enable = emit_known_true(raw_operand(memory->update_enable, 1));
                }
                if (const auto gate = mem_gate_cond(*memory); !gate.empty()) {
                  enable = enable.empty() ? gate : absl::StrCat("(", enable, " && ", gate, ")");
                }
                if (enable.empty()) {
                  enable = "true";
                }
                std::string reset;
                if (!memory->reset.is_invalid()) {
                  reset = emit_known_true(raw_operand(memory->reset, 1));
                }
                if (reset.empty()) {
                  fout->append("  ", pending, "_din = ", update, ";\n");
                  fout->append("  ", pending, "_cen = ", enable, ";\n");
                } else {
                  fout->append(absl::StrCat("  ", pending, "_din = ", reset, " ? ", init, " : ", update, ";\n"));
                  fout->append(absl::StrCat("  ", pending, "_cen = ", reset, " || ", enable, ";\n"));
                }
                emit_state_commit_flag(member, "true");
              }
              for (const auto& guard : memory->clock_guards) {
                auto resolved_input = occurrence_inputs.begin();
                for (size_t input = 0; input < definition_inputs.size(); ++input, ++resolved_input) {
                  if (definition_inputs[input].sink.get_port_id() != Ntype::get_sink_pid(op, "clock_pin")) {
                    continue;
                  }
                  if (const auto occurrence_guard = resolve_occurrence_pin(resolved_input->driver, guard, 0)) {
                    const auto guard_value = occurrence_bool_expr(*occurrence_guard, 0);
                    if (!guard_value.empty()) {
                      pin2var[guard.get_class_index()] = guard_value;
                      canonical_.insert(guard.get_class_index());
                    }
                  }
                  break;
                }
              }
              fout->append("  ", state, ".clear_pending();\n");
              for (const auto& port : memory->ports) {
                if (port.rd || port.addr.is_invalid() || port.din.is_invalid()) {
                  continue;
                }
                if (port.enable.is_known_false()) {
                  continue;
                }
                fout->append(absl::StrCat("  ",
                                          state,
                                          ".stage_write<",
                                          port.wridx,
                                          ">(",
                                          emit_wen(*memory, port),
                                          ", ",
                                          raw_operand(port.addr, std::max(1, bits_of(port.addr))),
                                          ", ",
                                          stored_value_operand(port.din, memory->bits, memory->unsign),
                                          ");\n"));
              }
              emit_state_commit_flag(member, "true");
              for (const auto& port : memory->ports) {
                if (!port.rd || memory->type != 1 || port.addr.is_invalid()) {
                  continue;
                }
                const std::string q    = absl::StrCat(state, "_q", port.rdidx);
                const std::string gate = mem_gate_cond(*memory);
                const std::string read
                    = absl::StrCat(state, ".read<", port.rdidx, ">(", raw_operand(port.addr, std::max(1, bits_of(port.addr))), ")");
                fout->append(absl::StrCat("  ",
                                          q,
                                          "_din = ",
                                          gate.empty() ? read : absl::StrCat("(", gate, " ? ", read, " : ", q, ")"),
                                          ";\n"));
              }
              capture_member_body();
              continue;
            }
            const int  state_bits   = wbits_of(node.get_driver_pin(0));
            const bool state_unsign = is_unsign(node.get_driver_pin(0));
            I(!state.empty());
            const auto din      = get_driver(find_sink_pin(node, "din"));
            const auto rstp     = get_driver(find_sink_pin(node, "reset_pin"));
            const auto initp    = get_driver(find_sink_pin(node, "initial"));
            const auto enp      = get_driver(find_sink_pin(node, "enable"));
            bool       negreset = false;
            if (const auto np = get_driver(find_sink_pin(node, "negreset")); np.is_const()) {
              negreset = !const_of(np).is_known_false();
            }
            const std::string rstval       = initp.is_invalid()
                                                 ? absl::StrCat(value_type(state_bits, state_unsign), "::create_integer(0)")
                                                 : stored_value_operand(initp, state_bits, state_unsign);
            bool              reset_always = false;
            std::string       rtest;
            if (!rstp.is_invalid()) {
              if (rstp.is_const()) {
                reset_always = !const_of(rstp).is_known_false();
              } else {
                rtest = absl::StrCat(raw_operand(rstp, 1), negreset ? ".is_known_false()" : ".is_known_true()");
              }
            }
            bool phase_window_latch = false;
            if (op == Ntype_op::Latch) {
              const auto& local_clocks = local_clocks_for(node.get_graph());
              const auto  commit       = livehd::latch_contract::commit_class_of(node, &local_clocks);
              if (commit.has_value() && commit->role == livehd::latch_contract::Net_role::Clock) {
                const auto root    = livehd::latch_contract::control_root(enp);
                phase_window_latch = !root.net.is_invalid() && livehd::graph_util::is_graph_input_pin(root.net)
                                     && pin_name_of(root.net) == clock_input_of(node.get_graph());
              }
            }
            std::string etest;
            if (!phase_window_latch && !enp.is_invalid() && !enp.is_const()) {
              etest = absl::StrCat(raw_operand(enp, 1),
                                   local_flop != nullptr && local_flop->neg_enable ? ".is_known_false()" : ".is_known_true()");
            } else if (!phase_window_latch && enp.is_const() && local_flop != nullptr && local_flop->neg_enable
                       && !const_of(enp).is_known_false()) {
              etest = "false";
            }
            const std::string din_expr = din.is_invalid() ? state : stored_value_operand(din, state_bits, state_unsign);
            // The ENABLE stays in the pending value even though it also joins
            // the activation predicate below. The predicate is a scheduling
            // hint that the domain analysis may legitimately widen (or drop
            // entirely, when another member's guard reads this `_din`, or when
            // a reader's domain merges into it); the hold mux is what MAKES a
            // de-asserted enable hold, so it may never be the only copy.
            const auto        next_of  = [&](const std::string& source, const std::string& hold) {
              return etest.empty() ? source : absl::StrCat("(", etest, " ? ", source, " : ", hold, ")");
            };
            int pipe_depth = 1;
            if (const auto pipe_min = get_driver(find_sink_pin(node, "pipe_min")); pipe_min.is_const()) {
              pipe_depth = std::max<int>(1, static_cast<int>(const_of(pipe_min).to_just_i64()));
            }
            // Defer the next-value text until the exact structural event
            // predicate below is known.  A closed clock gate (or inactive
            // conditional/secondary edge) cannot commit this state, so there
            // is no reason to evaluate its enable/data landing work. Dynamic
            // reset is emitted as a separate grouped slow path below, preserving
            // asynchronous reset semantics without a hot-path value mux.
            const auto next_value_mark = fout->mark();
            if (pipe_depth == 1) {
              fout->append("  ", state, "_din = ", next_of(din_expr, state), ";\n");
            } else {
              const auto stage = [&](int index) { return absl::StrCat(state, "_p", index); };
              fout->append("  ", stage(0), "_din = ", next_of(din_expr, stage(0)), ";\n");
              for (int index = 1; index < pipe_depth - 1; ++index) {
                fout->append("  ", stage(index), "_din = ", next_of(stage(index - 1), stage(index)), ";\n");
              }
              fout->append("  ", state, "_din = ", next_of(stage(pipe_depth - 2), state), ";\n");
            }
            std::string commit_test;
            const auto  guard_expr = [&](const hhds::Pin_class& guard) {
              if (guard.is_const()) {
                return operand(guard, 1);
              }
              const auto guard_root = livehd::latch_contract::control_root(guard);
              if (!guard_root.net.is_invalid() && type_op_of(guard_root.net.get_master_node()) == Ntype_op::Latch) {
                const auto candidates = direct_latch_sites_by_path.find(site.node.path());
                if (candidates != direct_latch_sites_by_path.end()) {
                  for (const size_t candidate_index : candidates->second) {
                    const auto& candidate_site = color_plan_->sites()[candidate_index];
                    const auto  qpin           = candidate_site.node.base_node().get_driver_pin(0);
                    if (!qpin.is_invalid() && qpin.get_definition_index() == guard_root.net.get_definition_index()) {
                      return occurrence_member(candidate_site) + "_din";
                    }
                  }
                }
              }
              if (guard.get_graph() == g && livehd::graph_util::is_graph_input_pin(guard)) {
                const auto field = input_field(guard.get_port_id());
                if (!field.empty()) {
                  return "__in." + field;
                }
              }
              const auto candidates = direct_latch_sites_by_path.find(site.node.path());
              if (candidates != direct_latch_sites_by_path.end()) {
                for (const size_t candidate_index : candidates->second) {
                  const auto& candidate_site = color_plan_->sites()[candidate_index];
                  const auto  qpin           = candidate_site.node.base_node().get_driver_pin(0);
                  if (!qpin.is_invalid() && qpin.get_definition_index() == guard.get_definition_index()) {
                    return occurrence_member(candidate_site) + "_din";
                  }
                }
              }
              for (const auto& candidate : flops) {
                const auto qpin = candidate.node.get_driver_pin(0);
                if (!qpin.is_invalid() && qpin.get_class_index() == guard.get_class_index()) {
                  return candidate.is_latch ? candidate.member + "_din" : candidate.member;
                }
              }
              if (const auto it = pin2var.find(guard.get_class_index()); it != pin2var.end()) {
                return it->second;
              }
              return operand(guard, 1);
            };
            const auto state_clock = get_driver(find_sink_pin(node, "clock_pin"));
            if (!state_clock.is_invalid() && type_op_of(state_clock.get_master_node()) == Ntype_op::Clock_cell) {
              // A Clock_cell color produces its normalized activation enable;
              // the reference edge itself is represented by the execution-slot
              // barrier rather than by a driven Boolean clock value.
              auto resolved_input = occurrence_inputs.begin();
              for (size_t input = 0; input < definition_inputs.size(); ++input, ++resolved_input) {
                if (definition_inputs[input].sink.get_port_id() != Ntype::get_sink_pid(op, "clock_pin")) {
                  continue;
                }
                const auto root       = livehd::latch_contract::control_root(resolved_input->driver);
                const auto activation = occurrence_guard_expr(resolved_input->driver, root.net.base_pin(), livehd::sim::Color_plan::commit_slot_of(version), 0);
                commit_test = emit_known_true(activation.empty() ? operand(state_clock, 1) : activation);
                break;
              }
            } else if (local_flop != nullptr && !local_flop->clock_guards.empty()) {
              // The implicit reference clock is a scheduler event; testbenches
              // need not drive its value high.  Therefore test the recursively
              // resolved gate leaves, not the complete `clk & guards` net.  A
              // direct color has no legacy pin2var forest for those leaves, so
              // bind graph inputs and state Qs explicitly before falling back to
              // the ordinary expression resolver.
              for (const auto& guard : local_flop->clock_guards) {
                commit_test = combine_activation(commit_test, emit_known_true(guard_expr(guard)));
              }
            }
            if (commit_test.empty() && node.get_graph() != g) {
              // A conditional call may transport its activation Clock_cell as a
              // rewritten child reference-clock input rather than leave an
              // explicit Clock_cell on the state endpoint. Resolve the exact
              // occurrence edge: a plain root-clock forwarding is only the slot
              // event and needs no predicate, while (__valid || reset) is a real
              // commit guard already bound in this color's pin2var table.
              I(definition_inputs.size() == occurrence_inputs.size());
              auto resolved_input = occurrence_inputs.begin();
              for (size_t input = 0; input < definition_inputs.size(); ++input, ++resolved_input) {
                if (definition_inputs[input].sink.get_port_id() != Ntype::get_sink_pid(op, "clock_pin")) {
                  continue;
                }
                const auto resolved = resolved_input->driver;
                if (conditional_occurrence(site.node)) {
                  const auto activation = occurrence_bool_expr(resolved, 0);
                  commit_test           = absl::StrCat(
                      "(",
                      activation.empty()
                          ? (transported_clock.empty() ? operand(definition_inputs[input].driver, 1) : transported_clock)
                          : activation,
                      ").is_known_true()");
                  break;
                }
                const auto resolved_root              = livehd::latch_contract::control_root(resolved);
                const bool definition_reference_clock = livehd::graph_util::is_graph_input_pin(state_clock)
                                                        && pin_name_of(state_clock) == clock_input_of(node.get_graph());
                if (((local_flop != nullptr && local_flop->sec_clock.is_invalid()) || definition_reference_clock)
                    && !resolved_root.net.is_invalid()) {
                  const auto activation = occurrence_guard_expr(resolved, resolved_root.net.base_pin(), livehd::sim::Color_plan::commit_slot_of(version), 0);
                  commit_test           = activation.empty() ? std::string{} : emit_known_true(activation);
                  break;
                }
                bool       resolved_fall   = false;
                const auto resolved_guards = icg_guards(resolved.base_pin(), clock_input_of(g), &design_clocks, &resolved_fall);
                if (!resolved_guards.empty()) {
                  // The occurrence edge carries the complete clock value
                  // (`clk & enables`).  The reference clock is an execution
                  // event, however, and is normally low while pre-edge colors
                  // run.  Testing the transported value would therefore close
                  // every parent-side gate.  Reuse the structural clock answer
                  // and test only its enable leaves, exactly as a local gated
                  // state element does above.
                  for (const auto& guard : resolved_guards) {
                    std::string guard_value;
                    if (const auto occurrence_guard = resolve_occurrence_pin(resolved, guard, 0)) {
                      guard_value = occurrence_bool_expr(*occurrence_guard, 0);
                    }
                    if (guard_value.empty()) {
                      guard_value = guard_expr(guard);
                    }
                    commit_test = combine_activation(commit_test, emit_known_true(guard_value));
                  }
                  break;
                }
                const auto root             = livehd::latch_contract::control_root(resolved);
                const bool plain_root_clock = !root.inverted && !root.net.is_invalid()
                                              && livehd::graph_util::is_graph_input_pin(root.net.base_pin())
                                              && root.net.get_graph() == g && pin_name_of(root.net.base_pin()) == clock_input_of(g);
                if (!plain_root_clock) {
                  const auto activation = occurrence_bool_expr(resolved, 0);
                  commit_test           = absl::StrCat(
                      "(",
                      activation.empty()
                          ? (transported_clock.empty() ? operand(definition_inputs[input].driver, 1) : transported_clock)
                          : activation,
                      ").is_known_true()");
                }
                break;
              }
            }
            commit_test = combine_activation(commit_test, conditional_activation_expr(site.node));
            if (local_flop != nullptr && !local_flop->sec_clock.is_invalid()) {
              const std::string current = emit_known_true(guard_expr(local_flop->sec_clock));
              const std::string edge    = local_flop->posedge ? absl::StrCat("(", current, " && !", local_flop->prev_member, ")")
                                                              : absl::StrCat("(!", current, " && ", local_flop->prev_member, ")");
              commit_test               = combine_activation(commit_test, edge);
            }
            if (local_flop != nullptr && !local_flop->tick_field.empty()) {
              commit_test = combine_activation(commit_test, absl::StrCat("__in.", local_flop->tick_field, "__tick"));
            }
            // A folded ICG may leave its enable latch behind a transparent cast
            // or a Clock_cell output. Normalize any same-occurrence/same-edge Q
            // reference in the final guard to the staged latch value. The plan's
            // explicit latch-update -> state-update edge makes that pending value
            // ready before this color runs.
            const auto latch_candidates = direct_latch_sites_by_path.find(site.node.path());
            if (latch_candidates != direct_latch_sites_by_path.end()) {
              for (const size_t candidate_index : latch_candidates->second) {
                const auto&  candidate_site = color_plan_->sites()[candidate_index];
                const size_t update         = state_update_version(candidate_index);
                if (update == livehd::sim::Color_plan::invalid_index || color_plan_->version_sites()[update].slot != version.slot) {
                  continue;
                }
                const std::string current = occurrence_member(candidate_site);
                const std::string pending = current + "_din";
                if (commit_test.find(pending) != std::string::npos) {
                  continue;
                }
                const auto identifier_char = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
                for (size_t at = commit_test.find(current); at != std::string::npos;) {
                  const bool left_bound = at == 0 || !identifier_char(commit_test[at - 1]);
                  const bool right_bound
                      = at + current.size() == commit_test.size() || !identifier_char(commit_test[at + current.size()]);
                  if (!left_bound || !right_bound) {
                    at = commit_test.find(current, at + current.size());
                    continue;
                  }
                  commit_test.replace(at, current.size(), pending);
                  at = commit_test.find(current, at + pending.size());
                }
              }
            }
            const std::string normal_activation = combine_activation(commit_test, etest);
            // Do not enqueue a register boundary that would write exactly its
            // current representation back. This retains edge/reset semantics
            // (the pending value was computed above) while avoiding the commit
            // shard, slop_update, and dirty propagation for stable lanes.
            // Without dirty-bit gating (sim.tune.dirty=off) nothing downstream
            // uses the "changed" answer: the flag only saves a copy of an equal
            // value, and Slop::identical costs more than that copy (axi_demux:
            // 1207 -> 890 ms with the compare dropped). Commit unconditionally.
            std::string       value_changed     = tune_dirty() ? absl::StrCat("!", state, "_din.identical(", state, ")") : "true";
            if (tune_dirty() && pipe_depth > 1) {
              const auto stage = [&](int index) { return absl::StrCat(state, "_p", index); };
              for (int index = 0; index < pipe_depth - 1; ++index) {
                value_changed = absl::StrCat("(", value_changed, " || !", stage(index), "_din.identical(", stage(index), "))");
              }
            }
            auto       next_value_body  = fout->detach_from(next_value_mark);
            const auto emit_reset_value = [&] {
              if (pipe_depth == 1) {
                fout->append("  ", state, "_din = ", rstval, ";\n");
                return;
              }
              const auto stage = [&](int index) { return absl::StrCat(state, "_p", index); };
              for (int index = 0; index < pipe_depth - 1; ++index) {
                fout->append("  ", stage(index), "_din = ", rstval, ";\n");
              }
              fout->append("  ", state, "_din = ", rstval, ";\n");
            };

            // A latch's `_din` writes are bracketed for its cross-color readers'
            // dirty marks (see latch_din_member); a no-op for every other state.
            // The snapshot goes to the START of the member body, ahead of the
            // binding prologue, so it never separates a prologue temporary from
            // the `_din` store that folds it (compact_body_temps folds only into
            // the adjacent statement).
            const auto emit_latch_din_snapshot = [&] {
              if (!tune_dirty() || op != Ntype_op::Latch) {
                return;
              }
              auto prologue = fout->detach_from(member_body_mark);
              emit_latch_din_placeholder('S', member);
              fout->append(prologue);
            };
            if (reset_always) {
              // A statically asserted reset has no normal path at all.
              emit_latch_din_snapshot();
              emit_reset_value();
              emit_state_commit_flag(member, value_changed);
              emit_latch_din_placeholder('M', member);
              member_activation.clear();
            } else if (!rtest.empty()) {
              // Keep reset as a separately grouped slow path. The normal region
              // is explicitly reset-false, so neither its data cone nor its
              // register landing work runs during reset, and the hot path has
              // no per-register reset mux.
              emit_latch_din_snapshot();
              fout->append(next_value_body);
              emit_state_commit_flag(member, value_changed);
              emit_latch_din_placeholder('M', member);
              member_activation = combine_activation(absl::StrCat("!", rtest), normal_activation);

              const auto reset_body_mark = fout->mark();
              emit_latch_din_placeholder('s', member);
              emit_reset_value();
              emit_state_commit_flag(member, value_changed);
              emit_latch_din_placeholder('m', member);
              direct_reset_bodies.push_back(Direct_reset_body{rtest, fout->detach_from(reset_body_mark), member});
            } else {
              emit_latch_din_snapshot();
              fout->append(next_value_body);
              emit_state_commit_flag(member, value_changed);
              emit_latch_din_placeholder('M', member);
              member_activation = normal_activation;
            }
          } else if (version.role == livehd::sim::Color_plan::Version_role::data) {
            if (op == Ntype_op::Clock_cell) {
              const auto& local_clocks = local_clocks_for(node.get_graph());
              const auto  cone         = livehd::latch_contract::clock_cell_cone(node, local_clocks);
              I(cone.has_value());
              std::string enabled;
              for (const auto& enable : cone->enables) {
                enabled = combine_activation(enabled, emit_known_true(operand(enable, 1)));
              }
              const auto output = node.get_driver_pin(0);
              I(!output.is_invalid());
              const auto temp_name = absl::StrCat("__color_tmp_", temporary++);
              fout->append(absl::StrCat("  Slop<1> ",
                                        temp_name,
                                        " = Slop<1>::create_integer(",
                                        enabled.empty() ? "1" : absl::StrCat("(", enabled, ") ? 1 : 0"),
                                        ");\n"));
              pin2var[output.get_class_index()] = temp_name;
              canonical_.insert(output.get_class_index());
              member_value = temp_name;
            } else if (op == Ntype_op::Memory) {
              const auto* memory = find_local_mem(version.base_site);
              const auto  state  = occurrence_member(site);
              I(memory != nullptr && !state.empty());
              if (memory->is_whole() && !memory->registered()) {
                fout->append("  ",
                             state,
                             ".apply_update(",
                             stored_value_operand(memory->update, memory->bits * memory->size, memory->unsign),
                             ");  // combinational whole-array contents for this version\n");
              }
              int        staged        = 0;
              const auto stage_through = [&](int upto) {
                if (version.version != livehd::sim::Color_plan::State_version::pre_rise || memory->type == 1 || upto <= staged) {
                  staged = std::max(staged, upto);
                  return;
                }
                if (staged == 0) {
                  fout->append("  ", state, ".clear_pending();\n");
                }
                for (const auto& port : memory->ports) {
                  if (port.rd || port.addr.is_invalid() || port.din.is_invalid() || port.wridx < staged || port.wridx >= upto) {
                    continue;
                  }
                  if (port.enable.is_known_false()) {
                    continue;
                  }
                  fout->append(absl::StrCat("  ",
                                            state,
                                            ".stage_write<",
                                            port.wridx,
                                            ">(",
                                            emit_wen(*memory, port),
                                            ", ",
                                            raw_operand(port.addr, std::max(1, bits_of(port.addr))),
                                            ", ",
                                            stored_value_operand(port.din, memory->bits, memory->unsign),
                                            ");\n"));
                }
                staged = upto;
              };
              for (const auto& port : memory->ports) {
                if (!port.rd || port.addr.is_invalid() || static_cast<hhds::Port_id>(port.dout_pid) != version.output_port) {
                  continue;
                }
                int prefix = 0;
                if (memory->type != 1) {
                  if (memory->order == Mem::Order::fwd || memory->order == Mem::Order::none) {
                    prefix = memory->n_user_wr;
                  } else if (memory->order == Mem::Order::program) {
                    prefix = memory->fwd_upto[static_cast<size_t>(port.rdidx)];
                  }
                }
                stage_through(prefix);
                const auto output = node.get_driver_pin(static_cast<hhds::Port_id>(port.dout_pid));
                I(!output.is_invalid());
                const auto temp_name = absl::StrCat("__color_tmp_", temporary++);
                if (memory->type == 1) {
                  fout->append(absl::StrCat("  ",
                                            value_type(memory->bits, memory->unsign),
                                            " ",
                                            temp_name,
                                            " = ",
                                            state,
                                            "_q",
                                            port.rdidx,
                                            ";\n"));
                } else {
                  fout->append(absl::StrCat("  ",
                                            value_type(memory->bits, memory->unsign),
                                            " ",
                                            temp_name,
                                            " = ",
                                            state,
                                            ".read<",
                                            port.rdidx,
                                            ">(",
                                            raw_operand(port.addr, std::max(1, bits_of(port.addr))),
                                            ");\n"));
                }
                pin2var[output.get_class_index()] = temp_name;
                canonical_.insert(output.get_class_index());
                if (memory->unsign) {
                  mark_slop_u_binding(output);
                  member_value_is_u = true;
                }
                member_value = temp_name;
              }
              if (memory->has_read_all && version.output_port == Ntype::Memory_readall_pid) {
                const auto output = node.get_driver_pin(static_cast<hhds::Port_id>(Ntype::Memory_readall_pid));
                I(!output.is_invalid());
                const auto temp_name = absl::StrCat("__color_tmp_", temporary++);
                fout->append("  auto ", temp_name, " = ", state, ".read_all();\n");
                pin2var[output.get_class_index()] = temp_name;
                canonical_.insert(output.get_class_index());
                if (memory->unsign) {
                  mark_slop_u_binding(output);
                  member_value_is_u = true;
                }
                member_value = temp_name;
              }
            } else {
              hhds::Pin_class output;
              for (const auto& candidate : node.out_pins()) {
                output = candidate;
                break;
              }
              if (output.is_invalid()) {
                output = node.get_driver_pin(0);
              }
              I(!output.is_invalid());
              sub_width_expr_              = false;
              const int   width            = wbits_of(output);
              const bool  unsigned_result  = proven_unsigned_result(node, output);
              const bool  canonical_result = proven_canonical_unsigned_result(node, output);
              const bool  use_u            = slop_u_ && unsigned_result;
              const int   eval_width       = unsigned_result ? width + 1 : width;
              const auto  expr             = node_expr(node, eval_width);
              const bool  expr_is_u        = slop_u_expr_;
              const bool  cse_extract      = op == Ntype_op::Get_mask && !preextracted_get_masks_.contains(node.get_class_index());
              std::string landing;
              if (use_u) {
                landing = sub_width_expr_    ? absl::StrCat("(", expr, ").zext_to_u<", width, ">()")
                          : debug_           ? absl::StrCat("Slop_u<", width, ">::land(", expr, ")")
                          : expr_is_u        ? expr
                          : canonical_result ? absl::StrCat("Slop_u<", width, ">::from_proven(", expr, ")")
                                             : absl::StrCat("Slop_u<", width, ">{", expr, "}");
              } else {
                // A signed landing over an UNSIGNED expression (a Get_mask
                // rendered as `Slop_u<W>::get_mask_op_opt`, feeding a signed
                // pin such as a packed-array element write) has no implicit
                // Slop_u -> Slop conversion: wrap it, exactly as the inline
                // render path does.
                landing = (sub_width_expr_ || expr_is_u) ? absl::StrCat("Slop<", eval_width, ">{", expr, "}") : expr;
              }
              const auto  cse_key = absl::StrCat(use_u ? "u" : "s", ":", eval_width, ":", landing);
              auto        cse_it  = cse_extract ? extraction_cse.find(cse_key) : extraction_cse.end();
              std::string temp_name;
              if (cse_it != extraction_cse.end()) {
                temp_name = cse_it->second.first;
                // The emitter-only CSE creates a value-flow edge that is not
                // present in Color_plan. Keep both endpoints in the always
                // prefix so activation-region reordering cannot move the
                // declaration behind its use or into another guard scope.
                force_always_members.insert(cse_it->second.second);
                force_always_members.insert(member);
              } else {
                temp_name = absl::StrCat("__color_tmp_", temporary++);
                fout->append(absl::StrCat("  ",
                                          use_u ? "Slop_u<" : "Slop<",
                                          use_u ? width : eval_width,
                                          "> ",
                                          temp_name,
                                          " = ",
                                          landing,
                                          ";\n"));
                if (cse_extract) {
                  extraction_cse.emplace(cse_key, std::pair<std::string, size_t>{temp_name, member});
                }
              }
              if (use_u) {
                slop_u_values_.insert(output.get_class_index());
                slop_u_binding_width_.insert_or_assign(temp_name, width);
              }
              pin2var[output.get_class_index()] = temp_name;
              canonical_.insert(output.get_class_index());
              member_value      = temp_name;
              member_value_is_u = use_u;
            }
          }
        }

        if (!member_value.empty()) {
          local_version_value.insert_or_assign(member, member_value);
          if (member_value_is_u) {
            local_version_slop_u.insert(member);
          }
        }

        for (const size_t slot_index : direct_produced_slots[member]) {
          const auto& slot   = color_plan_->boundary_slots()[slot_index];
          const auto  source = find_site_output(version.base_site, slot.producer_port);
          if (source.is_invalid()) {
            continue;
          }
          std::string source_expr;
          const bool  has_extract = slot.producer_extract_hi > slot.producer_extract_lo;
          if (has_extract) {
            const std::string source_value = !member_value.empty() && slot.producer_port == version.output_port
                                                 ? member_value
                                                 : stored_operand(source, std::max(wbits_of(source), 1));
            source_expr                    = range_extract_expr(source_value,
                                                                slot.width,
                                                                direct_slot_is_u[slot_index],
                                                                slot.producer_extract_lo,
                                                                slot.producer_extract_hi);
          } else if (op == Ntype_op::Sub) {
            const auto sio = node.get_subnode_io();
            I(sio != nullptr);
            const auto sub_member = occurrence_sub_member(site);
            I(!sub_member.empty());
            for (const auto& decl : sio->get_output_pin_decls()) {
              if (decl.port_id == slot.producer_port) {
                const char* surface
                    = version.version == livehd::sim::Color_plan::State_version::pre_rise ? ".__last_out." : ".__out.";
                source_expr = sub_member + surface + cpp_port_path(decl.name);
                break;
              }
            }
            I(!source_expr.empty());
          } else if (!member_value.empty() && slot.producer_port == version.output_port) {
            // The value was materialized by this exact version just above. Use
            // that occurrence-local temporary directly: looking the definition
            // pin up again is ambiguous for multi-output cells (notably Memory,
            // whose canonical read pin need not appear in out_pins()).
            // Both sides of this assignment are answered by the emission that
            // ran, NOT by a second lookup through `source`: asking
            // `slop_u_values_` about the definition pin is the same ambiguous
            // multi-output lookup this branch exists to avoid (a Memory's
            // read_all pin is not the pin the temporary was bound to, so a
            // canonical whole-array value read as `Slop_u<W>` looked signed here
            // and was wrapped back through `Slop<W>` on its way into Slop_u
            // storage). `member_value_is_u` is the type the temporary was
            // actually declared with; `direct_slot_is_u` is the destination's
            // DECLARED storage type, the same authority `range_extract_expr`
            // above and the LLVM `from_packed_words` landing below consult.
            const int  source_width = wbits_of(source);
            const bool value_is_u   = slop_u_ && (member_value_is_u || slop_u_values_.contains(source.get_class_index()));
            if (source_width == static_cast<int>(slot.width)) {
              if (value_is_u) {
                source_expr
                    = direct_slot_is_u[slot_index] ? member_value : absl::StrCat("Slop<", slot.width, ">{", member_value, "}");
              } else if (is_unsign(source)) {
                source_expr = absl::StrCat("Slop<", slot.width, ">{", member_value, "}");
              } else {
                source_expr = member_value;
              }
            } else if (is_unsign(source)) {
              source_expr = append_zext(member_value, static_cast<int>(slot.width));
            } else {
              source_expr = absl::StrCat("Slop<", slot.width, ">{", member_value, "}");
            }
          } else if (op == Ntype_op::Memory && slot.producer_port == Ntype::Memory_readall_pid) {
            I(direct_memory(site) != nullptr);
            source_expr = absl::StrCat(occurrence_member(site), ".read_all()");
          } else {
            source_expr = operand(source, slot.width);
          }
          if (!has_extract && slot.producer_shift != 0) {
            source_expr = absl::StrCat("Slop<", slot.width, ">::shl_op(", source_expr, ", ", slot.producer_shift, ")");
          }
          if (slot.kind == livehd::sim::Color_plan::Boundary_kind::color_value) {
            if (tune_dirty()) {
              fout->append(absl::StrCat("  if (slop_update(", direct_slot_storage[slot_index], ", ", source_expr, ")) {\n"));
              emit_serial_dirty_consumers(slot_index, "    ");
              fout->append("  }\n");
            } else {
              fout->append(absl::StrCat("  ", direct_slot_storage[slot_index], " = ", source_expr, ";\n"));
            }
          } else if (slot.kind == livehd::sim::Color_plan::Boundary_kind::top_output) {
            const auto field = output_field(slot.public_port);
            I(!field.empty());
            const char* surface = slot.version == livehd::sim::Color_plan::State_version::pre_rise ? "__last_out." : "__out.";
            fout->append("  ", surface, field, " = ", source_expr, ";\n");
          } else if (slot.kind == livehd::sim::Color_plan::Boundary_kind::observation_output) {
            const auto destination = observation_output(slot);
            I(!destination.empty());
            fout->append("  ", destination, " = ", source_expr, ";\n");
          } else if (slot.kind == livehd::sim::Color_plan::Boundary_kind::observation_input) {
            const auto destination = observation_input(slot);
            I(!destination.empty());
            fout->append("  ", destination, " = ", source_expr, ";\n");
          }
        }
        capture_member_body();
      }

      // Build exact intra-color activation domains.  State-update predicates
      // are already the simulator's semantic clock/reset/conditional tests;
      // propagate a predicate backwards only while a producer is used
      // exclusively by that same domain. Shared values, public/cross-color
      // writes, side effects, and values needed to *compute* a predicate stay
      // in the unconditional prefix. This is the event-region shape used by
      // cycle simulators, but requires no new cone walk or size heuristic.
      struct Activation_domain {
        bool        assigned = false;
        bool        always   = false;
        std::string predicate;
      };
      std::vector<Activation_domain>      domains(direct_member_bodies.size());
      absl::flat_hash_map<size_t, size_t> local_position;
      local_position.reserve(direct_member_bodies.size());
      for (size_t position = 0; position < direct_member_bodies.size(); ++position) {
        local_position.emplace(direct_member_bodies[position].member, position);
      }
      // Same-color producers of the body at `consumer`: its plan predecessors,
      // plus -- for a compact-loop sibling -- the sibling whose body holds the
      // shared walk. Without that edge the walk followed only the OWNER's
      // consumers: xs rename_table's snapshot loop carries `payloads` (read by an
      // un-reset flop, unconditional) and `valids` (read by an async-reset flop,
      // `!reset`); the walk landed in the `!reset` block after the payload
      // capture, so the capture read the previous period's `__last_out` and,
      // during reset, the never-walked power-on one. Values depended on the
      // coarsening (dirty=off / fence=16 vs fence=0).
      const auto for_each_local_producer = [&](size_t consumer, const auto& visit) {
        const size_t member = direct_member_bodies[consumer].member;
        for (const size_t producer_member : direct_incoming_versions[member]) {
          if (const auto producer = local_position.find(producer_member); producer != local_position.end()) {
            visit(producer->second);
          }
        }
        if (const auto owner = compact_loop_owner_of.find(member); owner != compact_loop_owner_of.end()) {
          const auto position = local_position.find(owner->second);
          I(position != local_position.end() && position->second < consumer);  // the owner's body precedes its siblings
          visit(position->second);
        }
      };
      std::vector<size_t> local_outgoing(direct_member_bodies.size(), 0);
      for (size_t consumer = 0; consumer < direct_member_bodies.size(); ++consumer) {
        for_each_local_producer(consumer, [&](size_t producer) { ++local_outgoing[producer]; });
      }
      const auto make_always = [&](size_t position) {
        domains[position].assigned = true;
        domains[position].always   = true;
        domains[position].predicate.clear();
      };
      const auto merge_domain = [&](Activation_domain& destination, const Activation_domain& source) {
        if (!source.assigned || destination.always) {
          return;
        }
        if (!destination.assigned) {
          destination = source;
          return;
        }
        if (source.always || destination.predicate != source.predicate) {
          destination.always = true;
          destination.predicate.clear();
        }
      };

      // The first occurrence of a generated temporary is its declaration.
      // Guard expressions may name such temporaries; those declarations and
      // their full fan-in must precede the region's `if`.
      absl::flat_hash_map<std::string, size_t> temp_owner;
      absl::flat_hash_map<std::string, size_t> pending_owner;
      for (size_t position = 0; position < direct_member_bodies.size(); ++position) {
        const auto& body = direct_member_bodies[position];
        for (size_t at = body.text.find("__color_tmp_"); at != std::string::npos;) {
          size_t end = at + std::string_view("__color_tmp_").size();
          while (end < body.text.size() && std::isdigit(static_cast<unsigned char>(body.text[end]))) {
            ++end;
          }
          temp_owner.try_emplace(body.text.substr(at, end - at), position);
          at = body.text.find("__color_tmp_", end);
        }
        const auto& version = color_plan_->version_sites()[body.member];
        if (version.role == livehd::sim::Color_plan::Version_role::state_update) {
          pending_owner.emplace(occurrence_member(color_plan_->sites()[version.base_site]) + "_din", position);
        }
      }
      const auto force_expression_dependencies = [&](std::string_view expression, bool include_pending) {
        const auto identifier_char = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
        for (size_t begin = 0; begin < expression.size();) {
          while (begin < expression.size() && !identifier_char(expression[begin])) {
            ++begin;
          }
          size_t end = begin;
          while (end < expression.size() && identifier_char(expression[end])) {
            ++end;
          }
          if (end == begin) {
            break;
          }
          const std::string name(expression.substr(begin, end - begin));
          if (const auto owner = temp_owner.find(name); owner != temp_owner.end()) {
            force_always_members.insert(direct_member_bodies[owner->second].member);
          }
          if (include_pending) {
            if (const auto owner = pending_owner.find(name); owner != pending_owner.end()) {
              force_always_members.insert(direct_member_bodies[owner->second].member);
            }
          }
          begin = end;
        }
      };
      for (const auto& body : direct_member_bodies) {
        force_expression_dependencies(body.activation, true);
      }
      for (const auto& body : direct_reset_bodies) {
        force_expression_dependencies(body.predicate, true);
        force_expression_dependencies(body.text, false);
      }
      // A latch whose `_din` ANOTHER member of this color names (the D-driver
      // binding, a normalized ICG commit guard) must finish its reset override
      // BEFORE that reader runs. The grouped reset slow path runs after every
      // member body of the color, so a same-color reader sampled the pre-reset
      // `_din` while a reader in a later color saw the reset value: an ICG
      // latch with `= true` opened the gate during reset only when the
      // coarsener split it from the flop it gates -- values depended on
      // sim.tune.live_words even with dirty gating off. Inline that latch's
      // reset right after its own `_din` store and keep the latch in the
      // unconditional domain (an activation-guarded body is skipped during
      // reset, so the inlined override would never run).
      if (!direct_reset_bodies.empty() && !latch_din_member.empty()) {
        absl::flat_hash_set<size_t> read_in_color;  // latch members with a same-color `_din` reader
        for (const auto& body : direct_member_bodies) {
          const auto note = [&](size_t latch) {
            if (latch != body.member && local_position.contains(latch)) {
              read_in_color.insert(latch);
            }
          };
          latch_dins_named(body.text, note);
          latch_dins_named(body.activation, note);
        }
        if (!read_in_color.empty()) {
          std::vector<Direct_reset_body> grouped;
          grouped.reserve(direct_reset_bodies.size());
          for (auto& reset : direct_reset_bodies) {
            if (!read_in_color.contains(reset.member)) {
              grouped.push_back(std::move(reset));
              continue;
            }
            auto& owner = direct_member_bodies[local_position.at(reset.member)];
            absl::StrAppend(&owner.text,
                            "  if (",
                            reset.predicate,
                            ") [[unlikely]] {  // reset, inline: a same-color reader binds this `_din`\n",
                            reset.text,
                            "  }\n");
            force_always_members.insert(reset.member);
          }
          direct_reset_bodies = std::move(grouped);
        }
      }

      for (size_t position = 0; position < direct_member_bodies.size(); ++position) {
        const auto& body    = direct_member_bodies[position];
        const auto& version = color_plan_->version_sites()[body.member];
        if (force_always_members.contains(body.member)) {
          make_always(position);
          continue;
        }
        // A cross-color reader of this latch's `_din` (see
        // direct_pending_read_across_colors) is an external consumer too.
        bool external_write = direct_pending_read_across_colors[body.member];
        for (const size_t slot_index : direct_produced_slots[body.member]) {
          const auto& produced = color_plan_->boundary_slots()[slot_index];
          if (produced.kind == livehd::sim::Color_plan::Boundary_kind::state_pending) {
            continue;  // this member's OWN `_din`, written under its activation -- not an external boundary
          }
          if (!direct_write_expr(produced, slot_index).empty()) {
            external_write = true;
            break;
          }
        }
        if (external_write
            || (local_outgoing[position] == 0 && version.role != livehd::sim::Color_plan::Version_role::state_update)) {
          make_always(position);
        } else if (version.role == livehd::sim::Color_plan::Version_role::state_update) {
          domains[position].assigned  = true;
          domains[position].always    = body.activation.empty();
          domains[position].predicate = body.activation;
        }
      }
      for (size_t consumer = direct_member_bodies.size(); consumer-- > 0;) {
        if (!domains[consumer].assigned) {
          make_always(consumer);  // incomplete/side-effect dependency: conservative
        }
        for_each_local_producer(consumer, [&](size_t producer) { merge_domain(domains[producer], domains[consumer]); });
      }

      for (size_t position = 0; position < direct_member_bodies.size(); ++position) {
        if (!domains[position].always) {
          continue;
        }
        auto&        body   = direct_member_bodies[position];
        const size_t member = body.member;
        // Unsharded commits only: a shard's commit runs only when its mask
        // has a bit, so an always-commit member there still needs its bit.
        if (!tune_dirty() && direct_commit_shards.empty() && member != livehd::sim::Color_plan::invalid_index
            && direct_color_condition_phase[color_index] == livehd::sim::Color_plan::invalid_index
            && color_plan_->version_sites()[member].role == livehd::sim::Color_plan::Version_role::state_update
            && direct_state_commit_flag_of_member[member] != livehd::sim::Color_plan::invalid_index
            && type_op_of(color_plan_->sites()[color_plan_->version_sites()[member].base_site].node.base_node())
                   != Ntype_op::Memory) {
          const std::string flag
              = (member < direct_commit_shard_of_member.size()
                 && direct_commit_shard_of_member[member] != livehd::sim::Color_plan::invalid_index)
                    ? absl::StrCat("  __rt.__commit_shard_mask[",
                                   direct_commit_shard_of_member[member],
                                   "] |= uint64_t{1} << ",
                                   direct_commit_shard_bit_of_member[member],
                                   ";\n")
                    : absl::StrCat("  __rt.__state_commit[", direct_state_commit_flag_of_member[member], "] = true;\n");
          if (const auto at = body.text.find(flag); at != std::string::npos && body.text.find(flag, at + 1) == std::string::npos) {
            body.text.erase(at, flag.size());
            direct_always_commit[member] = true;
          }
        }
        fout->append(body.text);
      }
      std::vector<std::string>                 activation_order;
      absl::flat_hash_map<std::string, size_t> activation_index;
      std::vector<std::vector<size_t>>         activation_members;
      for (size_t position = 0; position < direct_member_bodies.size(); ++position) {
        if (domains[position].always) {
          continue;
        }
        I(domains[position].assigned && !domains[position].predicate.empty());
        const auto [it, inserted] = activation_index.emplace(domains[position].predicate, activation_order.size());
        if (inserted) {
          activation_order.push_back(domains[position].predicate);
          activation_members.emplace_back();
        }
        activation_members[it->second].push_back(position);
      }
      // No branch hint: an activation is a clock edge or a data-driven enable,
      // and with fused captures the block holds the flop's whole next-state
      // cone. `[[unlikely]]` made clang optimize that hot cone as cold code
      // (br_enc_gray2bin: +31% instructions).
      for (size_t activation = 0; activation < activation_order.size(); ++activation) {
        fout->append("  if (", activation_order[activation], ") {\n");
        for (const size_t position : activation_members[activation]) {
          fout->append(direct_member_bodies[position].text);
        }
        fout->append("  }\n");
      }
      std::vector<std::string>                 reset_order;
      absl::flat_hash_map<std::string, size_t> reset_index;
      std::vector<std::vector<size_t>>         reset_members;
      for (size_t position = 0; position < direct_reset_bodies.size(); ++position) {
        const auto [it, inserted] = reset_index.emplace(direct_reset_bodies[position].predicate, reset_order.size());
        if (inserted) {
          reset_order.push_back(direct_reset_bodies[position].predicate);
          reset_members.emplace_back();
        }
        reset_members[it->second].push_back(position);
      }
      for (size_t reset = 0; reset < reset_order.size(); ++reset) {
        fout->append("  if (", reset_order[reset], ") [[unlikely]] {  // reset slow path\n");
        for (const size_t position : reset_members[reset]) {
          fout->append(direct_reset_bodies[position].text);
        }
        fout->append("  }\n");
      }
      for (const auto& predicate : activation_order) {
        record_state_q_guard_readers(predicate, color_index);
      }
      for (const auto& predicate : reset_order) {
        record_state_q_guard_readers(predicate, color_index);
      }
      close_case();
      fout->append("    return;\n  }\n");
    }
    resolve_latch_din_marks();  // every color is emitted: its `_din` readers are known
    if (color_eval_outputs.empty()) {
      fout->append("  default: assert(false && \"invalid color index\"); return;\n  }\n}\n");
    } else {
      for (auto& out : color_eval_outputs) {
        out->append("  default: assert(false && \"color index routed to wrong evaluator shard\"); return;\n  }\n}\n");
      }
      fout = root_fout;
    }

    // State-update colors only sample their pending value and edge predicate.
    // The phase barrier performs the small commit after every old-Q reader in
    // that edge class has finished, preserving coincident-edge/NBA semantics
    // without a second evaluation of any color.
    if (!odir.empty()) {
      absl::flat_hash_set<std::string> expected;
      for (size_t shard = 0; shard < direct_commit_shards.size(); ++shard) {
        expected.insert(absl::StrCat(fstem, ".color-commit-", shard, ".cpp"));
      }
      std::error_code   ec;
      const std::string prefix = fstem + ".color-commit-";
      for (const auto& entry : std::filesystem::directory_iterator(std::string(odir), ec)) {
        const auto filename = entry.path().filename().string();
        if (entry.is_regular_file() && filename.starts_with(prefix) && filename.ends_with(".cpp") && !expected.contains(filename)) {
          std::filesystem::remove(entry.path(), ec);
        }
      }
    }
    const auto emit_commit_member = [&](size_t member) {
      const auto&  version   = color_plan_->version_sites()[member];
      const size_t color_idx = direct_version_color[member];
      I(color_idx != livehd::sim::Color_plan::invalid_index);
      const auto& site  = color_plan_->sites()[version.base_site];
      const auto  state = occurrence_member(site);
      const auto  op    = type_op_of(site.node.base_node());
      I(!state.empty());
      I(member < direct_state_commit_flag_of_member.size()
        && direct_state_commit_flag_of_member[member] != livehd::sim::Color_plan::invalid_index);
      // Without dirty gating a scalar flop commit reports nothing, so spell it
      // as a SELECT, not a branch: its flag follows the captured enable, which
      // a data-driven design toggles unpredictably, and the host compiler will
      // not if-convert a conditional store for us (axi_demux: 780 -> 704 ms).
      if (!tune_dirty() && op != Ntype_op::Memory) {
        const auto pipe_min = get_driver(find_sink_pin(site.node.base_node(), "pipe_min"));
        const bool scalar   = !pipe_min.is_const() || const_of(pipe_min).to_just_i64() <= 1;
        if (scalar) {
          if (direct_always_commit[member]) {
            fout->append("  ", state, " = ", state, "_din;\n");
          } else if (direct_commit_shards.empty()) {
            fout->append(absl::StrCat("  ",
                                      state,
                                      " = __rt.__state_commit[",
                                      direct_state_commit_flag_of_member[member],
                                      "] ? ",
                                      state,
                                      "_din : ",
                                      state,
                                      ";\n"));
          } else {
            fout->append(absl::StrCat("  ",
                                      state,
                                      " = (__rt.__commit_shard_mask[",
                                      direct_commit_shard_of_member[member],
                                      "] & (uint64_t{1} << ",
                                      direct_commit_shard_bit_of_member[member],
                                      ")) != 0 ? ",
                                      state,
                                      "_din : ",
                                      state,
                                      ";\n"));
          }
          return;
        }
      }
      if (direct_always_commit[member]) {
        fout->append("  {\n");  // captured every period: the flag would always be set
      } else if (direct_commit_shards.empty()) {
        fout->append("  if (__rt.__state_commit[", std::to_string(direct_state_commit_flag_of_member[member]), "]) {\n");
      } else {
        I(member < direct_commit_shard_of_member.size()
          && direct_commit_shard_of_member[member] != livehd::sim::Color_plan::invalid_index);
        fout->append("  if ((__rt.__commit_shard_mask[",
                     std::to_string(direct_commit_shard_of_member[member]),
                     "] & (uint64_t{1} << ",
                     std::to_string(direct_commit_shard_bit_of_member[member]),
                     ")) != 0) {\n");
      }
      if (op == Ntype_op::Memory) {
        const auto* memory = find_local_mem(version.base_site);
        I(memory != nullptr);
        fout->append("    bool __changed = false;\n");
        if (memory->has_reset_stage()) {
          const auto pending = whole_pending(version.base_site);
          fout->append(absl::StrCat("    if (", pending, "_cen) __changed |= ", state, ".apply_update(", pending, "_din) != 0;\n"));
        }
        for (const auto& port : memory->ports) {
          if (!port.rd || memory->type != 1 || port.addr.is_invalid()) {
            continue;
          }
          const auto q = absl::StrCat(state, "_q", port.rdidx);
          fout->append("    __changed |= slop_update(", q, ", ", q, "_din) != 0;\n");
        }
        fout->append("    __changed |= ", state, ".tick() != 0;\n");
        fout->append("    __rt.__color_state_changed |= __changed;\n");
      } else {
        int pipe_depth = 1;
        if (const auto pipe_min = get_driver(find_sink_pin(site.node.base_node(), "pipe_min")); pipe_min.is_const()) {
          pipe_depth = std::max<int>(1, static_cast<int>(const_of(pipe_min).to_just_i64()));
        }
        if (pipe_depth == 1) {
          // The dynamic commit flag is emitted only after proving D != Q, so a
          // scalar state commit need not repeat the same identical() test.
          fout->append("    ", state, " = ", state, "_din;\n");
          if (tune_dirty()) {
            fout->append("    constexpr bool __changed = true;\n");
          }
        } else {
          fout->append("    bool __changed = false;\n");
          for (int index = 0; index < pipe_depth - 1; ++index) {
            const auto stage = absl::StrCat(state, "_p", index);
            fout->append("    __changed |= slop_update(", stage, ", ", stage, "_din) != 0;\n");
          }
          fout->append("    __changed |= slop_update(", state, ", ", state, "_din) != 0;\n");
        }
        // Without dirty gating nothing reads a per-state change bit:
        // __color_run marks the period changed once (see below).
        if (tune_dirty() || pipe_depth > 1) {
          fout->append("    __rt.__color_state_changed |= __changed;\n");
        }
      }
      if (tune_dirty()) {
        fout->append("    if (__changed) {\n");
        fout->append("      ", dirty_mark(color_idx), "\n");
        if (op == Ntype_op::Memory) {
          emit_serial_dirty_site_colors(version.base_site, "      ");
        }
        fout->append("    }\n");
        for (const size_t state_slot : direct_state_current_slots[version.base_site]) {
          const auto& current = color_plan_->boundary_slots()[state_slot];
          I(current.kind == livehd::sim::Color_plan::Boundary_kind::state_current && current.owner_site == version.base_site);
          fout->append("    if (__changed) {\n");
          emit_serial_dirty_consumers(state_slot, "      ");
          fout->append("    }\n");
        }
        if (version.base_site < state_q_guard_readers.size() && !state_q_guard_readers[version.base_site].empty()) {
          fout->append("    if (__changed) {  // guards that name this state's Q (record_state_q_guard_readers)\n");
          for (const size_t reader : state_q_guard_readers[version.base_site]) {
            fout->append("      ", dirty_mark(reader), "\n");
          }
          fout->append("    }\n");
        }
      }
      fout->append("  }\n");
    };

    // The dirty words a commit member can mark: its own color, a memory's site
    // colors, the consumers of its state-current slots, and the colors whose
    // guards name its Q (state_q_guard_readers) -- the same targets
    // emit_commit_member emits, known up front so the accumulator locals can
    // be declared before the members that OR into them.
    const auto commit_member_dirty_words = [&](size_t member, absl::flat_hash_set<size_t>& words) {
      if (!tune_dirty()) {
        return;
      }
      const auto& version = color_plan_->version_sites()[member];
      words.insert(dirty_word(direct_version_color[member]));
      const auto& site = color_plan_->sites()[version.base_site];
      // Same bounds guard emit_serial_dirty_site_colors applies to this index:
      // the pre-scan must not read further than the emission it mirrors.
      if (type_op_of(site.node.base_node()) == Ntype_op::Memory && version.base_site < direct_site_colors.size()) {
        for (const size_t color_index : direct_site_colors[version.base_site]) {
          words.insert(dirty_word(color_index));
        }
      }
      for (const size_t state_slot : direct_state_current_slots[version.base_site]) {
        for (const auto& consumer : color_plan_->boundary_slots()[state_slot].consumers) {
          if (consumer.color != livehd::sim::Color_plan::invalid_index) {
            words.insert(dirty_word(consumer.color));
          }
        }
      }
      if (version.base_site < state_q_guard_readers.size()) {
        for (const size_t reader : state_q_guard_readers[version.base_site]) {
          words.insert(dirty_word(reader));
        }
      }
    };
    const auto emit_commit_members = [&](const std::vector<size_t>& members, std::string_view indent) {
      absl::flat_hash_set<size_t> words;
      for (const size_t member : members) {
        commit_member_dirty_words(member, words);
      }
      std::vector<size_t> ordered_words(words.begin(), words.end());
      std::ranges::sort(ordered_words);
      for (const size_t word : ordered_words) {
        fout->append(indent, "uint64_t __dacc_", std::to_string(word), " = 0;\n");
      }
      dirty_accumulator.emplace();
      for (const size_t member : members) {
        emit_commit_member(member);
      }
      for (const size_t word : *dirty_accumulator) {
        I(words.contains(word));  // the pre-scan must cover every mark the members emit
      }
      dirty_accumulator.reset();
      for (const size_t word : ordered_words) {
        fout->append(indent, "__rt.__color_dirty[", std::to_string(word), "] |= __dacc_", std::to_string(word), ";\n");
      }
    };

    fout->append("void ", mod, "::__color_commit(std::size_t __slot) {\n");
    if (direct_commit_shards.empty()) {
      fout->append("  assert(__color_runtime);\n  [[maybe_unused]] auto& __rt = *__color_runtime;\n");
      for (const auto slot :
           {livehd::sim::Color_plan::Execution_slot::rise_commit, livehd::sim::Color_plan::Execution_slot::fall_commit}) {
        fout->append("  if (__slot == ", std::to_string(static_cast<size_t>(slot)), ") {\n");
        std::vector<size_t> members;
        for (size_t member = 0; member < color_plan_->version_sites().size(); ++member) {
          const auto& version = color_plan_->version_sites()[member];
          if (version.role == livehd::sim::Color_plan::Version_role::state_update
              && livehd::sim::Color_plan::commit_slot_of(version) == slot) {
            members.push_back(member);
          }
        }
        emit_commit_members(members, "    ");
        fout->append("    return;\n  }\n");
      }
      fout->append("}\n");
    } else {
      for (const auto slot :
           {livehd::sim::Color_plan::Execution_slot::rise_commit, livehd::sim::Color_plan::Execution_slot::fall_commit}) {
        fout->append("  if (__slot == ", std::to_string(static_cast<size_t>(slot)), ") {\n");
        for (size_t shard = 0; shard < direct_commit_shards.size(); ++shard) {
          if (direct_commit_shards[shard].slot == slot) {
            fout->append("    if (__color_runtime->__commit_shard_mask[", std::to_string(shard), "] != 0) {\n");
            fout->append("      __color_commit_part_", std::to_string(shard), "();\n");
            fout->append("      __color_runtime->__commit_shard_mask[", std::to_string(shard), "] = 0;\n");
            fout->append("    }\n");
          }
        }
        fout->append("    return;\n  }\n");
      }
      fout->append("}\n");

      for (size_t shard = 0; shard < direct_commit_shards.size(); ++shard) {
        const std::string filename = absl::StrCat(fstem, ".color-commit-", shard, ".cpp");
        auto              out      = open_out(filename);
        out->append("// Generated simulator color-commit shard. Do not edit.\n#include \"",
                    fstem,
                    ".color-runtime.hpp\"\n#include <cassert>\n\n");
        fout = out;
        fout->append("void ", mod, "::__color_commit_part_", std::to_string(shard), "() {\n");
        fout->append("  assert(__color_runtime);\n  [[maybe_unused]] auto& __rt = *__color_runtime;\n");
        emit_commit_members(direct_commit_shards[shard].members, "  ");
        fout->append("}\n");
      }
      fout = root_fout;
    }

    const auto emit_color_refresh_inputs = [&] {
      if (tune_dirty()) {
        const auto emit_top_input_slot = [&](size_t slot_index, std::string_view indent) {
          const auto& slot  = color_plan_->boundary_slots()[slot_index];
          const auto  field = input_field(slot.public_port);
          I(!field.empty());
          std::string current = direct_read_expr(slot, slot_index);
          I(!current.empty());
          if (slot.producer_extract_hi > slot.producer_extract_lo) {
            // The canonical extract already landed AT the slot width:
            // `direct_read_expr` spelled it `Slop_u<W>::get_mask_op_opt(...)`,
            // and `Slop_u<W>::from_proven(Slop_u<W>)` is the identity. Only the
            // lazy extract still needs landing -- it comes back on the
            // `Slop<W+1>` carrier and must be re-signed down to the prev
            // shadow's `Slop<W>` (slop_update refuses a wider source).
            if (!slop_u_ || !slot.unsign) {
              current = absl::StrCat("Slop<", slot.width, ">{", current, "}");
            }
          } else {
            for (const auto& io : ios) {
              if (!io.is_input || io.port_id != static_cast<uint32_t>(slot.public_port)
                  || static_cast<uint32_t>(io.bits) == slot.width) {
                continue;
              }
              current = slot.unsign ? append_zext(std::move(current), static_cast<int>(slot.width))
                                    : absl::StrCat("Slop<", slot.width, ">{", current, "}");
              break;
            }
          }
          fout->append(absl::StrCat(indent, "if (slop_update(", direct_input_prev_storage[slot_index], ", ", current, ")) {\n"));
          fout->append(indent, "  __rt.__color_input_changed = true;\n");
          emit_serial_dirty_consumers(slot_index, absl::StrCat(indent, "  "));
          fout->append(indent, "}\n");
        };
        std::map<uint32_t, std::vector<size_t>> gated_slots;
        std::map<uint32_t, std::vector<size_t>> versioned_slots;
        for (size_t slot_index = 0; slot_index < color_plan_->boundary_slots().size(); ++slot_index) {
          const auto& slot = color_plan_->boundary_slots()[slot_index];
          if (slot.kind != livehd::sim::Color_plan::Boundary_kind::top_input) {
            continue;
          }
          if (direct_port_ver_storage.contains(static_cast<uint32_t>(slot.public_port))) {
            versioned_slots[static_cast<uint32_t>(slot.public_port)].push_back(slot_index);
            continue;
          }
          const auto gate = direct_port_gate_storage.find(static_cast<uint32_t>(slot.public_port));
          if (gate != direct_port_gate_storage.end() && slot.producer_extract_hi > slot.producer_extract_lo) {
            gated_slots[gate->first].push_back(slot_index);
            continue;
          }
          emit_top_input_slot(slot_index, "  ");
        }
        // One whole-port compare guards every lane of a sliced port: the lane
        // shadows only move when the port did, so an unchanged port skips them
        // all. The port shadow and the lane shadows advance in lockstep (both
        // start zeroed with the port), which keeps the guard exact.
        for (const auto& [port, slots] : gated_slots) {
          const auto field = input_field(port);
          I(!field.empty());
          fout->append(absl::StrCat("  if (slop_update(", direct_port_gate_storage.at(port), ", __in.", field, ")) {\n"));
          for (const size_t slot_index : slots) {
            emit_top_input_slot(slot_index, "    ");
          }
          fout->append("  }\n");
        }
        // A versioned port: every writer of `__in.<field>` bumped its version
        // counter on change, so one integer compare stands in for the bus scan.
        for (const auto& [port, slots] : versioned_slots) {
          const auto field = input_field(port);
          I(!field.empty());
          const auto ver = absl::StrCat("__in.", ver_path(field));
          fout->append(absl::StrCat("  if (",
                                    direct_port_ver_storage.at(port),
                                    " != ",
                                    ver,
                                    ") {\n    ",
                                    direct_port_ver_storage.at(port),
                                    " = ",
                                    ver,
                                    ";\n"));
          for (const size_t slot_index : slots) {
            emit_top_input_slot(slot_index, "    ");
          }
          fout->append("  }\n");
        }
      }
      for (const auto& slot : color_plan_->boundary_slots()) {
        if ((slot.kind == livehd::sim::Color_plan::Boundary_kind::top_output
             || slot.kind == livehd::sim::Color_plan::Boundary_kind::observation_output)
            && slot.producer_version == livehd::sim::Color_plan::invalid_index) {
          const auto destination = direct_write_expr(slot, 0);
          I(!destination.empty());
          const auto guard = "";
          if (!slot.literal.empty()) {
            // Runs every cycle: take the create_integer fast path when the
            // simulated value fits an int64 (see sim_const_expr). A `?` bit
            // survives unless sim.unknown_zero, and sim_const_expr then draws it
            // once behind a `static const` -- never per cycle.
            auto literal = slot.literal;
            if (unknown_zero_) {
              std::replace(literal.begin(), literal.end(), '?', '0');
            }
            fout->append(absl::StrCat("  ", guard, destination, " = ", sim_const_expr(literal, std::to_string(slot.width)), ";\n"));
          } else {
            const auto field = input_field(slot.producer_port);
            I(!field.empty());
            fout->append(absl::StrCat("  ",
                                      guard,
                                      destination,
                                      " = ",
                                      slot.unsign ? absl::StrCat("__in.", field, ".zext_to<", slot.width, ">()")
                                                  : absl::StrCat("Slop<", slot.width, ">{__in.", field, "}"),
                                      ";\n"));
          }
          continue;
        }
        if (slot.kind != livehd::sim::Color_plan::Boundary_kind::observation_input
            || slot.producer_version != livehd::sim::Color_plan::invalid_index) {
          continue;
        }
        const auto destination = observation_input(slot);
        I(!destination.empty());
        if (!slot.literal.empty()) {
          auto literal = slot.literal;
          if (unknown_zero_) {
            std::replace(literal.begin(), literal.end(), '?', '0');
          }
          fout->append(absl::StrCat("  ", destination, " = ", sim_const_expr(literal, std::to_string(slot.width)), ";\n"));
        } else {
          const auto field = input_field(slot.producer_port);
          I(!field.empty());
          fout->append(absl::StrCat("  ", destination, " = __in.", field, ".zext_to<", slot.width, ">();\n"));
        }
      }
    };

    // Runtime allocation and shared-kernel binding are lazy and persistent.
    // Copies rebuild because the owner pointer changes.
    fout->append("void ", mod, "::__color_prepare_runtime() {\n");
    fout->append(
        "  if (__color_runtime && __color_runtime->owner == this) return;\n"
        "  __color_runtime = std::make_shared<__Color_runtime>();\n"
        "  __color_runtime->owner = this;\n"
        "  auto runtime = __color_runtime;\n"
        "  [[maybe_unused]] auto& __rt = *runtime;\n");
    // A constant packed lane owns a color_value slot with NO producer version,
    // so nothing in the per-version writer loop ever assigns its storage. The
    // inline reader short-circuits on `slot.literal`, but a shared/LLVM kernel
    // binds the STORAGE address (see the binding loop below) and would read a
    // default-constructed zero instead of the constant. Seed it once here: the
    // value never changes, and the runtime object is rebuilt whenever the owner
    // does.
    for (size_t slot_index = 0; slot_index < color_plan_->boundary_slots().size(); ++slot_index) {
      const auto& slot = color_plan_->boundary_slots()[slot_index];
      if (slot.kind != livehd::sim::Color_plan::Boundary_kind::color_value || slot.literal.empty()) {
        continue;
      }
      auto literal = slot.literal;
      if (unknown_zero_) {
        std::replace(literal.begin(), literal.end(), '?', '0');
      }
      fout->append(absl::StrCat("  ",
                                direct_slot_storage[slot_index],
                                " = ",
                                sim_const_expr(literal, std::to_string(slot.width)),
                                ";  // fixed packed lane\n"));
    }
    std::vector<std::shared_ptr<File_output>> binding_outputs;
    std::vector<size_t>                       binding_shard_of_color(direct_kernel.size(), livehd::sim::Color_plan::invalid_index);
    absl::flat_hash_set<std::string>          expected_binding_files;
    for (size_t shard = 0; shard < direct_binding_shards.size(); ++shard) {
      const auto filename = absl::StrCat(fstem, ".color-bind-", shard, ".cpp");
      expected_binding_files.insert(filename);
      auto out = open_out(filename);
      out->append("// Generated simulator kernel bindings. Do not edit.\n#include \"",
                  fstem,
                  ".color-runtime.hpp\"\n#include \"",
                  kernel_header_name,
                  "\"\n\n");
      out->append("void ",
                  mod,
                  "::__color_bind_part_",
                  std::to_string(shard),
                  "() {\n",
                  "  auto runtime = __color_runtime;\n  [[maybe_unused]] auto& __rt = *runtime;\n");
      binding_outputs.push_back(out);
      for (const auto color : direct_binding_shards[shard]) {
        binding_shard_of_color[color] = shard;
      }
      fout->append("  __color_bind_part_", std::to_string(shard), "();\n");
    }
    if (!odir.empty()) {
      std::error_code ec;
      const auto      prefix = fstem + ".color-bind-";
      for (const auto& entry : std::filesystem::directory_iterator(std::string(odir), ec)) {
        const auto filename = entry.path().filename().string();
        if (entry.is_regular_file(ec) && filename.starts_with(prefix) && filename.ends_with(".cpp")
            && !expected_binding_files.contains(filename)) {
          std::filesystem::remove(entry.path(), ec);
        }
      }
    }
    for (size_t color_index = 0; color_index < direct_kernel.size(); ++color_index) {
      const auto* kernel = direct_kernel[color_index];
      if (kernel == nullptr) {
        continue;
      }
      const auto shard = binding_shard_of_color[color_index];
      fout             = shard == livehd::sim::Color_plan::invalid_index ? root_fout : binding_outputs[shard];
      const auto& abi  = direct_abi[color_index];
      fout->append("    runtime->__color_kernel[",
                   std::to_string(color_index),
                   "] = &",
                   kernel_instance_name(color_index, kernel->signature),
                   ";\n");
      fout->append("    runtime->__color_bindings[", std::to_string(color_index), "] = {");
      bool first_binding = true;
      for (const auto& read : abi.reads) {
        const auto& slot = color_plan_->boundary_slots()[read.slot_index];
        // The BINDING passes the storage OBJECT's address, not the read
        // spelling: a Slop_u's `.raw()` hands back a const reference, so `&`
        // of it is a `const Carrier*` and the void* cast is ill-formed. The
        // kernel below casts back to the matching type.
        const auto  expr = (slot.kind == livehd::sim::Color_plan::Boundary_kind::color_value)
                               ? direct_slot_storage[read.slot_index]
                               : direct_read_expr(slot, read.slot_index);
        I(!expr.empty());
        fout->append(first_binding ? "" : ",", "static_cast<void*>(&", expr, ")");
        first_binding = false;
      }
      for (const auto& write : abi.writes) {
        const auto& slot = color_plan_->boundary_slots()[write.slot_index];
        const auto  expr = direct_write_expr(slot, write.slot_index);
        I(!expr.empty());
        fout->append(first_binding ? "" : ",", "static_cast<void*>(&", expr, ")");
        first_binding = false;
      }
      fout->append("};\n");
    }
    for (auto& out : binding_outputs) {
      out->append("}\n");
    }
    fout = root_fout;
    fout->append("}\n");
    fout->append("void ", mod, "::__color_refresh_inputs() {\n");
    fout->append("  assert(__color_runtime);\n  [[maybe_unused]] auto& __rt = *__color_runtime;\n");
    emit_color_refresh_inputs();
    fout->append("}\n");

    fout->append("void ", mod, "::__color_reset_settle() {\n");
    fout->append(
        "  __color_prepare_runtime();\n"
        "  [[maybe_unused]] auto& __rt = *__color_runtime;\n"
        "  __rt.__color_state_changed = false;\n");
    if (direct_commit_shards.empty()) {
      fout->append("  __rt.__state_commit.fill(false);\n");
    } else {
      fout->append("  __rt.__commit_shard_mask.fill(0);\n");
    }
    fout->append("  __color_refresh_inputs();\n");
    if (tune_dirty()) {
      fout->append("  __rt.__color_dirty.fill(~uint64_t{0});  // reset/load invalidates the serial activation cache\n");
    }
    std::vector<bool> reset_required(color_plan_->colors().size(), false);
    for (size_t color = 0; color < color_plan_->colors().size(); ++color) {
      reset_required[color] = color_plan_->colors()[color].slot == livehd::sim::Color_plan::Execution_slot::post_fall_publish;
    }
    bool reset_changed = true;
    while (reset_changed) {
      reset_changed = false;
      for (const auto& edge : color_plan_->color_dependencies()) {
        if (reset_required[edge.consumer] && !reset_required[edge.producer] && pure_data_color(edge.producer)) {
          reset_required[edge.producer] = true;
          reset_changed                 = true;
        }
      }
    }
    for (const size_t color : color_plan_->colors_in_execution_order()) {
      if (reset_required[color]) {
        fout->append("  __color_eval(", std::to_string(color), ");\n");
      }
    }
    fout->append("  __last_out = __out;\n}\n");

    const auto emit_color_period_finish = [&] {
      absl::flat_hash_set<std::string> advanced;
      for (const auto& flop : flops) {
        if (flop.prev_member.empty() || !advanced.insert(flop.prev_member).second) {
          continue;
        }
        auto clock = flop.sec_clock;
        for (int hops = 0; hops < 8 && !clock.is_invalid() && !livehd::graph_util::is_graph_input_pin(clock); ++hops) {
          const auto node = clock.get_master_node();
          if (type_op_of(node) != Ntype_op::Get_mask && type_op_of(node) != Ntype_op::Sext
              && type_op_of(node) != Ntype_op::Set_mask) {
            break;
          }
          clock = livehd::graph_util::first_value_driver(node);
        }
        I(!clock.is_invalid() && livehd::graph_util::is_graph_input_pin(clock));
        const auto field = input_field(clock.get_port_id());
        I(!field.empty());
        fout->append("  ", flop.prev_member, " = (__in.", field, ").is_known_true();\n");
      }
      if (vcd_on) {
        fout->append("  __vcd_publish_period();\n");
      }
      fout->append(
          "  const bool __any_state_changed = __rt.__color_state_changed;\n"
          "  __gen += __any_state_changed ? 1 : 0;\n");
      // A full period with stable inputs and no committed state change is a
      // fixed point, including for stateful designs: the same state and inputs
      // produce the same pending values on the next period. Runtime-random and
      // VCD modes remain active because they have observable per-period work.
      if (tune_dirty() && !color_plan_->summary().runtime_random && !vcd_on) {
        fout->append("  __rt.__color_quiescent = !__any_state_changed;\n");
      } else {
        fout->append("  __rt.__color_quiescent = false;\n");
      }
      if (vcd_on) {
        fout->append("  __vcd_tick += (__clk_ratio > 0 ? __clk_ratio : 1);\n");
      }
    };

    const auto& serial_colors = color_plan_->colors_in_execution_order();

    // `snapshot` non-empty = the caller holds this color's dirty word in that
    // local (grouped emission): TEST the bit there -- one word load stands in
    // for a load per color -- but still CLEAR it in memory at the point the
    // color actually runs. The clear cannot be hoisted to the end of the group:
    // a color whose condition-phase predicate is false never runs and must keep
    // its activation for the next period, and a color that re-marks itself
    // while running must keep the mark it just wrote.
    // THE one definition of "this color is gated by its activation bit", shared
    // by the emitter and the word grouping below: a color grouped by one and
    // emitted unguarded by the other would never test its bit.
    const auto dirty_guarded        = [&](size_t color) { return tune_dirty() && !direct_random_color[color]; };
    const auto emit_scheduled_color = [&](size_t color, std::string_view snapshot = {}) {
      const size_t condition_phase = direct_color_condition_phase[color];
      if (condition_phase != livehd::sim::Color_plan::invalid_index) {
        fout->append("  if (", direct_condition_phases[condition_phase].predicate, ") {\n");
      }
      const std::string indent      = condition_phase == livehd::sim::Color_plan::invalid_index ? "  " : "    ";
      const bool        dirty_guard = dirty_guarded(color);
      if (dirty_guard) {
        const auto test = snapshot.empty() ? dirty_test(color) : absl::StrCat("(", snapshot, " & ", dirty_mask(color), ") != 0");
        fout->append(indent, "if (", test, ") {\n");
        fout->append(indent, "  ", dirty_clear(color), "\n");
      }
      const std::string call_indent = dirty_guard ? indent + "  " : indent;
      if (direct_kernel[color] != nullptr) {
        emit_direct_kernel_call(color, call_indent);
      } else {
        fout->append(call_indent, "__color_eval(", std::to_string(color), ");\n");
      }
      if (dirty_guard) {
        fout->append(indent, "}\n");
      }
      if (condition_phase != livehd::sim::Color_plan::invalid_index) {
        fout->append("  }\n");
      }
    };
    // Colors in execution order, grouped by dirty word: a run of dirty-guarded
    // colors sharing one word is skipped with a single word test when none of
    // them is active. An idle group then costs ONE load and ONE test; only the
    // colors that actually run pay the per-color clear, exactly as the
    // ungrouped form does.
    const auto emit_scheduled_colors = [&](const std::vector<size_t>& colors) {
      const auto& guarded = dirty_guarded;
      for (size_t begin = 0; begin < colors.size();) {
        size_t end = begin;
        if (guarded(colors[begin])) {
          while (end < colors.size() && guarded(colors[end]) && dirty_word(colors[end]) == dirty_word(colors[begin])) {
            ++end;
          }
        } else {
          end = begin + 1;
        }
        const bool grouped = end - begin > 1;
        if (!grouped) {
          emit_scheduled_color(colors[begin]);
          begin = end;
          continue;
        }
        const size_t word       = dirty_word(colors[begin]);
        uint64_t     group_mask = 0;
        for (size_t index = begin; index < end; ++index) {
          group_mask |= uint64_t{1} << (direct_color_pos[colors[index]] % 64);
        }
        const auto mask_literal = absl::StrCat("0x", absl::Hex(group_mask), "ULL");
        fout->append("  { uint64_t __w = __rt.__color_dirty[", std::to_string(word), "];\n");
        fout->append("  if ((__w & ", mask_literal, ") != 0) {\n");
        // An evaluated color marks its consumers in memory; later colors of
        // this word must see those marks, so fold the word back in after each
        // color (a plain load: the store it may depend on only happened when
        // that color actually ran). Bits already consumed stay set in `__w`,
        // which is harmless -- each color is tested exactly once.
        for (size_t index = begin; index < end; ++index) {
          emit_scheduled_color(colors[index], "__w");
          if (index + 1 < end) {
            fout->append("  __w |= __rt.__color_dirty[", std::to_string(word), "];\n");
          }
        }
        fout->append("  } }\n");
        begin = end;
      }
    };
    // direct_run_shards is only populated when direct_color_eval_shards (and so
    // color_eval_outputs) is non-empty; assert that coupling rather than trust
    // it at a modulo two thousand lines away from where it is established.
    I(direct_run_shards.empty() || !color_eval_outputs.empty());
    for (size_t shard = 0; shard < direct_run_shards.size() && !color_eval_outputs.empty(); ++shard) {
      fout = color_eval_outputs[shard % color_eval_outputs.size()];
      fout->append("void ", mod, "::__color_run_part_", std::to_string(shard), "() {\n");
      fout->append("  [[maybe_unused]] auto& __rt = *__color_runtime;\n");
      emit_scheduled_colors(direct_run_shards[shard]);
      fout->append("}\n");
    }
    fout = root_fout;

    if (color_plan_->colors().size() <= 256) {
      fout->append("__attribute__((flatten)) ");
    }
    fout->append("void ", mod, "::__color_run() {\n");
    fout->append("  __color_prepare_runtime();\n  [[maybe_unused]] auto& __rt = *__color_runtime;\n");
    // Without dirty gating nothing is skipped, so conservatively count every
    // period as a state change (it only advances the observation generation).
    fout->append(tune_dirty() ? "  __rt.__color_state_changed = false;\n" : "  __rt.__color_state_changed = true;\n");
    for (size_t site = 0; site < color_plan_->sites().size(); ++site) {
      if (color_plan_->sites()[site].kind == livehd::sim::Color_plan::Site_kind::loop_control) {
        fout->append("  __rt.__loop_advanced_", std::to_string(site), " = false;\n");
      }
    }
    if (direct_commit_shards.empty()) {
      fout->append("  __rt.__state_commit.fill(false);\n");
    } else {
      fout->append("  __rt.__commit_shard_mask.fill(0);\n");
    }
    if (tune_dirty()) {
      fout->append(
          "  __rt.__color_input_changed = false;\n"
          "  __color_refresh_inputs();\n"
          "  if (__rt.__color_quiescent && !__rt.__color_input_changed) {\n");
      if (vcd_on) {
        fout->append("    __vcd_tick += (__clk_ratio > 0 ? __clk_ratio : 1);\n");
      }
      fout->append("    return;\n  }\n");
    } else {
      fout->append("  __color_refresh_inputs();\n");
    }
    for (size_t slot = 0; slot < 5; ++slot) {
      if (direct_run_shards.empty()) {
        std::vector<size_t> slot_colors;
        for (const size_t color : serial_colors) {
          if (static_cast<size_t>(color_plan_->colors()[color].slot) == slot) {
            slot_colors.push_back(color);
          }
        }
        emit_scheduled_colors(slot_colors);
      } else {
        for (size_t shard = 0; shard < direct_run_shards.size(); ++shard) {
          if (static_cast<size_t>(color_plan_->colors()[direct_run_shards[shard].front()].slot) != slot) {
            continue;
          }
          fout->append("  __color_run_part_", std::to_string(shard), "();\n");
        }
      }
      if (vcd_on && slot == 0) {
        fout->append("  __vcd_snapshot(true);\n");
      }
      if (slot == 1) {
        fout->append("  __color_commit(1);\n");
      }
      if (vcd_on && slot == 2) {
        fout->append("  __vcd_snapshot(false);\n");
      }
      if (slot == 3) {
        fout->append("  __color_commit(3);\n");
      }
    }
    emit_color_period_finish();
    fout->append("}\n");
  }

  // ---- sim.tune: the executable root's support tables and identity TU ----
  // Both are root artifacts registered through open_out, so they are in this
  // module's Gen_record and a missing one is a generation miss.
  if (executable_root) {
    emit_tune_support(g, fstem, mod, occurrence_member, occurrence_sub_member);

    // <stem>.tune-id.cpp: what this binary was generated with, for drv.bin
    // (weak defaults in the driver, overridden here) and the tune ledger.
    // Self-contained -- no include -- and named per module: with no --top every
    // uninstantiated module is a root, and `lhd sim` links every *.cpp in the
    // directory. Everything baked here is already folded into the ROOT key
    // (tune vector, slop_u, debug, unknown_zero, vcd, vcd_fake_delay,
    // runtime_support, observe; the plan flag derives from them), so a cache
    // hit can never serve a stale identity.
    //
    // `structure` is the ledger's comparability key ACROSS vectors: the cone,
    // the emitter and the env switches, but deliberately neither the tune
    // vector nor the backend (a backend A/B must compare).
    const std::string structure = absl::StrCat(absl::Hex(hier_graph_digest(g), absl::kZeroPad16),
                                               "-",
                                               absl::Hex(livehd::kCgenSrcSalt, absl::kZeroPad16),
                                               "-",
                                               kSimGenVersion,
                                               "-e",
                                               env_.bits());
    const auto        flag      = [](bool b) -> std::string_view { return b ? "true" : "false"; };
    const std::string codegen   = absl::StrCat("sim.tune.dirty=",
                                               tune_.dirty_str(),
                                               "\nsim.tune.fence=",
                                               tune_.fence_str(),
                                               "\nsim.tune.live_words=",
                                               tune_.live_words_str(),
                                               "\nsim.tune.backend=",
                                               tune_.backend_str(),
                                               "\nsim.slop_u=",
                                               flag(slop_u_),
                                               "\nsim.debug=",
                                               flag(debug_),
                                               "\nsim.unknown_zero=",
                                               flag(unknown_zero_),
                                               "\nsim.vcd=",
                                               vcd_file.empty() ? std::string_view{"false"} : std::string_view{vcd_file},
                                               "\nsim.vcd_fake_delay=",
                                               flag(vcd_fakedelay),
                                               "\nplan.runtime_random=",
                                               flag(color_plan_->summary().runtime_random),
                                               "\nruntime_support=",
                                               flag(runtime_support_on),
                                               "\nobserve=",
                                               flag(observation_on),
                                               "\n");
    auto              tid       = open_out(absl::StrCat(fstem, ".tune-id.cpp"));
    tid->append("// Generated by inou.cgen.sim (LiveHD). Do not edit.\n");
    tid->append("extern \"C\" const char* __lhd_tune_vector_", mod, "()    { return ", cpp_string_literal(tune_vector_), "; }\n");
    tid->append("extern \"C\" const char* __lhd_tune_structure_", mod, "() { return ", cpp_string_literal(structure), "; }\n");
    tid->append("extern \"C\" const char* __lhd_tune_codegen_", mod, "()   { return ", cpp_string_literal(codegen), "; }\n");
  }

  // ---- <stem>.iface.json — the machine-readable module manifest (2f-sim B0) ----
  // Replaces the historical TEXT SCRAPE of this module's generated .hpp
  // (prp_sim.cpp's parse_hpp), which silently bit-rotted the day memories stopped
  // being `std::array<Slop<...>>` members and became `hlop::Memory_*<...>`. One
  // JSON object per module holds exactly what a testbench generator and the
  // sim-query catalog need: IO with tuple leaves already flattened to their dotted
  // C++/RTL path, state elements, memories (with the shape a word read needs), and
  // sub-instances. Two widths are published per entry because they differ and both
  // matter: `bits` is the literal LGraph width used internally and
  // `declared_bits` is the source-declared width an agent sees in the
  // Pyrope/Verilog. Under the literal-width convention they are equal.
  // Names here are cpp_id()/cpp_port_path() output (identifier chars plus '.' for
  // tuple leaves), so no JSON string escaping is required.
  {
    auto        jout     = open_out(absl::StrCat(fstem, ".iface.json"));
    const auto  decl_reg = [](int b, bool /*uns*/) { return b; };
    std::string j;
    absl::StrAppend(&j,
                    "{\"schema_version\":1,\"kind\":\"sim_iface\",\"gen\":\"",
                    kSimGenVersion,
                    "\",\"module\":\"",
                    mod,
                    "\",\n");
    // Is this class an executable simulator (does it have step()/cycle())?
    // Only a Color_plan ROOT is. An ordinary hierarchy definition is emitted as
    // a storage-only record owned by its root's occurrence-wide DAG, so a
    // testbench cannot drive it directly — a `test` naming one used to reach
    // the host compiler as `no member named 'step'`. prp_sim reads this to
    // refuse with an actionable diagnostic instead.
    absl::StrAppend(&j, " \"executable\":", executable_root ? "true" : "false", ",\n");

    // IO, in port_id order (the order the In/Out structs are emitted in).
    absl::StrAppend(&j, " \"io\":[");
    bool first = true;
    for (const auto& io : ios) {
      absl::StrAppend(&j,
                      first ? "" : ",\n      ",
                      "{\"name\":\"",
                      io.field,
                      "\",\"dir\":\"",
                      io.is_input ? "input" : "output",
                      "\",\"bits\":",
                      io.bits,
                      ",\"declared_bits\":",
                      io.bits,
                      ",\"signed\":",
                      io.unsign ? "false" : "true",
                      io.is_input && io.raw == "__valid" ? ",\"role\":\"activation\",\"default\":true" : "",
                      "}");
      first = false;
    }
    absl::StrAppend(&j, "],\n");

    // State: pipe stages precede their flop, matching describe_signals() order.
    absl::StrAppend(&j, " \"regs\":[");
    first = true;
    for (const auto& f : flops) {
      const bool uns = is_unsign(f.node.get_driver_pin(0));
      for (const auto& s : f.stages) {
        absl::StrAppend(&j,
                        first ? "" : ",\n        ",
                        "{\"name\":\"",
                        s,
                        "\",\"kind\":\"pipe\",\"bits\":",
                        f.bits,
                        ",\"declared_bits\":",
                        decl_reg(f.bits, uns),
                        ",\"signed\":",
                        uns ? "false" : "true",
                        "}");
        first = false;
      }
      absl::StrAppend(&j,
                      first ? "" : ",\n        ",
                      "{\"name\":\"",
                      f.member,
                      "\",\"kind\":\"flop\",\"bits\":",
                      f.bits,
                      ",\"declared_bits\":",
                      decl_reg(f.bits, uns),
                      ",\"signed\":",
                      uns ? "false" : "true",
                      ",\"latch\":",
                      f.is_latch ? "true" : "false",
                      "}");
      first = false;
    }
    absl::StrAppend(&j, "],\n");

    // Memories: `rd_regs` are the sync-read output registers already carried by
    // the catalog as kind "memrd"; `size`/`bits` are what a word read needs.
    absl::StrAppend(&j, " \"mems\":[");
    first = true;
    for (const auto& m : mems) {
      // `m.unsign` is the fact the CARRIER was actually built with
      // (infer_memory_unsign -> value_type). The old local recomputation used a
      // different rule -- first read port only, defaulting to UNSIGNED -- so a
      // memory with no materialized read dout (write-only, or read only through
      // read_all) advertised the opposite sign from the array it describes, and
      // the Pyrope testbench reading through array_signed() compared it wrong.
      // The register row a few lines up already reads its element's own flag.
      const bool  uns = m.unsign;
      std::string rds;
      for (const auto& p : m.ports) {
        if (p.rd && m.type == 1) {
          absl::StrAppend(&rds, rds.empty() ? "" : ",", "\"", m.member, "_q", p.rdidx, "\"");
        }
      }
      absl::StrAppend(&j,
                      first ? "" : ",\n        ",
                      "{\"name\":\"",
                      m.member,
                      "\",\"bits\":",
                      m.bits,
                      ",\"declared_bits\":",
                      m.bits,
                      ",\"signed\":",
                      uns ? "false" : "true",
                      ",\"size\":",
                      m.size,
                      ",\"ordering\":\"",
                      m.order == Mem::Order::fwd       ? "fwd"
                      : m.order == Mem::Order::none    ? "none"
                      : m.order == Mem::Order::program ? "program"
                                                       : "old",
                      "\",\"wensize\":",
                      m.wensize,
                      ",\"n_rd\":",
                      m.n_rd,
                      ",\"n_wr\":",
                      m.n_wr,
                      ",\"sync_read\":",
                      m.type == 1 ? "true" : "false",
                      ",\"rd_regs\":[",
                      rds,
                      "]}");
      first = false;
    }
    absl::StrAppend(&j, "],\n");

    absl::StrAppend(&j, " \"subs\":[");
    first = true;
    for (const auto& s : subs) {
      absl::StrAppend(&j,
                      first ? "" : ",\n        ",
                      "{\"inst\":\"",
                      s.inst,
                      "\",\"module\":\"",
                      s.callee_struct,
                      "\"",
                      // `loop` is what tells a consumer that this instance's state
                      // is published per lane (`<inst>[<i>].<sig>`). `count` alone
                      // cannot: a single-trip rolled loop and an ordinary Sub both
                      // read as 1, and they are named differently.
                      s.loop ? absl::StrCat(",\"loop\":true,\"count\":", s.loop->count) : std::string{},
                      "}");
      first = false;
    }
    absl::StrAppend(&j, "]\n}\n");
    jout->append(j);
  }

  // Persist the (updated) generation digests only after a CLEAN emission. What
  // must never be recorded is a run that FAILED: the next run would skip the
  // emission and a build that legitimately errors would turn green. The
  // artifact list is recorded here, and only here, for the same reason — a
  // record whose files were never finished would authorize a skip over a
  // half-written tree.
  //
  // "Clean" is the absence of a reported ERROR, NOT the absence of the internal
  // `cycle_unresolved_` flag. On the occurrence-wide color ROOT that flag fires
  // on ordinary register reads — a flop's value is bound by the color runtime
  // rather than by pin2var, so operand() takes its "no binding must mean a
  // back-edge" branch and sets it without anything being wrong (measured: dino
  // reports `flop_368:pc`, its program counter). Gating on it therefore excluded
  // precisely the one module this reuse exists for, on every design with a
  // register. A genuine unresolved cycle still blocks the record, because the
  // paths that find one set `cycle_reported_` and emit an error.
  if (!odir.empty() && !cycle_reported_ && !livehd::diag::sink().has_errors()) {
    std::sort(emitted_files_.begin(), emitted_files_.end());
    emitted_files_.erase(std::unique(emitted_files_.begin(), emitted_files_.end()), emitted_files_.end());
    gen_digests_[std::string(gname)] = Gen_record{gen_key_, std::move(emitted_files_)};
    save_gen_digests();
  }
}
