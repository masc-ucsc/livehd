//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "eprp.hpp"
#include "lhd.hpp"

void setup_inou_yosys();
class Lnast;

namespace lhd {

namespace fs = std::filesystem;

// Cross-frontend graph names pair by canonical entity only when that entity is
// unique within one side; ambiguous names retain their full spelling.
class Entity_canonicalizer {
public:
  explicit Entity_canonicalizer(const Eprp_var& var);
  [[nodiscard]] std::string operator()(std::string_view full_name) const;

private:
  absl::flat_hash_map<std::string, size_t> counts_;
};

inline constexpr uint32_t kHhdsGraphBodyMagic = 0x48484742;  // "HHGB"
inline constexpr uint32_t kHhdsTreeBodyMagic  = 0x48485442;  // "HHTB"

// One --set/--config namespace. `list` controls `lhd list options`/`describe`
// VISIBILITY only — validation and merge_sets accept every named namespace, so
// a legacy spelling keeps working while the listing shows one canonical name
// per option (sim.* IS the sim vocabulary (compile.sim.* deleted); the formal
// tools share the `formal.` root — formal.lec.* / formal.isabelle.* /
// formal.lean.* — with options common across them listed once as formal.*).
struct Set_pass {
  std::string_view set_name;
  std::string_view method;
  enum class List : uint8_t {
    all,       // canonical namespace: list every label
    none,      // accepted alias (legacy spelling): list nothing
    common,    // list only the labels set_flag_is_common() places here
    specific,  // list every label set_flag_is_common() does not
  } list = List::all;
};

// pass.lec labels whose MEANING is shared across the formal tools (`lhd lec`
// and the `lhd formal` verify engine read all of these): canonical spelling
// `formal.<flag>`. Everything else on pass.lec is ref/impl-pairing machinery:
// canonical spelling `formal.lec.<flag>`.
inline constexpr std::string_view kFormalCommonFlags[] = {
    "allow_oversize",
    "assume_check",
    "bound",
    "engine",
    "hard_timeout_mult",
    "ignore_memory",
    "jobs",
    "mine",
    "min_timeout",
    "partitions",
    "phase",
    "report",
    "reset",
    "reset_cycles",
    "retry",
    "rlimit",
    "solver",
    "spec_mining_timeout",
    "simfail",
    "simfail_run",
    "split",
    "stats",
    "timeout",
    "witness",
};

// pass.color labels that ONLY the `synth` algorithm reads (color_synth*.cpp):
// canonical spelling `pass.color.synth.<flag>`. Every other pass.color label --
// the algorithm choice, generic post-processing, the ware_* policy that
// pass.abc honors under ANY coloring, and the other algorithms' own knobs --
// stays `pass.color.<flag>`.
inline constexpr std::string_view kColorSynthFlags[] = {
    "ctrl_cones",
    "ctrl_max_gate",
    "ctrl_min_gate",
    "flop_to_flop",
    "forward",
    "mapper",
    "max_gate",
    "max_ge",
    "min_color_nodes",
    "min_ge",
    "mode",
    "name_weight",
    "stop_arith",
    "stop_cmp",
    "stop_mux",
    "stop_shift",
};

// The common/specific split of a method shared by two namespaces (Set_pass
// List::common / List::specific): true when `flag` belongs to the COMMON one.
// pass.lec: `formal.<flag>` for kFormalCommonFlags, `formal.lec.<flag>` for the
// pairing machinery. pass.color: `pass.color.<flag>` except kColorSynthFlags,
// which are `pass.color.synth.<flag>`. Keyed on (method, flag), so a label that
// merely SHARES a name with one of these on another method is unaffected.
inline bool set_flag_is_common(std::string_view method, std::string_view flag) {
  const auto in = [flag](const auto& table) {
    for (const auto& f : table) {
      if (f == flag) {
        return true;
      }
    }
    return false;
  };
  if (method == "pass.lec") {
    return in(kFormalCommonFlags);
  }
  if (method == "pass.color") {
    return !in(kColorSynthFlags);
  }
  return false;
}

// REMOVED namespaces/flags (no back-compat): using one
// errors with a directed "use X instead" hint. `formal_split` = the lec split
// (common flags -> formal.<f>, pairing machinery -> formal.lec.<f>).
struct Renamed_ns {
  std::string_view old_ns;
  std::string_view new_ns;
  bool             formal_split = false;
};
inline constexpr Renamed_ns kRenamedSetPasses[] = {
    {      "pass.synth",       "pass.usyn", false},
    {             "lec",          "formal",  true},
    {     "compile.sim",             "sim", false},
    {"compile.isabelle", "formal.isabelle", false},
    {    "compile.lean",     "formal.lean", false},
};
inline constexpr std::pair<std::string_view, std::string_view> kRenamedFlags[] = {
    // `mine` read as "my own", and the one budget funds three SPECULATIVE
    // post-run phases (straggler list, timeout-core diagnosis, invariant
    // mining), so the spelling now says which. Renaming it also frees the
    // `min_*` space: `min_timeout` (the per-unit floor under the soft total) and
    // `mine_timeout` differed by one letter and meant unrelated things.
    { "minetimeout", "spec_mining_timeout"},
    {"mine_timeout", "spec_mining_timeout"},
    {     "prpfail",             "simfail"},
    {  "prpfailrun",         "simfail_run"},
    { "prpfail_run",         "simfail_run"},
    {"vcdfakedelay",      "vcd_fake_delay"},
    {         "min",              "min_ge"},
    {         "max",              "max_ge"},
    {         "seq",            "register"},
};

// REMOVED flags with no replacement: a directed "was removed, here is why"
// instead of the generic unknown-flag guess. Keyed on the flag leaf, so it
// applies whatever namespace it was spelled under.
inline constexpr std::pair<std::string_view, std::string_view> kRemovedFlags[] = {
    {      "cache",
     "incremental reuse is ONE kernel switch now: `--set lhd.incremental=false` turns the compile, pass.abc and "
     "formal/lec caches off together (they are on by default under a user --workdir). The per-tier "
     "compile.cache / pass.abc.cache / formal.cache flags are gone"                                                  },
    {      "yosys",
     "yosys is linked in-process now (Yosys::Pass::call), so there is no external binary to point at. The label was "
     "registered but never read; drop the flag"                                                                      },
    {"budget_mode",
     "the budget scheduler is no longer a mode: accounting is ON whenever formal.timeout>0 and formal.rlimit==0, and "
     "the deterministic tier is selected by setting formal.rlimit (which owns the bound by itself). Drop the flag; use "
     "--set formal.rlimit=N for the old budget_mode=rlimit behavior"                                                 },
    {     "absorb",
     "the synth algorithms colour the flat view now, so crossing a module boundary is the default, not a size-triggered "
     "inline; min_ge no longer doubles as the absorb threshold (pass.color.synth.max_gate bounds a `cones` region instead)"},
};

// Retired public settings, including pass labels still used by the kernel.
// Match on method rather than spelling so aliases cannot bypass the boundary.
struct Retired_set_option {
  std::string_view method;
  std::string_view flag;
  std::string_view hint;
};
inline constexpr Retired_set_option kRetiredSetOptions[] = {
    {"inou.cgen.verilog","verbose",                                                                    "This option had no implemented effect; drop the setting."                         },
    {        "pass.lean",                 "normalize",                                                                    "This option had no implemented effect; drop the setting."},
    {        "pass.lean",           "cert_chunk_size",                                                                    "This option had no implemented effect; drop the setting."},
    {        "pass.lean",          "cert_chunk_limit",                                                                    "This option had no implemented effect; drop the setting."},
    {        "pass.lean",          "cert_wf_fallback",                                                                    "This option had no implemented effect; drop the setting."},
    {     "pass.semdiff",                       "alg",                                                                    "This option had no implemented effect; drop the setting."},
    {     "pass.semdiff",                   "verbose",                                                                    "This option had no implemented effect; drop the setting."},
    {       "pass.color",                   "compact",                                                                    "This option had no implemented effect; drop the setting."},
    {              "sim",                   "flatten",                                                                    "This option had no implemented effect; drop the setting."},
    {      "pass.formal",                   "enabled",                                                        "Use --set compile.formal.mode=none to disable compile-time checking."},
    {         "pass.abc",                       "out",                                                                         "Use --emit-dir lg:DIR for the output graph library."},
    {   "pass.partition",                       "out",                                                                         "Use --emit-dir lg:DIR for the output graph library."},
    {     "pass.liberty",                       "out",                                                                         "Use --emit-dir lg:DIR for the output graph library."},
    { "pass.single_edge",                       "out",                                                                         "Use --emit-dir lg:DIR for the output graph library."},
    {  "inou.yosys.tolg",                  "frontend",                                                                      "Select --reader yosys-verilog or --reader yosys-slang."},
    {       "inou.slang",                   "defines",                                                                            "Pass -D NAME=VALUE after -- to the slang reader."},
    {       "inou.slang",                  "includes",                                                                                   "Pass -I DIR after -- to the slang reader."},
    {       "inou.slang",                 "undefines",                                                                                  "Pass -U NAME after -- to the slang reader."},
    {         "pass.abc",                   "threads",                                         "Use --set synth.threads=N for the shared ABC worker limit (0 = automatic, up to 8)."},
    {         "pass.abc",                "small_flow",                                                                  "This unused optional policy was removed; drop the setting."},
    {         "pass.abc",                  "small_ge",                                                                  "This unused optional policy was removed; drop the setting."},
    {         "pass.abc",              "small_min_ge",                                                                  "This unused optional policy was removed; drop the setting."},
    {         "pass.abc",                 "ctrl_flow",                                                                  "This unused optional policy was removed; drop the setting."},
    {         "pass.abc",           "ctrl_area_relax",                                                                  "This unused optional policy was removed; drop the setting."},
    {         "pass.abc",       "ctrl_time_budget_ms",                                                                  "This unused optional policy was removed; drop the setting."},
    {  "inou.yosys.tolg",                       "abc",                                                                  "This unused optional policy was removed; drop the setting."},
    {  "inou.yosys.tolg",                   "techmap",                                                                  "This unused optional policy was removed; drop the setting."},
    {  "inou.yosys.tolg",                  "elab_top",                                                                  "This unused optional policy was removed; drop the setting."},
    {  "inou.yosys.tolg",                "rename_top",                                                                  "This unused optional policy was removed; drop the setting."},
    {      "pass.formal",                    "active",                                                "The kernel manages this implementation setting internally; drop the setting."},
    {      "pass.formal",            "hier_preflight",                                                "The kernel manages this implementation setting internally; drop the setting."},
    {       "pass.upass",              "import_defer",                                                "The kernel manages this implementation setting internally; drop the setting."},
    {       "pass.upass",                       "dce",                                                "The kernel manages this implementation setting internally; drop the setting."},
    {       "pass.upass",                   "inherit",                                                "The kernel manages this implementation setting internally; drop the setting."},
    {       "pass.upass", "preserve_param_provenance",                                                "The kernel manages this implementation setting internally; drop the setting."},
    {       "pass.upass",                "ssa_stream",                                                "The kernel manages this implementation setting internally; drop the setting."},
    {       "inou.slang",               "slang_flags",                                                     "Pass reader arguments after -- instead of setting serialized arguments."},
    {  "inou.yosys.tolg",               "slang_flags",                                                     "Pass reader arguments after -- instead of setting serialized arguments."},
    {         "pass.abc",                     "stats",                                                                         "Use --stats or --set lhd.stats=true for statistics."},
    {       "pass.color",                     "stats",                                                                         "Use --stats or --set lhd.stats=true for statistics."},
    {   "pass.opentimer",                     "stats",                                                                         "Use --stats or --set lhd.stats=true for statistics."},
    {     "pass.semdiff",                     "stats",                                                                         "Use --stats or --set lhd.stats=true for statistics."},
    {         "pass.lec",                     "stats",                                                                         "Use --stats or --set lhd.stats=true for statistics."},
    {         "pass.lec",
     "strict", "An UNKNOWN verdict always fails now (exit 7): an inconclusive run proved nothing, so it can never exit 0. Drop the setting."                                        },
    {        "pass.usyn",
     "recipes", "pass.usyn runs ONE recipe now: --set pass.usyn.support=6, pass.usyn.literals=16 and pass.usyn.series=4."},
    {        "pass.usyn",
     "proof_seconds", "pass.usyn no longer proves equivalence; verify the mapped netlist with `lhd lec` as a separate step. Drop the setting."},
    {        "pass.usyn",
     "proof_nodes", "pass.usyn no longer proves equivalence; verify the mapped netlist with `lhd lec` as a separate step. Drop the setting."},
    {        "pass.usyn",
     "delay_tolerance", "pass.usyn no longer compares its result against an ABC baseline. Drop the setting."},
    {        "pass.usyn",
     "max_depth", "The unate search this bounded was removed (pass.usyn is the LUT cover: domino_levels, depth_slack and cover_cuts bound it). Drop the setting."},
    {        "pass.usyn",
     "work", "The unate search this bounded was removed (pass.usyn is the LUT cover: domino_levels, depth_slack and cover_cuts bound it). Drop the setting."},
    {        "pass.usyn",
     "cuts", "The unate search this bounded was removed (pass.usyn is the LUT cover: domino_levels, depth_slack and cover_cuts bound it). Drop the setting."},
    {        "pass.usyn",
     "joint_limit", "The unate search this bounded was removed (pass.usyn is the LUT cover: domino_levels, depth_slack and cover_cuts bound it). Drop the setting."},
    {        "pass.usyn",
     "joint_windows", "The unate search this bounded was removed (pass.usyn is the LUT cover: domino_levels, depth_slack and cover_cuts bound it). Drop the setting."},
    {        "pass.usyn",
     "image_inputs", "The unate search this bounded was removed (pass.usyn is the LUT cover: domino_levels, depth_slack and cover_cuts bound it). Drop the setting."},
    {        "pass.usyn",
     "reshape_limit", "The unate search this bounded was removed (pass.usyn is the LUT cover: domino_levels, depth_slack and cover_cuts bound it). Drop the setting."},
    {        "pass.usyn",
     "encoding_limit", "The unate search this bounded was removed (pass.usyn is the LUT cover: domino_levels, depth_slack and cover_cuts bound it). Drop the setting."},
    {        "pass.usyn",
     "encoding_code_limit", "The unate search this bounded was removed (pass.usyn is the LUT cover: domino_levels, depth_slack and cover_cuts bound it). Drop the setting."},
    {        "pass.usyn",
     "encoding_pair_limit", "The unate search this bounded was removed (pass.usyn is the LUT cover: domino_levels, depth_slack and cover_cuts bound it). Drop the setting."},
    {        "pass.usyn",
     "symbolic_nodes", "The unate search this bounded was removed (pass.usyn is the LUT cover: domino_levels, depth_slack and cover_cuts bound it). Drop the setting."},
    {        "pass.usyn",
     "cover_limit", "The unate search this bounded was removed (pass.usyn is the LUT cover: domino_levels, depth_slack and cover_cuts bound it). Drop the setting."},
    {        "pass.usyn",
     "divisor_limit", "The unate search this bounded was removed (pass.usyn is the LUT cover: domino_levels, depth_slack and cover_cuts bound it). Drop the setting."},
    {        "pass.usyn",
     "split", "The per-cone split mode was removed; pass.usyn always covers the whole region. Drop the setting."},
    {        "pass.usyn",
     "split_exact", "The per-cone split mode was removed; pass.usyn always covers the whole region. Drop the setting."},
    {        "pass.usyn",
     "split_factor", "The per-cone split mode was removed; pass.usyn always covers the whole region. Drop the setting."},
    {        "pass.usyn",
     "split_cut", "The per-cone split mode was removed; pass.usyn always covers the whole region. Drop the setting."},
    {        "pass.usyn",
     "split_share", "The per-cone split mode was removed; pass.usyn always covers the whole region. Drop the setting."},
    {        "pass.usyn",
     "reference", "The ABC `&if` LUT reference was removed. Drop the setting."},
    {        "pass.usyn",
     "witness_bytes", "pass.usyn no longer writes a witness archive; verify the mapped netlist with `lhd lec`. Drop the setting."},
};
inline std::string_view retired_set_hint(std::string_view method, std::string_view flag) {
  if ((method == "pass.abc" || method == "pass.usyn") && flag == "satopt") {
    return "use --set pass.satopt=true|false (satopt runs in the compile step; default on only for synth/lec compiling a source)";
  }
  for (int pass = 0; pass < 2; ++pass) {
    for (const auto& option : kRetiredSetOptions) {
      if (option.method == method && option.flag == flag) {
        return option.hint;
      }
    }
    if (method != "pass.usyn") {
      break;
    }
    method = "pass.abc";  // shared mapping labels have the same kernel-owned slots
  }
  return {};
}

inline constexpr Set_pass kSetPasses[] = {
    {     "compile.upass",        "pass.upass",      Set_pass::List::all},
    {     "compile.cprop",        "pass.cprop",      Set_pass::List::all},
    {  "compile.bitwidth",     "pass.bitwidth",      Set_pass::List::all},
    {   "compile.bitfuzz",      "pass.bitfuzz",      Set_pass::List::all},
    {    "compile.formal",       "pass.formal", Set_pass::List::specific},
    {       "pass.formal",       "pass.formal",     Set_pass::List::none}, // alias of compile.formal
    {      "compile.cgen", "inou.cgen.verilog",      Set_pass::List::all},
    {     "compile.yosys",   "inou.yosys.tolg",      Set_pass::List::all},
    {     "compile.slang",        "inou.slang",      Set_pass::List::all},
    {   "formal.isabelle",     "pass.isabelle",      Set_pass::List::all},
    {       "formal.lean",         "pass.lean",      Set_pass::List::all},
    {"compile.prp_writer",   "pass.prp_writer",      Set_pass::List::all},
    {        "pass.color",        "pass.color",   Set_pass::List::common},
    {  "pass.color.synth",        "pass.color", Set_pass::List::specific},
    {    "pass.partition",    "pass.partition",      Set_pass::List::all},
    {  "pass.single_edge",  "pass.single_edge",      Set_pass::List::all},
    {          "pass.abc",          "pass.abc",      Set_pass::List::all},
    {       "pass.satopt",       "pass.satopt",      Set_pass::List::all},
    {         "pass.usyn",         "pass.usyn",      Set_pass::List::all},
    {      "pass.liberty",      "pass.liberty",      Set_pass::List::all},
    {    "pass.opentimer",    "pass.opentimer",      Set_pass::List::all},
    {            "formal",          "pass.lec",   Set_pass::List::common},
    {        "formal.lec",          "pass.lec", Set_pass::List::specific},
    {      "pass.semdiff",      "pass.semdiff",      Set_pass::List::all},
    {      "pass.analyze",      "pass.analyze",      Set_pass::List::all},
};

struct Ir_inputs {
  std::vector<std::string> ln_dirs;
  std::vector<std::string> lg_dirs;
};

struct Ln_inputs {
  std::vector<std::string> prp_files;
  std::vector<std::string> sv_files;
  std::vector<std::string> ln_dirs;
};

class Stdout_to_log {
public:
  explicit Stdout_to_log(const std::string& log_path);
  Stdout_to_log(const Stdout_to_log&)            = delete;
  Stdout_to_log& operator=(const Stdout_to_log&) = delete;
  ~Stdout_to_log();

private:
  int saved_fd_ = -1;
};

// Scoped wall clock for ONE pipeline phase: appends {name, elapsed ms} to
// res.phase_ms when the scope ends. RAII rather than a start/stop pair because
// the timed regions exit early (a failed build returns, a halting pass throws)
// and a phase that ran must still be reported with the time it burned.
class Phase_timer {
public:
  Phase_timer(Result& res, std::string_view name) : res_(res), name_(name), t0_(std::chrono::steady_clock::now()) {}
  Phase_timer(const Phase_timer&)            = delete;
  Phase_timer& operator=(const Phase_timer&) = delete;
  ~Phase_timer() { stop(); }

