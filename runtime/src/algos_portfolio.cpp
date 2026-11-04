// =============================================================================
//  OWNER: Member A  (scheduling-algorithm portfolio)
// =============================================================================
// Implement the 9 remaining algorithms of the 12-algorithm portfolio.
// STATIC, SS and GSS are done in algos_reference.cpp: copy their patterns.
//
// Rules every algorithm must respect (checked by `make test`):
//   * every iteration in [0, N) is handed out exactly once
//   * next_chunk() is called concurrently -> use atomics or a mutex
//   * chunk parameter: chunk_delivered = max(chunk_algorithm, p.min_chunk())
//     (never more than the remaining iterations)
//   * begin() is called before EVERY loop instance (time-step); keep the
//     state that must survive across instances (AWF weights, mAF stats)
//     outside of what begin() resets.
//
// Primary sources (read them; the paper only summarises):
//   * paper Sect. 2 (Eqs. 3-7) and Figs. 1-2
//   * LB4OMP source: https://github.com/unibas-dmi-hpc/LB4OMP
//     (runtime/src/kmp_dispatch.cpp). Matching its formulas is the goal.
//   * TSS: Tzen & Ni 1993; FAC: Hummel et al. 1992; AWF: Banicescu et al. 2003;
//     AF: Banicescu & Liu 2000 (paper refs [42], [18], [5], [4])
//
// When an algorithm is done: delete its `throw NotImplemented`, run
// `make test`, then `make trace SYSTEM=<you>` to regenerate the chunk plots.
// =============================================================================
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

#include "algos_internal.h"

namespace lb {

namespace {

// ---------------------------------------------------------------- TSS
// Eq. 4. f = ceil(N / (2P)), l = 1 (or chunk param), A = ceil(2N / (f + l)),
// delta = (f - l) / (A - 1), Cs_1 = f, Cs_i = Cs_{i-1} - delta.
// Hint: keep a chunk index i; Cs_i = f - (i-1)*delta (round, clamp to >= l).
class TSS final : public SchedAlgorithm {
 public:
  void begin(const LoopParams& p) override {
    p_ = p;
    throw NotImplemented("TSS (Member A)");
  }
  bool next_chunk(int tid, int64_t* start, int64_t* size) override {
    (void)tid; (void)start; (void)size;
    return false;  // TODO
  }

 private:
  LoopParams p_;
  std::mutex m_;
};

// ---------------------------------------------------------------- Static Steal
// LLVM's kmp_sch_static_steal: every thread starts with its STATIC share
// split into chunks; a thread that runs out steals chunks from the END of
// another thread's range. Hint: one {lo, hi} range + mutex per thread
// (Padded<...>), steal from a victim in round-robin order.
class StaticSteal final : public SchedAlgorithm {
 public:
  void begin(const LoopParams& p) override {
    p_ = p;
    throw NotImplemented("StaticSteal (Member A)");
  }
  bool next_chunk(int tid, int64_t* start, int64_t* size) override {
    (void)tid; (void)start; (void)size;
    return false;  // TODO
  }

 private:
  LoopParams p_;
};

// ---------------------------------------------------------------- mFAC2
// FAC2 = factoring with x = 2: work is handed out in batches of P equal
// chunks; batch j chunk size Cs_j = ceil(R_j / (2P)), R_{j+1} = R_j - P*Cs_j.
// mFAC2 (paper p.4-5): no mutex; an atomic chunk counter k gives the batch
// j = k / P and each thread computes Cs_j itself (closed form in j).
class MFac2 final : public SchedAlgorithm {
 public:
  void begin(const LoopParams& p) override {
    p_ = p;
    throw NotImplemented("mFAC2 (Member A)");
  }
  bool next_chunk(int tid, int64_t* start, int64_t* size) override {
    (void)tid; (void)start; (void)size;
    return false;  // TODO
  }

