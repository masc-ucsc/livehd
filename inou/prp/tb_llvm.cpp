//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "tb_llvm.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ModRef.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include "prp_ast_facade.hpp"
#include "prpparse/lexer.hpp"
#include "prpparse/parser.hpp"
#include "prpparse/source_buffer.hpp"
#include "pyrope_literal.hpp"
#include "rapidjson/document.h"
#include "sim_llvm_target.hpp"
#include "str_tools.hpp"

namespace prp_tb {

namespace {

namespace fs = std::filesystem;

struct Gen_error {
  std::string msg;
};
[[noreturn]] void fail(const std::string& m) { throw Gen_error{m}; }

std::string slurp(const std::string& path) {
  std::ifstream      ifs(path, std::ios::binary);
  std::ostringstream ss;
  ss << ifs.rdbuf();
  return ss.str();
}

// ---- AST helpers (the same node shapes prp_sim reads) ---------------------------------------------
TSNode field(TSNode n, const char* name) { return ts_node_child_by_field_name(n, name, static_cast<uint32_t>(std::char_traits<char>::length(name))); }
std::string_view ntype(TSNode n) { return ts_node_type(n); }

std::string text_of(const std::string& src, TSNode n) {
  const auto s = ts_node_start_byte(n);
  const auto e = ts_node_end_byte(n);
  if (ts_node_is_null(n) || e > src.size() || s > e) {
    return {};
  }
  return std::string(str_tools::canonical_escaped_ident(std::string_view(src).substr(s, e - s)));
}

std::string trim(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
    s.pop_back();
  }
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])) != 0) {
    ++b;
  }
  return s.substr(b);
}

std::string lvalue_name(const std::string& src, TSNode n) {
  if (ts_node_is_null(n)) {
    return {};
  }
  const auto t = ntype(n);
  if (t == "identifier") {
    return text_of(src, n);
  }
  if (t == "typed_identifier") {
    for (TSNode c : ts_node_named_children(n)) {
      if (ntype(c) == "identifier") {
        return text_of(src, c);
      }
    }
  }
  return {};
}

// `U<N>` / `Unsigned(bits=N)` / `Unsigned(max=M)` annotation of a declaration, else 0.
int declared_uint_bits(const std::string& src, TSNode lv) {
  if (ts_node_is_null(lv) || ntype(lv) != "typed_identifier") {
    return 0;
  }
  for (TSNode c : ts_node_named_children(lv)) {
    if (ntype(c) != "type_cast") {
      continue;
    }
    for (TSNode tc : ts_node_named_children(c)) {
      if (ntype(tc) != "uint_type") {
        continue;
      }
      std::string tx = text_of(src, tc);
      tx.erase(std::remove_if(tx.begin(), tx.end(), [](unsigned char ch) { return std::isspace(ch) != 0 || ch == '_'; }), tx.end());
      if (tx.size() >= 2 && std::isdigit(static_cast<unsigned char>(tx[1])) != 0) {
        return std::max(std::atoi(tx.c_str() + 1), 0);
      }
      if (const auto p = tx.find("bits="); p != std::string::npos) {
        return std::max(std::atoi(tx.c_str() + p + 5), 0);
      }
    }
  }
  return 0;
}

struct Param_raw {
  std::string name;
  bool        required = false;
  TSNode      default_node{};
  std::string type;
  bool        is_string = false;  // the default is a quoted string: the parameter is a string handle
  bool        dynamic   = false;  // the default is an expression the test evaluates itself (not a literal)
};

std::vector<Param_raw> read_params_raw(const std::string& src, TSNode test) {
  std::vector<Param_raw> out;
  TSNode                 inp = field(test, "input");
  if (ts_node_is_null(inp)) {
    return out;
  }
  Param_raw cur;
  bool      have  = false;
  const auto flush = [&]() {
    if (!have) {
      return;
    }
    cur.required = ts_node_is_null(cur.default_node) || text_of(src, cur.default_node) == "nil";
    if (!cur.required) {
      std::string d = text_of(src, cur.default_node);
      while (!d.empty() && std::isspace(static_cast<unsigned char>(d.front())) != 0) {
        d.erase(0, 1);
      }
      cur.is_string = d.size() >= 2 && (d.front() == '"' || d.front() == '\'') && d.back() == d.front();
      const bool lit_int = !d.empty() && (std::isdigit(static_cast<unsigned char>(d.front())) != 0 || d.front() == '-');
      cur.dynamic = !cur.is_string && !lit_int && d != "true" && d != "false";
    }
    out.push_back(cur);
    have = false;
  };
  for (TSNode ci : ts_node_named_children(inp)) {
    if (ntype(ci) == "typed_identifier") {
      flush();
      cur       = Param_raw{};
      TSNode id = field(ci, "identifier");
      cur.name  = ts_node_is_null(id) ? std::string{} : text_of(src, id);
      for (TSNode child : ts_node_named_children(ci)) {
        if (ntype(child) == "type_cast") {
          for (TSNode type : ts_node_named_children(child)) {
            cur.type = text_of(src, type);
          }
        }
      }
      have = true;
    } else if (have) {
      cur.default_node = ci;
    }
  }
  flush();
  return out;
}

std::string json_str(std::string_view s) {
  std::string o = "\"";
  for (const char c : s) {
    switch (c) {
      case '\\': o += "\\\\"; break;
      case '"' : o += "\\\""; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default  : o += static_cast<unsigned char>(c) < 0x20 ? ' ' : c;
    }
  }
  return o + "\"";
}

// ---- the DUT descriptors pass/lnast_llvm wrote (`<stem>.dut.json`) ---------------------------------
struct Field {
  std::string name;
  uint32_t    bits  = 0;
  size_t      word  = 0;
  int         kind  = 0;  // 0 input, 1 output, 2 register, 3 memory
  bool        sign  = false;
  std::vector<uint32_t> marks;
};

struct Dut {
  std::string              stem, module;
  size_t                   words = 0, gate_word = 0, gate_bits = 0;
  std::vector<std::string> edge, fall, settle, objects, clocks;
  std::vector<size_t>      undefined_words;  // sim.warn_undefined: one state word per undefined-access site (consecutive)
  std::string              commit, fall_commit;
  std::vector<Field>       fields;
  std::string              sym;  // identifier-safe stem

  const Field* find(const std::string& name) const {
    for (const auto& f : fields) {
      if (f.name == name) {
        return &f;
      }
    }
    return nullptr;
  }
};

std::string sanitize(std::string_view s) {
  std::string o;
  for (const char c : s) {
    o += std::isalnum(static_cast<unsigned char>(c)) != 0 ? c : '_';
  }
  return o;
}

std::vector<Dut> load_duts(const std::string& simdir) {
  std::vector<Dut> out;
  std::error_code  ec;
  std::vector<fs::path> files;
  for (const auto& e : fs::directory_iterator(simdir, ec)) {
    if (e.path().filename().string().ends_with(".dut.json")) {
      files.push_back(e.path());
    }
  }
  std::sort(files.begin(), files.end());
  for (const auto& p : files) {
    const auto           text = slurp(p.string());
    rapidjson::Document  d;
    d.Parse(text.c_str());
    if (d.HasParseError() || !d.IsObject()) {
      fail("unreadable DUT descriptor " + p.string());
    }
    Dut u;
    u.stem      = d["stem"].GetString();
    u.module    = d["module"].GetString();
    u.words     = d["words"].GetUint64();
    u.gate_word = d["gate_word"].GetUint64();
    u.gate_bits = d["gate_bits"].GetUint64();
    u.commit    = d["commit"].GetString();
    u.fall_commit = d["fall_commit"].GetString();
    const auto strs = [&](const char* key, std::vector<std::string>& v) {
      for (const auto& x : d[key].GetArray()) {
        v.emplace_back(x.GetString());
      }
    };
    strs("edge", u.edge);
    strs("fall", u.fall);
    strs("settle", u.settle);
    strs("objects", u.objects);
    strs("clocks", u.clocks);
    for (const auto& x : d["undefined"].GetArray()) {
      u.undefined_words.push_back(x["word"].GetUint64());
    }
    for (const auto& f : d["fields"].GetArray()) {
      Field fld;
      fld.name = f["name"].GetString();
      fld.bits = f["bits"].GetUint();
      fld.word = f["word"].GetUint64();
      fld.kind = f["kind"].GetInt();
      fld.sign = f["sign"].GetBool();
      for (const auto& m : f["marks"].GetArray()) {
        fld.marks.push_back(m.GetUint());
      }
      u.fields.push_back(std::move(fld));
    }
    u.sym = sanitize(u.stem);
    out.push_back(std::move(u));
  }
  return out;
}

// ---- LLVM ---------------------------------------------------------------------------------------------
using llvm::BasicBlock;
using llvm::ConstantInt;
using llvm::Function;
using llvm::Value;

// Physical encoding of testbench values (debug switches; measured in todo/livehd/2sl-llvm-only-sim.md):
//   default                       a value's LLVM type is its semantic width rounded up to 32, 64 or a multiple of 64,
//                                 and the value is kept sign-extended over it ("canonical"): most operations then
//                                 need no masking or re-extension (+15% on narrow-value arithmetic over exact types);
//   LHD_TB_WIDTHS=exact           the LLVM type is exactly the value's width (iN for any N);
//   LHD_TB_NSW=0                  drop the no-signed-wrap flags the width rules justify on + - * << (no measurable effect).
// The width RULES (what a value needs) are the same either way; only the carrier changes.
bool tb_round_widths() {
  static const bool v = [] {
    const char* e = std::getenv("LHD_TB_WIDTHS");
    return e == nullptr || std::string_view(e) != "exact";
  }();
  return v;
}
bool tb_nsw() {
  static const bool v = [] {
    const char* e = std::getenv("LHD_TB_NSW");
    return e == nullptr || std::string_view(e) != "0";
  }();
  return v;
}

constexpr unsigned kMaxTbWidth = 16384;   // a testbench value wider than this is an error, not a slow compile
constexpr unsigned kDefaultTbWidth = 64;  // an un-annotated testbench local is at least this wide (the old driver's floor)

// Every testbench value is an LLVM integer of EXACTLY the bits it needs: two's complement, `w` bits including
// the sign bit (a non-negative value carries a spare 0 sign bit, so widening is always a sign extension).
// A comparison result is an i1 (`is_bool`).
struct Val {
  Value*   v       = nullptr;
  unsigned w       = 0;
  bool     is_bool = false;  // an i1
  bool     is_str  = false;  // a string: its value is the run time's interned handle (equal text, equal handle)
};

struct Assert_site {
  int         line = 0;
  std::string loc, cond, lhs, op, rhs, msg;
  bool        cmp = false, lhs_lit = false, rhs_lit = false;
  unsigned    lhs_bits = 0, rhs_bits = 0;  // widths of the operand values the runtime is handed
};

// One `puts`/`print`: literal text and value holes (`{name}`, `{}`, `{name:spec}`), formatted at run time.
struct Print_site {
  struct Seg {
    bool        hole = false;
    std::string text;        // literal text
    char        base = 'd';  // d b x X
    size_t      width = 0;
    bool        sep   = false;
    unsigned    bits  = 0;   // the hole value's width (signed)
  };
  std::string      prefix;  // "file:line:puts:"
  bool             newline = true;
  std::vector<Seg> segs;
};

// A literal with `?` bits: drawn ONCE per run by the runtime (or zero under sim.unknown_zero).
struct Unknown_site {
  unsigned    bits = 0;
  bool        sign = false;
  std::string known, mask;  // hex words, little endian, bits of the literal's value / of its `?` positions
  uint64_t    key = 0;
};

struct Instance {
  std::string var;
  const Dut*  dut  = nullptr;
  size_t      base = 0;
  // The testbench drives the DUT's clock port as a waveform (`acc.rclk = ..`): each `step` is then ONE clock event, the
  // rise kernels on a 0->1 change of the poked level, the fall kernels on 1->0, and settle only otherwise. The poked
  // level and the previous one live in two spilled locals (`__clk_<var>`, `__clkp_<var>`).
  bool        explicit_clk = false;
};

class Module_gen {
public:
  Module_gen() : mod_("prp_tb", ctx_), b_(ctx_) {
    i64_ = llvm::Type::getInt64Ty(ctx_);
    i1_  = llvm::Type::getInt1Ty(ctx_);
    ptr_ = llvm::PointerType::get(ctx_, 0);
    void_ = llvm::Type::getVoidTy(ctx_);
    kernel_ty_ = llvm::FunctionType::get(void_, {ptr_, ptr_}, false);
  }

  llvm::LLVMContext& ctx() { return ctx_; }
  llvm::Module&      mod() { return mod_; }
  llvm::IntegerType* ity(unsigned w) { return llvm::IntegerType::get(ctx_, w); }

  // Emit only the clock dispatchers used by this DUT's tests. A later test may use the other clock mode.
  void emit_dut(const Dut& d, bool explicit_clock) {
    const unsigned mode = explicit_clock ? 2 : 1;
    auto [it, first]    = dut_clock_modes_.try_emplace(d.stem, mode);
    if (!first && (it->second & mode) != 0) {
      return;
    }
    it->second |= mode;
    const auto fty = llvm::FunctionType::get(void_, {ptr_}, false);
    const auto mk  = [&](const std::string& name) {
      const bool exported = name.starts_with("sim_settle_") || name.starts_with("sim_dirty_");
      auto*      f
          = Function::Create(fty, exported ? llvm::GlobalValue::ExternalLinkage : llvm::GlobalValue::InternalLinkage, name, mod_);
      b_.SetInsertPoint(BasicBlock::Create(ctx_, "entry", f));
      return f;
    };
    // Activity gating: edge kernels own bits 0.., then fall, then settle (the order lnast_llvm numbered them in).
    // A native DUT kernel reads or modifies only its state allocation. This
    // also lets LLVM keep unrelated testbench locals across a kernel call.
    const auto kernel = [&](const std::string& name) {
      auto callee = mod_.getOrInsertFunction(name, kernel_ty_);
      llvm::cast<Function>(callee.getCallee())->setMemoryEffects(llvm::MemoryEffects::argMemOnly());
      return callee;
    };
    size_t     bit  = 0;
    const auto call = [&](Function* self, const std::string& name) {
      llvm::FunctionCallee callee = kernel(name);
      Value*               s      = self->getArg(0);
      if (d.gate_bits == 0) {
        b_.CreateCall(callee, {s, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_))});
        return;
      }
      const size_t   word = d.gate_word + bit / 64;
      const uint64_t mask = uint64_t{1} << (bit % 64);
      ++bit;
      Value* gp   = b_.CreateGEP(i64_, s, {ConstantInt::get(i64_, word)});
      Value* w    = b_.CreateLoad(i64_, gp);
      Value* on   = b_.CreateICmpNE(b_.CreateAnd(w, ConstantInt::get(i64_, mask)), ConstantInt::get(i64_, 0));
      auto*  then = BasicBlock::Create(ctx_, "run", self);
      auto*  cont = BasicBlock::Create(ctx_, "next", self);
      b_.CreateCondBr(on, then, cont);
      b_.SetInsertPoint(then);
      Value* w2 = b_.CreateLoad(i64_, gp);
      b_.CreateStore(b_.CreateAnd(w2, ConstantInt::get(i64_, ~mask)), gp);
      b_.CreateCall(callee, {s, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_))});
      b_.CreateBr(cont);
      b_.SetInsertPoint(cont);
    };
    const auto plain = [&](Function* self, const std::string& name) {
      llvm::FunctionCallee callee = kernel(name);
      b_.CreateCall(callee, {self->getArg(0), llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_))});
    };
    // A zero dirty word skips its entire group. Within a live group re-read every
    // bit in order: a kernel can mark a later kernel during this same phase.
    const auto phase = [&](Function* self, const std::vector<std::string>& functions, size_t first_bit) {
      bit = first_bit;
      for (size_t i = 0; i < functions.size();) {
        const size_t count = std::min(64 - bit % 64, functions.size() - i);
        BasicBlock*  next  = nullptr;
        if (d.gate_bits != 0 && count > 1) {
          const uint64_t mask   = (~uint64_t{0} >> (64 - count)) << (bit % 64);
          Value*         gp     = b_.CreateGEP(i64_, self->getArg(0), {ConstantInt::get(i64_, d.gate_word + bit / 64)});
          Value*         any    = b_.CreateAnd(b_.CreateLoad(i64_, gp), ConstantInt::get(i64_, mask));
          auto*          active = BasicBlock::Create(ctx_, "active", self);
          next                  = BasicBlock::Create(ctx_, "next.group", self);
          b_.CreateCondBr(b_.CreateIsNotNull(any), active, next);
          b_.SetInsertPoint(active);
        }
        for (size_t j = 0; j < count; ++j) {
          call(self, functions[i++]);
        }
        if (next) {
          b_.CreateBr(next);
          b_.SetInsertPoint(next);
        }
      }
    };
    const size_t fall_bit   = d.edge.size();
    const size_t settle_bit = fall_bit + d.fall.size();
    const auto   edge       = [&](Function* fn, bool fall) {
      phase(fn, fall ? d.fall : d.edge, fall ? fall_bit : 0);
      const auto& commit = fall ? d.fall_commit : d.commit;
      if (!commit.empty()) {
        plain(fn, commit);
      }
    };
    const auto finish = [&](Function* fn) {
      phase(fn, d.settle, settle_bit);
      b_.CreateRetVoid();
    };
    if (explicit_clock) {
      Function* rise = mk("sim_rise_" + d.sym);
      edge(rise, false);
      finish(rise);
      Function* fall = mk("sim_fall_" + d.sym);
      edge(fall, true);
      finish(fall);
    } else {
      Function* step = mk("sim_step_" + d.sym);
      edge(step, false);
      edge(step, true);
      finish(step);
    }
    if (!first) {
      return;
    }
    finish(mk("sim_settle_" + d.sym));
    Function* dirty = mk("sim_dirty_" + d.sym);
    for (size_t w = 0; w < (d.gate_bits + 63) / 64; ++w) {
      const bool     part = w + 1 == (d.gate_bits + 63) / 64 && d.gate_bits % 64 != 0;
      const uint64_t m    = part ? (uint64_t{1} << (d.gate_bits % 64)) - 1 : ~uint64_t{0};
      b_.CreateStore(ConstantInt::get(i64_, m), b_.CreateGEP(i64_, dirty->getArg(0), {ConstantInt::get(i64_, d.gate_word + w)}));
    }
    b_.CreateRetVoid();
  }

  llvm::IRBuilder<>& b() { return b_; }

  llvm::Type* i64_ = nullptr;
  llvm::Type* i1_  = nullptr;
  llvm::Type* ptr_ = nullptr;
  llvm::Type* void_ = nullptr;
  llvm::FunctionType* kernel_ty_ = nullptr;

