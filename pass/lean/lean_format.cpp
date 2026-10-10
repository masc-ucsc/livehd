// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "lean_format.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
namespace lean_export {
const std::unordered_set<std::string> kLeanReserved = {
    "abbrev",  "axiom",     "by",   "class",    "def",      "deriving", "do",        "else",     "end",        "example",
    "false",   "for",       "fun",  "if",       "import",   "in",       "inductive", "instance", "let",        "match",
    "mutual",  "namespace", "open", "opaque",   "partial",  "private",  "protected", "rec",      "set_option", "structure",
    "theorem", "then",      "true", "universe", "variable", "where",    "with",
};

std::string lean_string(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2);
  out.push_back('"');
  for (unsigned char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        // Anything else non-printable becomes an escape rather than a raw byte:
        // a clock root key can carry the \x01/\x02 sentinels latch_contract
        // uses, and a raw control byte in a source file is not portable.
        if (c < 0x20 || c == 0x7f) {
          char buf[16];
          std::snprintf(buf, sizeof(buf), "\\x%02x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  out.push_back('"');
  return out;
}

std::string sanitize_lean(std::string_view name) {
  std::string out;
  out.reserve(name.size() + 4);

  for (unsigned char c : name) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    if (ok) {
      out.push_back(static_cast<char>(c));
    } else {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "_x%02x_", c);
      out += buf;
    }
  }

  if (out.empty()) {
    out = "id";
  }
  if (out[0] >= '0' && out[0] <= '9') {
    out = "id_" + out;
  }
  if (kLeanReserved.count(out) > 0) {
    out = "id_" + out;
  }
  return out;
}

std::string lean_integer(std::string_view value) {
  if (value.empty()) {
    throw std::invalid_argument("empty decimal integer");
  }
  if (value == "0") {
    return "0";
  }
  if (value.front() == '-') {
    return "(-Int.ofNat " + std::string(value.substr(1)) + ")";
  }
  return "(Int.ofNat " + std::string(value) + ")";
}
std::string nat_array(const std::vector<uint32_t>& values) {
  std::ostringstream out;
  out << "#[";
  for (size_t i = 0; i < values.size(); ++i) {
    if (i) {
      out << ", ";
    }
    out << values[i];
  }
  out << "]";
  return out.str();
}
void write_atomic(const std::string& path, const std::function<void(std::ostream&)>& emit) {
  const auto tmp = path + ".tmp";
  try {
    std::ofstream out;
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out.open(tmp);
    emit(out);
    out.close();
    std::filesystem::rename(tmp, path);
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(tmp, ignored);
    throw;
  }
}
namespace {
// Stage `emit` into `path + suffix`, so a failed write never touches `path`.
void stage(const std::string& path, const std::string& suffix,
           const std::function<void(std::ostream&)>& emit) {
  std::ofstream out;
  out.exceptions(std::ios::failbit | std::ios::badbit);
  out.open(path + suffix);
  emit(out);
  out.close();
}
}  // namespace

void publish_pair(const std::string& path_a, const std::function<void(std::ostream&)>& emit_a,
                  const std::string& path_b, const std::function<void(std::ostream&)>& emit_b) {
  const std::string ta = path_a + ".tmp", tb = path_b + ".tmp";
  const std::string ba = path_a + ".bak", bb = path_b + ".bak";
  std::error_code   ec;

  // EVERY UNDO IS CONDITIONED ON HAVING DONE THE THING.
  //
  // The first version of this function assumed that, by the time anything threw,
  // both originals had been backed up -- so its catch deleted both targets and
  // restored from both backups. When the FIRST backup rename failed (a directory
  // sitting at `cert.lean.bak` is enough), the original had never been moved, the
  // catch deleted it anyway, and the restore then moved that directory into its
  // place. Measured: original A lost and replaced by a directory, original B
  // deleted outright. The same shape with `io.json.bak` blocked lost B.
  //
  // Losing the previous pair is precisely what this function exists to prevent,
  // so the state is tracked rather than assumed.
  bool moved_a = false, moved_b = false, put_a = false, put_b = false;

  // REJECT A COLLISION BEFORE MUTATING ANYTHING. A pre-existing `.bak` is either
  // a leftover from an interrupted publish -- which may be the only copy of a
  // previous artifact -- or an unrelated file. Renaming over it destroys it, and
  // renaming ONTO a directory fails in the middle of the transaction. Refusing up
  // front means the targets are untouched when it happens.
  if (std::filesystem::exists(ba, ec) || std::filesystem::exists(bb, ec)) {
    throw std::runtime_error("lean publish: a backup path already exists (" + ba + " or " + bb
                             + "); refusing to publish rather than overwrite it. Move or remove it "
                               "after checking whether it holds the only copy of a previous artifact.");
  }

  auto scrub_tmp = [&] {
    std::filesystem::remove(ta, ec);
    std::filesystem::remove(tb, ec);
  };
  // Both files are written BEFORE either is published, so an emit that throws
  // cannot leave one target replaced and the other not.
  try {
    stage(path_a, ".tmp", emit_a);
    stage(path_b, ".tmp", emit_b);
  } catch (...) {
    scrub_tmp();
    throw;
  }

  const bool had_a = std::filesystem::exists(path_a, ec);
  const bool had_b = std::filesystem::exists(path_b, ec);
  try {
    if (had_a) {
      std::filesystem::rename(path_a, ba);
      moved_a = true;
    }
    if (had_b) {
      std::filesystem::rename(path_b, bb);
      moved_b = true;
    }
    std::filesystem::rename(ta, path_a);
    put_a = true;
    std::filesystem::rename(tb, path_b);
    put_b = true;
  } catch (...) {
    // Remove only what WE published. A target we never wrote is either the
    // untouched original or absent, and deleting it is the loss this guards.
    if (put_a) {
      std::filesystem::remove(path_a, ec);
    }
    if (put_b) {
      std::filesystem::remove(path_b, ec);
    }
    // Restore only from backups we actually made, and KEEP any backup whose
    // restore fails: at that point it is the sole copy, and removing it to tidy
    // up would destroy the artifact this whole path is protecting.
    std::string stranded;
    if (moved_a) {
      std::error_code r;
      std::filesystem::rename(ba, path_a, r);
      if (r) {
        stranded += " " + ba;
      }
    }
    if (moved_b) {
      std::error_code r;
      std::filesystem::rename(bb, path_b, r);
      if (r) {
        stranded += " " + bb;
      }
    }
    scrub_tmp();
    if (!stranded.empty()) {
      // Report rather than swallow: a persistent filesystem error can prevent
      // restoration, and the caller needs to know the recovery data is still
      // there under a different name.
      throw std::runtime_error("lean publish: FAILED and could not restore the previous artifact(s). "
                               "The only remaining copy is at:" + stranded
                               + " -- do not delete it; rename it back by hand.");
    }
    throw;
  }
  std::filesystem::remove(ba, ec);
  std::filesystem::remove(bb, ec);
}

std::string lit_bv(uint32_t w, std::string_view v) { return "(BitVec.ofInt " + std::to_string(w) + " (" + std::string(v) + "))"; }

std::string lit_zero(uint32_t w) { return "(0#" + std::to_string(w) + ")"; }

std::string lit_one(uint32_t w) { return "(1#" + std::to_string(w) + ")"; }

std::string bst_literal(const std::vector<std::pair<uint32_t, std::string>>& sorted, size_t lo, size_t hi) {
  if (lo >= hi) {
    return "BT.lf";
  }
  const size_t mid = lo + (hi - lo) / 2;
  return "(BT.nd " + std::to_string(sorted[mid].first) + " (" + sorted[mid].second + ") " + bst_literal(sorted, lo, mid) + " "
         + bst_literal(sorted, mid + 1, hi) + ")";
}

std::string bst_literal(std::vector<std::pair<uint32_t, std::string>> pairs) {
  std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  return bst_literal(pairs, 0, pairs.size());
}

std::string nat_list(const std::vector<uint32_t>& xs) {
  std::ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < xs.size(); ++i) {
    if (i != 0) {
      oss << ", ";
    }
    oss << xs[i];
  }
  oss << "]";
  return oss.str();
}

