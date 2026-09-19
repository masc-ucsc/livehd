//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Generated-C++ runtime text prp_sim emits VERBATIM into drv.cpp for sim.tune
// (sim_profile.md §6, P0.5 + P1): per-test run metrics and digests in every
// --result-json row, the dormant profile sampler behind the step hook, the raw
// run file, and the generic `--set key=value` parser. Kept here as raw strings
// (not in prp_sim.cpp's stream code) so the emitted C++ reads as C++; drv.cpp is
// its only consumer, which is why this is not an hlop header (no pin bump, no
// sim.hlop_dir skew).
//
// Emission contract (prp_sim.cpp generate()):
//   * kTuneRuntime after `_Fail`, `_json_esc`, `_to_i64`/`_to_u64`, the shared
//     sim_tune_rt.hpp helpers (`__lhd_tune_support`, `__lhd_unknown_zero`), and
//     the generated identity table `_tp_roots[]` + the `_baked_*` constants --
//     and BEFORE the run-functions, which call `_tp`, `_tp_out`, `_tp_var`.
//   * kSetParser right after kTuneRuntime (it also needs `_ckpt`/`_init_zero`).
// Every identifier is `_`-prefixed: a Pyrope testbench name can never be (see
// is_valid_param_name), so nothing here can collide with a test's locals.
//
// NEVER mention the checkpoint fork entry point by name in these texts: a lean
// (sim.checkpoint=false) driver must not contain it anywhere
// (lhd_sim_checkpoint_test.sh greps for its absence).

#include <string_view>

namespace prp_sim {

inline constexpr std::string_view kTuneRuntime = R"cpp(
  // ---- sim.tune: run metrics, digests, the profile sampler ---------------------
  // Always compiled, dormant unless `--set sim.tune.profile=on`: with profiling
  // off the step hook is one decrement-and-branch per DUT step, and the only other
  // work is two counter reads per test and the end-of-test digest walk.
#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/resource.h>
  extern "C" int proc_pid_rusage(int, int, rusage_info_t*);  // libSystem (libproc.h); declared by hand
  extern "C" int sysctlbyname(const char*, void*, std::size_t*, void*, std::size_t);
#elif defined(__linux__)
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#endif

