// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Cgen_llvm is the small, target-independent IR builder used by the opt-in
// simulator backend.  It deliberately exposes no LLVM types in this header:
// cgen_sim describes a color with exact-width integer operations and this
// class owns LLVM context/module/target details behind the pimpl.
class Cgen_llvm {
public:
  struct Value {
    size_t   id     = 0;
    uint32_t width  = 0;
    bool     unsign = false;
  };

  enum class Binary_op {
    add,
    sub,
    mul,
    div,
    rem,
    bit_and,
    bit_or,
    bit_xor,
    shl,
    lshr,
    ashr,
    eq,
    ne,
    lt,
    le,
    gt,
    ge,
  };

  explicit Cgen_llvm(std::string_view function_name, const std::vector<std::pair<uint32_t, bool>>& inputs, bool scalar_abi = false);
  ~Cgen_llvm();

  Cgen_llvm(const Cgen_llvm&)            = delete;
  Cgen_llvm& operator=(const Cgen_llvm&) = delete;
  Cgen_llvm(Cgen_llvm&&) noexcept;
  Cgen_llvm& operator=(Cgen_llvm&&) noexcept;

  [[nodiscard]] Value input(size_t index) const;
  [[nodiscard]] Value constant(uint32_t width, uint64_t value, bool unsign = true);
  [[nodiscard]] Value constant_words(uint32_t width, const std::vector<uint64_t>& words, bool unsign = true);
  [[nodiscard]] Value resize(Value value, uint32_t result_width, bool result_unsign);
  [[nodiscard]] Value unary_not(Value value, uint32_t result_width, bool result_unsign);
  [[nodiscard]] Value reduce_or(Value value, uint32_t result_width, bool result_unsign);
  [[nodiscard]] Value bitfield_insert(Value base, Value inserted, uint32_t lo, uint32_t hi, uint32_t result_width,
                                      bool result_unsign);
  [[nodiscard]] Value sign_extend_from(Value value, uint32_t sign_bit, uint32_t result_width, bool result_unsign);
  [[nodiscard]] Value binary(Binary_op op, Value lhs, Value rhs, uint32_t result_width, bool result_unsign);
  [[nodiscard]] Value mux(Value select, Value when_false, Value when_true, uint32_t result_width, bool result_unsign);
  [[nodiscard]] Value indexed_mux(Value select, const std::vector<Value>& arms, bool one_hot, uint32_t result_width,
                                  bool result_unsign);
  [[nodiscard]] Value lut(Value table, Value address, uint32_t result_width, bool result_unsign);
  [[nodiscard]] Value external_read(std::string_view symbol, Value address, uint32_t result_width, bool result_unsign);
  [[nodiscard]] Value external_read_all(std::string_view symbol, uint32_t result_width, bool result_unsign);
  bool                external_apply(std::string_view symbol, Value data);
  bool                external_clear(std::string_view symbol);
  bool                external_stage_whole(std::string_view symbol, Value enable, Value force, Value data);
  bool                external_stage_write(std::string_view symbol, Value enable, Value address, Value data);

  // Values cross the ABI as packed little-endian 64-bit words. `index` is the
  // logical output number; physical word offsets are derived from the exact
  // output widths when the object is finalized.
  bool add_output(size_t index, Value value, std::string& error);

  // Verify, optimize, and emit LLVM bitcode. The module
  // exports:
  //   void function_name(const uint64_t* inputs,
  //                      uint64_t* outputs,
  //                      uint64_t* changed,
  //                      void* owner)
  // `changed` is a packed bitset with one bit per logical output. LLVM uses
  // exact-width integers internally; uint64_t is only the stable packed ABI.
  bool write_object(std::string_view path, std::string& error);

  // Inline the emitted color bitcode into one host-C++ bitcode translation
  // unit and lower the combined module to a native relocatable object.
  static bool link_bitcode_object(std::string_view host_path, const std::vector<std::string>& kernel_paths,
                                  std::string_view object_path, std::string& error);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
