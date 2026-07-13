//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "eprp.hpp"
#include "lhd.hpp"

void setup_inou_yosys();
class Lnast;

namespace lhd {

namespace fs = std::filesystem;

inline constexpr uint32_t kHhdsGraphBodyMagic = 0x48484742;  // "HHGB"
inline constexpr uint32_t kHhdsTreeBodyMagic  = 0x48485442;  // "HHTB"

inline constexpr std::pair<std::string_view, std::string_view> kSetPasses[] = {
    {     "compile.upass",        "pass.upass"},
    {     "compile.cprop",        "pass.cprop"},
    {  "compile.bitwidth",     "pass.bitwidth"},
    {    "compile.formal",       "pass.formal"},
    {       "pass.formal",       "pass.formal"},
    {      "compile.cgen", "inou.cgen.verilog"},
    {       "compile.sim",     "inou.cgen.sim"},
    {     "compile.yosys",   "inou.yosys.tolg"},
    {  "compile.isabelle",     "pass.isabelle"},
    {      "compile.lean",         "pass.lean"},
    {"compile.prp_writer",   "pass.prp_writer"},
    {        "pass.color",        "pass.color"},
    {    "pass.partition",    "pass.partition"},
    {          "pass.abc",          "pass.abc"},
    {      "pass.liberty",      "pass.liberty"},
    {    "pass.opentimer",    "pass.opentimer"},
    {               "lec",          "pass.lec"},
    {            "formal",          "pass.lec"},
    {      "pass.semdiff",      "pass.semdiff"},
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
std::string_view  set_pass_method(std::string_view set_name);
bool              is_kernel_label(std::string_view flag);
void              merge_sets(const Options& opts, std::string_view pass_name, Eprp_var::Eprp_dict& labels);
void              check_known_set_passes(const Options& opts);
bool              lnastfmt_enabled(const Options& opts);
void              apply_log_settings(const Options& opts);
void              apply_lhd_settings(Options& opts);
std::vector<std::pair<std::string, std::string>> recipe_graph_passes(const Options& opts, std::string_view def);

void save_ln_dir(Options& opts, Result& res, const std::vector<std::shared_ptr<Lnast>>& units, const std::string& dir);
std::vector<std::shared_ptr<Lnast>> load_ln_dir(const std::string& dir);
void                                emit_ln_outputs(const std::vector<std::shared_ptr<Lnast>>& units, Options& opts, Result& res);
void                     emit_lnast_dump_outputs(const std::vector<std::shared_ptr<Lnast>>& units, Options& opts, Result& res);
std::vector<std::string> cgen_into(Options& opts, Result& res, Eprp_var& var, const std::string& output_dir,
                                   bool default_srcmap = false);
void                     emit_verilog_outputs(Options& opts, Result& res, Eprp_var& var);
std::vector<std::string> sim_into(Options& opts, Result& res, Eprp_var& var, const std::string& output_dir);
std::string              sim_hlop_include_dir(const Options& opts);
std::string              sim_iassert_include_dir(const Options& opts);
std::string              sim_host_cxx();
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
void discover_imports(Eprp_var& var, size_t n_imports, const std::vector<std::string>& seed_files);
void graph_pipeline_and_emits(Options& opts, Result& res, Eprp_var& var, const std::string& lib_path);
void compile_sources(Options& opts, Result& res, const Ir_inputs& inputs);
void compile_command(Options& opts, Result& res);
void scan_command(Options& opts, Result& res);

std::string shell_quote(const std::string& value);
std::string locate_lgcheck();
std::string locate_lgcheck_yosys();
std::string materialize_verilog(Options& opts, Result& res, const std::string& kind, const std::string& path,
                                std::string_view side);
void        lec_lgyosys(Options& opts, Result& res);
void        sim_command(Options& opts, Result& res);
void        load_side_graphs(Options& opts, Result& res, const std::string& kind, const std::string& path, std::string_view side,
                             Eprp_var& var);
void        lec_command(Options& opts, Result& res);
void        formal_command(Options& opts, Result& res);
void        semdiff_command(Options& opts, Result& res);
void        load_lg_into_var(const std::string& library_path, Eprp_var& var);
void        pass_command(Options& opts, Result& res);
void        tool_command(Options& opts, Result& res);

}  // namespace lhd