 private:
  LoopParams p_;
};

// ---------------------------------------------------------------- AWF-B/C/D/E
// Adaptive weighted factoring (paper p.5). Each thread i has a weight w_i
// (start at 1). Chunk for thread i = w_i * (factoring batch chunk size).
//   B: batch-based, weights updated after each batch from iteration times
//   C: like B but a new "batch" is computed at every work request
//   D: like B but uses total chunk time (incl. overhead) for the weights
//   E: like C but with total chunk time as in D
// Weights persist across loop instances -> do NOT reset them in begin().
// Needs per-chunk timings: needs_chunk_timing() returns true.
class AWF final : public SchedAlgorithm {
 public:
  explicit AWF(char variant) : variant_(variant) {}
  void begin(const LoopParams& p) override {
    p_ = p;
    throw NotImplemented(std::string("AWF-") + variant_ + " (Member A)");
  }
  bool next_chunk(int tid, int64_t* start, int64_t* size) override {
    (void)tid; (void)start; (void)size;
    return false;  // TODO
  }
  bool needs_chunk_timing() const override { return true; }
  void chunk_done(int tid, int64_t size, double seconds) override {
    (void)tid; (void)size; (void)seconds;  // TODO: accumulate per-thread timing
  }

 private:
  char variant_;
  LoopParams p_;
};

// ---------------------------------------------------------------- mAF
// Adaptive factoring, Eqs. 6-7. Each thread keeps running mu_i and sigma_i
// of its iteration execution times (from chunk_done). First chunk >= 100
// iterations (Eq. 6, n = 1), then
//   D = sum_i sigma_i^2 / mu_i,   T = (sum_i 1/mu_i)^-1
//   Cs = (D + 2*T*R - sqrt(D^2 + 4*D*T*R)) / (2*mu_i)
// "m" variant = LB4OMP's lower-overhead implementation [26].
class MAF final : public SchedAlgorithm {
 public:
  void begin(const LoopParams& p) override {
    p_ = p;
    throw NotImplemented("mAF (Member A)");
  }
  bool next_chunk(int tid, int64_t* start, int64_t* size) override {
    (void)tid; (void)start; (void)size;
    return false;  // TODO
  }
  bool needs_chunk_timing() const override { return true; }
  void chunk_done(int tid, int64_t size, double seconds) override {
    (void)tid; (void)size; (void)seconds;  // TODO
  }

 private:
  LoopParams p_;
};

// ---------------------------------------------------------------- Auto(LLVM)
// schedule(auto) in the LLVM OpenMP runtime simply maps to one of the other
// LLVM schedules. Find which one (search for `__kmp_auto` in LLVM's
// openmp/runtime/src/kmp_global.cpp and kmp_dispatch.cpp), then implement it
// (or reuse one of the classes above) and document your finding in
// docs/TEAM_PLAN.md.
class AutoLLVM final : public SchedAlgorithm {
 public:
  void begin(const LoopParams& p) override {
    p_ = p;
    throw NotImplemented("Auto(LLVM) (Member A)");
  }
  bool next_chunk(int tid, int64_t* start, int64_t* size) override {
    (void)tid; (void)start; (void)size;
    return false;  // TODO
  }

 private:
  LoopParams p_;
};

}  // namespace

std::unique_ptr<SchedAlgorithm> make_tss() { return std::make_unique<TSS>(); }
std::unique_ptr<SchedAlgorithm> make_static_steal() { return std::make_unique<StaticSteal>(); }
std::unique_ptr<SchedAlgorithm> make_mfac2() { return std::make_unique<MFac2>(); }
std::unique_ptr<SchedAlgorithm> make_awf(char v) { return std::make_unique<AWF>(v); }
std::unique_ptr<SchedAlgorithm> make_maf() { return std::make_unique<MAF>(); }
std::unique_ptr<SchedAlgorithm> make_auto_llvm() { return std::make_unique<AutoLLVM>(); }

}  // namespace lb