std::string format_op(const Op& op) {
  switch (op.kind) {
    case Operation::Const     : return std::string("LGraphOp.Op_Const") + " (" + lean_integer(op.value) + ")";
    case Operation::Sum       : return std::string("LGraphOp.Op_Sum") + " " + std::to_string(op.parameter);
    case Operation::Mult      : return std::string("LGraphOp.Op_Mult");
    case Operation::UDiv      : return std::string("LGraphOp.Op_UDiv");
    case Operation::And       : return std::string("LGraphOp.Op_And");
    case Operation::Or        : return std::string("LGraphOp.Op_Or");
    case Operation::Xor       : return std::string("LGraphOp.Op_Xor");
    case Operation::Ror       : return std::string("LGraphOp.Op_Ror");
    case Operation::EQ        : return std::string("LGraphOp.Op_EQ");
    case Operation::Not       : return std::string("LGraphOp.Op_Not");
    case Operation::SLT       : return std::string("LGraphOp.Op_SLT");
    case Operation::ULT       : return std::string("LGraphOp.Op_ULT");
    case Operation::SGT       : return std::string("LGraphOp.Op_SGT");
    case Operation::UGT       : return std::string("LGraphOp.Op_UGT");
    case Operation::SHL       : return std::string("LGraphOp.Op_SHL");
    case Operation::SRA       : return std::string("LGraphOp.Op_SRA");
    case Operation::MuxBool   : return std::string("LGraphOp.Op_MuxBool");
    case Operation::MuxN      : return std::string("LGraphOp.Op_MuxN");
    case Operation::Sext      : return std::string("LGraphOp.Op_Sext");
    case Operation::GetMask   : return std::string("LGraphOp.Op_GetMask");
    case Operation::SetMask   : return std::string("LGraphOp.Op_SetMask");
    case Operation::MemRead   : return std::string("LGraphOp.Op_MemRead");
    case Operation::MemWrite  : return std::string("LGraphOp.Op_MemWrite");
    case Operation::MemWriteBE: return std::string("LGraphOp.Op_MemWriteBE") + " " + std::to_string(op.parameter);
  }
  throw std::invalid_argument("unknown certificate operation");
}
}  // namespace lean_export