private:
  llvm::LLVMContext   ctx_;
  llvm::Module        mod_;
  llvm::IRBuilder<>   b_;
  std::map<std::string, unsigned> dut_clock_modes_;
};

// ---- one test -------------------------------------------------------------------------------------------
struct Frame_slot {
  std::string       name;
  llvm::AllocaInst* slot  = nullptr;
  unsigned          width = 0;
  size_t            word  = 0;  // first 64-bit word in the frame
  size_t            words = 0;  // per element
  size_t            elems = 0;  // 0: a scalar; else an array of this many `width`-bit elements
};

struct Test_result {
  std::string              symbol;
  std::vector<Instance>    instances;
  size_t                   state_words = 0;
  std::vector<Assert_site> asserts;
  std::vector<Print_site>  prints;
  std::vector<Unknown_site> unknowns;
  std::vector<Print_site>  strings;
  std::vector<Param_raw>   params;
  std::vector<std::string> frame;  // spilled locals, then "%fails"
  size_t                   frame_words = 0;
};

class Test_gen {
public:
  Test_gen(Module_gen& m, const std::string& src, const std::string& file_short, const std::map<std::string, TSNode>& fconsts,
           const std::vector<Dut>& duts, bool trace)
      : m_(m), b_(m.b()), src_(src), file_short_(file_short), fconsts_(fconsts), duts_(duts), trace_(trace) {}

  Test_result gen(TSNode test, const std::string& name, size_t index) {
    TSNode code = field(test, "code");
    if (ts_node_is_null(code)) {
      fail("test '" + name + "' has no body");
    }
    Test_result r;
    r.params = read_params_raw(src_, test);
    std::vector<TSNode> stmts;
    for (TSNode c : ts_node_named_children(code)) {
      stmts.push_back(c);
    }
    // the DUT instances (`mut acc = Module`), laid end to end in the test's state.
    for (const auto s : stmts) {
      discover(s);
    }
    for (const auto s : stmts) {
      discover_clock_drives(s);
    }
    size_t base = 0;
    for (auto& inst : insts_) {
      inst.base = base;
      base      += inst.dut->words + 1;
      m_.emit_dut(*inst.dut, inst.explicit_clk);
    }
    r.instances   = insts_;
    r.state_words = base;

    // Every local's width, before any real code: an annotation wins, else the widest non-self-referential value
    // assigned to it (floored at kDefaultTbWidth). Decided by lowering the right-hand sides in a scratch function.
    for (const auto& p : r.params) {
      local_w_[p.name] = param_width(p);
    }
    for (const auto s : stmts) {
      collect_assigns(s);
    }
    infer_widths(r.params);

    const auto fty = llvm::FunctionType::get(m_.i64_, {m_.ptr_, m_.ptr_, m_.ptr_}, false);
    begin_function(llvm::GlobalValue::ExternalLinkage, std::format("tb_{}", index), fty);
    // The runner allocates the state, parameter words and runtime context
    // separately, including for each independent checkpoint replay.
    for (unsigned arg = 0; arg < 3; ++arg) {
      fn_->addParamAttr(arg, llvm::Attribute::NoAlias);
    }
    r.symbol = fn_->getName().str();
    bind_params(r.params);
    // a parameter whose default is an expression (`derived:U8 = std.valueplusarg("k", 2)`): evaluated here when the
    // run supplies no `+derived=`
    for (size_t i = 0; i < r.params.size(); ++i) {
      const auto& p = r.params[i];
      if (!p.dynamic || p.required) {
        continue;
      }
      Value* sup  = b_.CreateLoad(m_.i64_, b_.CreateGEP(m_.i64_, args_, {c64(static_cast<int64_t>(2 * i + 1))}));
      auto*  eval_bb = BasicBlock::Create(m_.ctx(), "param.default", fn_);
      auto*  cont    = BasicBlock::Create(m_.ctx(), "param.bound", fn_);
      b_.CreateCondBr(b_.CreateICmpNE(sup, c64(0)), cont, eval_bb);
      b_.SetInsertPoint(eval_bb);
      Val dv = dat(eval(p.default_node));
      b_.CreateStore(resize(dv.v, dv.w, local_w_[p.name]), locals_.at(p.name));
      b_.CreateBr(cont);
      b_.SetInsertPoint(cont);
    }
    // every local gets its slot (and frame index) up front, so a checkpoint can spill all of them
    for (const auto& p : r.params) {
      add_frame(p.name, locals_[p.name], local_w_[p.name]);
    }
    make_arrays();
    for (const auto s : stmts) {
      collect_locals(s);
    }
    for (auto& [an, arr] : arrays_) {
      add_frame_array(an, arr.slot, arr.width, arr.elems.size());
    }
    for (const auto& in : insts_) {
      if (in.explicit_clk) {
        for (const char* pre : {"__clk_", "__clkp_"}) {
          local_w_[pre + in.var] = 64;
          add_frame(pre + in.var, local(pre + in.var), 64);
        }
      }
    }
    add_frame("%fails", fails_, 64);
    tick_count_ = alloca_named("tick.count", 64);
    add_frame("%tick_count", tick_count_, 64);
    for (const auto& f : frame_) {
      r.frame.push_back(f.name);
    }
    r.frame_words = frame_words_;
    for (const auto s : stmts) {
      has_top_tick_ = has_top_tick_ || ntype(s) == "tick_statement";
    }
    int64_t index_of_stmt = 0;
    for (const auto s : stmts) {
      top_stmt_ = index_of_stmt;
      if (has_top_tick_) {
        // restart: statements before the resumed tick already ran (their effects are in the checkpoint)
        Value* skip = b_.CreateICmpSGT(b_.CreateLoad(m_.i64_, rt_word(prp_tb::kRtResumeStmt)), c64(index_of_stmt));
        auto*  run  = BasicBlock::Create(m_.ctx(), "top.run", fn_);
        auto*  next = BasicBlock::Create(m_.ctx(), "top.next", fn_);
        b_.CreateCondBr(skip, next, run);
        b_.SetInsertPoint(run);
        stmt(s);
        b_.CreateBr(next);
        b_.SetInsertPoint(next);
      } else {
        stmt(s);
      }
      ++index_of_stmt;
    }
    b_.CreateRet(b_.CreateLoad(m_.i64_, fails_));
    r.asserts   = std::move(sites_);
    r.prints    = std::move(prints_);
    r.unknowns  = std::move(unknowns_);
    r.strings   = std::move(strings_);
    return r;
  }

private:
  // ---- function context (a scratch function serves the width inference) ----
  void begin_function(llvm::GlobalValue::LinkageTypes link, const std::string& name, llvm::FunctionType* fty) {
    fn_    = Function::Create(fty, link, name, m_.mod());
    rt_    = fn_->getArg(0);
    st_    = fn_->getArg(1);
    args_  = fn_->getArg(2);
    entry_ = BasicBlock::Create(m_.ctx(), "entry", fn_);
    b_.SetInsertPoint(entry_);
    locals_.clear();
    loops_.clear();
    frame_.clear();
    frame_words_ = 0;
    fails_       = alloca_named("fails", 64);
    b_.CreateStore(c64(0), fails_);
  }

  static unsigned param_width(const Param_raw& p) {
    if (p.type.size() >= 2 && (p.type.front() == 'U' || p.type.front() == 'S')
        && p.type.find_first_not_of("0123456789", 1) == std::string::npos) {
      const auto bits = std::strtoul(p.type.c_str() + 1, nullptr, 10);
      if (bits == 0 || bits > kMaxTbWidth - (p.type.front() == 'U')) {
        fail("unsupported test parameter width " + p.type);
      }
      return static_cast<unsigned>(bits) + (p.type.front() == 'U');
    }
    if (p.is_string) {
      return 64;  // an interned string handle
    }
    return p.type == "Bool" ? 2 : kDefaultTbWidth;
  }

  void bind_params(const std::vector<Param_raw>& params) {
    for (size_t i = 0; i < params.size(); ++i) {
      const unsigned w         = local_w_[params[i].name];
      auto*          slot      = alloca_named(params[i].name, w);
      locals_[params[i].name]  = slot;
      // the runtime passes (value, supplied) per parameter
      Value*     raw           = b_.CreateLoad(m_.i64_, b_.CreateGEP(m_.i64_, args_, {c64(static_cast<int64_t>(2 * i))}));
      const bool unsigned_word = params[i].type.starts_with("U") && w > 64;
      b_.CreateStore(unsigned_word ? b_.CreateZExt(raw, ity(w)) : resize(raw, 64, w), slot);
      if (params[i].is_string) {
        str_locals_.insert(params[i].name);
      }
      if (const auto& ty = params[i].type; ty.size() >= 2 && ty[0] == 'U' && std::isdigit(static_cast<unsigned char>(ty[1])) != 0) {
        ubits_[params[i].name] = std::atoi(ty.c_str() + 1);
      }
    }
  }

  void add_frame(const std::string& name, llvm::AllocaInst* slot, unsigned w) {
    Frame_slot f;
    f.name  = name;
    f.slot  = slot;
    f.width = w;
    f.words = (w + 63) / 64;
    f.word  = frame_words_;
    frame_words_ += f.words;
    frame_.push_back(f);
  }

