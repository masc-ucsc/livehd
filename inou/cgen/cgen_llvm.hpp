// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sim_native_abi.hpp"

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

  // Packed ABI: inputs, outputs, and changed are disjoint buffers and do not
  // overlap resource storage. Generated callers guarantee this for LLVM noalias.
  // Values (and boundary casts) load lazily at their first arithmetic use.
  explicit Cgen_llvm(std::string_view function_name, const std::vector<std::pair<uint32_t, bool>>& inputs, bool scalar_abi = false);
  // Native simulator ABI: void(uint64_t* state, void** resources). Every
  // field occupies whole little-endian words in one caller-owned allocation.
  // Inputs and outputs may alias: outputs observe the state before any output
  // is written. No per-invocation packing buffers or C++ adapters are needed.
  struct State_input {
    size_t   word   = 0;
    uint32_t width  = 0;
    bool     unsign = false;
    uint32_t bit    = 0;  // a fixed slice, without another stored copy
  };
  struct State_layout {
    size_t                   words = 0;
    std::vector<State_input> inputs;
  };
  struct Dirty_mask {
    size_t   word = 0;
    uint64_t mask = 0;
  };
  Cgen_llvm(std::string_view function_name, const State_layout& layout);
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
  [[nodiscard]] Value count_bits(Value value, uint32_t count, uint32_t result_width, bool parity);
  [[nodiscard]] Value bitfield_insert(Value base, Value inserted, uint32_t lo, uint32_t hi, uint32_t result_width,
                                      bool result_unsign);
  [[nodiscard]] Value sign_extend_from(Value value, uint32_t sign_bit, uint32_t result_width, bool result_unsign);
  [[nodiscard]] Value binary(Binary_op op, Value lhs, Value rhs, uint32_t result_width, bool result_unsign);
  // Bits [count, count + len) of the UNSIGNED `source` (zero past its width),
  // len <= 64, `count` read as a non-negative word index + bit offset: two
  // guarded word loads and a funnel shift instead of a variable shift of the
  // whole value (a 7,800-bit register file read per lane iteration).
  [[nodiscard]] Value dynamic_extract(Value source, Value count, uint32_t len, uint32_t result_width, bool result_unsign);
  [[nodiscard]] Value mux(Value select, Value when_false, Value when_true, uint32_t result_width, bool result_unsign);
  [[nodiscard]] Value hotmux(const std::vector<Value>& inputs, uint32_t result_width, bool result_unsign);
  [[nodiscard]] Value indexed_mux(Value select, const std::vector<Value>& arms, uint32_t result_width, bool result_unsign);
  [[nodiscard]] Value lut(Value table, Value address, uint32_t result_width, bool result_unsign);
  // Resource indices address a caller-owned array of data pointers, never
  // functions. Memory layout is the shared packed simulator ABI.
  struct Memory {
    size_t   data               = 0;
    size_t   pending            = 0;
    size_t   gate               = 0;
    size_t   random             = 0;
    size_t   draws              = 0;
    uint32_t bits               = 0;
    uint64_t size               = 0;
    uint32_t writes             = 0;
    uint32_t lanes              = 1;
    uint32_t port               = 0;
    uint32_t forward            = 0;
    uint32_t undefined          = 0;
    bool     gated              = false;
    bool     commit_before_read = false;
    bool     packed_value       = false;
  };
  void                bind_memory(std::string_view symbol, const Memory& memory);
  [[nodiscard]] Value memory_read(std::string_view symbol, Value address, uint32_t result_width, bool result_unsign);
  [[nodiscard]] Value memory_read_all(std::string_view symbol, uint32_t result_width, bool result_unsign);
  bool                memory_apply(std::string_view symbol, Value data);
  bool                memory_clear(std::string_view symbol);
  bool                memory_stage_whole(std::string_view symbol, Value enable, Value force, Value data);
  bool                memory_stage_write(std::string_view symbol, Value enable, Value address, Value data);
  // Phase-barrier entry writes, emitted into a native commit object.
  bool                memory_commit(std::string_view symbol);

  // Values cross the ABI as packed little-endian 64-bit words. `index` is the
  // logical output number; physical word offsets are derived from the exact
  // output widths when the object is finalized.
  bool add_output(size_t index, Value value, std::string& error);
  // Compare against `previous_word` (the current-state bank for a pending
  // register), store into `word`, and OR dirty masks when the value changes.
  bool add_state_output(size_t word, size_t previous_word, Value value, const std::vector<Dirty_mask>& dirty, std::string& error);
  // Phase-barrier copy. Source and destination spans must not overlap.
  bool copy_state(size_t destination, size_t source, size_t words, std::string& error);

  // Add a rolled reduction entry to this stateless body object. The body is
  // inlined into the native loop once; neither a call nor loop unrolling is
  // permitted. Domain arguments stay dynamic so changing the trip count does
  // not invalidate the body object. Inputs use the whole declared-port layout.
  struct Loop_binding {
    size_t   input = 0;
    uint32_t bit   = 0;
  };
  struct Loop_layout {
    std::vector<std::pair<uint32_t, bool>> inputs;
    std::vector<Loop_binding>              bindings;  // one per body ABI input
    std::optional<size_t>                  index, activation, next_active;
    std::vector<std::pair<size_t, size_t>> carries;  // input, output indices
  };
  // void entry(const uint64_t* inputs, uint64_t* outputs,
  //            uint64_t count, int64_t first, int64_t step)
  bool add_loop(std::string_view entry, const Loop_layout& layout, std::string& error, bool track_changed = true);

  // Finalize the ABI and return exact bitcode with the color and optional loop
  // entry symbols normalized. Equal keys can share code; mutable instance storage is still
  // supplied separately. Do not add operations after requesting this key.
  std::string sharing_key(std::string& error, bool track_changed = true);

  // Verify, optimize, and emit a self-contained native object. The module
  // exports:
  //   void function_name(const uint64_t* inputs,
  //                      uint64_t* outputs,
  //                      uint64_t* changed,
  //                      void* owner)
  // `changed` is a packed bitset with one bit per logical output. LLVM uses
  // exact-width integers internally; uint64_t is only the stable packed ABI.
  //
  // `track_changed == false` drops that bitset, and with it the load of every
  // output's PREVIOUS value out of the caller's buffer. The caller then has to
  // pre-pack nothing: it learns what moved from the store it performs anyway
  // (`slop_update`, or `_din.identical(state)`), where the old value is already
  // the live object rather than a marshalled copy. `changed` is still a
  // parameter, so the ABI and the adapter's call do not change shape.
  // An optional sidecar allows a verified IR/object pair to skip optimization
  // and machine-code generation. Empty disables reuse. No graph-global key
  // or mutable binding offsets are added to the kernel identity.
  bool write_object(std::string_view path, std::string& error, bool track_changed = true, std::string_view cache_path = {},
                    bool* reused = nullptr);

  // Package a state-ABI kernel with its allocation/initialization descriptor.
  // Only `descriptor` is exported; code, offsets, and defaults stay local to
  // this object. The first public_words are boundary storage, and the tail is
  // private to each instance. Unspecified initial words are zero.
  bool write_state_object(std::string_view path, std::string_view descriptor, size_t public_words,
                          const std::vector<livehd::sim::Native_initial_word>& initial, std::string& error);

  // IR inspection for code-generator tests; not the simulator build path.
  bool write_bitcode(std::string_view path, std::string& error, bool track_changed = true);

  // Inline the emitted color bitcode into one host-C++ bitcode translation
  // unit and lower the combined module to a native relocatable object.
  static bool link_bitcode_object(std::string_view host_path, const std::vector<std::string>& kernel_paths,
                                  std::string_view object_path, std::string& error);

private:
  bool seal(std::string& error, bool track_changed);
  bool write_module(std::string_view path, std::string& error, bool track_changed, bool native);
  class Impl;
  std::unique_ptr<Impl> impl_;
};
