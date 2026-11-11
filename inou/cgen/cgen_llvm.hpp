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
  [[nodiscard]] size_t instruction_count() const;
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

  // Where one value sits in a packed ABI buffer (inputs, outputs, and the
  // transport buffers of inlined bodies and native loops): bits [bit, bit +
  // width) of word `word`, or whole words from `word` for a value wider than
  // 64 bits. `alone` = no other value shares any of its words, so the words
  // may be loaded and stored whole.
  struct Packed_field {
    size_t   word  = 0;
    uint32_t bit   = 0;
    bool     alone = true;
  };
  // The ONE layout rule, shared by the kernel and its C++ adapter: values of
  // at most 64 bits pack first-fit into the current word (a value that does
  // not fit the remaining bits starts the next word); a wider value starts a
  // fresh word and owns whole words. So 64 one-bit outputs cross as one word,
  // not 64. `words` receives the buffer length. LHD_SIM_LLVM_WORD_ABI=1 (a
  // debug A/B switch) gives every value its own word(s), the old layout.
  [[nodiscard]] static std::vector<Packed_field> packed_layout(const std::vector<uint32_t>& widths, size_t* words = nullptr);

  // Packed ABI: inputs, outputs, and changed are disjoint buffers and do not
  // overlap resource storage. Generated callers guarantee this for LLVM noalias.
  // Values (and boundary casts) load lazily at their first arithmetic use.
  // Scalar ABI: inputs are word arguments; outputs are concatenated into one
  // uint64_t return, first-added output in the low bits (at most 64 bits total).
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
  // Most-significant lane first; adjacent slices of one value stay a wide slice.
  [[nodiscard]] Value concat(const std::vector<Value>& lanes);
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
  // `base` with the `len` (<= 64) bits from bit `count` replaced by the low bits of `value`; what
  // would land past `base`'s width is dropped, a `count` past it changes nothing.
  [[nodiscard]] Value dynamic_insert(Value base, Value count, uint32_t len, Value value, uint32_t result_width, bool result_unsign);
  // A wide value kept in MEMORY while a loop reads and writes lanes of it at positions that depend
  // on the loop index (`x#[i*8..+8] = y#[(31-i)*8..+8]`). As an SSA value every trip shifts and
  // masks the whole value several times (a 256-bit byte swap: ~300 instructions a trip); in a
  // buffer an aligned lane is one load or one store. `lane_buffer_create` stores `seed` at the
  // current position (before the loop); `lane_buffer_load` reads the whole value back (after it).
  [[nodiscard]] size_t lane_buffer_create(Value seed);
  [[nodiscard]] Value  lane_buffer_load(size_t buffer, uint32_t result_width, bool result_unsign);
  [[nodiscard]] Value  lane_buffer_extract(size_t buffer, Value count, uint32_t len, uint32_t result_width, bool result_unsign);
  bool                 lane_buffer_insert(size_t buffer, Value count, uint32_t len, Value value);
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
    // The scheduler has placed reads and writes in memory-semantic order.
    bool     direct_write       = false;
    // pass.bitwidth proved every port address of this memory inside [0, size)
    // (attrs::mem_address_proven): no range check, out-of-range select, unknown-
    // address path or undefined-access report is emitted for it.
    bool     address_proven     = false;
    // sim.warn_undefined: resource of this port's __lhd_undef_site (an
    // enabled write or a read outside [0, size)), or no_site.
    size_t   warn_site          = no_site;
  };
  static constexpr size_t no_site = ~size_t{0};
  // A clockless (type=2) array as a kernel-local std::array: entries are
  // stored at their natural width (8/16/32/64 bits, wider ones in whole
  // 64-bit words), so an array of 8/16/32/64-bit entries has exactly the
  // layout of its packed value. It holds no state: a read sequence seeds it
  // with the cycle's base contents, applies the writes that precede the read
  // in program order, and loads.
  [[nodiscard]] size_t    array_create(uint32_t bits, uint64_t size);
  bool                    array_seed(size_t array, Value base);
  // The same array living IN the state allocation (state ABI only), at word `word`, in its natural
  // layout (array_state_words 64-bit words): reads and writes go straight to the state, so there is
  // no seed and no publish. A write lands at once, program order decides what later reads see.
  [[nodiscard]] static size_t array_state_words(uint32_t bits, uint64_t size);
  [[nodiscard]] size_t    array_create_in_state(uint32_t bits, uint64_t size, size_t word);
  // Zero every entry when `condition` is non-zero.
  // `dirty`: masks ORed in whenever the array is cleared.
  bool                        array_clear_if(size_t array, Value condition, const std::vector<Dirty_mask>& dirty = {});
  // `values[i]` is replaced by `overrides[i]` while `condition` is non-zero, as ONE branch the optimizer is
  // told is unlikely (a reset) with a phi per value: the hot path pays a compare, not a select per register.
  bool                    unlikely_override(Value condition, const std::vector<Value>& values, const std::vector<Value>& overrides,
                                            std::vector<Value>& results);
  // State ABI: while `condition` is non-zero, store each value into its state word(s), on a branch the
  // optimizer is told is unlikely (a reset). The stores happen where this is called, so a state copy
  // made before it (copy_state) is overwritten and nothing is paid when the condition is clear.
  // `dirty`: masks ORed in whenever the condition holds (no compare: a reset is rare).
  bool                        state_store_if(Value condition, const std::vector<std::pair<size_t, Value>>& stores,
                                             const std::vector<Dirty_mask>& dirty = {});
  // `dirty` (an array in the state only): masks ORed in when the write changes the entry.
  bool array_write(size_t array, Value enable, Value address, Value data, const std::vector<Dirty_mask>& dirty = {});
  [[nodiscard]] Value     array_read(size_t array, Value address, uint32_t result_width, bool result_unsign);
  [[nodiscard]] Value     array_read_all(size_t array, uint32_t result_width, bool result_unsign);
  void                    bind_memory(std::string_view symbol, const Memory& memory);
  [[nodiscard]] Value     memory_read(std::string_view symbol, Value address, uint32_t result_width, bool result_unsign);
  [[nodiscard]] Value     memory_read_all(std::string_view symbol, uint32_t result_width, bool result_unsign);
  bool                    memory_apply(std::string_view symbol, Value data);
  bool                    memory_clear(std::string_view symbol);
  bool                    memory_stage_whole(std::string_view symbol, Value enable, Value force, Value data);
  bool                    memory_stage_write(std::string_view symbol, Value enable, Value address, Value data);
  // Phase-barrier entry writes, emitted into a native commit object.
  bool                    memory_commit(std::string_view symbol);
  // sim.warn_undefined: on a cold branch taken when `condition` is non-zero,
  // set the `hit` flag of resource `site` (a __lhd_undef_site*); the caller
  // polls it after the call and reports the first hit of each site.
  bool                    undefined_if(Value condition, size_t site);

  // Values cross the ABI as packed little-endian 64-bit words. `index` is the
  // logical output number; physical word offsets are derived from the exact
  // output widths when the object is finalized.
  bool add_output(size_t index, Value value, std::string& error);

  // Direct storage (packed ABI only): a simulator Slop object reached through
  // resource `resource` of the owner table. Its layout is an int64 type tag
  // (0 = Integer) followed by the value words: a Slop_u<width> (`canonical`)
  // keeps them zero-extended over its width+1-bit carrier, a Slop<width>
  // sign-extended from bit width-1. A storage-bound input loads straight from
  // the object, and a storage output compares and stores there, so neither
  // takes space in the packed buffers (packed_layout runs over the others).
  // `element` indexes an array of such objects at the resource (a color slot
  // array): the object sits element * sizeof(object) bytes past it, sizeof
  // being 8 * (1 + value words). One table entry then serves a whole array.
  struct Storage {
    size_t   resource  = 0;
    uint32_t width     = 0;
    bool     canonical = false;
    size_t   element   = 0;
    // Native state (no tag): the value alone at its natural width -- 1, 2, 4
    // or 8 bytes, or whole words past 64 bits -- zero-extended when
    // `canonical`, else sign-extended from bit width-1.
    bool     native    = false;
    // Bytes past the resource (plus `element`): a field of an instance's
    // native register run, or a slot array inside the runtime's slot block.
    size_t   offset    = 0;
  };
  // Read input `index` from `storage` instead of the packed input buffer.
  // Bind before the input's first use.
  bool bind_storage_input(size_t index, const Storage& storage, std::string& error);
  // Output `index` is stored into `dest` (its words and the Integer tag), and
  // its `changed` bit says whether its words differ from `compare`'s (dest
  // itself when absent). Unlike Slop::identical a non-Integer old tag alone
  // is not a change: the bits a consumer reads did not move. The bit is always produced. An object this
  // kernel also reads through a storage input is loaded before the store, so
  // every read of it sees the old value.
  // `report_changed` false: no compare and no changed bit (the caller has no
  // dirty tracking to feed), just the store.
  // What a changed output does to the simulator's scheduling state, done by
  // the kernel instead of its caller: OR `mask` into the uint64 word at
  // resource `resource`, or (`flag`) set the bool there. Effects on one
  // target accumulate in registers and reach it once, when the kernel ends.
  // `offset`: bytes past the resource (an element of a word or bool array).
  struct Change_effect {
    size_t   resource = 0;
    uint64_t mask     = 0;
    bool     flag     = false;
    size_t   offset   = 0;
  };
  // `guard` (1 bit): publish only while it is set -- no compare, store,
  // changed bit or effect otherwise (a clock-gated register whose gate is
  // closed keeps its value). The caller guarantees the skipped store is a
  // no-op: the object already holds the value its reader expects.
  bool add_storage_output(size_t index, Value value, const Storage& dest, const std::optional<Storage>& compare, std::string& error,
                          bool report_changed = true, const std::vector<Change_effect>& effects = {},
                          std::optional<Value> guard = std::nullopt);
  // Reset-free fast path: `value` (an input, possibly through a widening
  // resize) is a reset net expected to hold `inactive`. The native object then
  // checks every such input on entry: all inactive runs a copy of the kernel
  // with them folded to constants (no reset muxes, no reset-dependent logic);
  // any other value runs the kernel as built. False when `value` is not an
  // input this can specialize (the kernel is unchanged then).
  bool speculate_input(Value value, uint64_t inactive);
  // Whether input `index` is storage-bound (it then has no packed field).
  [[nodiscard]] bool is_storage_input(size_t index) const;
  // Compare against `previous_word` (the current-state bank for a pending
  // register), store into `word`, and OR dirty masks when the value changes.
  bool add_state_output(size_t word, size_t previous_word, Value value, const std::vector<Dirty_mask>& dirty, std::string& error);
  // The same store, performed only while `guard` is non-zero (`when_zero`: only while it is zero): a
  // register that keeps its value when its enable is off is not rewritten, and what only its new
  // value needs can sink under the test. Outputs of one guard share one branch, taken after every
  // unconditional output is stored. No dirty masks: the caller owns change tracking for these.
  // `dirty`: masks ORed in when the store changes the word (compared inside the guarded block).
  bool add_state_output_if(Value guard, bool when_zero, size_t word, Value value, std::string& error,
                           const std::vector<Dirty_mask>& dirty = {});
  // Phase-barrier copy. Source and destination spans must not overlap.
  bool copy_state(size_t destination, size_t source, size_t words, std::string& error);
  // `state[destination + i] |= state[source + i]; state[source + i] = 0`, right here (not at the kernel's
  // end): dirty marks recorded for the next phase become current. The spans must not overlap.
  bool merge_state_words(size_t destination, size_t source, size_t words, std::string& error);

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
    // Optional caller-proven NONNEGATIVE index range [0, 2^index_bits).
    // Zero leaves the general signed/wrapping index representation intact.
    uint32_t                               index_bits = 0;
    std::vector<std::pair<size_t, size_t>> carries;  // input, output indices
    // A STATEFUL body (per-trip resource tables): trip `ordinal` runs the body
    // with resource table `tables[ordinal]` (the lane's own state, slots and
    // runtime flags), so state stays in each lane's storage while the loop
    // ports (index, carries, invariants) stay SSA values. `bindings` then
    // covers only the body's PACKED inputs, in input order: a storage-bound
    // input (a register read) has no loop port.
    bool                                   trip_tables = false;
    // The entry's `outputs`: one word run per listed (packed) body output, in
    // this order. Empty: every packed output, in output order.
    std::vector<size_t>                    entry_outputs;
    // (count, first, step) known at code generation: the entry ignores its
    // runtime domain arguments, so LLVM sees a constant trip count and index.
    std::optional<std::array<int64_t, 3>>  constant_bounds;
  };
  // void entry(const uint64_t* inputs, uint64_t* outputs,
  //            uint64_t count, int64_t first, int64_t step
  //            [, void* const* tables])   -- the last one with trip_tables
  bool add_loop(std::string_view entry, const Loop_layout& layout, std::string& error, bool track_changed = true);

  // A sealed, resource-free color can be embedded in a larger body without
  // introducing a runtime call. Bitcode crosses contexts, never LLVM pointers.
  struct Inline_body {
    std::string                            bitcode;
    std::vector<std::pair<uint32_t, bool>> inputs, outputs;
    bool                                   scalar = false;
  };
  bool inline_body(const Inline_body& body, const std::vector<Value>& inputs, std::vector<Value>& outputs, std::string& error);

  // Emit a rolled loop of a sealed, resource-free `body` INSIDE the current
  // function: the caller's color keeps one function, the body is inlined into
  // the loop once, and operands/results stay SSA values -- no call, no packed
  // operand transport, no separate color. The native code alone is fully
  // unrolled when `count` trips of the body (its own embedded loops counted
  // expanded) fit a small instruction budget: a per-bit loop then folds to
  // constant lane positions; a larger loop stays rolled. `layout` is the one
  // add_loop() takes (one `inputs` entry per declared loop port); `inputs`
  // supplies one Value per layout.inputs entry (the index/activation entries
  // are replaced by the loop itself); `outputs` receives one Value per body
  // output, a carry output being the retained carry, exactly as add_loop
  // publishes it.
  // With `layout.trip_tables`, `tables_resource` is the resource of THIS
  // kernel holding the `void* const*` array of per-trip tables (the inner
  // wrapper's lanes), and `body.inputs` lists only the body's packed inputs.
  // A counted loop spelled directly in the current function (the M3 path's `for`): `loop_begin` opens
  // `for ordinal in 0..count` and returns the index (first + ordinal, `index_width` bits) and one header
  // phi per carry (seeded with `carry_init`); the body is emitted once, touching state in place;
  // `loop_end` closes it with the carries' next values and returns their values after the loop. The
  // loop stays rolled (LLVM is told not to unroll it) unless it has at most 4 trips and no inner loop.
  bool loop_begin(uint64_t count, int64_t first, uint32_t index_width, const std::vector<Value>& carry_init, Value& index,
                  std::vector<Value>& carries);
  bool loop_end(const std::vector<Value>& carry_next, std::vector<Value>& carry_final);
  bool inline_loop(const Inline_body& body, const Loop_layout& layout, uint64_t count, int64_t first, int64_t step,
                   const std::vector<Value>& inputs, std::vector<Value>& outputs, std::string& error,
                   std::optional<size_t> tables_resource = std::nullopt);

  // Finalize the ABI and return exact bitcode with the color and optional loop
  // entry symbols normalized. Equal keys can share code; mutable instance storage is still
  // supplied separately. Do not add operations after requesting this key.
  std::string sharing_key(std::string& error, bool track_changed = true);
  // The finalized module's bitcode alone (no reset-split annotation): what an
  // Inline_body carries. The standalone object keeps its speculation.
  std::string inline_bitcode(std::string& error, bool track_changed = true);

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