  static constexpr std::uint64_t _tp_fnv0 = 0xcbf29ce484222325ULL;
  static inline std::uint64_t    _tp_fnv(std::uint64_t _h, const void* _p, std::size_t _n) {
    const auto* _b = static_cast<const unsigned char*>(_p);
    for (std::size_t _i = 0; _i < _n; ++_i) {
      _h ^= _b[_i];
      _h *= 0x100000001b3ULL;
    }
    return _h;
  }
  static inline std::uint64_t _tp_fnv_u64(std::uint64_t _h, std::uint64_t _v) { return _tp_fnv(_h, &_v, sizeof _v); }
  // A testbench local's final value: `name=<pyrope>\n`, so two locals cannot trade
  // values without changing the digest.
  static inline std::uint64_t _tp_fnv_local(std::uint64_t _h, const char* _name, const std::string& _v) {
    _h = _tp_fnv(_h, _name, std::strlen(_name));
    _h = _tp_fnv(_h, "=", 1);
    _h = _tp_fnv(_h, _v.data(), _v.size());
    return _tp_fnv(_h, "\n", 1);
  }
  static inline std::uint64_t _tp_now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
  }

  // One CPU-counter snapshot. cpu_ns is always CLOCK_PROCESS_CPUTIME_ID (rusage's
  // ri_user_time is mach ticks on arm64, not ns).
  struct _TpCnt {
    std::uint64_t ins = 0, cyc = 0, pcyc = 0, cpu_ns = 0, wall_ns = 0;
  };
  struct _TpReader {
    enum class Src { none, rusage_v6, rusage_v4, perf, cputime };
    Src         src = Src::none;
    int         fd  = -1;
    const char* name() const {
      switch (src) {
        case Src::rusage_v6: return "rusage_v6";
        case Src::rusage_v4: return "rusage_v4";
        case Src::perf     : return "perf";
        case Src::cputime  : return "cputime";
        default            : return "none";
      }
    }
    // Lazy (the first test's begin), so a --list-tests / --help run opens nothing.
    void init() {
      if (src != Src::none) {
        return;
      }
#if defined(__APPLE__)
#ifdef RUSAGE_INFO_V6
      {
        rusage_info_v6 _r{};
        if (proc_pid_rusage(getpid(), RUSAGE_INFO_V6, reinterpret_cast<rusage_info_t*>(&_r)) == 0) {
          src = Src::rusage_v6;
          return;
        }
      }
#endif
      {
        rusage_info_v4 _r{};
        if (proc_pid_rusage(getpid(), RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t*>(&_r)) == 0) {
          src = Src::rusage_v4;
          return;
        }
      }
#elif defined(__linux__)
      // User-space instructions + cycles as ONE group (read atomically). inherit=0:
      // a forked checkpoint child is never counted. EACCES (perf_event_paranoid) or
      // ENOENT (a VM without a PMU) falls back to CPU time only.
      perf_event_attr _a{};
      _a.type           = PERF_TYPE_HARDWARE;
      _a.size           = sizeof _a;
      _a.config         = PERF_COUNT_HW_INSTRUCTIONS;
      _a.disabled       = 1;
      _a.exclude_kernel = 1;
      _a.exclude_hv     = 1;
      _a.read_format    = PERF_FORMAT_GROUP;
      fd                = static_cast<int>(syscall(SYS_perf_event_open, &_a, 0, -1, -1, 0));
      if (fd >= 0) {
        _a.config   = PERF_COUNT_HW_CPU_CYCLES;
        _a.disabled = 0;
        if (syscall(SYS_perf_event_open, &_a, 0, -1, fd, 0) >= 0) {
          ioctl(fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
          src = Src::perf;
          return;
        }
        close(fd);
        fd = -1;
      }
#endif
      src = Src::cputime;
    }
    _TpCnt read() const {
      _TpCnt   _c;
      timespec _ts{};
      clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &_ts);
      _c.cpu_ns  = static_cast<std::uint64_t>(_ts.tv_sec) * 1000000000ULL + static_cast<std::uint64_t>(_ts.tv_nsec);
      _c.wall_ns = _tp_now_ns();
#if defined(__APPLE__)
#ifdef RUSAGE_INFO_V6
      if (src == Src::rusage_v6) {
        rusage_info_v6 _r{};
        if (proc_pid_rusage(getpid(), RUSAGE_INFO_V6, reinterpret_cast<rusage_info_t*>(&_r)) == 0) {
          _c.ins  = _r.ri_instructions;
          _c.cyc  = _r.ri_cycles;
          _c.pcyc = _r.ri_pcycles;  // P-core share: pcore_frac = pcyc / cyc
        }
        return _c;
      }
#endif
      if (src == Src::rusage_v4) {
        rusage_info_v4 _r{};
        if (proc_pid_rusage(getpid(), RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t*>(&_r)) == 0) {
          _c.ins  = _r.ri_instructions;
          _c.cyc  = _r.ri_cycles;
          _c.pcyc = _r.ri_cycles;  // no core-class split before V6
        }
      }
#elif defined(__linux__)
      if (fd >= 0) {
        struct {
          std::uint64_t nr, v[2];
        } _g{};
        if (::read(fd, &_g, sizeof _g) == static_cast<ssize_t>(sizeof _g)) {
          _c.ins  = _g.v[0];
          _c.cyc  = _g.v[1];
          _c.pcyc = _g.v[1];
        }
      }
#endif
      return _c;
    }
  };

  // One DUT instance of a test, type-erased so the cold sampler is ONE function
  // for every test. `wcount`/`whash` are the per-def walkers (always present in a
  // current cgen; absent -> the test still runs, its end_digest covers only the
  // testbench frame); `sup`/`shash` are a color root's support table + hasher.
  struct _TpVar {
    const char* var                             = "";
    const char* cls                             = "";
    const void* dut                             = nullptr;
    std::size_t (*wcount)(const void*)          = nullptr;
    void (*whash)(const void*, std::uint64_t*&) = nullptr;
    const __lhd_tune_support& (*sup)()          = nullptr;
    void (*shash)(const void*, std::uint64_t*)  = nullptr;
  };
  struct _TpDut {
    const _TpVar* v = nullptr;
    std::size_t   n = 0;
  };
  // C++ requires-expressions, so a driver still compiles against a DUT class that
  // lacks either API (an executable root has both; nothing else is a test DUT).
  template <class _D>
  static _TpVar _tp_var(const char* _var, const char* _cls, const _D& _d) {
    _TpVar _v;
    _v.var = _var;
    _v.cls = _cls;
    _v.dut = &_d;
    if constexpr (requires(const _D& _x, std::uint64_t*& _p) {
                    _x.__tune_count(true);
                    _x.__tune_hash(_p, true);
                  }) {
      _v.wcount = [](const void* _p) -> std::size_t { return static_cast<const _D*>(_p)->__tune_count(true); };
      _v.whash  = [](const void* _p, std::uint64_t*& _to) { static_cast<const _D*>(_p)->__tune_hash(_to, true); };
    }
    if constexpr (requires(const _D& _x, std::uint64_t* _p) {
                    _D::__tune_support();
                    _x.__tune_sources(_p);
                  }) {
      _v.sup   = []() -> const __lhd_tune_support& { return _D::__tune_support(); };
      _v.shash = [](const void* _p, std::uint64_t* _to) { static_cast<const _D*>(_p)->__tune_sources(_to); };
    }
    return _v;
  }
  // Walk one instance's root state (+ its root inputs) into exactly n words. A
  // walker that disagrees with its own count is a codegen bug: fewer words ->
  // false (the caller stops sampling rather than report wrong stats); MORE words
  // already wrote past the buffer, so stop the process.
  static bool _tp_walk(const _TpVar& _v, std::uint64_t* _p, std::size_t _n) {
    std::uint64_t* _end = _p;
    _v.whash(_v.dut, _end);
    if (_end == _p + _n) {
      return true;
    }
    std::fprintf(stderr,
                 "lhd sim: internal error: %s.__tune_hash wrote %lld words, __tune_count said %zu\n",
                 _v.var,
                 static_cast<long long>(_end - _p),
                 _n);
    if (_end > _p + _n) {
      std::abort();
    }
    return false;
  }

  // One occurrence (instance of a def under a root) with >= 1 support source.
  // Occurrences with no source are never listed: they can never be active, and
  // averaging them in as "idle" would inflate the idleness estimate.
  struct _TpOcc {
    std::string   var, path, def;
    std::uint64_t ge = 0, cost = 0;
  };
  // Profile accumulators, one per log2(cycle) bucket of the pair's first cycle, so
  // the warm-up cut (which depends on the run length, unknown up front) is
  // applied at test end by dropping whole buckets. Idleness is kept under every
  // class weight the support table publishes (ge, sites, cost, cost_flat; see
  // __lhd_tune_support): the tuner picks one.
  struct _TpAcc {
    std::uint64_t              pairs = 0, quiescent = 0, idle_ge = 0, idle_words = 0;
    std::uint64_t              idle_sites = 0, idle_cost = 0, idle_cost_flat = 0;
    std::vector<std::uint64_t> occ_active;  // parallel to _TpTest::occs
    std::vector<std::uint64_t> class_idle;  // idle pairs per class, parallel to the _TpTest::dumps classes
  };
  // One support-profiled DUT var's slice of _TpAcc::class_idle (the raw file's
  // per-class calibration dump).
  struct _TpDump {
    std::string var, module;
    std::size_t base = 0, n = 0;
  };
  struct _TpTest {
    std::string          name;
    bool                 active = false, first = false, ended = false, profiled = false;
    long                 f          = 0;
    std::uint64_t        sim_cycles = 0, rng0 = 0, rng_draws = 0, ckpt_taken = 0;
    std::uint64_t        out_h = _tp_fnv0, end_h = _tp_fnv0;
    _TpCnt               c0, c1, c2;  // test begin, after the first step, body end
    // ---- profile (valid iff profiled) ----
    bool                 has_sup = false, has_walker = false, exact = true;
    std::uint64_t        sup_total_ge = 0, walker_words = 0;
    std::uint64_t        sup_total_sites = 0, sup_total_cost = 0, sup_total_cost_flat = 0;
    std::uint64_t        stride = 0, sample_ns = 0, warm_cycles = 0;
    std::vector<_TpOcc>  occs;
    std::vector<_TpDump> dumps;            // support-profiled vars, in _TpDut order
    std::size_t          class_total = 0;  // sum of their class counts
    _TpAcc               acc;              // the post-warm-up total (filled at body end)
  };

  struct _TuneProf {
    bool                 on           = false;  // --set sim.tune.profile=on
    std::uint64_t        stride_fixed = 0;      // --set sim.tune.profile_stride=N (TEST-ONLY: no calibration, no jitter)
    std::string          dir;                   // --set sim.tune.profile_dir=DIR (default <dir of drv.bin>/tune_runs)
    _TpReader            rd;
    _TpTest              cur;
    std::vector<_TpTest> done;
    std::vector<std::pair<std::string, std::string>> roots;  // (var, class) of every DUT that ran, first-seen order

    // ---- step countdown (the current test) ----
    // The run-function's hook is `if (--_tp_left == 0) [[unlikely]] _tp_left =
    // _tp.hit(_tp_d);`: ONE local, so it stays in a register (a count + a target
    // spilled the target on a real tick loop). `at` is the DUT cycle of the last
    // hit and `cd` the countdown it handed out, so the cycle count is always
    // at + (cd - _tp_left).
    std::uint64_t at = 0, cd = 1;

    // ---- sampler state (the current test) ----
    struct _VarRt {
      std::size_t               off = 0, n = 0;
      const __lhd_tune_support* sup      = nullptr;  // support path iff non-null
      std::size_t               occ_base = 0;        // into occ_map
      std::size_t               cls_base = 0;        // into _TpAcc::class_idle
    };
    std::vector<_VarRt>        vrt;
    std::vector<std::uint32_t> occ_map;  // (occ_base + table occ) -> index into cur.occs, or UINT32_MAX
    std::vector<std::uint64_t> a, b, mask;
    std::vector<unsigned char> occ_flag;
    std::vector<_TpAcc>        bucket;  // [64], by log2(pair cycle)
    bool                       pend = false, bad = false;
    std::uint64_t              pair_t = 0, last_ns = 0, last_cyc = 0, cost_ns = 0;
    double                     ns_per_cyc = 0, cost_avg = 0;
    std::uint64_t              xs = 0;  // PRIVATE xorshift64 jitter: never hlop's PRNG (checkpoints record its draws)

    void begin_test(const char* _name) {
      rd.init();
      cur        = _TpTest{};
      cur.name   = _name;
      cur.active = true;
      cur.rng0   = hlop_random_draws();
      at         = 0;
      cd         = 1;
      vrt.clear();
      occ_map.clear();
      bucket.clear();
      pend = bad = false;
      ns_per_cyc = cost_avg = 0;
      xs                    = 0x9E3779B97F4A7C15ULL;
      cur.c0                = rd.read();
    }
    void out(const char* _p, std::size_t _n) {
      if (cur.active) {
        cur.out_h = _tp_fnv(cur.out_h, _p, _n);
      }
    }
    void ckpt() {
      if (cur.active) {
        ++cur.ckpt_taken;
      }
    }

    // Size the pair buffers and index the support tables for the current test.
    // A table that breaks its own invariants is a codegen bug: that instance is
    // profiled through its walker instead (and says so) rather than read wrong.
    void prepare(const _TpDut& _d) {
      std::size_t _off = 0, _mw = 0;
      for (std::size_t _i = 0; _i < _d.n; ++_i) {
        const _TpVar& _v = _d.v[_i];
        _VarRt        _r;
        _r.off                       = _off;
        const __lhd_tune_support* _s = _v.sup != nullptr ? &_v.sup() : nullptr;
        if (_s != nullptr && _s->sources > 0) {
          bool _ok = _s->words > 0
                     && (_s->classes == 0
                         || (_s->class_bits != nullptr && _s->class_ge != nullptr && _s->class_sites != nullptr
                             && _s->class_cost != nullptr && _s->class_cost_flat != nullptr));
          for (std::uint32_t _k = 0; _ok && _k < _s->sources; ++_k) {
            _ok = _s->source_bucket[_k] < 64u * _s->words && _s->source_occ[_k] <= _s->occs;
          }
          if (!_ok) {
            std::fprintf(stderr, "lhd sim: warning: %s: malformed sim.tune support table; profiling it by its state walk\n", _v.var);
            _s = nullptr;
          }
        } else {
          _s = nullptr;
        }
        if (_s != nullptr) {
          _r.sup      = _s;
          _r.n        = _s->sources;
          _r.occ_base = occ_map.size();
          std::vector<std::uint32_t> _cnt(_s->occs, 0);
          for (std::uint32_t _k = 0; _k < _s->sources; ++_k) {
            if (_s->source_occ[_k] < _s->occs) {
              ++_cnt[_s->source_occ[_k]];
            }
          }
          for (std::uint32_t _o = 0; _o < _s->occs; ++_o) {
            if (_cnt[_o] == 0) {
              occ_map.push_back(UINT32_MAX);
              continue;
            }
            occ_map.push_back(static_cast<std::uint32_t>(cur.occs.size()));
            const char* _path = _s->occ_path != nullptr && _s->occ_path[_o] != nullptr ? _s->occ_path[_o] : "";
            const char* _def  = _s->occ_def != nullptr && _s->occ_def[_o] != nullptr ? _s->occ_def[_o] : "";
            cur.occs.push_back(_TpOcc{_v.var,
                                      _path,
                                      _def,
                                      _s->occ_ge != nullptr ? _s->occ_ge[_o] : 0,
                                      _s->occ_cost != nullptr ? _s->occ_cost[_o] : 0});
          }
          _r.cls_base = cur.class_total;
          cur.dumps.push_back(_TpDump{_v.var, _v.cls, cur.class_total, _s->classes});
          cur.class_total         += _s->classes;
          cur.has_sup              = true;
          cur.exact                = cur.exact && _s->exact;
          cur.sup_total_ge        += _s->total_ge;
          cur.sup_total_sites     += _s->total_sites;
          cur.sup_total_cost      += _s->total_cost;
          cur.sup_total_cost_flat += _s->total_cost_flat;
          _mw                      = _s->words > _mw ? _s->words : _mw;
        } else if (_v.wcount != nullptr) {
          _r.n              = _v.wcount(_v.dut);
          cur.has_walker    = true;
          cur.walker_words += _r.n;
        }
        _off += _r.n;
        vrt.push_back(_r);
      }
      a.assign(_off, 0);
      b.assign(_off, 0);
      mask.assign(_mw, 0);
      occ_flag.assign(cur.occs.size(), 0);
      bucket.assign(64, _TpAcc{});
      cur.profiled = true;
    }
    bool hash(const _TpDut& _d, std::vector<std::uint64_t>& _buf) {
      for (std::size_t _i = 0; _i < vrt.size(); ++_i) {
        std::uint64_t* _p = _buf.data() + vrt[_i].off;
        if (vrt[_i].sup != nullptr) {
          _d.v[_i].shash(_d.v[_i].dut, _p);
        } else if (vrt[_i].n != 0 && !_tp_walk(_d.v[_i], _p, vrt[_i].n)) {
          return false;
        }
      }
      return true;
    }
    // One consecutive-cycle pair (a = after step t, b = after step t+1).
    void accumulate(std::uint64_t _t) {
      const int _bk = 63 - __builtin_clzll(_t);
      _TpAcc&   _B  = bucket[static_cast<std::size_t>(_bk)];
      if (_B.occ_active.size() < cur.occs.size()) {
        _B.occ_active.resize(cur.occs.size(), 0);
      }
      if (_B.class_idle.size() < cur.class_total) {
        _B.class_idle.resize(cur.class_total, 0);
      }
      ++_B.pairs;
      bool _any = false;
      for (const auto& _r : vrt) {
        const std::uint64_t* _pa = a.data() + _r.off;
        const std::uint64_t* _pb = b.data() + _r.off;
        if (_r.sup != nullptr) {
          const __lhd_tune_support& _s = *_r.sup;
          std::fill(mask.begin(), mask.begin() + _s.words, 0);
          for (std::size_t _k = 0; _k < _r.n; ++_k) {
            if (_pa[_k] == _pb[_k]) {
              continue;
            }
            _any                      = true;
            const std::uint32_t _bit  = _s.source_bucket[_k];
            mask[_bit >> 6]          |= std::uint64_t{1} << (_bit & 63);
            if (_s.source_occ[_k] < _s.occs) {
              occ_flag[occ_map[_r.occ_base + _s.source_occ[_k]]] = 1;
            }
          }
          for (std::uint32_t _c = 0; _c < _s.classes; ++_c) {
            const std::uint64_t* _cb   = _s.class_bits + static_cast<std::size_t>(_c) * _s.words;
            bool                 _idle = true;
            for (std::uint32_t _w = 0; _idle && _w < _s.words; ++_w) {
              _idle = (_cb[_w] & mask[_w]) == 0;
            }
            if (_idle) {
              _B.idle_ge        += _s.class_ge[_c];
              _B.idle_sites     += _s.class_sites[_c];
              _B.idle_cost      += _s.class_cost[_c];
              _B.idle_cost_flat += _s.class_cost_flat[_c];
              ++_B.class_idle[_r.cls_base + _c];
            }
          }
        } else {
          for (std::size_t _k = 0; _k < _r.n; ++_k) {
            if (_pa[_k] == _pb[_k]) {
              ++_B.idle_words;
            } else {
              _any = true;
            }
          }
        }
      }
      if (!_any) {
        ++_B.quiescent;
      }
      for (std::size_t _o = 0; _o < occ_flag.size(); ++_o) {
        if (occ_flag[_o] != 0) {
          ++_B.occ_active[_o];
          occ_flag[_o] = 0;
        }
      }
    }
    // Next pair distance: <= 1% overhead (pair cost / (0.01 * ns per cycle)),
    // clamped to [64, 2^20], +-25% private jitter so a periodic design is not
    // sampled in phase. A fixed test stride skips all of that.
    std::uint64_t next_stride() {
      if (stride_fixed != 0) {
        cur.stride = stride_fixed;
        return stride_fixed;
      }
      constexpr double _lo = 64, _hi = 1048576;
      double           _s  = ns_per_cyc > 0 ? cost_avg / (0.01 * ns_per_cyc) : _hi;
      _s                   = _s < _lo ? _lo : (_s > _hi ? _hi : _s);
      cur.stride           = static_cast<std::uint64_t>(_s);
      xs                  ^= xs << 13;
      xs                  ^= xs >> 7;
      xs                  ^= xs << 17;
      const double _j      = 0.75 + 0.5 * static_cast<double>(xs >> 11) / 9007199254740992.0;
      double       _n      = _s * _j;
      _n                   = _n < _lo ? _lo : (_n > _hi ? _hi : _n);
      return static_cast<std::uint64_t>(_n);
    }

    // The step hook's cold path: `_tp_left = _tp.hit(_tp_d)` when the countdown
    // runs out. Returns the next countdown (UINT64_MAX = never again).
    [[gnu::cold, gnu::noinline]] std::uint64_t hit(const _TpDut& _d) {
      if (!cur.active) {
        return UINT64_MAX;  // accounting frozen (the --vcd-on-fail re-run)
      }
      at                        += cd;
      const std::uint64_t _next  = sample(at, _d);
      cd                         = _next == UINT64_MAX ? UINT64_MAX : _next - at;
      return cd;
    }
    // `_cyc` DUT cycles have run; returns the ABSOLUTE cycle to call back at
    // (always > _cyc; UINT64_MAX = never).
    std::uint64_t sample(std::uint64_t _cyc, const _TpDut& _d) {
      if (!cur.first) {
        cur.first = true;
        cur.c1    = rd.read();  // sim_ns / counters start AFTER the first step (excludes construction + reset)
        if (!on) {
          return UINT64_MAX;
        }
        prepare(_d);
        last_ns  = cur.c1.wall_ns;
        last_cyc = _cyc;
        return _cyc < 2048 ? 2048 : _cyc + 1;  // never sample the first 2048 cycles
      }
      if (bad) {
        return UINT64_MAX;
      }
      const std::uint64_t _t0 = _tp_now_ns();
      if (!pend) {
        // ns per simulated cycle since the previous pair ended, testbench included
        if (_cyc > last_cyc) {
          const double _npc = static_cast<double>(_t0 - last_ns) / static_cast<double>(_cyc - last_cyc);
          ns_per_cyc        = ns_per_cyc <= 0 ? _npc : 0.75 * ns_per_cyc + 0.25 * _npc;
        }
        if (!hash(_d, a)) {
          bad = true;
          return UINT64_MAX;
        }
        pend                     = true;
        pair_t                   = _cyc;
        const std::uint64_t _t1  = _tp_now_ns();
        cost_ns                  = _t1 - _t0;
        cur.sample_ns           += _t1 - _t0;
        return _cyc + 1;
      }
      pend = false;
      if (!hash(_d, b)) {
        bad = true;
        return UINT64_MAX;
      }
      accumulate(pair_t);
      const std::uint64_t _t1  = _tp_now_ns();
      cost_ns                 += _t1 - _t0;
      cur.sample_ns           += _t1 - _t0;
      cost_avg = cost_avg <= 0 ? static_cast<double>(cost_ns) : 0.75 * cost_avg + 0.25 * static_cast<double>(cost_ns);
      last_ns  = _t1;
      last_cyc = _cyc;
      return _cyc + next_stride();
    }

    // End of the test body (inside the run-function, while the DUT still lives):
    // stop the clock FIRST (the digest walk is not timed), then fold the end digest
    // and close the profile. `_left` is the run-function's countdown.
    void body_end(std::uint64_t _left, const _TpDut& _d, std::uint64_t _tb_h) {
      if (!cur.active) {
        return;
      }
      cur.c2                   = rd.read();
      const std::uint64_t _cyc = at + (cd - _left);
      if (!cur.first) {  // the body never stepped: an empty sim window
        cur.first = true;
        cur.c1    = cur.c2;
      }
      cur.ended                     = true;
      cur.sim_cycles                = _cyc;
      cur.rng_draws                 = hlop_random_draws() - cur.rng0;
      std::uint64_t              _h = _tp_fnv0;
      std::vector<std::uint64_t> _w;
      for (std::size_t _i = 0; _i < _d.n; ++_i) {
        const _TpVar& _v    = _d.v[_i];
        bool          _seen = false;
        for (const auto& _r : roots) {
          _seen = _seen || (_r.first == _v.var && _r.second == _v.cls);
        }
        if (!_seen) {
          roots.emplace_back(_v.var, _v.cls);
        }
        if (_v.wcount == nullptr) {
          continue;
        }
        _w.assign(_v.wcount(_v.dut), 0);
        (void)_tp_walk(_v, _w.data(), _w.size());
        _h = _tp_fnv(_h, _w.data(), _w.size() * sizeof(std::uint64_t));
      }
      cur.end_h = _tp_fnv_u64(_h, _tb_h);
      if (cur.profiled) {
        // Warm-up: drop every bucket that starts below max(2048, 1% of the run).
        const std::uint64_t _cut = _cyc / 100 > 2048 ? _cyc / 100 : 2048;
        std::uint64_t       _wb  = 2048;
        while (_wb < _cut) {
          _wb <<= 1;
        }
        cur.warm_cycles = _wb < _cyc ? _wb : _cyc;
        cur.acc         = _TpAcc{};
        cur.acc.occ_active.assign(cur.occs.size(), 0);
        cur.acc.class_idle.assign(cur.class_total, 0);
        for (int _k = 0; _k < 64; ++_k) {
          if ((std::uint64_t{1} << _k) < _cut) {
            continue;
          }
          const _TpAcc& _B        = bucket[static_cast<std::size_t>(_k)];
          cur.acc.pairs          += _B.pairs;
          cur.acc.quiescent      += _B.quiescent;
          cur.acc.idle_ge        += _B.idle_ge;
          cur.acc.idle_words     += _B.idle_words;
          cur.acc.idle_sites     += _B.idle_sites;
          cur.acc.idle_cost      += _B.idle_cost;
          cur.acc.idle_cost_flat += _B.idle_cost_flat;
          for (std::size_t _o = 0; _o < _B.occ_active.size(); ++_o) {
            cur.acc.occ_active[_o] += _B.occ_active[_o];
          }
          for (std::size_t _c = 0; _c < _B.class_idle.size(); ++_c) {
            cur.acc.class_idle[_c] += _B.class_idle[_c];
          }
        }
        bucket.clear();
        a.clear();
        b.clear();
      }
    }
    // Right after the run-function returns and BEFORE anything else prints or
    // re-runs it: the verdict joins out_digest and the accounting freezes, so the
    // --vcd-on-fail re-run (same run-function, stdout to /dev/null) is invisible.
    void finish_test(long _f, const _Fail& _ff) {
      if (!cur.active) {
        return;
      }
      cur.out_h  = _tp_fnv_u64(cur.out_h, static_cast<std::uint64_t>(_f));
      cur.out_h  = _tp_fnv_u64(cur.out_h, _ff.has ? 1 : 0);
      cur.out_h  = _tp_fnv_u64(cur.out_h, static_cast<std::uint64_t>(_ff.cycle));
      cur.f      = _f;
      cur.active = false;
      done.push_back(std::move(cur));
      cur = _TpTest{};
    }

    static std::string hex16(std::uint64_t _v) {
      char _b[20];
      std::snprintf(_b, sizeof _b, "%016llx", static_cast<unsigned long long>(_v));
      return _b;
    }
    // The per-test metric members appended to a --result-json row (leading comma;
    // empty for a test whose body never ran). sim_ns and the counter deltas cover
    // [after the first step, body end] = sim_cycles - 1 steps, so the simulation
    // rate is (sim_cycles - 1) / sim_ns; init_ns is construction + reset + step 1.
    // `_raw` (the raw run file only, never sim_tests.json) adds the per-class
    // calibration dump `profile.class_dump`.
    std::string row_json(const _TpTest& _t, bool _raw = false) const {
      if (!_t.ended) {
        return {};
      }
      const auto          _d   = [](std::uint64_t _x, std::uint64_t _y) { return std::to_string(_y > _x ? _y - _x : 0); };
      const std::uint64_t _cyc = _t.c2.cyc > _t.c1.cyc ? _t.c2.cyc - _t.c1.cyc : 0;
      const std::uint64_t _pc  = _t.c2.pcyc > _t.c1.pcyc ? _t.c2.pcyc - _t.c1.pcyc : 0;
      char                _pf[32];
      std::snprintf(_pf, sizeof _pf, "%.4f", _cyc > 0 ? static_cast<double>(_pc) / static_cast<double>(_cyc) : 0.0);
      std::string _j  = ",\"sim_cycles\":" + std::to_string(_t.sim_cycles);
      _j             += ",\"init_ns\":" + _d(_t.c0.wall_ns, _t.c1.wall_ns);
      _j             += ",\"sim_ns\":" + _d(_t.c1.wall_ns, _t.c2.wall_ns);
      _j             += ",\"cpu_ns\":" + _d(_t.c1.cpu_ns, _t.c2.cpu_ns);
      _j             += ",\"cpu_cycles\":" + std::to_string(_cyc);
      _j             += ",\"instructions\":" + _d(_t.c1.ins, _t.c2.ins);
      _j             += ",\"pcore_frac\":" + std::string(_pf);
      _j             += ",\"counters\":\"" + std::string(rd.name()) + "\"";
      _j             += ",\"rng_draws\":" + std::to_string(_t.rng_draws);
      _j             += ",\"ckpt_taken\":" + std::to_string(_t.ckpt_taken);
      _j             += ",\"end_digest\":\"" + hex16(_t.end_h) + "\"";
      _j             += ",\"out_digest\":\"" + hex16(_t.out_h) + "\"";
      if (on) {
        const _TpAcc& _A  = _t.acc;
        _j               += ",\"profile\":{\"pairs\":" + std::to_string(_A.pairs);
        _j               += ",\"quiescent_pairs\":" + std::to_string(_A.quiescent);
        _j               += ",\"stride\":" + std::to_string(_t.stride);
        _j               += ",\"sample_ns\":" + std::to_string(_t.sample_ns);
        _j               += ",\"warm_cycles\":" + std::to_string(_t.warm_cycles);
        _j               += ",\"support\":";
        if (_t.has_sup) {
          _j += "{\"total_ge\":" + std::to_string(_t.sup_total_ge) + ",\"idle_ge\":" + std::to_string(_A.idle_ge)
                + ",\"exact\":" + (_t.exact ? "true" : "false") + "}";
        } else {
          _j += "null";
        }
        // The same idleness under each class weight: totals summed over the DUT
        // roots with a support table, idle summed over the pairs.
        _j += ",\"weights\":";
        if (_t.has_sup) {
          const auto _w = [](const char* _k, std::uint64_t _tot, std::uint64_t _idle) {
            return "\"" + std::string(_k) + "\":{\"total\":" + std::to_string(_tot) + ",\"idle\":" + std::to_string(_idle) + "}";
          };
          _j += "{" + _w("ge", _t.sup_total_ge, _A.idle_ge) + "," + _w("sites", _t.sup_total_sites, _A.idle_sites) + ","
                + _w("cost", _t.sup_total_cost, _A.idle_cost) + "," + _w("cost_flat", _t.sup_total_cost_flat, _A.idle_cost_flat)
                + "}";
        } else {
          _j += "null";
        }
        _j += ",\"walker\":";
        if (_t.has_walker) {
          _j += "{\"words\":" + std::to_string(_t.walker_words) + ",\"idle_words\":" + std::to_string(_A.idle_words) + "}";
        } else {
          _j += "null";
        }
        _j += ",\"occ\":[";
        for (std::size_t _o = 0; _o < _t.occs.size(); ++_o) {
          const auto& _oc  = _t.occs[_o];
          _j              += _o == 0 ? "{" : ",{";
          _j += "\"var\":\"" + _json_esc(_oc.var) + "\",\"path\":\"" + _json_esc(_oc.path) + "\",\"def\":\"" + _json_esc(_oc.def)
                + "\",\"ge\":" + std::to_string(_oc.ge) + ",\"cost\":" + std::to_string(_oc.cost)
                + ",\"active_pairs\":" + std::to_string(_o < _A.occ_active.size() ? _A.occ_active[_o] : 0) + "}";
        }
        _j += "]";
        if (_raw) {
          _j += ",\"class_dump\":[";
          for (std::size_t _i = 0; _i < _t.dumps.size(); ++_i) {
            const _TpDump& _dm  = _t.dumps[_i];
            _j                 += _i == 0 ? "{" : ",{";
            _j += "\"var\":\"" + _json_esc(_dm.var) + "\",\"module\":\"" + _json_esc(_dm.module) + "\",\"idle_pairs\":[";
            for (std::size_t _c = 0; _c < _dm.n; ++_c) {
              const std::size_t _x  = _dm.base + _c;
              _j                   += (_c == 0 ? "" : ",") + std::to_string(_x < _A.class_idle.size() ? _A.class_idle[_x] : 0);
            }
            _j += "]}";
          }
          _j += "]";
        }
        _j += "}";
      }
      return _j;
    }

    // The raw run file (profiling runs only), written at the END of main -- never
    // from atexit or a destructor (a forked checkpoint child _exit()s, and must
    // never write one). tmp + rename, so an ingest never sees a partial file.
    void write_raw(const char* _argv0, unsigned long long _seed, const std::map<std::string, std::string>& _args,
                   const std::vector<std::string>& _selected) const;
  };
  static _TuneProf _tp;

  // The one stdout path of a test BODY (puts/print/located asserts): the same
  // bytes std::printf would write, also folded into the test's out_digest.
  [[gnu::format(printf, 1, 2)]] static int _tp_out(const char* _fmt, ...) {
    char    _sb[512];
    va_list _ap;
    va_start(_ap, _fmt);
    const int _n = std::vsnprintf(_sb, sizeof _sb, _fmt, _ap);
    va_end(_ap);
    if (_n < 0) {
      return _n;
    }
    if (static_cast<std::size_t>(_n) < sizeof _sb) {
      std::fwrite(_sb, 1, static_cast<std::size_t>(_n), stdout);
      _tp.out(_sb, static_cast<std::size_t>(_n));
      return _n;
    }
    std::string _big(static_cast<std::size_t>(_n) + 1, '\0');
    va_start(_ap, _fmt);
    std::vsnprintf(_big.data(), _big.size(), _fmt, _ap);
    va_end(_ap);
    std::fwrite(_big.data(), 1, static_cast<std::size_t>(_n), stdout);
    _tp.out(_big.data(), static_cast<std::size_t>(_n));
    return _n;
  }

  static const _TpRoot* _tp_root(const std::string& _cls) {
    for (const _TpRoot* _r = _tp_roots; _r->cls != nullptr; ++_r) {
      if (_cls == _r->cls) {
        return _r;
      }
    }
    return nullptr;
  }
  static std::string _tp_json_str_or_null(const char* _s) {
    return _s == nullptr ? std::string("null") : "\"" + _json_esc(_s) + "\"";
  }
  static std::string _tp_host_cpu() {
#if defined(__APPLE__)
    char        _b[256] = {0};
    std::size_t _n      = sizeof _b - 1;
    if (sysctlbyname("machdep.cpu.brand_string", _b, &_n, nullptr, 0) == 0) {
      return _b;
    }
#elif defined(__linux__)
    std::ifstream _f("/proc/cpuinfo");
    std::string   _l;
    while (std::getline(_f, _l)) {
      if (_l.compare(0, 10, "model name") == 0) {
        const auto _c = _l.find(':');
        const auto _b = _c == std::string::npos ? std::string::npos : _l.find_first_not_of(" \t", _c + 1);
        return _b == std::string::npos ? std::string() : _l.substr(_b);
      }
    }
#endif
    return {};
  }
  void _TuneProf::write_raw(const char* _argv0, unsigned long long _seed, const std::map<std::string, std::string>& _args,
                            const std::vector<std::string>& _selected) const {
    std::string _dir = dir;
    if (_dir.empty()) {
      const std::string _a0 = _argv0;
      const auto        _sl = _a0.rfind('/');
      _dir                  = (_sl == std::string::npos ? std::string(".") : _a0.substr(0, _sl)) + "/tune_runs";
    }
    hlop::ckpt::make_dirs(_dir);
    const bool  _zero = _baked_unknown_zero || __lhd_unknown_zero;
    std::string _j    = "{\"schema\":\"lhd-sim-tune-raw-1\",\n \"roots\":[";
    for (std::size_t _i = 0; _i < roots.size(); ++_i) {
      const _TpRoot* _r  = _tp_root(roots[_i].second);
      _j                += _i == 0 ? "{" : ",{";
      _j                += "\"var\":\"" + _json_esc(roots[_i].first) + "\",\"module\":\"" + _json_esc(roots[_i].second) + "\"";
      _j                += ",\"vector\":" + _tp_json_str_or_null(_r != nullptr ? _r->vector() : nullptr);
      _j                += ",\"structure\":" + _tp_json_str_or_null(_r != nullptr ? _r->structure() : nullptr);
      _j                += ",\"codegen\":" + _tp_json_str_or_null(_r != nullptr ? _r->codegen() : nullptr) + "}";
    }
    _j += "],\n \"seed\":\"" + std::to_string(_seed) + "\",\"init_zero\":" + (_init_zero ? "true" : "false");
    _j += ",\"fill\":\"" + std::string(_zero ? "zero" : "random") + "\"";
    _j += ",\"baked_unknown_zero\":" + std::string(_baked_unknown_zero ? "true" : "false");
    _j += ",\"unknown_draws\":" + std::to_string(__lhd_unknown_draws);
    _j += ",\"tb_unknown_literals\":" + std::to_string(_tb_unknown_literals);
    _j += ",\n \"selected\":[";
    for (std::size_t _i = 0; _i < _selected.size(); ++_i) {
      _j += (_i == 0 ? "\"" : ",\"") + _json_esc(_selected[_i]) + "\"";
    }
    _j       += "],\"args\":{";
    bool _af  = true;
    for (const auto& [_k, _v] : _args) {
      _j  += (_af ? "\"" : ",\"") + _json_esc(_k) + "\":\"" + _json_esc(_v) + "\"";
      _af  = false;
    }
    char _host[256] = {0};
    if (gethostname(_host, sizeof _host - 1) != 0) {
      _host[0] = '\0';
    }
    _j += "},\"counters\":\"" + std::string(rd.name()) + "\",\"host\":{\"name\":\"" + _json_esc(_host) + "\",\"cpu\":\""
          + _json_esc(_tp_host_cpu()) + "\"},\n \"tests\":[";
    for (std::size_t _i = 0; _i < done.size(); ++_i) {
      const _TpTest& _t  = done[_i];
      _j                += _i == 0 ? "\n  {" : ",\n  {";
      _j += "\"test\":\"" + _json_esc(_t.name) + "\",\"status\":\"" + (_t.f < 0 ? "error" : (_t.f > 0 ? "fail" : "pass")) + "\"";
      _j += row_json(_t, true) + "}";
    }
    _j += "]}\n";
    const auto _ns
        = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string _base
        = std::to_string(static_cast<long long>(_ns)) + "-" + std::to_string(static_cast<long>(getpid())) + ".json";
    const std::string _tmp = _dir + "/." + _base + ".tmp";
    const std::string _fin = _dir + "/" + _base;
    {
      std::ofstream _o(_tmp, std::ios::binary);
      if (!_o || !(_o << _j) || !_o.flush()) {
        std::fprintf(stderr, "lhd sim: warning: cannot write the sim.tune run file in '%s'\n", _dir.c_str());
        return;
      }
    }
    if (std::rename(_tmp.c_str(), _fin.c_str()) != 0) {
      std::fprintf(stderr, "lhd sim: warning: cannot publish the sim.tune run file '%s'\n", _fin.c_str());
      std::remove(_tmp.c_str());
    }
  }
)cpp";