  // Record now instead of at scope end, for a phase whose region cannot be a
  // scope of its own (the sim host build: its two branches declare locals the
  // code after them does not use, but wrapping them would reindent the world).
  // Idempotent, so the destructor is still the safety net on an early return.
  //
  // noexcept, and it means it: ~Phase_timer is implicitly noexcept, so an
  // exception escaping here while a pass unwinds calls std::terminate — abort,
  // no error envelope, exactly on the whole-design ABC/LEC runs where
  // install_memory_backstop() makes `new -> bad_alloc` a designed-for path
  // (pass/cost/host_mem.hpp). The vector growth inside emplace_back is the only
  // thing that can throw; losing one timing row beats losing the diagnosis.
  void stop() noexcept {
    if (done_) {
      return;
    }
    done_                                              = true;
    const std::chrono::duration<double, std::milli> dt = std::chrono::steady_clock::now() - t0_;
    try {
      res_.phase_ms.emplace_back(std::move(name_), dt.count());
    } catch (...) {  // NOLINT(bugprone-empty-catch) — see above: dropping the row is the fix
    }
  }

private:
  Result&                               res_;
  std::string                           name_;
  std::chrono::steady_clock::time_point t0_;
  bool                                  done_ = false;
};

std::string       join_csv(const std::vector<std::string>& values);
void              ensure_dir(const std::string& path);
void              check_inputs_exist(const std::vector<std::string>& files);
void              check_ir_body_magic(std::string_view dir, std::string_view subdir_prefix, uint32_t magic, std::string_view kind);
void              check_lg_input_dir(std::string_view dir);
const Typed_path* find_slot(const std::vector<Typed_path>& slots, std::string_view kind);
bool              wants_dump(const Options& opts, std::string_view what);
void              screen_dump_lnasts(const std::vector<std::shared_ptr<Lnast>>& units, std::string_view stage);
void              screen_dump_graphs(const Eprp_var& var, std::string_view stage);
std::string&      workdir(Options& opts);
std::string       next_log_path(Options& opts, std::string_view method);
void              mirror_log_to_stderr(const std::string& log_path);
std::string       map_diag_category(std::string_view category);
void              setup_diag(const Options& opts, std::string_view step);
void              run_step(std::string_view method, Eprp_var& var, const Eprp_var::Eprp_dict& labels, Options& opts, Result& res);
// pass.satopt over `var` with the caller's labels plus the --set pass.satopt.*
// options, the workdir proof cache and the report harvest (res.satopt_json).
void              run_satopt_step(Eprp_var& var, Eprp_var::Eprp_dict labels, Options& opts, Result& res);
std::string       synth_invocation_context(const Options& opts, const Result& res, const Eprp_var::Eprp_dict& labels);
// Park "which step, which log" where the SIGSEGV handler can read it without
// allocating (see install_crash_reporter). Empty strings clear the slot.
void              set_crash_context(std::string_view step, std::string_view log);
std::string_view  set_pass_method(std::string_view set_name);
bool              is_kernel_label(std::string_view flag);
void              merge_sets(const Options& opts, std::string_view pass_name, Eprp_var::Eprp_dict& labels);
// merge_sets for a MAPPER method (lhd.hpp kMappers). pass.usyn registers
// ABC's mapping labels (Pass_abc::add_mapping_labels), so `abc.*` tuning
// applies to both mappers; an explicit `pass.usyn.*` wins. Both the
// fused `lhd synth` and the standalone `lhd pass <mapper>` go through here so
// the precedence rule cannot drift between the two entry points.
void              merge_mapper_sets(const Options& opts, std::string_view method, Eprp_var::Eprp_dict& labels);
// merge_sets for pass.color: its generic options (`pass.color.*`) and the synth
// coloring's own (`pass.color.synth.*`) both feed the one pass.color method.
void              merge_color_sets(const Options& opts, Eprp_var::Eprp_dict& labels);
void              check_known_set_passes(const Options& opts);
// The `synth.*` command namespace, readable from any command: `synth.liberty`
// is THE one Liberty spelling (`lhd synth`, `lhd pass abc`, `lhd pass
// opentimer` all resolve through resolve_liberty), so no two Liberty readers
// in one flow can end up on different files.
std::string       synth_set(const Options& opts, std::string_view flag, std::string_view def);
std::string       resolve_liberty(const Options& opts);
bool              lnastfmt_enabled(const Options& opts);
// `compile.verify_frozen`: freeze every legalized def and re-check it after the
// emits (pass/legalize). A full structural digest per graph twice, so on by
// default only in debug builds (same policy as lnast_fmt).
bool              verify_frozen_enabled(const Options& opts);
bool              compile_unroll_requested(const Options& opts);  // compile.unroll (default false)
std::optional<bool> satopt_setting(const Options& opts);  // the explicit pass.satopt, if any
// Options for materializing a --lib model library (lg: -> cgen Verilog): no
// --lib recursion, no top selection (a cell library holds many unrelated
// tops), and satopt pinned off -- an explicit pass.satopt=true targets the
// elaborated design, never the model library.
Options           library_model_opts(const Options& opts);
// satopt in the compile graph pipeline: explicit setting, else on for synth/lec
// compiling a Pyrope/Verilog source (`from_source`).
bool              satopt_during_compile(const Options& opts, bool from_source);
bool              compile_cache_enabled(const Options& opts);
void              apply_log_settings(const Options& opts);
void              apply_lhd_settings(Options& opts);
std::vector<std::pair<std::string, std::string>> compile_graph_passes(const Options& opts);

void save_ln_dir(Options& opts, Result& res, const std::vector<std::shared_ptr<Lnast>>& units, const std::string& dir);
std::vector<std::shared_ptr<Lnast>> load_ln_dir(const std::string& dir);
void                                emit_ln_outputs(const std::vector<std::shared_ptr<Lnast>>& units, Options& opts, Result& res);
void                     emit_lnast_dump_outputs(const std::vector<std::shared_ptr<Lnast>>& units, Options& opts, Result& res);
std::vector<std::string> cgen_into(Options& opts, Result& res, Eprp_var& var, const std::string& output_dir,
                                   bool default_srcmap = false, std::string_view top = {});
void                     emit_verilog_outputs(Options& opts, Result& res, Eprp_var& var);
std::vector<std::string> sim_into(Options& opts, Result& res, Eprp_var& var, const std::string& output_dir);
std::string              sim_hlop_include_dir(const Options& opts);
std::string              find_header_in_runfiles(std::string_view header);
std::string              sim_iassert_include_dir(const Options& opts);
std::string              sim_host_cxx();
std::string              sim_llvm_link_tool();
void                     emit_sim_outputs(Options& opts, Result& res, Eprp_var& var);
void                     emit_isabelle_outputs(Options& opts, Result& res, Eprp_var& var);
void                     emit_lean_outputs(Options& opts, Result& res, Eprp_var& var);
void                     emit_pyrope_outputs(Options& opts, Result& res, Eprp_var& var);
void                     emit_pyrope_single_file(Options& opts, Result& res, Eprp_var& var);
std::vector<std::string> harvest_source_files(Result& res, const std::vector<std::shared_ptr<Lnast>>& units);
void                     write_depfile(const Options& opts, Result& res);
void                     write_unused_inputs(const Options& opts, Result& res, const std::vector<std::string>& closure);

void                                validate_emits(const Options& opts);
void                                validate_dumps(const Options& opts);
std::vector<std::shared_ptr<Lnast>> filter_top(const std::vector<std::shared_ptr<Lnast>>& units, const std::string& top);

// Entity tail of a full internal module name ("file.entity" -> "entity").
std::string_view top_entity_of(std::string_view name);

// Resolve a user --top/--ref-top/--impl-top against the full internal names
// (`file.entity`) in `names`. An exact match wins (returned as-is, silently).
// Otherwise the entity fallback (lec's long-standing rule), with a diag
// warning naming the substitution: match on the entity tail of BOTH sides, so
// a bare `--top XXX` resolves to the unique `file.XXX` (commonly Pyrope's
// self-named `XXX.XXX`) and a dotted `--top a.XXX` can pair with a
// regenerated `plain.XXX` (the v2prp LEC case). Accepted only when exactly
// one name matches. Returns "" when nothing (or more than one name) matches;
// the caller keeps its own not-found handling. `diag_pass` is the warning's
// origin identity (e.g. "pass.lec").
// Resolve `want` against internal `file.entity` unit names: exact match first,
// then the unique entity-tail match. Returns "" when zero or more than one
// candidate matches. An EMPTY `diag_pass` resolves QUIETLY (no
// `top-entity-fallback` warning) -- for internal probes that are not selecting
// the top on the user's behalf.
std::string resolve_top_name(const std::vector<std::string>& names, std::string_view want, std::string_view diag_pass);

// The shared "pick the top module on a side" ladder (lec / formal verify /
// pass semdiff): explicit per-side top, else --top (resolved via
// resolve_top_name), else the sole module. `side` is "ref"/"impl", or "" for
// a single-sided command (changes the error phrasing); `cmd` prefixes the
// error messages ("lec", "formal verify", "pass semdiff").
std::shared_ptr<hhds::Graph>        pick_top_graph(const Eprp_var& v, const std::string& side_top, const std::string& shared_top,
                                                   std::string_view side, std::string_view cmd, std::string_view diag_pass);
Ir_inputs                           gather_ir_inputs(const Options& opts, std::string_view command);
std::string                         json_escape_min(std::string_view value);
Ln_inputs                           classify_ln_inputs(const std::vector<std::string>& tokens, std::string_view command);
std::vector<std::shared_ptr<Lnast>> ln_tool_units(Options& opts, Result& res, const Ln_inputs& inputs);
std::vector<std::shared_ptr<Lnast>> sorted_by_name(std::vector<std::shared_ptr<Lnast>> units);
void print_line_diff(std::string& out, const std::vector<std::string>& a, const std::vector<std::string>& b, size_t context = 2);
void tool_cat_ln(Options& opts, Result& res, const std::vector<std::string>& tokens);
void tool_diff_ln(Options& opts, Result& res, const std::vector<std::string>& tokens);
void lower_lnasts(Options& opts, Result& res, Eprp_var& var, const std::string& lib_path, bool need_graphs);
// 2i-import S1 — transitively pull in imported sibling .prp sources from each
// importing file's own directory (fixpoint; importer-dir-relative only), so a
// single-file load needs no dependency list. `seed_files` are the already-parsed
// on-disk sources (they seed unit->dir); `n_imports` is the index of the first
// source unit in var.lnasts (earlier entries are pre-loaded ln: imports).
// Shared by compile AND the lec/verify side loaders (a Pyrope side never needs
// a pre-compile to lg: just to resolve its imports).
void discover_imports(Eprp_var& var, Result& res, size_t n_imports, const std::vector<std::string>& seed_files);
// `from_source`: the graphs were just lowered from Pyrope/Verilog (not an lg:/ln:
// input); it only sets the satopt default (satopt_during_compile).
void graph_pipeline_and_emits(Options& opts, Result& res, Eprp_var& var, const std::string& lib_path, bool already_final = false,
                              bool from_source = false);
void compile_sources(Options& opts, Result& res, const Ir_inputs& inputs);
void compile_command(Options& opts, Result& res);
void scan_command(Options& opts, Result& res);

std::string shell_quote(const std::string& value);
std::string locate_lgcheck();
std::string locate_lgcheck_yosys();
std::string materialize_verilog(Options& opts, Result& res, const std::string& kind, const std::string& path,
                                std::string_view side);
void        sim_command(Options& opts, Result& res);
void        load_side_graphs(Options& opts, Result& res, const std::string& kind, const std::string& path, std::string_view side,
                             Eprp_var& var);
void        lec_command(Options& opts, Result& res);
void        formal_command(Options& opts, Result& res);
void        semdiff_command(Options& opts, Result& res);
void        load_lg_into_var(const std::string& library_path, Eprp_var& var);
void        pass_command(Options& opts, Result& res);
// Stamp labels["top"] with --top resolved once against the graphs in `var`
// (full internal `file.entity` name; an unresolvable name passes through).
void        set_top_label(const Options& opts, const Eprp_var& var, Eprp_var::Eprp_dict& labels, std::string_view diag_pass);
// Slurp a pass's "qor" sidecar label into the envelope's "qor" member.
void        embed_qor_sidecar(const Eprp_var::Eprp_dict& labels, Result& res);
// Fill res.abc_incr from the abc-map report in res.qor_json (a bare abc-map
// or the `abc` member of a synth report); no-op when there is none.
void        harvest_abc_incremental(Result& res);
// pass.opentimer's STA reuse counters, from the `sta` qor object (its own
// report, or the `sta` member of a fused synth envelope) into res.sta_incr.
void        harvest_sta_incremental(Result& res, std::string_view sta_json);
// `lhd synth`: compile -> pass.color reduce -> pass.color synth -> pass.abc ->
// pass.opentimer over
// ONE in-memory design (lhd_kernel_synth.cpp).
void        synth_command(Options& opts, Result& res);
void        tool_command(Options& opts, Result& res);

}  // namespace lhd