  // ---- discovery ----
  static std::string import_ref_of(const std::string& text) {
    static const std::regex re(R"re(import\(\s*"(?:lg:|ln:)?([^"]+)"\s*\))re");
    std::smatch             m;
    return std::regex_search(text, m, re) ? m[1].str() : std::string{};
  }

  const Dut* resolve_dut(TSNode rv) const {
    while (!ts_node_is_null(rv) && (ntype(rv) == "expression_item" || ntype(rv) == "paren_group") && ts_node_named_child_count(rv) == 1) {
      rv = ts_node_named_child(rv, 0);
    }
    if (ts_node_is_null(rv) || ntype(rv) != "identifier") {
      return nullptr;
    }
    const std::string nm = text_of(src_, rv);
    std::string       ref;
    if (const auto it = fconsts_.find(nm); it != fconsts_.end()) {
      ref = import_ref_of(text_of(src_, it->second));
      if (ref.empty()) {
        return nullptr;  // a plain constant, not an import
      }
      // An artifact reference (`lg:`/`ln:`) is exact by construction: it must name a simulated module, never fall back
      // to "the sole module" or "the unique root" (a typo would simulate some other module and pass).
      static const std::regex art(R"re(import\(\s*"(?:lg|ln):)re");
      if (std::regex_search(text_of(src_, it->second), art)) {
        for (const auto& d : duts_) {
          if (d.module == ref || d.stem == ref) {
            return &d;
          }
        }
        // an entity name (`lg:memimg` against a source defining `memimg.memimg`) is exact too, when only one module has it
        const Dut* by_tail = nullptr;
        size_t     tails   = 0;
        for (const auto& d : duts_) {
          const auto at   = d.module.find_last_of('.');
          const auto tail = d.module.substr(at == std::string::npos ? 0 : at + 1);
          if (tail == ref) {
            by_tail = &d;
            ++tails;
          }
        }
        if (tails == 1) {
          return by_tail;
        }
        std::string have;
        for (const auto& d : duts_) {
          have += (have.empty() ? "" : ", ") + d.module.substr(d.module.find_last_of('.') == std::string::npos ? 0 : d.module.find_last_of('.') + 1);
        }
        fail("import(\"" + ref + "\") names no simulated module (simulated modules: " + have + ")");
      }
    } else {
      ref = nm;
      // A bare module name several units define is the testbench file's OWN unit's one (`imp_tb.top`), never a guess
      // between them.
      const auto unit = file_short_.substr(0, file_short_.rfind('.'));
      for (const auto& d : duts_) {
        if (d.module == unit + "." + nm || d.stem == unit + "." + nm) {
          return &d;
        }
      }
    }
    const Dut* found = nullptr;
    for (const auto& d : duts_) {
      if (d.module == ref || d.stem == ref) {
        return &d;
      }
    }
    for (const auto& d : duts_) {  // the entity tail
      const auto tail = d.module.substr(d.module.find_last_of('.') == std::string::npos ? 0 : d.module.find_last_of('.') + 1);
      if (tail == ref || tail == nm) {
        if (found != nullptr) {
          return nullptr;  // ambiguous: never guess
        }
        found = &d;
      }
    }
    if (found == nullptr && fconsts_.count(nm) != 0 && duts_.size() == 1) {
      return &duts_.front();
    }
    return found;
  }

  void discover(TSNode n) {
    if (ts_node_is_null(n)) {
      return;
    }
    if (ntype(n) == "assignment") {
      const std::string ln = lvalue_name(src_, field(n, "lvalue"));
      if (!ln.empty() && !ts_node_is_null(field(n, "rvalue"))) {
        if (const Dut* d = resolve_dut(field(n, "rvalue")); d != nullptr) {
          if (inst_index(ln) < 0) {
            insts_.push_back({ln, d, 0, false});
          }
        }
      }
    }
    for (TSNode c : ts_node_named_children(n)) {
      discover(c);
    }
  }

  void discover_clock_drives(TSNode n) {
    if (ntype(n) == "assignment") {
      std::string base, port;
      if (inst_dot(field(n, "lvalue"), base, port)) {
        auto&       inst   = insts_[inst_index(base)];
        const auto& clocks = inst.dut->clocks;
        if (std::ranges::find(clocks, port) != clocks.end()
            && (!compound_binop(n).empty() || text_of(src_, field(n, "rvalue")) != "clock")) {
          if (clocks.size() > 1) {
            std::string names;
            for (const auto& clock : clocks) {
              names += (names.empty() ? "" : ", ") + base + "." + clock;
            }
            fail("independent clock waveforms are unsupported for " + names + " (explicit drive of " + base + "." + port + ")");
          }
          inst.explicit_clk = true;
        }
      }
    }
    for (TSNode c : ts_node_named_children(n)) {
      discover_clock_drives(c);
    }
  }

  int inst_index(const std::string& var) const {
    for (size_t i = 0; i < insts_.size(); ++i) {
      if (insts_[i].var == var) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  // The assignments that size a local: not a compound write and not self-referential (`total = total + 1` would
  // grow by one bit on every pass and never settle; an accumulator that needs more than the floor is annotated).
  struct Assign {
    std::string name;
    TSNode      rv{};
  };
  bool reads_name(TSNode n, const std::string& name) const {
    if (ts_node_is_null(n)) {
      return false;
    }
    if (ntype(n) == "identifier" && text_of(src_, n) == name) {
      return true;
    }
    for (TSNode c : ts_node_named_children(n)) {
      if (reads_name(c, name)) {
        return true;
      }
    }
    return false;
  }
  void collect_assigns(TSNode n) {
    if (ts_node_is_null(n)) {
      return;
    }
    if (ntype(n) == "assignment") {
      TSNode            lv = field(n, "lvalue");
      TSNode            rv = field(n, "rvalue");
      const std::string ln = lvalue_name(src_, lv);
      if (!ln.empty() && is_ref_call(rv)) {
        refs_[ln] = rv;
        return;
      }
      if (!ln.empty() && !ts_node_is_null(rv) && inst_index(ln) < 0 && ntype(rv) == "tuple_sq") {
        auto& arr = arrays_[ln];
        arr.elems.clear();
        for (TSNode c : ts_node_named_children(rv)) {
          arr.elems.push_back(c);
        }
      }
      if (!ln.empty() && !ts_node_is_null(rv) && inst_index(ln) < 0 && ntype(rv) != "tuple_sq") {
        if (const int dw = declared_slop_width(src_, lv); dw > 0) {
          local_w_[ln]   = std::max(local_w_.count(ln) ? local_w_[ln] : 0u, static_cast<unsigned>(dw));
          declared_.insert(ln);
        } else if (!local_w_.count(ln)) {
          local_w_[ln] = kDefaultTbWidth;
        }
        if (compound_binop(n).empty() && !reads_name(rv, ln)) {
          assigns_.push_back({ln, rv});
        }
      }
    }
    for (TSNode c : ts_node_named_children(n)) {
      collect_assigns(c);
    }
  }
  static int declared_slop_width(const std::string& src, TSNode lv) {
    if (ts_node_is_null(lv) || ntype(lv) != "typed_identifier") {
      return 0;
    }
    for (TSNode c : ts_node_named_children(lv)) {
      if (ntype(c) != "type_cast") {
        continue;
      }
      for (TSNode tc : ts_node_named_children(c)) {
        const auto        tt = ntype(tc);
        const std::string tx = text_of(src, tc);
        if (tx.size() < 2) {
          continue;
        }
        const int n = std::atoi(tx.c_str() + 1);
        if (n <= 0) {
          continue;
        }
        if (tt == "uint_type") {
          return n + 1;
        }
        if (tt == "int_type" || tt == "sint_type") {
          return n;
        }
      }
    }
    return 0;
  }
  void infer_widths(const std::vector<Param_raw>& params) {
    scratch_ = true;
    const auto fty = llvm::FunctionType::get(m_.i64_, {m_.ptr_, m_.ptr_, m_.ptr_}, false);
    for (int pass = 0; pass < 4; ++pass) {
      begin_function(llvm::GlobalValue::InternalLinkage, "__tb_scratch", fty);
      bind_params(params);
      make_arrays();
      bool changed = false;
      for (auto& [name, arr] : arrays_) {  // an array element is as wide as its widest initializer
        for (TSNode e : arr.elems) {
          const Val v = dat(eval(e));
          if (v.w > arr.width) {
            arr.width = v.w;
            changed   = true;
          }
        }
      }
      for (const auto& a : assigns_) {
        if (declared_.count(a.name) != 0) {
          continue;
        }
        const Val v = eval(a.rv);
        if (v.is_str) {
          str_locals_.insert(a.name);
        }
        if (v.w > local_w_[a.name]) {
          local_w_[a.name] = v.w;
          changed          = true;
        }
      }
      b_.CreateRetVoid();  // never verified: the function is dropped below
      fn_->eraseFromParent();
      fn_ = nullptr;
      if (!changed) {
        break;
      }
    }
    scratch_ = false;
    ubits_.clear();
  }

  // one alloca per test-local array, in the current function's entry block
  void make_arrays() {
    for (auto& [name, arr] : arrays_) {
      llvm::IRBuilder<> eb(entry_, entry_->begin());
      arr.slot = eb.CreateAlloca(llvm::ArrayType::get(ity(arr.width), std::max<size_t>(arr.elems.size(), 1)), nullptr, name);
    }
  }

  void add_frame_array(const std::string& name, llvm::AllocaInst* slot, unsigned w, size_t n) {
    Frame_slot f;
    f.name  = name;
    f.slot  = slot;
    f.width = w;
    f.words = (w + 63) / 64;
    f.elems = n;
    f.word  = frame_words_;
    frame_words_ += f.words * n;
    frame_.push_back(f);
  }

  // `name[index]`: the element, or 0 past the table (guarded: nothing is read outside it)
  Val array_read(const std::string& name, TSNode index) {
    auto&       arr = arrays_.at(name);
    const Val   iv  = dat(eval(index));
    const auto  n   = arr.elems.size();
    const unsigned cw = std::max<unsigned>(iv.w, 33);
    Value*      i   = resize(iv.v, iv.w, cw);
    Value*      ok  = b_.CreateAnd(b_.CreateICmpSGE(i, ConstantInt::get(ity(cw), 0)), b_.CreateICmpSLT(i, ConstantInt::get(ity(cw), n)));
    Value*      ix  = b_.CreateSelect(ok, resize(i, cw, 64), c64(0));
    Value*      p   = b_.CreateGEP(llvm::ArrayType::get(ity(arr.width), std::max<size_t>(n, 1)), arr.slot, {c64(0), ix});
    Value*      x   = b_.CreateLoad(ity(arr.width), p);
    return data(b_.CreateSelect(ok, x, ConstantInt::get(ity(arr.width), 0)), arr.width);
  }

  void array_write(const std::string& name, TSNode index, Val v) {
    auto&          arr = arrays_.at(name);
    const Val      iv  = dat(eval(index));
    const auto     n   = arr.elems.size();
    const unsigned cw  = std::max<unsigned>(iv.w, 33);
    Value*         i   = resize(iv.v, iv.w, cw);
    Value*         ok  = b_.CreateAnd(b_.CreateICmpSGE(i, ConstantInt::get(ity(cw), 0)), b_.CreateICmpSLT(i, ConstantInt::get(ity(cw), n)));
    auto*          then = BasicBlock::Create(m_.ctx(), "arr.set", fn_);
    auto*          cont = BasicBlock::Create(m_.ctx(), "arr.done", fn_);
    b_.CreateCondBr(ok, then, cont);
    b_.SetInsertPoint(then);
    Value* p = b_.CreateGEP(llvm::ArrayType::get(ity(arr.width), std::max<size_t>(n, 1)), arr.slot, {c64(0), resize(i, cw, 64)});
    b_.CreateStore(at(v, arr.width).v, p);
    b_.CreateBr(cont);
    b_.SetInsertPoint(cont);
  }

  // the base name and index expression of `name[index]`, or false
  bool array_ref(TSNode n, std::string& name, TSNode& index) const {
    if (ts_node_is_null(n) || ntype(n) != "member_selection") {
      return false;
    }
    TSNode base = ts_node_named_child(n, 0);
    if (ntype(base) != "identifier") {
      return false;
    }
    name = text_of(src_, base);
    if (arrays_.count(name) == 0) {
      return false;
    }
    for (uint32_t i = 1; i < ts_node_named_child_count(n); ++i) {
      TSNode c = ts_node_named_child(n, i);
      if (ntype(c) == "select") {
        for (TSNode sc : ts_node_named_children(c)) {
          index = sc;
        }
        return !ts_node_is_null(index);
      }
    }
    return false;
  }

  // `regref(acc.field)` / `regref("unit/field")`: a writable alias of one cell of the design (a register, possibly
  // inside a sub-instance), bound once. Reading it is a load of the cell, writing it forces the cell.
  bool is_ref_call(TSNode n) const {
    if (ts_node_is_null(n) || ntype(n) != "function_call_expression") {
      return false;
    }
    TSNode fn = field(n, "function");
    return !ts_node_is_null(fn) && text_of(src_, fn) == "regref";
  }

  // The instance variable and field path a `regref` call names.
  void ref_target(const std::string& alias, std::string& base, std::string& fld) const {
    TSNode call = refs_.at(alias);
    TSNode args = field(call, "argument");
    if (ts_node_is_null(args) || ts_node_named_child_count(args) < 1) {
      fail("regref needs a signal: a dotted `acc.field` or a \"unit/field\" path");
    }
    TSNode a0 = ts_node_named_child(args, 0);
    while (ts_node_named_child_count(a0) == 1 && (ntype(a0) == "expression_item" || ntype(a0) == "constant")) {
      a0 = ts_node_named_child(a0, 0);
    }
    if (inst_dot(a0, base, fld)) {
      return;
    }
    std::string path = text_of(src_, a0);
    if (path.size() >= 2 && (path.front() == '"' || path.front() == '\'') && path.back() == path.front()) {
      path = path.substr(1, path.size() - 2);
      if (const auto slash = path.find('/'); slash != std::string::npos) {
        const std::string unit = path.substr(0, slash);
        fld                    = path.substr(slash + 1);
        if (inst_index(unit) >= 0) {
          base = unit;
          return;
        }
        for (const auto& in : insts_) {  // the unit may name the module instead of the variable
          const auto tail = in.dut->module.substr(in.dut->module.find_last_of('.') == std::string::npos ? 0 : in.dut->module.find_last_of('.') + 1);
          if (in.dut->module == unit || tail == unit) {
            base = in.var;
            return;
          }
        }
      }
    }
    fail("regref does not name a cell of an instance: " + text_of(src_, call).substr(0, 60));
  }

  // Force a register through its alias: store its words, then wake every kernel of the instance (the readers of a
  // register are not tracked per word).
  void reg_write(const std::string& base, const std::string& fld, Val v) {
    const int       ix = inst_index(base);
    const Instance& in = insts_[static_cast<size_t>(ix)];
    const Field*    f  = in.dut->find(fld);
    if (f == nullptr || f->kind == 3) {
      fail("unknown cell '" + fld + "' on instance '" + base + "' (regref drives a port or a register)");
    }
    if (f->kind == 0) {
      port_write(base, fld, v);
      return;
    }
    const unsigned n  = (f->bits + 63) / 64;
    const Val      d  = dat(v);
    Value*         x  = poke_value(d, f->bits);
    const unsigned pb = phys(f->bits);
    if (!f->sign && f->bits < pb) {
      x = b_.CreateAnd(x, ConstantInt::get(m_.ity(pb), low_mask(pb, f->bits)));
    }
    if (pb != n * 64) {
      x = f->sign ? b_.CreateSExt(x, wty(n * 64)) : b_.CreateZExt(x, wty(n * 64));
    }
    b_.CreateAlignedStore(x, word_ptr(in, f->word), llvm::Align(8));
    auto dirty = m_.mod().getOrInsertFunction("sim_dirty_" + in.dut->sym, llvm::FunctionType::get(m_.void_, {m_.ptr_}, false));
    b_.CreateCall(dirty, {b_.CreateGEP(m_.i64_, st_, {c64(static_cast<int64_t>(in.base))})});
  }

  // Create the slot of every local the body assigns (an identifier lvalue that is not an instance or a port).
  void collect_locals(TSNode n) {
    if (ts_node_is_null(n)) {
      return;
    }
    if (ntype(n) == "assignment") {
      const std::string ln = lvalue_name(src_, field(n, "lvalue"));
      TSNode            rv = field(n, "rvalue");
      if (!ln.empty() && !ts_node_is_null(rv) && inst_index(ln) < 0 && ntype(rv) != "tuple_sq" && !is_ref_call(rv) && locals_.find(ln) == locals_.end()) {
        local(ln);
        add_frame(ln, locals_[ln], local_w_[ln]);
      }
    }
    for (TSNode c : ts_node_named_children(n)) {
      collect_locals(c);
    }
  }

  // ---- IR helpers ----
  // the PHYSICAL width of a value of semantic width `w`
  static unsigned phys(unsigned w) {
    if (!tb_round_widths()) {
      return w;
    }
    return w <= 32 ? 32 : w <= 64 ? 64 : (w + 63) / 64 * 64;
  }
  // the LLVM type of a value of semantic width `w`: its physical width
  llvm::IntegerType* ity(unsigned w) {
    if (w == 0 || w > kMaxTbWidth) {
      fail("a testbench value of " + std::to_string(w) + " bits is outside the supported 1.." + std::to_string(kMaxTbWidth) + " range");
    }
    return m_.ity(phys(w));
  }
  // exactly `w` bits (words in memory)
  llvm::IntegerType* wty(unsigned w) { return m_.ity(w); }
  // the low `n` bits of a value of physical width `pw`
  static llvm::APInt low_mask(unsigned pw, unsigned n) { return llvm::APInt::getLowBitsSet(pw, n); }
  llvm::AllocaInst* alloca_named(const std::string& name, unsigned w) {
    llvm::IRBuilder<> eb(entry_, entry_->begin());
    auto*             a = eb.CreateAlloca(ity(w), nullptr, name);
    slot_w_[a]          = w;
    return a;
  }
  llvm::AllocaInst* local(const std::string& name) {
    auto it = locals_.find(name);
    if (it != locals_.end()) {
      return it->second;
    }
    const unsigned w   = local_w_.count(name) ? local_w_[name] : kDefaultTbWidth;
    auto*          slot = alloca_named(name, w);
    // the entry-block store keeps a read-before-write defined (a Pyrope local starts at 0)
    llvm::IRBuilder<> eb(entry_);
    eb.SetInsertPoint(slot->getNextNode());
    eb.CreateStore(ConstantInt::get(ity(w), 0), slot);
    locals_[name] = slot;
    return slot;
  }
  Value* c64(int64_t v) { return ConstantInt::get(m_.i64_, static_cast<uint64_t>(v)); }
  Value* cst(unsigned w, int64_t v) { return ConstantInt::get(ity(w), static_cast<uint64_t>(v), true); }
  Value* word_ptr(const Instance& in, size_t word) { return b_.CreateGEP(m_.i64_, st_, {c64(static_cast<int64_t>(in.base + word))}); }
  Value* rt_word(int index) { return b_.CreateGEP(m_.i64_, rt_, {c64(index)}); }

  // The low `w` bits of an integer of any physical width, as a CANONICAL value of semantic width `w`: its type is
  // phys(w) and the bits above `w` are copies of bit w-1 (so widening is free and compares/divides need no masking).
  Value* wrap(Value* v, unsigned w) {
    const unsigned have = v->getType()->getIntegerBitWidth();
    const unsigned pt   = phys(w);
    if (have > pt) {
      v = b_.CreateTrunc(v, m_.ity(pt));
    } else if (have < pt) {
      v = b_.CreateSExt(v, m_.ity(pt));
    }
    if (w < pt) {  // re-establish the sign extension from bit w-1
      Value* sh = ConstantInt::get(m_.ity(pt), pt - w);
      v         = b_.CreateAShr(b_.CreateShl(v, sh), sh);
    }
    return v;
  }
  // a canonical value of semantic width `from` as a canonical value of semantic width `to`: sign-extends on
  // widen (free when the physical widths agree), wraps on narrow
  Value* resize(Value* v, unsigned from, unsigned to) {
    if (from == to) {
      return v;
    }
    if (to > from) {
      return phys(to) == phys(from) ? v : b_.CreateSExt(v, ity(to));
    }
    return wrap(v, to);
  }
  // A value poked into a port or register of `bits` bits. A testbench local is a 64-bit wrapping accumulator by default
  // (an LCG state, a hash), so a value of EXACTLY 64 bits is a bit pattern: poking it into a wider port zero-extends
  // (the reference driver's `__prp_poke` and the Verilog goldens do), where every other width keeps its sign.
  Value* poke_value(const Val& d, unsigned bits) {
    if (d.w == 64 && bits > 64) {
      return b_.CreateZExt(resize(d.v, 64, 64), ity(bits));
    }
    return resize(d.v, d.w, bits);
  }
  // `v` as a DATA value (a bool enters the data plane as the integer 0/1 -- never all ones)
  Val dat(Val v) {
    if (!v.is_bool) {
      return v;
    }
    return {b_.CreateZExt(v.v, ity(2)), 2, false};
  }
  Val data(Value* v, unsigned w) { return {v, w, false}; }
  Val load_local(llvm::AllocaInst* slot) {
    const unsigned w = slot_w_.at(slot);
    return data(b_.CreateLoad(slot->getAllocatedType(), slot), w);
  }
  Val boolean(Value* v) { return {v, 1, true}; }
  // `v` re-expressed at width `w`: sign-extends on widen, wraps on narrow
  Val at(Val v, unsigned w) {
    v = dat(v);
    return {resize(v.v, v.w, w), w, false};
  }
  Value* to_i1(Val v) { return v.is_bool ? v.v : b_.CreateICmpNE(v.v, ConstantInt::get(ity(v.w), 0)); }
  Value* as_i64(Val v) {
    v = dat(v);
    return resize(v.v, v.w, 64);
  }
  std::optional<llvm::APInt> const_of(Val v) {
    if (auto* ci = llvm::dyn_cast<ConstantInt>(v.v)) {
      return ci->getValue();
    }
    return std::nullopt;
  }

  // The value of a port/literal held in whole words (`x` is iN, N = 64 * words): the low `bits` bits, as a value of
  // semantic width `bits` (signed) or `bits + 1` (unsigned: a spare 0 sign bit).
  Val from_words(Value* x, unsigned bits, bool sign) {
    if (sign) {
      return data(wrap(x, bits), bits);
    }
    const unsigned w = bits + 1, pw = phys(w);
    const unsigned have = x->getType()->getIntegerBitWidth();
    Value*         y    = have > pw ? b_.CreateTrunc(x, ity(w)) : have < pw ? b_.CreateZExt(x, ity(w)) : x;
    if (bits < pw) {  // clear everything above the value's own bits
      y = b_.CreateAnd(y, ConstantInt::get(ity(w), low_mask(pw, bits)));
    }
    return data(y, w);
  }

  // a value held in 64-bit words in memory (little endian, sign extended over its last word): what the runtime reads
  llvm::AllocaInst* spill_words(Val v, const std::string& name) {
    v                = dat(v);
    const unsigned n = (v.w + 63) / 64;
    llvm::IRBuilder<> eb(entry_, entry_->begin());
    auto*             buf = eb.CreateAlloca(wty(n * 64), nullptr, name);
    b_.CreateStore(resize(v.v, v.w, n * 64), buf);
    return buf;
  }

  void new_block_after_terminator() { b_.SetInsertPoint(BasicBlock::Create(m_.ctx(), "dead", fn_)); }

  // ---- expressions ----
  // `const T = (const lo = 1, const hi = 9)`: the right-hand side of field `fld` of the file-scope tuple `base`
  TSNode file_const_field(const std::string& base, const std::string& fld) const {
    const auto it = fconsts_.find(base);
    if (it == fconsts_.end()) {
      return TSNode{};
    }
    TSNode tup = it->second;
    while (!ts_node_is_null(tup) && ntype(tup) != "tuple" && ts_node_named_child_count(tup) == 1
           && (ntype(tup) == "expression_item" || ntype(tup) == "expression_list" || ntype(tup) == "paren_group")) {
      tup = ts_node_named_child(tup, 0);
    }
    if (ts_node_is_null(tup) || ntype(tup) != "tuple") {
      return TSNode{};
    }
    for (TSNode item : ts_node_named_children(tup)) {
      if (ntype(item) == "assignment" && lvalue_name(src_, field(item, "lvalue")) == fld) {
        return field(item, "rvalue");
      }
    }
    return TSNode{};
  }

  Val eval_file_const(const std::string& nm, TSNode rv) {
    if (!const_stack_.insert(nm).second) {
      fail("file-scope const '" + nm + "' reads itself");
    }
    Val v = eval(rv);
    const_stack_.erase(nm);
    return v;
  }

  Val port_read(const std::string& base, const std::string& fld) {
    const int ix = inst_index(base);
    if (ix < 0) {
      fail("`" + base + "` is not a declared instance");
    }
    const Instance& in = insts_[static_cast<size_t>(ix)];
    const Field*    f  = in.dut->find(fld);
    if (f == nullptr && std::find(in.dut->clocks.begin(), in.dut->clocks.end(), fld) != in.dut->clocks.end()) {
      // a clock port has no state word; read, it is the tick's cycle count (0 before any tick)
      Value* cyc = b_.CreateLoad(m_.i64_, rt_word(prp_tb::kRtCycle));
      return data(b_.CreateSelect(b_.CreateICmpSLT(cyc, c64(0)), c64(0), cyc), 64);
    }
    if (f == nullptr || f->kind == 3) {
      fail("unknown field '" + fld + "' on instance '" + base + "' of module '" + in.dut->module + "' (the LLVM testbench reads ports and registers)");
    }
    const unsigned n = (f->bits + 63) / 64;
    Value*         x = b_.CreateAlignedLoad(wty(n * 64), word_ptr(in, f->word), llvm::Align(8));
    return from_words(x, f->bits, f->sign);  // signed: exactly `bits`; unsigned: `bits` plus a spare 0 sign bit
  }

  void port_write(const std::string& base, const std::string& fld, Val v) {
    const int ix = inst_index(base);
    const Instance& in = insts_[static_cast<size_t>(ix)];
    const Field*    f  = in.dut->find(fld);
    if (f == nullptr && std::find(in.dut->clocks.begin(), in.dut->clocks.end(), fld) != in.dut->clocks.end()) {
      if (in.explicit_clk) {  // the waveform level the next `step` turns into an edge (or none)
        Value* lvl = b_.CreateAnd(b_.CreateZExtOrTrunc(poke_value(dat(v), 1), m_.i64_), c64(1));
        b_.CreateStore(lvl, locals_.at("__clk_" + base));
      }
      return;  // otherwise driving a clock port has no effect here: `step` is the clock
    }
    if (f == nullptr || f->kind != 0) {
      fail("cannot drive '" + base + "." + fld + "': it is not an input of module '" + in.dut->module + "'");
    }
    const unsigned n = (f->bits + 63) / 64;
    // wrap to the port's width, then extend over its words (a signed port sign-extended, as the old class stored it)
    const Val d = dat(v);
    Value*    x = poke_value(d, f->bits);  // canonical at phys(bits)
    const unsigned pb = phys(f->bits);
    if (!f->sign && f->bits < pb) {  // an unsigned port keeps zeros above its bits
      x = b_.CreateAnd(x, ConstantInt::get(m_.ity(pb), low_mask(pb, f->bits)));
    }
    if (pb != n * 64) {
      x = f->sign ? b_.CreateSExt(x, wty(n * 64)) : b_.CreateZExt(x, wty(n * 64));
    }
    Value* p = word_ptr(in, f->word);
    if (f->marks.empty()) {
      b_.CreateAlignedStore(x, p, llvm::Align(8));
      return;
    }
    // Activity gating: an input that changed wakes the kernels that read it.
    Value* old  = b_.CreateAlignedLoad(wty(n * 64), p, llvm::Align(8));
    auto*  then = BasicBlock::Create(m_.ctx(), "poke", fn_);
    auto*  cont = BasicBlock::Create(m_.ctx(), "poked", fn_);
    b_.CreateCondBr(b_.CreateICmpNE(old, x), then, cont);
    b_.SetInsertPoint(then);
    b_.CreateAlignedStore(x, p, llvm::Align(8));
    std::map<size_t, uint64_t> masks;
    for (const auto bit : f->marks) {
      masks[in.dut->gate_word + bit / 64] |= uint64_t{1} << (bit % 64);
    }
    for (const auto& [word, mask] : masks) {
      Value* gp = word_ptr(in, word);
      b_.CreateStore(b_.CreateOr(b_.CreateLoad(m_.i64_, gp), c64(static_cast<int64_t>(mask))), gp);
    }
    b_.CreateBr(cont);
    b_.SetInsertPoint(cont);
  }

  bool inst_dot(TSNode n, std::string& base, std::string& fld) const {
    if (ts_node_is_null(n) || ntype(n) != "dot_expression" || ts_node_named_child_count(n) < 2) {
      return false;
    }
    const auto unq = [](std::string t) {
      if (t.size() >= 2 && t.front() == '`' && t.back() == '`') {
        return t.substr(1, t.size() - 2);
      }
      return t;
    };
    base = unq(text_of(src_, ts_node_named_child(n, 0)));
    fld.clear();
    for (uint32_t k = 1; k < ts_node_named_child_count(n); ++k) {
      fld += (fld.empty() ? "" : ".") + unq(text_of(src_, ts_node_named_child(n, k)));
    }
    if (inst_index(base) < 0) {
      if (const auto dot = base.find('.'); dot != std::string::npos && inst_index(base.substr(0, dot)) >= 0) {
        fld  = base.substr(dot + 1) + "." + fld;
        base = base.substr(0, dot);
      }
    }
    return inst_index(base) >= 0;
  }

  // An integer literal of any size: exactly the bits its value needs plus a sign bit. A `?` bit makes it a literal
  // the runtime draws once per run.
  Val literal(const std::string& raw) {
    const auto parsed = livehd::parse_pyrope_integer_spelling(raw);
    if (!parsed) {
      fail("invalid integer literal '" + raw + "'");
    }
    const auto& lit = *parsed;
    const auto  bpd = lit.bits_per_digit();
    if (lit.digits.size() > (std::numeric_limits<unsigned>::max() - 1) / bpd) {
      fail("integer literal is too large: " + raw);
    }
    const unsigned bits = static_cast<unsigned>(lit.digits.size()) * bpd;
    if (lit.unknown()) {
      const unsigned width = bits + !lit.signed_pattern;
      if (width > kMaxTbWidth) {
        fail("integer literal is wider than " + std::to_string(kMaxTbWidth) + " bits");
      }
      return unknown_literal(raw, lit.digits, lit.radix, lit.signed_pattern, lit.negate);
    }
    llvm::APInt v(bits + 1, llvm::StringRef(lit.digits), static_cast<uint8_t>(lit.radix));
    if (lit.negative_pattern()) {
      v = v.trunc(bits).sext(bits + 1);
    }
    if (lit.negate) {
      v = -v;
    }
    const unsigned width = v.isNegative() ? v.getSignificantBits() : std::max(2u, v.getActiveBits() + 1);
    if (width > kMaxTbWidth) {
      fail("integer literal is wider than " + std::to_string(kMaxTbWidth) + " bits");
    }
    return data(ConstantInt::get(m_.ctx(), v.sextOrTrunc(phys(width))), width);
  }

  // `0ub????????`, `0sb?`, `0x??`: known bits are fixed, each `?` bit is drawn ONCE per run (zero under
  // sim.unknown_zero). The runtime writes the words into a slot filled at function entry.
  Val unknown_literal(const std::string& raw, const std::string& body, unsigned radix, bool sign, bool neg) {
    if (neg) {
      fail("a negative literal with `?` bits is not supported: " + raw);
    }
    const unsigned bpd  = radix == 2 ? 1 : 4;
    const unsigned bits = static_cast<unsigned>(body.size()) * bpd;
    llvm::APInt    known(bits, 0), mask(bits, 0);
    for (size_t i = 0; i < body.size(); ++i) {
      const unsigned pos = static_cast<unsigned>(body.size() - 1 - i) * bpd;
      const char     c   = body[i];
      if (c == '?') {
        mask.setBits(pos, pos + bpd);
        continue;
      }
      const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? 10 + c - 'a' : c >= 'A' && c <= 'F' ? 10 + c - 'A' : -1;
      if (d < 0 || static_cast<unsigned>(d) >= radix) {
        fail("integer literal '" + raw + "' has a bad digit");
      }
      known |= llvm::APInt(bits, static_cast<uint64_t>(d)).shl(pos);
    }
    const unsigned w = sign ? bits : bits + 1;  // `0ub..` is unsigned (spare sign bit); `0sb..` carries its own sign
    if (scratch_) {
      return data(ConstantInt::get(ity(w), 0), w);
    }
    Unknown_site site;
    site.bits = bits;
    site.sign = sign;
    site.key  = std::hash<std::string>{}(raw);
    const auto hex = [&](const llvm::APInt& a) {
      std::string out;
      for (unsigned wd = 0; wd < (bits + 63) / 64; ++wd) {
        out += std::format("{}{:x}", wd ? "," : "", a.extractBitsAsZExtValue(std::min(64u, bits - wd * 64), wd * 64));
      }
      return out;
    };
    site.known = hex(known);
    site.mask  = hex(mask);
    const auto idx = static_cast<uint32_t>(unknowns_.size());
    unknowns_.push_back(site);
    // one slot per site, filled by the runtime when the test starts (the draw is once per run)
    const unsigned n = (bits + 63) / 64;
    llvm::IRBuilder<> eb(entry_, entry_->begin());
    auto*             buf = eb.CreateAlloca(wty(n * 64), nullptr, "unk");
    auto*             uty = llvm::FunctionType::get(m_.void_, {m_.ptr_, llvm::Type::getInt32Ty(m_.ctx()), m_.ptr_}, false);
    // emitted at the START of the entry block so every use sees it
    llvm::IRBuilder<> cb(buf->getNextNode());
    Value*            fp = cb.CreateLoad(m_.ptr_, cb.CreateGEP(m_.i64_, rt_, {c64(prp_tb::kRtUnknown)}));
    cb.CreateCall(uty, fp, {rt_, ConstantInt::get(llvm::Type::getInt32Ty(m_.ctx()), idx), buf});
    return from_words(b_.CreateLoad(wty(n * 64), buf), bits, sign);
  }

  static bool is_unary_op(std::string_view t) { return t == "op_log_not" || t == "op_bit_not" || t == "op_unary_minus"; }
  static bool is_binary_op(std::string_view t) {
    return t == "binary_compare_op" || t == "binary_other_op" || t == "binary_times_op" || t == "binary_logical_op" || t == "binary_step_op";
  }
  static int prec_class(std::string_view t) {
    if (t == "binary_logical_op") {
      return 0;
    }
    if (t == "binary_compare_op") {
      return 1;
    }
    if (t == "binary_other_op" || t == "binary_step_op") {
      return 2;
    }
    if (t == "binary_times_op") {
      return 3;
    }
    return -1;
  }

  Val eval(TSNode n) {
    {
      TSNode lit = n;
      while (ts_node_named_child_count(lit) == 1
             && (ntype(lit) == "expression_item" || ntype(lit) == "expression_list" || ntype(lit) == "paren_group" || ntype(lit) == "constant")) {
        lit = ts_node_named_child(lit, 0);
      }
      if (ntype(lit) == "string_literal" || ntype(lit) == "interpolated_string_literal") {
        return string_value(lit);
      }
    }
    const auto t = ntype(n);
    if (t == "identifier") {
      const std::string nm = text_of(src_, n);
      if (refs_.count(nm) != 0) {  // an alias reads its cell afresh on every use
        std::string base, fld;
        ref_target(nm, base, fld);
        return port_read(base, fld);
      }
      if (const auto it = locals_.find(nm); it != locals_.end()) {
        Val x    = load_local(it->second);
        x.is_str = str_locals_.count(nm) != 0;
        return x;
      }
      if (nm == "nil") {
        return data(cst(2, 0), 2);
      }
      if (const auto it = fconsts_.find(nm); it != fconsts_.end()) {
        return eval_file_const(nm, it->second);
      }
      if (inst_index(nm) >= 0) {
        fail("instance '" + nm + "' used as a value");
      }
      // a local read before any write is the zero it starts as
      return load_local(local(nm));
    }
    if (t == "constant") {
      for (TSNode c : ts_node_named_children(n)) {
        if (ntype(c) == "integer_literal") {
          return literal(text_of(src_, c));
        }
        if (ntype(c) == "bool_literal") {
          return boolean(b_.getInt1(text_of(src_, c) == "true"));
        }
      }
      return literal(text_of(src_, n));
    }
    if (t == "integer_literal") {
      return literal(text_of(src_, n));
    }
    if (t == "bool_literal") {
      return boolean(b_.getInt1(text_of(src_, n) == "true"));
    }
    if (t == "unary_expression") {
      std::string op;
      TSNode      opnd{};
      for (TSNode c : ts_node_named_children(n)) {
        if (is_unary_op(ntype(c))) {
          op = text_of(src_, c);
        } else {
          opnd = c;
        }
      }
      if (ts_node_is_null(opnd)) {
        fail("unary operator with no operand: " + text_of(src_, n).substr(0, 40));
      }
      return unary(op, eval(opnd), op == "~" ? unsigned_bits(opnd) : 0);
    }
    if (t == "dot_expression") {
      std::string base, fld;
      if (inst_dot(n, base, fld)) {
        return port_read(base, fld);
      }
      if (ts_node_named_child_count(n) == 2) {  // `T.lo`: a field of a file-scope tuple constant
        const std::string b0 = text_of(src_, ts_node_named_child(n, 0));
        const std::string f0 = text_of(src_, ts_node_named_child(n, 1));
        if (const TSNode rv = file_const_field(b0, f0); !ts_node_is_null(rv)) {
          return eval_file_const(b0 + "." + f0, rv);
        }
      }
      fail("unsupported dot expression: " + text_of(src_, n).substr(0, 40));
    }
    if (t == "function_call_expression") {
      TSNode      fn   = field(n, "function");
      std::string fnnm = ts_node_is_null(fn) ? "" : text_of(src_, fn);
      TSNode      args = field(n, "argument");
      if (fnnm == "Bool" && !ts_node_is_null(args) && ts_node_named_child_count(args) == 1) {
        return boolean(to_i1(eval(ts_node_named_child(args, 0))));
      }
      if (fnnm.size() >= 2 && fnnm[0] == 'U' && std::all_of(fnnm.begin() + 1, fnnm.end(), [](unsigned char c) { return std::isdigit(c) != 0; })
          && !ts_node_is_null(args) && ts_node_named_child_count(args) == 1) {
        const int w = std::stoi(fnnm.substr(1));
        if (w <= 0 || w > static_cast<int>(kMaxTbWidth)) {
          fail("width-cast '" + fnnm + "' is outside the supported range");
        }
        // the low `w` bits, read back as a non-negative number of its own width
        const unsigned uw = static_cast<unsigned>(w);
        Val            x  = dat(eval(ts_node_named_child(args, 0)));
        Value*         v  = resize(x.v, x.w, uw + 1);
        if (uw < phys(uw + 1)) {
          v = b_.CreateAnd(v, ConstantInt::get(ity(uw + 1), low_mask(phys(uw + 1), uw)));
        } else {  // exactly N+1 bits: clear the sign bit
          v = b_.CreateAnd(v, ConstantInt::get(ity(uw + 1), low_mask(uw + 1, uw)));
        }
        return data(v, uw + 1);
      }
      if (fnnm == "std.testplusarg" || fnnm == "std.valueplusarg") {
        const bool presence = fnnm == "std.testplusarg";
        const auto count    = ts_node_is_null(args) ? 0u : ts_node_named_child_count(args);
        if (count < 1 || count > (presence ? 1u : 2u)) {
          fail(fnnm + " expects a key and, for valueplusarg, an optional default");
        }
        Val key = eval(ts_node_named_child(args, 0));
        if (!key.is_str) {
          fail(fnnm + " key must be a string");
        }
        int64_t mode     = 0;
        Value*  fallback = c64(0);
        if (!presence) {
          mode = 1;
          if (count == 2) {
            TSNode def = ts_node_named_child(args, 1);
            while ((ntype(def) == "expression_item" || ntype(def) == "expression_list") && ts_node_named_child_count(def) == 1) {
              def = ts_node_named_child(def, 0);
            }
            if (ntype(def) == "assignment" || ntype(def) == "arg_assignment") {
              if (text_of(src_, field(def, "lvalue")) != "default") {
                fail("valueplusarg optional argument is named default");
              }
              def = field(def, "rvalue");
            }
            Val dv = eval(def);
            if (dv.is_str) {
              mode = 2;
            }
            fallback = as_i64(dv);
          }
        }
        auto*  fty = llvm::FunctionType::get(m_.i64_, {m_.ptr_, m_.i64_, m_.i64_, m_.i64_, m_.i64_}, false);
        Value* fp  = b_.CreateLoad(m_.ptr_, rt_word(prp_tb::kRtPlusarg));
        Value* r   = b_.CreateCall(fty, fp, {rt_, c64(mode), as_i64(key), fallback, c64(count == 2 ? 1 : 0)});
        if (presence) {
          return boolean(b_.CreateICmpNE(r, c64(0)));
        }
        Val out = data(r, 64);
        out.is_str = mode == 2;
        return out;
      }
      fail("function `" + fnnm + "` in a test expression is not lowered yet");
    }
    if (t == "if_expression") {
      return eval_if_expression(n);
    }
    if (t == "bit_selection") {
      return eval_bit_select(n);
    }
    if (t == "member_selection") {
      std::string name;
      TSNode      index{};
      if (array_ref(n, name, index)) {
        return array_read(name, index);
      }
      fail("`" + text_of(src_, n).substr(0, 40) + "`: only test-local arrays can be indexed in a test expression");
    }
    // a flat operand/operator run (expression_item / paren_group / tuple)
    return eval_seq(n);
  }

  // `if c { a } elif d { b } else { z }` as a VALUE: every arm one expression, an `else` required.
  Val eval_if_expression(TSNode n) {
    std::vector<std::pair<TSNode, TSNode>> arms;
    TSNode                                 else_body{};
    TSNode                                 pending_cond{};
    bool                                   have_cond = false;
    bool                                   in_else   = false;
    const uint32_t                         nc        = ts_node_child_count(n);
    for (uint32_t i = 0; i < nc; ++i) {
      const char* fname = ts_node_field_name_for_child(n, i);
      TSNode      c     = ts_node_child(n, i);
      if (fname == nullptr) {
        continue;
      }
      const std::string_view f(fname);
      if (f == "condition") {
        pending_cond = c;
        have_cond    = true;
      } else if (f == "code") {
        if (in_else) {
          else_body = c;
          in_else   = false;
        } else if (have_cond) {
          arms.emplace_back(pending_cond, c);
          have_cond = false;
        }
      } else if (f == "else") {
        if (ntype(c) == "scope_statement") {
          else_body = c;
          in_else   = false;
        } else if (text_of(src_, c) == "else") {
          in_else = true;
        }
      } else if (f == "init") {
        fail("an `if` used as a value in a test may not declare variables in its header: " + text_of(src_, n).substr(0, 40));
      }
    }
    if (arms.empty()) {
      fail("if-expression with no arm in test: " + text_of(src_, n).substr(0, 40));
    }
    if (ts_node_is_null(else_body)) {
      fail("an `if` used as a value in a test needs an `else` arm (otherwise the unmatched case has no value): " + text_of(src_, n).substr(0, 40));
    }
    const auto arm_node = [&](TSNode block) {
      std::vector<TSNode> stmts;
      for (TSNode c : ts_node_named_children(block)) {
        stmts.push_back(c);
      }
      if (stmts.size() != 1) {
        fail("an `if` arm used as a value in a test must hold exactly one expression: " + text_of(src_, block).substr(0, 40));
      }
      return stmts[0];
    };
    // Each arm is lowered in its own block and branches to an (empty) extension block; once every arm's width is
    // known the result is as wide as the widest and the extension blocks are filled and merged by a phi.
    struct Arm_val {
      Value*      v;
      unsigned    w;
      BasicBlock* ext;
    };
    std::vector<Arm_val> vals;
    const auto           finish_arm = [&](TSNode block) {
      Val x = dat(eval(arm_node(block)));
      auto* ext = BasicBlock::Create(m_.ctx(), "ifv.ext", fn_);
      b_.CreateBr(ext);
      vals.push_back({x.v, x.w, ext});
    };
    auto* done = BasicBlock::Create(m_.ctx(), "ifv.end", fn_);
    for (const auto& [cond, block] : arms) {
      Value* c    = to_i1(eval(cond));
      auto*  then = BasicBlock::Create(m_.ctx(), "ifv.then", fn_);
      auto*  next = BasicBlock::Create(m_.ctx(), "ifv.else", fn_);
      b_.CreateCondBr(c, then, next);
      b_.SetInsertPoint(then);
      finish_arm(block);
      b_.SetInsertPoint(next);
    }
    finish_arm(else_body);
    unsigned width = 0;
    for (const auto& a : vals) {
      width = std::max(width, a.w);
    }
    std::vector<Value*> wide;
    for (const auto& a : vals) {
      b_.SetInsertPoint(a.ext);
      wide.push_back(resize(a.v, a.w, width));
      b_.CreateBr(done);
    }
    b_.SetInsertPoint(done);
    auto* phi = b_.CreatePHI(ity(width), static_cast<unsigned>(vals.size()));
    for (size_t i = 0; i < vals.size(); ++i) {
      phi->addIncoming(wide[i], vals[i].ext);
    }
    return data(phi, width);
  }

  // A compile-time integer (the IR builder folds constants): nullopt for a runtime value.
  std::optional<int64_t> const_int(TSNode k) {
    Val v = dat(eval(k));
    if (auto c = const_of(v); c) {
      return c->getSExtValue();
    }
    return std::nullopt;
  }

  // `value#[i]` / `#[lo..=hi]` / `#[lo..<hi]` / `#[lo..+n]` / `#[..=hi]`: constant bounds, zero-extended.
  Val eval_bit_select(TSNode n) {
    TSNode base = ts_node_named_child(n, 0);
    TSNode sel{};
    for (TSNode c : ts_node_named_children(n)) {
      if (ntype(c) == "select") {
        sel = c;
      }
    }
    if (ts_node_is_null(sel)) {
      fail("bit select with no range: " + text_of(src_, n).substr(0, 40));
    }
    const auto bound = [&](TSNode k) {
      const auto v = const_int(k);
      if (!v) {
        fail("bit select bounds must be compile-time constants in a test: " + text_of(src_, n).substr(0, 40));
      }
      return static_cast<int>(*v);
    };
    const auto range_kind = [&](TSNode item) -> std::string {
      if (ntype(item) != "expression_item" || ts_node_named_child_count(item) != 3) {
        return "";
      }
      const TSNode op = ts_node_named_child(item, 1);
      if (ntype(op) != "binary_other_op" || ts_node_named_child_count(op) == 0) {
        return "";
      }
      const auto k = ntype(ts_node_named_child(op, 0));
      return k == "op_range_inclusive" || k == "op_range_exclusive" || k == "op_range_count" ? std::string{k} : "";
    };
    int          lo = 0, hi = 0;
    const TSNode index = field(sel, "index");
    const TSNode range = field(sel, "range");
    if (!ts_node_is_null(range)) {
      const TSNode incl = field(range, "from_zero_inclusive");
      const TSNode excl = field(range, "from_zero_exclusive");
      if (!ts_node_is_null(incl)) {
        hi = bound(incl);
      } else if (!ts_node_is_null(excl)) {
        hi = bound(excl) - 1;
      } else {
        fail("open-ended bit select needs an explicit upper bound in a test (write `lo..=hi`): " + text_of(src_, n).substr(0, 40));
      }
    } else if (ts_node_is_null(index)) {
      fail("unsupported bit select in test: " + text_of(src_, n).substr(0, 40));
    } else if (const auto kind = range_kind(index); !kind.empty()) {
      lo = bound(ts_node_named_child(index, 0));
      hi = bound(ts_node_named_child(index, 2));
      if (kind == "op_range_exclusive") {
        --hi;
      } else if (kind == "op_range_count") {
        hi = lo + hi - 1;
      }
    } else {
      lo = hi = bound(index);
    }
    if (hi < lo || lo < 0) {
      fail("bit select range must be ascending and non-negative: " + text_of(src_, n).substr(0, 40));
    }
    const unsigned w = static_cast<unsigned>(hi - lo + 1);
    last_select_w_   = static_cast<int>(w);
    Val x            = dat(eval(base));
    // bits above the value's own width are its sign: read them from a sign-extended copy
    const unsigned wide = std::max<unsigned>(x.w, static_cast<unsigned>(hi) + 1);
    Value*         xs   = resize(x.v, x.w, wide);
    if (lo != 0) {
      xs = b_.CreateLShr(xs, ConstantInt::get(ity(wide), static_cast<uint64_t>(lo)));
    }
    // the selected bits as a non-negative value of width w + 1
    const unsigned pw = phys(w + 1), px = phys(wide);
    Value*         y  = px > pw ? b_.CreateTrunc(xs, ity(w + 1)) : px < pw ? b_.CreateZExt(xs, ity(w + 1)) : xs;
    y                 = b_.CreateAnd(y, ConstantInt::get(ity(w + 1), low_mask(pw, w)));
    return data(y, w + 1);
  }

  // The value of a `{name}` hole: a local, a parameter, the tick counter, or `inst.field`.
  Val interp_expr(std::string name) {
    name.erase(std::remove(name.begin(), name.end(), '`'), name.end());  // `d.`o`` == d.o
    if (refs_.count(name) != 0) {
      std::string base, fld;
      ref_target(name, base, fld);
      return port_read(base, fld);
    }
    if (const auto it = locals_.find(name); it != locals_.end()) {
      Val x    = load_local(it->second);
      x.is_str = str_locals_.count(name) != 0;
      return x;
    }
    if (local_w_.count(name) != 0) {  // an assigned local not yet created (the width-inference pass reads it before its slot exists)
      Val x    = load_local(local(name));
      x.is_str = str_locals_.count(name) != 0;
      return x;
    }
    if (const auto dot = name.find('.'); dot != std::string::npos && inst_index(name.substr(0, dot)) >= 0) {
      return port_read(name.substr(0, dot), name.substr(dot + 1));
    }
    if (const auto dot = name.find('.'); dot != std::string::npos) {  // a field of a tuple constant: `T.hi`
      if (const TSNode rv = file_const_field(name.substr(0, dot), name.substr(dot + 1)); !ts_node_is_null(rv)) {
        return eval_file_const(name, rv);
      }
    }
    if (const auto it = fconsts_.find(name); it != fconsts_.end()) {
      return eval_file_const(name, it->second);
    }
    fail("the print hole `{" + name + "}` is not a local, a parameter or `instance.field` (expressions in a hole are not lowered yet)");
  }

  // `puts("text {var} {}", expr)` / `print(...)`: the format is split into literal segments and holes here;
  // the run time renders each hole (decimal, `b`, `x`, `X`, a width, `s` separators) from its words.
  // Append the decoded bytes of the Pyrope escape at lit[i] (a backslash) to `out`, leaving `i` on its last byte:
  // \n \t \r \\ \" \' \` \0 \xNN \u{N}. A malformed escape keeps its backslash.
  static void decode_escape(std::string_view lit, size_t& i, std::string& out) {
    const auto hex = [](char c) -> int {
      return (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? 10 + (c - 'a') : (c >= 'A' && c <= 'F') ? 10 + (c - 'A') : -1;
    };
    if (i + 1 >= lit.size()) {
      out.push_back('\\');
      return;
    }
    const char d = lit[i + 1];
    switch (d) {
      case 'n' : out.push_back('\n'); ++i; return;
      case 't' : out.push_back('\t'); ++i; return;
      case 'r' : out.push_back('\r'); ++i; return;
      case '0' : out.push_back('\0'); ++i; return;
      case '\\':
      case '"' :
      case '\'':
      case '`' : out.push_back(d); ++i; return;
      case 'x' :
        if (i + 3 < lit.size() && hex(lit[i + 2]) >= 0 && hex(lit[i + 3]) >= 0) {
          out.push_back(static_cast<char>((hex(lit[i + 2]) << 4) | hex(lit[i + 3])));
          i += 3;
          return;
        }
        break;
      case 'u': {
        const auto close = (i + 2 < lit.size() && lit[i + 2] == '{') ? lit.find('}', i + 3) : std::string_view::npos;
        uint32_t   cp    = 0;
        bool       ok    = close != std::string_view::npos && close > i + 3 && close - (i + 3) <= 6;
        for (size_t k = i + 3; ok && k < close; ++k) {
          ok = hex(lit[k]) >= 0;
          cp = cp * 16 + static_cast<uint32_t>(std::max(hex(lit[k]), 0));
        }
        if (!ok || cp > 0x10FFFF) {
          break;
        }
        if (cp < 0x80) {
          out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
          out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
          out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
          out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
          out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
          out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
          out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
          out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
          out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
          out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        i = close;
        return;
      }
      default: break;
    }
    out.push_back('\\');
  }

  // A format string -> literal segments and value holes (`{name}`, `{}` = the next positional, `{name:spec}`).
  // `cooked`: the string is a VALUE, so escapes decode; a `puts` format keeps every escape pair as text.
  void parse_format(const std::string& lit, bool cooked, const std::vector<Val>& pos, Print_site& site, std::vector<Val>& vals) {
    std::string text;
    size_t      pi = 0;
    const auto  flush_text = [&]() {
      if (!text.empty()) {
        Print_site::Seg sg;
        sg.text = text;
        site.segs.push_back(sg);
        text.clear();
      }
    };
    for (size_t i = 0; i < lit.size(); ++i) {
      const char c = lit[i];
      if (c == '\\' && cooked) {
        decode_escape(lit, i, text);
        continue;
      }
      if (c == '\\' && i + 1 < lit.size()) {  // an escape pair is text: the old driver keeps the backslash (but `\{` `\}` print the brace)
        const char d = lit[++i];
        if (d == '{' || d == '}') {
          text += d;
        } else {
          text += '\\';
          text += d;
        }
        continue;
      }
      if (c == '{') {
        if (i + 1 < lit.size() && lit[i + 1] == '{') {
          text += '{';
          ++i;
          continue;
        }
        const auto j = lit.find('}', i);
        if (j == std::string::npos) {
          text += c;
          continue;
        }
        std::string name = trim(lit.substr(i + 1, j - i - 1));
        std::string spec;
        if (const auto c2 = name.rfind(':'); c2 != std::string::npos && name.find(']', c2) == std::string::npos) {
          spec = trim(name.substr(c2 + 1));
          name = trim(name.substr(0, c2));
        }
        if (cooked && name.empty()) {  // an expression-less hole is literal text in a string value
          text += "{}";
          i = j;
          continue;
        }
        Val v = dat(name.empty() ? (pi < pos.size() ? pos[pi++] : data(cst(2, 0), 2)) : interp_expr(name));
        Print_site::Seg seg;
        seg.hole = true;
        seg.bits = v.w;
        if (v.is_str) {
          seg.base = 's';
        } else {
          size_t si = 0;
          while (si < spec.size() && spec[si] >= '0' && spec[si] <= '9') {
            seg.width = seg.width * 10 + static_cast<size_t>(spec[si++] - '0');
          }
          seg.base = (si < spec.size() && spec[si] != 's') ? spec[si++] : 'd';
          seg.sep  = si < spec.size() && spec[si] == 's';
          if (seg.base != 'd' && seg.base != 'b' && seg.base != 'x' && seg.base != 'X') {
            fail("print format spec `" + spec + "` is not lowered yet");
          }
        }
        flush_text();
        site.segs.push_back(seg);
        vals.push_back(v);
        i = j;
      } else if (c == '}') {
        if (i + 1 < lit.size() && lit[i + 1] == '}') {
          ++i;
        }
        text += '}';
      } else {
        text += c;
      }
    }
    flush_text();
  }

  // the hole values as whole 64-bit words, one buffer the runtime reads
  llvm::AllocaInst* emit_words(const std::vector<Val>& vals) {
    size_t total = 0;
    for (const auto& v : vals) {
      total += (v.w + 63) / 64;
    }
    llvm::IRBuilder<> eb(entry_, entry_->begin());
    auto*             arr = eb.CreateAlloca(llvm::ArrayType::get(m_.i64_, std::max<size_t>(total, 1)), nullptr, "pv");
    size_t            off = 0;
    for (const auto& v : vals) {
      const unsigned nw = (v.w + 63) / 64;
      b_.CreateAlignedStore(resize(v.v, v.w, nw * 64), b_.CreateGEP(m_.i64_, arr, {c64(static_cast<int64_t>(off))}), llvm::Align(8));
      off += nw;
    }
    return arr;
  }

  // A string VALUE (`"v={v}"` formats now, `'raw'` is verbatim): the run time formats it and hands back the interned
  // handle, so two equal strings compare equal as integers.
  Val string_value(TSNode lit_node) {
    std::string lit = text_of(src_, lit_node);
    if (lit.size() < 2 || (lit.front() != '"' && lit.front() != '\'') || lit.back() != lit.front()) {
      fail("a string value must be a quoted literal");
    }
    const bool raw = lit.front() == '\'';
    lit            = lit.substr(1, lit.size() - 2);
    Print_site       site;
    std::vector<Val> vals;
    if (raw) {
      Print_site::Seg sg;
      sg.text = lit;
      site.segs.push_back(sg);
    } else {
      parse_format(lit, /*cooked=*/true, {}, site, vals);
    }
    const auto idx = static_cast<uint32_t>(strings_.size());
    if (!scratch_) {
      strings_.push_back(site);
    }
    auto*  arr = emit_words(vals);
    auto*  fty = llvm::FunctionType::get(m_.i64_, {m_.ptr_, llvm::Type::getInt32Ty(m_.ctx()), m_.ptr_}, false);
    Value* fp  = b_.CreateLoad(m_.ptr_, rt_word(prp_tb::kRtStr));
    Value* id  = b_.CreateCall(fty, fp, {rt_, ConstantInt::get(llvm::Type::getInt32Ty(m_.ctx()), idx), arr});
    Val    out = data(id, 64);
    out.is_str = true;
    return out;
  }

  void print_stmt(TSNode n, bool newline) {
    Print_site site;
    site.newline = newline;
    site.prefix  = file_short_ + ":" + std::to_string(ts_node_start_point(n).row + 1) + ":" + (newline ? "puts" : "print") + ":";
    std::vector<Val> vals;
    TSNode           args = field(n, "argument");
    if (!ts_node_is_null(args) && ts_node_named_child_count(args) >= 1) {
      std::vector<Val> pos;
      uint32_t         ai = 0;
      for (TSNode a : ts_node_named_children(args)) {
        if (ai++ != 0) {
          pos.push_back(eval(a));
        }
      }
      TSNode lit_node = ts_node_named_child(args, 0);
      while (ts_node_named_child_count(lit_node) == 1 && (ntype(lit_node) == "expression_item" || ntype(lit_node) == "constant")) {
        lit_node = ts_node_named_child(lit_node, 0);
      }
      std::string lit = text_of(src_, lit_node);
      if (lit.size() >= 2 && (lit.front() == '"' || lit.front() == '\'') && lit.back() == lit.front()) {
        lit = lit.substr(1, lit.size() - 2);
      } else {
        fail("`puts`/`print` need a string literal format");
      }
      parse_format(lit, /*cooked=*/false, pos, site, vals);
    }
    const auto idx = static_cast<uint32_t>(prints_.size());
    if (!scratch_) {
      prints_.push_back(site);
    }
    auto*  arr = emit_words(vals);
    auto*  fty = llvm::FunctionType::get(m_.void_, {m_.ptr_, llvm::Type::getInt32Ty(m_.ctx()), m_.ptr_}, false);
    Value* fp  = b_.CreateLoad(m_.ptr_, rt_word(prp_tb::kRtPrint));
    b_.CreateCall(fty, fp, {rt_, ConstantInt::get(llvm::Type::getInt32Ty(m_.ctx()), idx), arr});
  }

  // The N of an unsigned `U<N>`-typed operand (a typed `~` flips N bits), else 0 (user ruling 26): a typed
  // local, an untyped alias of one, a file const, an unsigned DUT field, a `U<N>(..)` cast, a bit select,
  // a `~` of such, a bitwise `& | ^` of such (the widest), any of them in parentheses.
  int unsigned_bits(TSNode n) {
    while (!ts_node_is_null(n) && (ntype(n) == "expression_item" || ntype(n) == "paren_group" || ntype(n) == "tuple")
           && ts_node_named_child_count(n) == 1) {
      n = ts_node_named_child(n, 0);
    }
    const auto t = ntype(n);
    if (t == "identifier") {
      const std::string nm = text_of(src_, n);
      if (const auto it = ubits_.find(nm); it != ubits_.end()) {
        return it->second;
      }
      if (locals_.count(nm) != 0) {
        return 0;
      }
      if (const auto it = fconsts_.find(nm); it != fconsts_.end() && const_stack_.insert("ub:" + nm).second) {
        TSNode     lv = field(ts_node_parent(it->second), "lvalue");
        int        ub = declared_uint_bits(src_, lv);
        if (ub == 0 && ntype(lv) == "identifier") {
          ub = unsigned_bits(it->second);
        }
        const_stack_.erase("ub:" + nm);
        return ub;
      }
      return 0;
    }
    if (t == "expression_item") {
      int  bits    = 0;
      bool operand = true;
      for (TSNode c : ts_node_named_children(n)) {
        if (!operand) {
          const TSNode op = ts_node_named_child(c, 0);
          const auto   ot = ts_node_is_null(op) ? std::string_view{} : ntype(op);
          if (ntype(c) != "binary_other_op" || (ot != "op_bit_and" && ot != "op_bit_or" && ot != "op_bit_xor")) {
            return 0;
          }
        } else {
          const int w = unsigned_bits(c);
          if (w == 0) {
            return 0;
          }
          bits = std::max(bits, w);
        }
        operand = !operand;
      }
      return operand ? 0 : bits;
    }
    if (t == "dot_expression") {
      std::string base, fld;
      if (!inst_dot(n, base, fld)) {
        return 0;
      }
      const Field* f = insts_[static_cast<size_t>(inst_index(base))].dut->find(fld);
      return f != nullptr && !f->sign && f->kind != 3 ? static_cast<int>(f->bits) : 0;
    }
    if (t == "function_call_expression") {
      const TSNode      fn   = field(n, "function");
      const std::string fnnm = ts_node_is_null(fn) ? "" : text_of(src_, fn);
      if (fnnm.size() >= 2 && fnnm[0] == 'U' && std::all_of(fnnm.begin() + 1, fnnm.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return std::atoi(fnnm.c_str() + 1);
      }
      return 0;
    }
    if (t == "unary_expression") {
      std::string op;
      TSNode      inner{};
      for (TSNode c : ts_node_named_children(n)) {
        if (is_unary_op(ntype(c))) {
          op = text_of(src_, c);
        } else {
          inner = c;
        }
      }
      return op == "~" && !ts_node_is_null(inner) ? unsigned_bits(inner) : 0;
    }
    if (t == "bit_selection") {
      (void)eval_bit_select(n);  // pure loads: only its width is wanted
      return last_select_w_;
    }
    return 0;
  }

  Val unary(const std::string& op, Val v, int ub = 0) {
    if (op == "!" || op == "not") {
      return boolean(b_.CreateNot(to_i1(v)));
    }
    v = dat(v);
    if (op == "~") {
      // a `U<N>` operand flips exactly its N bits (user ruling 26), read back as a non-negative N-bit number;
      // anything else is `-v - 1`
      if (ub > 0) {
        const unsigned w = static_cast<unsigned>(ub) + 1;
        Value*         x = resize(v.v, v.w, w);
        return data(b_.CreateXor(x, ConstantInt::get(ity(w), low_mask(phys(w), static_cast<unsigned>(ub)))), w);
      }
      return data(b_.CreateNot(v.v), v.w);
    }
    if (op == "-") {
      return data(b_.CreateNeg(resize(v.v, v.w, v.w + 1)), v.w + 1);
    }
    fail("unsupported unary operator '" + op + "' in test");
  }

  Val eval_seq(TSNode n) {
    std::vector<TSNode> kids;
    for (TSNode c : ts_node_named_children(n)) {
      kids.push_back(c);
    }
    if (kids.empty()) {
      return literal(text_of(src_, n));
    }
    if (kids.size() == 1) {
      return eval(kids[0]);
    }
    return eval_kids(kids, 0, kids.size());
  }

  Val eval_kids(const std::vector<TSNode>& kids, size_t b, size_t e) {
    if (e - b == 1) {
      return eval(kids[b]);
    }
    for (size_t i = b; i < e; ++i) {  // `a implies b` binds loosest of all: (!a || b)
      if (is_binary_op(ntype(kids[i])) && text_of(src_, kids[i]) == "implies") {
        Value* l = to_i1(eval_kids(kids, b, i));
        Value* r = to_i1(eval_kids(kids, i + 1, e));
        return boolean(b_.CreateOr(b_.CreateNot(l), r));
      }
    }
    int    loosest = 99;
    size_t at      = e;
    for (size_t i = b; i < e; ++i) {
      const int c = prec_class(ntype(kids[i]));
      if (c < 0) {
        continue;
      }
      if (c <= loosest) {
        loosest = c;
        at      = i;
      }
    }
    if (at == e) {
      size_t                   i = b;
      std::vector<std::string> pre;
      while (i < e && is_unary_op(ntype(kids[i]))) {
        pre.push_back(text_of(src_, kids[i]));
        ++i;
      }
      if (i >= e) {
        fail("dangling unary operator in test expression");
      }
      Val v  = eval(kids[i]);
      int ub = unsigned_bits(kids[i]);
      ++i;
      if (i != e) {
        fail("unsupported expression form in test: " + text_of(src_, kids[i]).substr(0, 40));
      }
      for (auto it = pre.rbegin(); it != pre.rend(); ++it) {
        v = unary(*it, v, ub);
        if (*it != "~") {
          ub = 0;
        }
      }
      return v;
    }
    const std::string op = text_of(src_, kids[at]);
    Val               l  = eval_kids(kids, b, at);
    Val               r  = eval_kids(kids, at + 1, e);
    if (loosest == 0) {
      if (op == "and") {
        return boolean(b_.CreateAnd(to_i1(l), to_i1(r)));
      }
      if (op == "or") {
        return boolean(b_.CreateOr(to_i1(l), to_i1(r)));
      }
      fail("unsupported logical operator '" + op + "' in test");
    }
    if (loosest == 1) {
      return compare(op, l, r);
    }
    return fold_binary(op, l, r);
  }

  // Both sides are built non-negative (or explicitly signed), so one common width is enough.
  Val compare(const std::string& op, Val a, Val b) {
    if (a.is_str || b.is_str) {
      if (!(a.is_str && b.is_str) || (op != "==" && op != "!=")) {
        fail("strings compare only with == / != against another string");
      }
      return boolean(op == "==" ? b_.CreateICmpEQ(a.v, b.v) : b_.CreateICmpNE(a.v, b.v));  // interned: equal text, equal handle
    }
    a                = dat(a);
    b                = dat(b);
    const unsigned w = std::max(a.w, b.w);
    Value*         x = at(a, w).v;
    Value*         y = at(b, w).v;
    if (op == "==") {
      return boolean(b_.CreateICmpEQ(x, y));
    }
    if (op == "!=") {
      return boolean(b_.CreateICmpNE(x, y));
    }
    if (op == "<") {
      return boolean(b_.CreateICmpSLT(x, y));
    }
    if (op == "<=") {
      return boolean(b_.CreateICmpSLE(x, y));
    }
    if (op == ">") {
      return boolean(b_.CreateICmpSGT(x, y));
    }
    if (op == ">=") {
      return boolean(b_.CreateICmpSGE(x, y));
    }
    fail("unsupported comparison operator '" + op + "' in test");
  }

  // Widths follow the operator's real value growth, so nothing overflows silently: an add needs one more bit
  // than its widest operand, a multiply the sum, a literal shift exactly its amount.
  Val fold_binary(const std::string& op, Val a, Val b) {
    a                 = dat(a);
    b                 = dat(b);
    const unsigned mx = std::max(a.w, b.w);
    const auto     in = [&](unsigned w) { return std::pair{at(a, w).v, at(b, w).v}; };
    if (op == "+") {
      auto [x, y] = in(mx + 1);
      return data(b_.CreateAdd(x, y, "", false, tb_nsw()), mx + 1);
    }
    if (op == "-") {
      auto [x, y] = in(mx + 1);
      return data(b_.CreateSub(x, y, "", false, tb_nsw()), mx + 1);
    }
    if (op == "*") {
      auto [x, y] = in(a.w + b.w);
      return data(b_.CreateMul(x, y, "", false, tb_nsw()), a.w + b.w);
    }
    if (op == "&" || op == "|" || op == "^") {
      auto [x, y] = in(mx);
      return data(op == "&" ? b_.CreateAnd(x, y) : op == "|" ? b_.CreateOr(x, y) : b_.CreateXor(x, y), mx);
    }
    if (op == "/" || op == "%") {
      auto [x, y]      = in(mx);
      Value* zero      = b_.CreateICmpEQ(y, cst(mx, 0));
      Value* minus_one = b_.CreateICmpEQ(y, cst(mx, -1));
      // Avoid LLVM poison at MIN / -1 (including srem). Keep the normal divide
      // at operand width; only the positive quotient can need an extra bit.
      Value* safe      = b_.CreateSelect(b_.CreateOr(zero, minus_one), cst(mx, 1), y);
      if (op == "%") {
        return data(b_.CreateSelect(zero, cst(mx, 0), b_.CreateSRem(x, safe)), mx);
      }
      const unsigned w        = a.w + 1;
      Value*         quotient = resize(b_.CreateSDiv(x, safe), mx, w);
      Value*         negated  = b_.CreateNeg(at(a, w).v);
      return data(b_.CreateSelect(zero, cst(w, 0), b_.CreateSelect(minus_one, negated, quotient)), w);
    }
    if (op == "<<") {
      // A literal shift grows the result exactly; a runtime amount gets 64 bits of headroom.
      const auto     lit  = const_of(b);
      const unsigned grow = lit ? static_cast<unsigned>(std::max<int64_t>(lit->getSExtValue(), 0)) : 64;
      const unsigned w    = a.w + grow;
      Value*         x    = at(a, w).v;
      if (lit) {
        return data(grow == 0 ? x : b_.CreateShl(x, ConstantInt::get(ity(w), grow), "", false, tb_nsw()), w);
      }
      Value* amt  = shift_amount(b, w);
      Value* over = shift_over(b, w);
      return data(b_.CreateSelect(over, ConstantInt::get(ity(w), 0), b_.CreateShl(x, amt)), w);
    }
    if (op == ">>") {
      Value* x = a.v;
      if (const auto lit = const_of(b); lit) {
        const uint64_t k = lit->getSExtValue() < 0 ? 0 : std::min<uint64_t>(lit->getZExtValue(), a.w - 1);
        return data(k == 0 ? x : b_.CreateAShr(x, ConstantInt::get(ity(a.w), k)), a.w);
      }
      Value* amt  = shift_amount(b, a.w);
      Value* over = shift_over(b, a.w);
      Value* sign = b_.CreateAShr(x, ConstantInt::get(ity(a.w), a.w - 1));
      return data(b_.CreateSelect(over, sign, b_.CreateAShr(x, amt)), a.w);
    }
    fail("unsupported operator '" + op + "' in test");
  }

  // a runtime shift amount as a value of the shifted value's width (an out-of-range amount is excluded by shift_over)
  Value* shift_amount(Val amt, unsigned w) { return resize(amt.v, amt.w, w); }
  // true when the amount is negative or >= the width (the whole value is shifted out)
  Value* shift_over(Val amt, unsigned w) {
    const unsigned cw = std::max<unsigned>(amt.w, 33);
    Value*         a  = resize(amt.v, amt.w, cw);
    return b_.CreateOr(b_.CreateICmpSLT(a, ConstantInt::get(ity(cw), 0)), b_.CreateICmpSGE(a, ConstantInt::get(ity(cw), w)));
  }

  // ---- statements ----
  std::string compound_binop(TSNode assign) {
    TSNode op = field(assign, "operator");
    if (ts_node_is_null(op)) {
      return "";
    }
    TSNode           inner = ts_node_named_child(op, 0);
    std::string_view kind  = ts_node_is_null(inner) ? std::string_view{} : ntype(inner);
    if (kind.empty() || kind == "assign") {
      return "";
    }
    static const std::map<std::string_view, std::string> table = {
        {"assign_add", "+"}, {"assign_sub", "-"}, {"assign_mul", "*"},   {"assign_div", "/"},  {"assign_bit_or", "|"},
        {"assign_bit_and", "&"}, {"assign_bit_xor", "^"}, {"assign_shl", "<<"}, {"assign_sra", ">>"},
    };
    const auto it = table.find(kind);
    if (it == table.end()) {
      fail("unsupported compound assignment `" + std::string(kind) + "` in test");
    }
    return it->second;
  }

  void stmt(TSNode n) {
    const auto t = ntype(n);
    if (t == "comment") {
      return;
    }
    if (t == "assignment") {
      assignment(n);
      return;
    }
    if (t == "tick_statement") {
      tick(n);
      return;
    }
    if (t == "step_statement") {
      step(n);
      return;
    }
    if (t == "if_expression") {
      if_stmt(n);
      return;
    }
    if (t == "control_statement") {
      for (TSNode c : ts_node_named_children(n)) {
        const auto ct = ntype(c);
        if (ct == "break_statement" || ct == "continue_statement") {
          if (loops_.empty()) {
            fail("`break`/`continue` outside a tick loop");
          }
          b_.CreateBr(ct == "break_statement" ? loops_.back().second : loops_.back().first);
          new_block_after_terminator();
          return;
        }
      }
      return;
    }
    if (t == "function_call_expression") {
      TSNode      fn   = field(n, "function");
      std::string fnnm = ts_node_is_null(fn) ? "" : text_of(src_, fn);
      if (fnnm == "cassert") {
        return;  // an elaboration check, already decided at build time
      }
      if (fnnm == "assert" || fnnm == "assume") {
        assertion(n);
        return;
      }
      if (fnnm == "puts" || fnnm == "print") {
        print_stmt(n, fnnm == "puts");
        return;
      }
      fail("`" + fnnm + "` as a statement in a test is not lowered yet");
    }
    fail(std::string("unsupported statement in test: `") + std::string(t) + "` -> " + text_of(src_, n).substr(0, 40));
  }

  void assignment(TSNode n) {
    TSNode            lv  = field(n, "lvalue");
    TSNode            rv  = field(n, "rvalue");
    const std::string bop = compound_binop(n);
    std::string       base, fld;
    if (inst_dot(lv, base, fld)) {
      Val r = eval(rv);
      port_write(base, fld, bop.empty() ? r : fold_binary(bop, port_read(base, fld), r));
      return;
    }
    if (std::string an; array_ref(lv, an, base_idx_)) {  // `table[i] = v`
      TSNode idx = base_idx_;
      Val    r   = eval(rv);
      if (!bop.empty()) {
        r = fold_binary(bop, eval(lv), r);
      }
      array_write(an, idx, r);
      return;
    }
    const std::string lname = lvalue_name(src_, lv);
    if (lname.empty()) {
      fail("unsupported assignment lvalue: " + text_of(src_, n).substr(0, 40));
    }
    if (refs_.count(lname) != 0) {
      if (is_ref_call(rv)) {
        return;  // the binding itself emits nothing
      }
      std::string rbase, rfld;
      ref_target(lname, rbase, rfld);
      Val r = eval(rv);
      reg_write(rbase, rfld, bop.empty() ? r : fold_binary(bop, port_read(rbase, rfld), r));
      return;
    }
    if (ts_node_is_null(rv)) {
      local(lname);
      return;
    }
    if (inst_index(lname) >= 0) {
      return;  // `mut acc = Module`: the instance was laid out up front
    }
    if (ntype(rv) == "tuple_sq") {  // `const t = [a, b, ..]`: fill the table where the statement runs
      auto& arr = arrays_.at(lname);
      for (size_t i = 0; i < arr.elems.size(); ++i) {
        Value* p = b_.CreateGEP(llvm::ArrayType::get(ity(arr.width), std::max<size_t>(arr.elems.size(), 1)), arr.slot, {c64(0), c64(static_cast<int64_t>(i))});
        b_.CreateStore(at(eval(arr.elems[i]), arr.width).v, p);
      }
      return;
    }
    Val r = eval(rv);
    if (r.is_str) {
      str_locals_.insert(lname);
    }
    if (!bop.empty()) {
      r = fold_binary(bop, eval(lv), r);
    }
    const unsigned lw = local_w_.count(lname) ? local_w_[lname] : kDefaultTbWidth;
    Val            x  = at(r, lw);  // wraps into the local's own width
    if (const int ub = declared_uint_bits(src_, lv); ub > 0) {
      ubits_[lname] = ub;
      // a `U<N>` local holds its low N bits as a non-negative number
      const unsigned uw = static_cast<unsigned>(ub);
      Value*         t  = resize(r.is_bool ? dat(r).v : r.v, dat(r).w, uw + 1);
      t                 = b_.CreateAnd(t, ConstantInt::get(ity(uw + 1), low_mask(phys(uw + 1), uw)));
      x                 = data(resize(t, uw + 1, lw), lw);
    } else if (bop.empty() && !ubits_.count(lname)) {
      TSNode decl = field(n, "decl");
      TSNode kind = ts_node_is_null(decl) ? TSNode{} : field(decl, "storage");
      if (!ts_node_is_null(kind) && ntype(kind) == "const_decl" && ntype(lv) == "identifier") {
        if (const int aub = unsigned_bits(rv); aub > 0) {
          ubits_[lname] = aub;  // an untyped `const` alias of a typed value has its type
        }
      }
    }
    b_.CreateStore(x.v, local(lname));
  }

  void advance_cycle() {
    Value* cycle = rt_word(prp_tb::kRtCycle);
    b_.CreateStore(b_.CreateAdd(b_.CreateLoad(m_.i64_, cycle), c64(1)), cycle);
  }

  void emit_trace(bool before) {
    auto*  tty = llvm::FunctionType::get(m_.void_, {m_.ptr_, m_.ptr_, llvm::Type::getInt32Ty(m_.ctx())}, false);
    Value* tf  = b_.CreateLoad(m_.ptr_, rt_word(prp_tb::kRtTrace));
    b_.CreateCall(tty, tf, {rt_, st_, ConstantInt::get(llvm::Type::getInt32Ty(m_.ctx()), before)});
    if (before) {
      auto* stop = BasicBlock::Create(m_.ctx(), "trace.stop", fn_);
      auto* run  = BasicBlock::Create(m_.ctx(), "trace.step", fn_);
      b_.CreateCondBr(b_.CreateICmpNE(b_.CreateLoad(m_.i64_, rt_word(prp_tb::kRtStop)), c64(0)), stop, run);
      b_.SetInsertPoint(stop);
      b_.CreateRet(b_.CreateLoad(m_.i64_, fails_));
      b_.SetInsertPoint(run);
    }
  }

  void step(TSNode n) {
    if (insts_.empty()) {
      fail("`step` with no instance declared (use `mut acc = Module` first)");
    }
    ++steps_lowered_;
    const auto emit_step = [&]() {
      if (!in_tick_) {
        advance_cycle();
      }
      if (trace_) {
        emit_trace(true);
      }
      for (size_t k = 0; k < insts_.size(); ++k) {
        const auto& in  = insts_[k];
        const auto  fty = llvm::FunctionType::get(m_.void_, {m_.ptr_}, false);
        Value*      sp  = b_.CreateGEP(m_.i64_, st_, {c64(static_cast<int64_t>(in.base))});
        if (!in.explicit_clk) {
          b_.CreateCall(m_.mod().getOrInsertFunction("sim_step_" + in.dut->sym, fty), {sp});
        } else {
          // one clock event: rise on 0->1 of the poked level, fall on 1->0, else only the settle
          auto*  lslot = locals_.at("__clk_" + in.var);
          auto*  pslot = locals_.at("__clkp_" + in.var);
          Value* lvl   = b_.CreateLoad(m_.i64_, lslot);
          Value* prev  = b_.CreateLoad(m_.i64_, pslot);
          auto*  rise  = BasicBlock::Create(m_.ctx(), "clk.rise", fn_);
          auto*  chk   = BasicBlock::Create(m_.ctx(), "clk.chk", fn_);
          auto*  fall  = BasicBlock::Create(m_.ctx(), "clk.fall", fn_);
          auto*  hold  = BasicBlock::Create(m_.ctx(), "clk.hold", fn_);
          auto*  done  = BasicBlock::Create(m_.ctx(), "clk.done", fn_);
          b_.CreateCondBr(b_.CreateAnd(b_.CreateICmpNE(lvl, c64(0)), b_.CreateICmpEQ(prev, c64(0))), rise, chk);
          b_.SetInsertPoint(rise);
          b_.CreateCall(m_.mod().getOrInsertFunction("sim_rise_" + in.dut->sym, fty), {sp});
          b_.CreateBr(done);
          b_.SetInsertPoint(chk);
          b_.CreateCondBr(b_.CreateAnd(b_.CreateICmpEQ(lvl, c64(0)), b_.CreateICmpNE(prev, c64(0))), fall, hold);
          b_.SetInsertPoint(fall);
          b_.CreateCall(m_.mod().getOrInsertFunction("sim_fall_" + in.dut->sym, fty), {sp});
          b_.CreateBr(done);
          b_.SetInsertPoint(hold);
          b_.CreateCall(m_.mod().getOrInsertFunction("sim_settle_" + in.dut->sym, fty), {sp});
          b_.CreateBr(done);
          b_.SetInsertPoint(done);
          b_.CreateStore(lvl, pslot);
        }
        if (!in.dut->undefined_words.empty()) {
          // an edge kernel flagged an undefined access: let the runtime report the site (one cold call, off the hot path)
          Value* any = c64(0);
          for (const auto w : in.dut->undefined_words) {
            any = b_.CreateOr(any, b_.CreateLoad(m_.i64_, word_ptr(in, w)));
          }
          auto* hit  = BasicBlock::Create(m_.ctx(), "undef.hit", fn_);
          auto* cont = BasicBlock::Create(m_.ctx(), "undef.cont", fn_);
          auto* br   = b_.CreateCondBr(b_.CreateICmpNE(any, c64(0)), hit, cont);
          br->setMetadata(llvm::LLVMContext::MD_prof, llvm::MDBuilder(m_.ctx()).createBranchWeights(1, 1u << 20));
          b_.SetInsertPoint(hit);
          auto*  uty = llvm::FunctionType::get(m_.void_, {m_.ptr_, m_.ptr_, llvm::Type::getInt32Ty(m_.ctx())}, false);
          Value* uf  = b_.CreateLoad(m_.ptr_, rt_word(prp_tb::kRtUndefined));
          b_.CreateCall(uty, uf, {rt_, st_, ConstantInt::get(llvm::Type::getInt32Ty(m_.ctx()), k)});
          b_.CreateBr(cont);
          b_.SetInsertPoint(cont);
        }
      }
      if (trace_) {
        emit_trace(false);
      }
      Value* period = rt_word(prp_tb::kRtPeriod);
      b_.CreateStore(b_.CreateAdd(b_.CreateLoad(m_.i64_, period), c64(1)), period);
    };
    TSNode cnt = field(n, "value");
    if (ts_node_is_null(cnt)) {
      emit_step();
      return;
    }
    Value* count = as_i64(eval(cnt));
    auto*  slot  = alloca_named("step_i", 64);
    b_.CreateStore(c64(0), slot);
    auto* head = BasicBlock::Create(m_.ctx(), "step.head", fn_);
    auto* body = BasicBlock::Create(m_.ctx(), "step.body", fn_);
    auto* exit = BasicBlock::Create(m_.ctx(), "step.exit", fn_);
    b_.CreateBr(head);
    b_.SetInsertPoint(head);
    b_.CreateCondBr(b_.CreateICmpSLT(b_.CreateLoad(m_.i64_, slot), count), body, exit);
    b_.SetInsertPoint(body);
    emit_step();
    b_.CreateStore(b_.CreateAdd(b_.CreateLoad(m_.i64_, slot), c64(1)), slot);
    b_.CreateBr(head);
    b_.SetInsertPoint(exit);
  }

  void if_stmt(TSNode n) {
    struct Arm {
      TSNode cond{};
      TSNode scope{};
      bool   has_cond = false;
    };
    std::vector<Arm> arms;
    TSNode           pend{};
    bool             have_pend = false;
    for (TSNode c : ts_node_named_children(n)) {
      if (ntype(c) == "scope_statement") {
        arms.push_back({have_pend ? pend : TSNode{}, c, have_pend});
        have_pend = false;
      } else {
        pend      = c;
        have_pend = true;
      }
    }
    if (arms.empty() || !arms.front().has_cond) {
      fail("unsupported if form in test");
    }
    auto* done = BasicBlock::Create(m_.ctx(), "if.end", fn_);
    ++depth_;
    for (size_t i = 0; i < arms.size(); ++i) {
      BasicBlock* next = nullptr;
      if (arms[i].has_cond) {
        Value* c    = to_i1(eval(arms[i].cond));
        auto*  then = BasicBlock::Create(m_.ctx(), "if.then", fn_);
        next        = BasicBlock::Create(m_.ctx(), "if.else", fn_);
        b_.CreateCondBr(c, then, next);
        b_.SetInsertPoint(then);
      }
      for (TSNode c : ts_node_named_children(arms[i].scope)) {
        stmt(c);
      }
      b_.CreateBr(done);
      if (next != nullptr) {
        b_.SetInsertPoint(next);
      }
    }
    if (arms.back().has_cond) {
      b_.CreateBr(done);  // no else: fall out
    }
    --depth_;
    b_.SetInsertPoint(done);
  }

  void spill_frame() {
    Value* frame = b_.CreateLoad(m_.ptr_, rt_word(prp_tb::kRtFrame));
    for (const auto& f : frame_) {
      const size_t n = std::max<size_t>(f.elems, 1);
      for (size_t e = 0; e < n; ++e) {
        Value* x = f.elems == 0 ? b_.CreateLoad(ity(f.width), f.slot)
                                : b_.CreateLoad(ity(f.width), b_.CreateGEP(llvm::ArrayType::get(ity(f.width), n), f.slot, {c64(0), c64(static_cast<int64_t>(e))}));
        b_.CreateAlignedStore(resize(x, f.width, static_cast<unsigned>(f.words * 64)),
                              b_.CreateGEP(m_.i64_, frame, {c64(static_cast<int64_t>(f.word + e * f.words))}), llvm::Align(8));
      }
    }
  }
  void reload_frame() {
    Value* frame = b_.CreateLoad(m_.ptr_, rt_word(prp_tb::kRtFrame));
    for (const auto& f : frame_) {
      const size_t n = std::max<size_t>(f.elems, 1);
      for (size_t e = 0; e < n; ++e) {
        Value* x = b_.CreateAlignedLoad(ity(static_cast<unsigned>(f.words * 64)),
                                        b_.CreateGEP(m_.i64_, frame, {c64(static_cast<int64_t>(f.word + e * f.words))}), llvm::Align(8));
        Value* v = resize(x, static_cast<unsigned>(f.words * 64), f.width);
        if (f.elems == 0) {
          b_.CreateStore(v, f.slot);
        } else {
          b_.CreateStore(v, b_.CreateGEP(llvm::ArrayType::get(ity(f.width), n), f.slot, {c64(0), c64(static_cast<int64_t>(e))}));
        }
      }
    }
  }

  void tick(TSNode n) {
    TSNode cnt  = field(n, "value");
    TSNode code = field(n, "code");
    if (ts_node_is_null(cnt) || ts_node_is_null(code)) {
      fail("tick requires a count and a body (unbounded `tick {}` is not supported)");
    }
    if (!ts_node_is_null(field(n, "clocks"))) {
      fail("`tick` clocks=(...) is not lowered yet");
    }
    if (in_tick_) {
      fail("a `tick` cannot be nested inside another `tick` (single-clock model) -- flatten into one tick loop");
    }
    in_tick_              = true;
    const std::string clk = "clock";
    {
      // the clock loop variable would silently shadow a parameter/local/instance/array of the same name
      bool taken = locals_.count(clk) != 0 || arrays_.count(clk) != 0;
      for (const auto& in : insts_) {
        taken = taken || in.var == clk;
      }
      if (taken) {
        fail("tick clock variable 'clock' collides with a test parameter/local/instance of the same name -- rename one");
      }
    }
    const size_t steps_before = steps_lowered_;
    const bool   hookable     = depth_ == 0 && !scratch_;
    auto*        slot         = alloca_named("clock", 64);
    auto*        count_slot   = hookable ? tick_count_ : alloca_named("nested.tick.count", 64);
    auto*        start        = hookable ? BasicBlock::Create(m_.ctx(), "tick.start", fn_) : b_.GetInsertBlock();
    auto*        go           = BasicBlock::Create(m_.ctx(), "tick.go", fn_);
    if (hookable) {
      auto*  resume = BasicBlock::Create(m_.ctx(), "tick.resume", fn_);
      Value* rc     = b_.CreateLoad(m_.i64_, rt_word(prp_tb::kRtResume));
      Value* rs     = b_.CreateLoad(m_.i64_, rt_word(prp_tb::kRtResumeStmt));
      b_.CreateCondBr(b_.CreateAnd(b_.CreateICmpSGE(rc, c64(0)), b_.CreateICmpEQ(rs, c64(top_stmt_))), resume, start);
      b_.SetInsertPoint(resume);
      reload_frame();
      b_.CreateStore(b_.CreateLoad(m_.i64_, rt_word(prp_tb::kRtTickIndex)), slot);
      b_.CreateStore(b_.CreateSub(rc, c64(1)), rt_word(prp_tb::kRtCycle));
      b_.CreateStore(c64(-1), rt_word(prp_tb::kRtResume));
      b_.CreateBr(go);
    }
    b_.SetInsertPoint(start);
    b_.CreateStore(as_i64(eval(cnt)), count_slot);
    b_.CreateStore(c64(0), slot);
    b_.CreateBr(go);
    b_.SetInsertPoint(go);
    auto* prev   = locals_.contains(clk) ? locals_.at(clk) : nullptr;
    locals_[clk] = slot;
    Value* count = b_.CreateLoad(m_.i64_, count_slot);
    auto*  head  = BasicBlock::Create(m_.ctx(), "tick.head", fn_);
    auto*  body  = BasicBlock::Create(m_.ctx(), "tick.body", fn_);
    auto*  latch = BasicBlock::Create(m_.ctx(), "tick.latch", fn_);
    auto*  exit  = BasicBlock::Create(m_.ctx(), "tick.exit", fn_);
    b_.CreateBr(head);
    b_.SetInsertPoint(head);
    Value* i    = b_.CreateLoad(m_.i64_, slot);
    Value* more = b_.CreateICmpSLT(i, count);
    b_.CreateCondBr(more, body, exit);
    b_.SetInsertPoint(body);
    advance_cycle();
    if (hookable && !trace_) {
      // checkpoint poll: every `ckpt_left` iterations the runtime may write the state, the frame and the cycle
      Value* lp   = rt_word(prp_tb::kRtCkptLeft);
      Value* left = b_.CreateLoad(m_.i64_, lp);
      auto*  poll = BasicBlock::Create(m_.ctx(), "ckpt.poll", fn_);
      auto*  go2  = BasicBlock::Create(m_.ctx(), "ckpt.go", fn_);
      b_.CreateCondBr(b_.CreateICmpSLE(left, c64(0)), poll, go2);
      b_.SetInsertPoint(poll);
      spill_frame();
      b_.CreateStore(i, rt_word(prp_tb::kRtTickIndex));
      auto*  cty = llvm::FunctionType::get(m_.void_, {m_.ptr_, m_.ptr_, m_.i64_, m_.i64_}, false);
      Value* cf  = b_.CreateLoad(m_.ptr_, rt_word(prp_tb::kRtCheckpoint));
      b_.CreateCall(cty, cf, {rt_, st_, b_.CreateLoad(m_.i64_, rt_word(prp_tb::kRtCycle)), c64(top_stmt_)});
      b_.CreateBr(go2);
      b_.SetInsertPoint(go2);
      Value* l2 = b_.CreateLoad(m_.i64_, lp);
      b_.CreateStore(b_.CreateSub(l2, c64(1)), lp);
    }
    ++depth_;
    loops_.emplace_back(latch, exit);
    for (TSNode c : ts_node_named_children(code)) {
      stmt(c);
    }
    loops_.pop_back();
    --depth_;
    if (steps_lowered_ == steps_before) {
      fail("a `tick` body must advance the clock with `step` (otherwise it never changes the design's state)");
    }
    b_.CreateBr(latch);
    b_.SetInsertPoint(latch);
    b_.CreateStore(b_.CreateAdd(b_.CreateLoad(m_.i64_, slot), c64(1)), slot);
    b_.CreateBr(head);
    b_.SetInsertPoint(exit);
    if (prev != nullptr) {
      locals_[clk] = prev;
    } else {
      locals_.erase(clk);
    }
    in_tick_ = false;
  }

  // `lhs op rhs` split of a top-level comparison (so a failure prints both operand values), as prp_sim does.
  bool decompose_compare(TSNode cond, std::vector<TSNode>& kids, size_t& at) const {
    TSNode n = cond;
    while (ts_node_named_child_count(n) == 1 && (ntype(n) == "expression_item" || ntype(n) == "paren_group" || ntype(n) == "expression_list")) {
      n = ts_node_named_child(n, 0);
    }
    kids.clear();
    for (TSNode c : ts_node_named_children(n)) {
      kids.push_back(c);
    }
    size_t ncmp = 0;
    at          = 0;
    for (size_t i = 0; i < kids.size(); ++i) {
      if (ntype(kids[i]) == "binary_logical_op") {
        return false;
      }
      if (ntype(kids[i]) == "binary_compare_op") {
        ++ncmp;
        at = i;
      }
    }
    return ncmp == 1 && at > 0 && at + 1 < kids.size();
  }

  void assertion(TSNode n) {
    TSNode args = field(n, "argument");
    if (ts_node_is_null(args) || ts_node_named_child_count(args) < 1) {
      fail("assert needs a condition");
    }
    TSNode      cond = ts_node_named_child(args, 0);
    std::string msg;
    if (ts_node_named_child_count(args) >= 2) {
      msg = text_of(src_, ts_node_named_child(args, 1));
      if (msg.size() >= 2 && msg.front() == '"') {
        msg = msg.substr(1, msg.size() - 2);
      }
    }
    Assert_site site;
    site.line = static_cast<int>(ts_node_start_point(n).row) + 1;
    site.loc  = file_short_ + ":" + std::to_string(site.line);
    site.cond = trim(text_of(src_, cond));
    site.msg  = msg;
    const auto is_lit = [](const std::string& s) { return !s.empty() && (std::isdigit(static_cast<unsigned char>(s[0])) != 0 || s == "true" || s == "false"); };
    Value* ok   = nullptr;
    Value* lhs  = nullptr;
    Value* rhs  = nullptr;
    std::vector<TSNode> kids;
    size_t              at_i = 0;
    if (decompose_compare(cond, kids, at_i)) {
      Val l    = dat(eval_kids(kids, 0, at_i));
      Val r    = dat(eval_kids(kids, at_i + 1, kids.size()));
      site.cmp = true;
      site.op  = text_of(src_, kids[at_i]);
      for (size_t i = 0; i < at_i; ++i) {
        site.lhs += (i ? " " : "") + trim(text_of(src_, kids[i]));
      }
      for (size_t i = at_i + 1; i < kids.size(); ++i) {
        site.rhs += (i > at_i + 1 ? " " : "") + trim(text_of(src_, kids[i]));
      }
      site.lhs_lit  = is_lit(site.lhs);
      site.rhs_lit  = is_lit(site.rhs);
      site.lhs_bits = l.w;
      site.rhs_bits = r.w;
      ok            = compare(site.op, l, r).v;
      lhs           = spill_words(l, "alhs");
      rhs           = spill_words(r, "arhs");
    } else {
      ok  = to_i1(eval(cond));
      lhs = rhs = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(m_.ptr_));
    }
    const auto idx = static_cast<uint32_t>(sites_.size());
    if (!scratch_) {
      sites_.push_back(site);
    }
    auto* bad  = BasicBlock::Create(m_.ctx(), "assert.fail", fn_);
    auto* cont = BasicBlock::Create(m_.ctx(), "assert.ok", fn_);
    b_.CreateCondBr(ok, cont, bad);
    b_.SetInsertPoint(bad);
    auto* fty = llvm::FunctionType::get(m_.void_, {m_.ptr_, llvm::Type::getInt32Ty(m_.ctx()), m_.i64_, m_.ptr_, m_.ptr_}, false);
    Value* fp = b_.CreateLoad(m_.ptr_, rt_word(prp_tb::kRtAssert));
    Value* cyc = b_.CreateLoad(m_.i64_, rt_word(prp_tb::kRtCycle));
    b_.CreateCall(fty, fp, {rt_, ConstantInt::get(llvm::Type::getInt32Ty(m_.ctx()), idx), cyc, lhs, rhs});
    b_.CreateStore(b_.CreateAdd(b_.CreateLoad(m_.i64_, fails_), c64(1)), fails_);
    b_.CreateBr(cont);
    b_.SetInsertPoint(cont);
  }

  Module_gen&                          m_;
  llvm::IRBuilder<>&                   b_;
  const std::string&                   src_;
  std::string                          file_short_;
  const std::map<std::string, TSNode>& fconsts_;
  const std::vector<Dut>&              duts_;
  std::vector<Instance>                insts_;
  std::map<std::string, llvm::AllocaInst*> locals_;
  std::map<std::string, unsigned>      local_w_;  // each local's width in bits (signed, sign bit included)
  std::set<std::string>                declared_;
  struct Array_info {
    std::vector<TSNode> elems;
    unsigned            width = kDefaultTbWidth;  // element width: the widest element, floored like a local
    llvm::AllocaInst*   slot  = nullptr;
  };
  std::map<std::string, Array_info>    arrays_;  // test-local `[a, b, ..]` tables
  std::vector<Assign>                  assigns_;
  std::vector<Assert_site>             sites_;
  std::vector<Print_site>              prints_;
  std::vector<Unknown_site>            unknowns_;
  std::vector<std::pair<BasicBlock*, BasicBlock*>> loops_;  // (continue, break)
  std::set<std::string>                const_stack_;
  std::map<std::string, int>           ubits_;  // `U<N>` typed locals / aliases: the width a typed `~` flips
  int                                  last_select_w_ = 0;
  std::set<std::string>                str_locals_;  // locals that hold a string handle
  std::vector<Print_site>              strings_;     // the `"..{x}.."` values, formatted by the runtime
  std::map<std::string, TSNode>        refs_;  // `mut c = regref(...)`: the name of the alias -> its call node
  std::map<llvm::AllocaInst*, unsigned> slot_w_;  // each slot's SEMANTIC width (its type is the physical one)
  TSNode                               base_idx_{};
  Function*                            fn_    = nullptr;
  BasicBlock*                          entry_ = nullptr;
  Value *                              rt_ = nullptr, *st_ = nullptr, *args_ = nullptr;
  llvm::AllocaInst*                    fails_  = nullptr;
  bool                                 in_tick_ = false;
  size_t                               steps_lowered_ = 0;
  bool                                 trace_ = false;
  bool                                 scratch_ = false;  // the width-inference pass: nothing it lowers is kept
  int                                  depth_ = 0;       // nesting of if/tick scopes (a checkpointable tick is at depth 0)
  int64_t                              top_stmt_ = 0;    // index of the top-level statement being lowered
  bool                                 has_top_tick_ = false;
  llvm::AllocaInst* tick_count_ = nullptr;
  std::vector<Frame_slot>              frame_;           // the checkpointed locals
  size_t                               frame_words_ = 0;
};

// ---- object emission --------------------------------------------------------------------------------------
bool emit_object(llvm::Module& mod, const std::string& path, std::string& err, bool dump_bitcode,
                 std::optional<uint64_t> cache_salt) {
  livehd::sim::initialize_llvm_target();
  llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
  mod.setTargetTriple(triple);
  std::string         lookup;
  const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, lookup);
  if (target == nullptr) {
    err = lookup;
    return false;
  }
  llvm::TargetOptions                  options;
  std::unique_ptr<llvm::TargetMachine> machine(
      target->createTargetMachine(triple, "generic", "", options, llvm::Reloc::PIC_, std::nullopt, llvm::CodeGenOptLevel::Default));
  if (!machine) {
    err = "LLVM could not create a native target machine";
    return false;
  }
  mod.setDataLayout(machine->createDataLayout());
  {
    std::string              why;
    llvm::raw_string_ostream os(why);
    if (llvm::verifyModule(mod, &os)) {
      err = "internal: the testbench IR does not verify: " + why;
      return false;
    }
  }
  // Bind reuse to the exact lowered module (including the target layout), the generator build
  // and the bytes on disk. A partial write or a stale sidecar is always a miss.
  const auto digest = [](llvm::StringRef bytes) {
    llvm::SHA256 hash;
    hash.update(bytes);
    const auto sum = hash.final();
    return std::string(reinterpret_cast<const char*>(sum.data()), sum.size());
  };
  std::string                key;
  llvm::SmallVector<char, 0> bitcode;
  if (cache_salt || dump_bitcode) {
    llvm::raw_svector_ostream stream(bitcode);
    llvm::WriteBitcodeToFile(mod, stream);
    if (dump_bitcode) {
      const auto    filename = fs::path(path).replace_extension(".bc");
      std::ofstream dump(filename, std::ios::binary);
      dump.write(bitcode.data(), static_cast<std::streamsize>(bitcode.size()));
      dump.close();
      if (!dump) {
        err = "could not write " + filename.string();
        return false;
      }
    }
  }
  if (cache_salt) {
    key              = std::to_string(*cache_salt) + ":" + digest({bitcode.data(), bitcode.size()});
    const auto saved = slurp(path + ".key");
    if (saved.size() == key.size() + 32 && saved.starts_with(key)) {
      const auto bytes = slurp(path);
      if (!bytes.empty() && saved == key + digest(bytes)) {
        return true;
      }
    }
  }
  llvm::LoopAnalysisManager     lam;
  llvm::FunctionAnalysisManager fam;
  llvm::CGSCCAnalysisManager    cgam;
  llvm::ModuleAnalysisManager   mam;
  llvm::PassBuilder             pb(machine.get());
  pb.registerModuleAnalyses(mam);
  pb.registerCGSCCAnalyses(cgam);
  pb.registerFunctionAnalyses(fam);
  pb.registerLoopAnalyses(lam);
  pb.crossRegisterProxies(lam, fam, cgam, mam);
  llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
  mpm.run(mod, mam);
  llvm::SmallVector<char, 0> buffer;
  llvm::raw_svector_ostream  os(buffer);
  llvm::legacy::PassManager  emit;
  if (machine->addPassesToEmitFile(emit, os, nullptr, llvm::CodeGenFileType::ObjectFile)) {
    err = "LLVM target cannot emit native testbench objects";
    return false;
  }
  emit.run(mod);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  out.close();
  if (!out) {
    err = "could not write " + path;
    return false;
  }
  if (cache_salt) {
    std::ofstream sidecar(path + ".key", std::ios::binary | std::ios::trunc);
    sidecar << key << digest({buffer.data(), buffer.size()});
  }
  return true;
}

}  // namespace

int generate(const std::string& file, const std::string& simdir, const std::string& test_sel,
             std::vector<prp_sim::Test_info>& tests, std::string& err, bool also_trace, bool dump_bitcode,
             std::optional<uint64_t> cache_salt) {
  try {
    const std::string src = slurp(file);
    if (src.empty()) {
      err = "could not read source file: " + file;
      return -1;
    }
    prpparse::Source_buffer buf(file, src);
    prpparse::Parser        parser(buf);
    prpparse::Ast*          root = nullptr;
    try {
      root = parser.parse_ast();
    } catch (...) {
      err = "parse error in " + file;
      return -1;
    }
    TSNode root_node{root, &buf};

    std::map<std::string, TSNode> fconsts;
    for (TSNode c : ts_node_named_children(root_node)) {
      TSNode decl = ntype(c) == "assignment" ? field(c, "decl") : TSNode{};
      TSNode kind = ts_node_is_null(decl) ? TSNode{} : field(decl, "storage");
      if (!ts_node_is_null(kind) && ntype(kind) == "const_decl") {
        if (const auto nm = lvalue_name(src, field(c, "lvalue")); !nm.empty() && !ts_node_is_null(field(c, "rvalue"))) {
          fconsts[nm] = field(c, "rvalue");
        }
      }
    }

    const auto        duts       = load_duts(simdir);
    const std::string file_short = fs::path(file).filename().string();
    std::vector<Test_result> results;
    // One pass over the `test` blocks per object: the simulation object (no tracing code) and, on request,
    // the trace object the VCD replay tool runs. Only the first pass reports the tests.
    const auto build = [&](bool trace, const std::string& object) -> bool {
      Module_gen               mg;
      std::vector<Test_result> mine;
      size_t                   index = 0;
      for (TSNode c : ts_node_named_children(root_node)) {
        if (ntype(c) != "test_statement") {
          continue;
        }
        TSNode name_node = field(c, "name");
        if (ts_node_is_null(name_node)) {
          continue;
        }
        const std::string name = str_tools::canonical_escaped_path(text_of(src, name_node));
        if (!test_sel.empty() && name != str_tools::canonical_escaped_path(test_sel)) {
          continue;
        }
        Test_gen    tg(mg, src, file_short, fconsts, duts, trace);
        Test_result r = tg.gen(c, name, index++);
        if (!trace) {
          prp_sim::Test_info info;
          info.name = name;
          for (const auto& p : r.params) {
            prp_sim::Param_info pi;
            pi.name     = p.name;
            pi.required = p.required;
            if (!p.required) {
              pi.default_text = text_of(src, p.default_node);
            }
            info.params.push_back(std::move(pi));
          }
          tests.push_back(std::move(info));
        }
        mine.push_back(std::move(r));
      }
      if (mine.empty()) {
        err = test_sel.empty() ? "no `test` block in " + file : "no test named '" + test_sel + "' in " + file;
        return false;
      }
      if (std::string why; !emit_object(mg.mod(), simdir + "/" + object, why, dump_bitcode, cache_salt)) {
        err = why;
        return false;
      }
      if (!trace) {
        results = std::move(mine);
      }
      return true;
    };
    if (!build(false, kObject)) {
      return -1;
    }
    if (also_trace) {
      if (!build(true, kTraceObject)) {
        return -1;
      }
    } else {
      std::error_code ec;
      fs::remove(simdir + "/" + kTraceObject, ec);  // a stale trace object must not outlive its sources
    }

    std::error_code cleanup_error;
    if (!dump_bitcode) {
      fs::remove(fs::path(simdir) / "drv.tb.bc", cleanup_error);
    }
    if (!dump_bitcode || !also_trace) {
      fs::remove(fs::path(simdir) / "drv.tb.trace.bc", cleanup_error);
    }

    std::set<std::string> used_stems;
    std::string           j = std::format("{{\"schema_version\":2,\"kind\":\"sim_tb\",\"file\":{},\"tests\":[", json_str(file));
    for (size_t t = 0; t < results.size(); ++t) {
      const auto& r = results[t];
      j += std::format("{}{{\"name\":{},\"symbol\":{},\"state_words\":{},\"params\":[", t ? "," : "", json_str(tests[t].name), json_str(r.symbol),
                       r.state_words);
      for (size_t p = 0; p < r.params.size(); ++p) {
        j += std::format("{}{{\"name\":{},\"type\":{},\"required\":{},\"default\":{},\"string\":{},\"dynamic\":{}}}", p ? "," : "",
                         json_str(r.params[p].name), json_str(r.params[p].type), r.params[p].required ? "true" : "false",
                         json_str(r.params[p].required ? std::string{} : text_of(src, r.params[p].default_node)),
                         r.params[p].is_string ? "true" : "false", r.params[p].dynamic ? "true" : "false");
      }
      j += std::format("],\"frame_words\":{},\"frame\":[", r.frame_words);
      for (size_t f = 0; f < r.frame.size(); ++f) {
        j += std::format("{}{}", f ? "," : "", json_str(r.frame[f]));
      }
      j += "],\"instances\":[";
      for (size_t i = 0; i < r.instances.size(); ++i) {
        used_stems.insert(r.instances[i].dut->stem);
        j += std::format("{}{{\"var\":{},\"stem\":{},\"base\":{}}}", i ? "," : "", json_str(r.instances[i].var), json_str(r.instances[i].dut->stem),
                         r.instances[i].base);
      }
      j += "],\"asserts\":[";
      for (size_t a = 0; a < r.asserts.size(); ++a) {
        const auto& s = r.asserts[a];
        j += std::format("{}{{\"line\":{},\"loc\":{},\"cond\":{},\"lhs\":{},\"op\":{},\"rhs\":{},\"msg\":{},\"cmp\":{},\"lhs_lit\":{},\"rhs_lit\":{},\"lhs_bits\":{},\"rhs_bits\":{}}}",
                         a ? "," : "", s.line, json_str(s.loc), json_str(s.cond), json_str(s.lhs), json_str(s.op), json_str(s.rhs), json_str(s.msg),
                         s.cmp ? "true" : "false", s.lhs_lit ? "true" : "false", s.rhs_lit ? "true" : "false", s.lhs_bits, s.rhs_bits);
      }
      j += "],\"prints\":[";
      for (size_t a = 0; a < r.prints.size(); ++a) {
        const auto& s = r.prints[a];
        j += std::format("{}{{\"prefix\":{},\"newline\":{},\"segs\":[", a ? "," : "", json_str(s.prefix), s.newline ? "true" : "false");
        for (size_t g = 0; g < s.segs.size(); ++g) {
          const auto& sg = s.segs[g];
          j += std::format("{}{{\"hole\":{},\"text\":{},\"base\":{},\"width\":{},\"sep\":{},\"bits\":{}}}", g ? "," : "", sg.hole ? "true" : "false",
                           json_str(sg.text), json_str(std::string(1, sg.base)), sg.width, sg.sep ? "true" : "false", sg.bits);
        }
        j += "]}";
      }
      j += "],\"strings\":[";
      for (size_t a = 0; a < r.strings.size(); ++a) {
        const auto& s = r.strings[a];
        j += std::format("{}{{\"segs\":[", a ? "," : "");
        for (size_t g = 0; g < s.segs.size(); ++g) {
          const auto& sg = s.segs[g];
          j += std::format("{}{{\"hole\":{},\"text\":{},\"base\":{},\"width\":{},\"sep\":{},\"bits\":{}}}", g ? "," : "", sg.hole ? "true" : "false",
                           json_str(sg.text), json_str(std::string(1, sg.base)), sg.width, sg.sep ? "true" : "false", sg.bits);
        }
        j += "]}";
      }
      j += "],\"unknowns\":[";
      for (size_t u = 0; u < r.unknowns.size(); ++u) {
        const auto& k = r.unknowns[u];
        j += std::format("{}{{\"bits\":{},\"sign\":{},\"known\":{},\"mask\":{},\"key\":{}}}", u ? "," : "", k.bits, k.sign ? "true" : "false",
                         json_str(k.known), json_str(k.mask), k.key);
      }
      j += "]}";
    }
    j += "],\"duts\":[";
    bool first = true;
    for (const auto& d : duts) {
      if (!used_stems.contains(d.stem)) {
        continue;
      }
      j += std::format("{}{}", first ? "" : ",", json_str(d.stem));
      first = false;
    }
    j += "]}\n";
    {
      std::ofstream out(simdir + "/" + kManifest, std::ios::binary | std::ios::trunc);
      out << j;
      out.close();
      if (!out) {
        err = "could not write " + simdir + "/" + kManifest;
        return -1;
      }
    }
    return 0;
  } catch (const Gen_error& e) {
    err = e.msg;
    return -1;
  }
}

}  // namespace prp_tb