// The generic `--set key=value` parser (ruling 8: no new driver flags; the SAME
// spellings as lhd). Applied in argv order, so the last occurrence of a key --
// and of `--seed` vs `lhd.seed`, `--init-zero` vs `sim.init_zero` -- wins.
inline constexpr std::string_view kSetParser = R"cpp(
  // ---- `--set key=value` (the lhd spellings) -----------------------------------
  static bool              _set_unknown_zero = false;  // applied right after seeding, before any test
  [[noreturn]] static void _set_die(const std::string& _k, const std::string& _v, const std::string& _why) {
    std::fprintf(stderr, "lhd sim: --set %s=%s: %s\n", _k.c_str(), _v.c_str(), _why.c_str());
    std::exit(2);
  }
  static bool _set_bool(const std::string& _k, const std::string& _v) {
    if (_v == "true" || _v == "1" || _v == "on") {
      return true;
    }
    if (_v == "false" || _v == "0" || _v == "off") {
      return false;
    }
    _set_die(_k, _v, "expects true|false");
  }
  // Whole number in [lo, hi], canonical decimal (no sign, no leading zeros).
  static bool _set_whole(const std::string& _v, unsigned long long _lo, unsigned long long _hi, std::string& _out) {
    if (_v.empty() || _v.size() > 20 || _v.find_first_not_of("0123456789") != std::string::npos) {
      return false;
    }
    const unsigned long long _n = std::strtoull(_v.c_str(), nullptr, 10);
    if (_n < _lo || _n > _hi) {
      return false;
    }
    _out = std::to_string(_n);
    return true;
  }
  // `key=value` line lookup in a baked `__lhd_tune_codegen_<mod>()` table.
  static bool _tp_table_get(const char* _t, const std::string& _k, std::string& _out) {
    const std::string _s(_t);
    for (std::size_t _p = 0; _p < _s.size();) {
      std::size_t _e = _s.find('\n', _p);
      if (_e == std::string::npos) {
        _e = _s.size();
      }
      const std::string _l = _s.substr(_p, _e - _p);
      if (_l.size() > _k.size() && _l.compare(0, _k.size(), _k) == 0 && _l[_k.size()] == '=') {
        _out = _l.substr(_k.size() + 1);
        return true;
      }
      _p = _e + 1;
    }
    return false;
  }
  // A codegen-only key: the binary was generated with ONE value, so a run can only
  // restate it. `_want` is the normalized value; `_any_true` (sim.vcd=true) accepts
  // any baked value other than "false". Every DUT root's table must agree; a root
  // with no table (no tune-id TU linked) cannot be checked, which is a note.
  static void _set_check_baked(const std::string& _k, const std::string& _v, const std::string& _want, bool _any_true) {
    bool _checked = false;
    for (const _TpRoot* _r = _tp_roots; _r->cls != nullptr; ++_r) {
      const char* _t = _r->codegen();
      std::string _b;
      if (_t == nullptr || !_tp_table_get(_t, _k, _b)) {
        continue;
      }
      _checked = true;
      if (_any_true ? _b == "false" : _b != _want) {
        _set_die(_k, _v, "this drv.bin was generated with " + _k + "=" + _b + "; re-run --setup-only with --set " + _k + "=" + _v);
      }
    }
    if (!_checked) {
      std::fprintf(stderr,
                   "lhd sim: note: --set %s=%s: this drv.bin has no baked codegen table to check it against (accepted)\n",
                   _k.c_str(),
                   _v.c_str());
    }
  }
  static void _apply_set(const std::string& _kv, unsigned long long& _seed) {
    const std::size_t _eq = _kv.find('=');
    if (_eq == std::string::npos || _eq == 0) {
      std::fprintf(stderr, "lhd sim: --set expects key=value, got '%s'\n", _kv.c_str());
      std::exit(2);
    }
    const std::string _k = _kv.substr(0, _eq), _v = _kv.substr(_eq + 1);
    std::string       _n;
    // ---- run-time keys ----
    if (_k == "lhd.seed") {
      _seed = _to_u64("set lhd.seed", _v);
    } else if (_k == "sim.init_zero") {
      _init_zero = _set_bool(_k, _v);
    } else if (_k == "sim.unknown_zero") {
      // true: zero-fill every `?` at run time -- byte-identical to a build generated
      // with sim.unknown_zero=true. A binary generated WITH it has no `?` text left
      // to draw from, so it cannot be asked for random fill.
      const bool _b     = _set_bool(_k, _v);
      bool       _baked = _baked_unknown_zero;
      for (const _TpRoot* _r = _tp_roots; _r->cls != nullptr; ++_r) {
        std::string _bv;
        _baked = _baked || (_r->codegen() != nullptr && _tp_table_get(_r->codegen(), _k, _bv) && _bv == "true");
      }
      if (!_b && _baked) {
        _set_die(_k,
                 _v,
                 "this drv.bin was generated with sim.unknown_zero=true (the `?` bits are folded into the code); re-run "
                 "--setup-only without it");
      }
      _set_unknown_zero = _b;
    } else if (_k == "sim.checkpoint") {
      const bool _b = _set_bool(_k, _v);
      if (_b && !_baked_runtime_support) {
        _set_die(_k,
                 _v,
                 "this drv.bin was generated with sim.checkpoint=false (no state walk to checkpoint); re-run --setup-only "
                 "without --set sim.checkpoint=false");
      }
      _ckpt.enabled = _b;
    } else if (_k == "sim.checkpoint_min_secs") {
      _ckpt.min_secs = std::strtod(_v.c_str(), nullptr);
    } else if (_k == "sim.checkpoint_max") {
      _ckpt.max = _to_i64("set sim.checkpoint_max", _v);
    } else if (_k == "sim.checkpoint_max_overhead") {
      _ckpt.max_overhead = std::strtod(_v.c_str(), nullptr);
    } else if (_k == "sim.checkpoint_every") {
      _ckpt.every = _to_i64("set sim.checkpoint_every", _v);
    } else if (_k == "sim.tune.profile") {
      // auto is lhd's decision (it forwards `on` when it profiles); a bare binary
      // treats auto like off.
      if (_v == "on") {
        _tp.on = true;
      } else if (_v == "off" || _v == "auto") {
        _tp.on = false;
      } else {
        _set_die(_k, _v, "expects auto|on|off");
      }
    } else if (_k == "sim.tune.profile_dir") {
      _tp.dir = _v;
    } else if (_k == "sim.tune.profile_stride") {
      if (!_set_whole(_v, 0, 1048576, _n)) {
        _set_die(_k, _v, "expects a whole number in [0, 1048576] (0 = self-calibrated)");
      }
      _tp.stride_fixed = std::strtoull(_n.c_str(), nullptr, 10);
    } else if (_k == "sim.tune.dirty") {
      // ---- codegen keys: a run can only restate what the binary was generated with
      if (_v == "on" || _v == "true" || _v == "1") {
        _set_check_baked(_k, _v, "on", false);
      } else if (_v == "off" || _v == "false" || _v == "0") {
        _set_check_baked(_k, _v, "off", false);
      } else if (_v != "auto" && !_v.empty()) {
        _set_die(_k, _v, "expects auto|on|off");
      }
    } else if (_k == "sim.tune.fence") {
      if (_v == "none") {
        _set_check_baked(_k, _v, "none", false);
      } else if (_set_whole(_v, 0, 1048576, _n)) {
        _set_check_baked(_k, _v, _n, false);
      } else if (_v != "auto" && !_v.empty()) {
        _set_die(_k, _v, "expects auto|none|N (N a whole number in [0, 1048576])");
      }
    } else if (_k == "sim.tune.live_words") {
      if (_set_whole(_v, 1, 1048576, _n)) {
        _set_check_baked(_k, _v, _n, false);
      } else if (_v != "auto" && !_v.empty()) {
        _set_die(_k, _v, "expects auto|N (N a whole number in [1, 1048576]; the default is auto)");
      }
    } else if (_k == "sim.tune.backend") {
      if (_v == "slop" || _v == "llvm") {
        _set_check_baked(_k, _v, _v, false);
      } else if (_v != "auto" && !_v.empty()) {
        _set_die(_k, _v, "expects auto|slop|llvm");
      }
    } else if (_k == "sim.slop_u" || _k == "sim.debug" || _k == "sim.vcd_fake_delay") {
      _set_check_baked(_k, _v, _set_bool(_k, _v) ? "true" : "false", false);
    } else if (_k == "sim.vcd") {
      // bool-or-FILE. The driver names its own per-test VCDs, so a FILE only has to
      // find the trace machinery baked in (as `true` does).
      const bool _off = _v == "false" || _v == "0" || _v == "off" || _v.empty();
      if (_off ? _baked_vcd : !_baked_vcd) {
        _set_die(_k,
                 _v,
                 std::string("this drv.bin was generated with sim.vcd=") + (_baked_vcd ? "true" : "false")
                     + "; re-run --setup-only with --set " + _k + "=" + _v);
      }
      _set_check_baked(_k, _v, "false", !_off);
    } else if (_k == "sim.jobs" || _k == "sim.ninja" || _k == "sim.hlop_dir" || _k == "sim.iassert_dir" || _k == "sim.compile_only"
               || _k == "sim.tune.file" || _k == "sim.tune.export" || _k.compare(0, 4, "lhd.") == 0) {
      // Build plumbing and lhd-only keys: meaningless inside a built binary, and
      // accepted so ONE --set list can go to both the setup and the run.
    } else if (_k == "sim.color_dirty" || _k == "sim.fence_ratio" || _k == "sim.live_words" || _k == "sim.backend") {
      // The pre-sim.tune spellings (ruling 6: a directed rename, with the value
      // translated so the hint is copy-pasteable).
      std::string _to = "sim.tune.backend=" + (_v.empty() ? std::string("auto") : _v);
      if (_k == "sim.color_dirty") {
        _to = std::string("sim.tune.dirty=") + (_v == "true" || _v == "1" || _v == "on" ? "on" : "off");
      } else if (_k == "sim.fence_ratio") {
        _to = "sim.tune.fence=" + (_v.empty() || _v[0] == '-' ? std::string("auto") : _v);
      } else if (_k == "sim.live_words") {
        _to = "sim.tune.live_words=" + (_v.empty() || _v == "0" ? std::string("auto") : _v);
      }
      _set_die(_k, _v, "renamed: use --set " + _to + " instead");
    } else {
      _set_die(_k, _v, "not a run-time key (it applies at setup, not to a built simulator)");
    }
  }
)cpp";

}  // namespace prp_sim
