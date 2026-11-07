// =============================================================================
//  OWNER: Daniel  (scheduling-algorithm portfolio)
// =============================================================================
// The 9 algorithms that complete the 12-algorithm portfolio (paper Sect. 2, 3.1).
// STATIC, SS and GSS live in algos_reference.cpp.
//
// Rules every algorithm respects (checked by `make test`):
//   * every iteration in [0, N) is handed out exactly once
//   * next_chunk() is called concurrently -> atomics or a mutex
//   * chunk_delivered = max(chunk_algorithm, p.min_chunk()), never more than what is left
//   * begin() runs before EVERY loop instance; AWF weights and mAF stats survive it
//
// Most algorithms only decide a chunk SIZE and then take it from a SharedQueue,
// so "each iteration exactly once" holds no matter what the size formula says.
// =============================================================================
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

#include "algos_internal.h"

namespace lb {

namespace {

int64_t ceil_div(int64_t a, int64_t b) { return a / b + (a % b != 0); }  // no overflow

// Chunk parameter, capped at N: a bigger chunk means the same thing and would overflow.
int64_t min_chunk(const LoopParams& p) {
  return std::min(p.min_chunk(), std::max<int64_t>(p.N, 1));
}

// FAC2 closed form: chunk of batch j = ceil(N / (2^(j+1) * P)).
int64_t fac2_chunk(int64_t N, int P, int64_t j) {
  const int e = static_cast<int>(std::min<int64_t>(j + 1, 1000));
  return static_cast<int64_t>(std::ceil(std::ldexp(static_cast<double>(N) / P, -e)));
}

// Central work queue: hands out the next `want` iterations (CAS loop, as in GSS).
class SharedQueue {
 public:
  void reset(int64_t N) {
    N_ = N;
    next_.store(0, std::memory_order_relaxed);
  }
  int64_t remaining() const {
    return std::max<int64_t>(N_ - next_.load(std::memory_order_relaxed), 0);
  }
  bool claim(int64_t want, int64_t* start, int64_t* size) {
    int64_t s = next_.load(std::memory_order_relaxed);
    for (;;) {
      if (s >= N_) return false;
      const int64_t cs = std::min(std::max<int64_t>(want, 1), N_ - s);
      if (next_.compare_exchange_weak(s, s + cs, std::memory_order_relaxed)) {
        *start = s;
        *size = cs;
        return true;
      }
    }
  }

 private:
  int64_t N_ = 0;
  alignas(kCacheLine) std::atomic<int64_t> next_{0};
};

// ---------------------------------------------------------------- TSS
// Paper Eq. 4 (Tzen & Ni [42]); LLVM kmp_sch_trapezoidal.
// Chunks shrink LINEARLY from f = N/(2P) down to l = chunk param, by delta each time.
class TSS final : public SchedAlgorithm {
 public:
  void begin(const LoopParams& p) override {
    last_ = min_chunk(p);
    first_ = std::max(ceil_div(p.N, 2 * p.P), last_);
    const int64_t A = std::max<int64_t>(ceil_div(2 * p.N, first_ + last_), 2);
    delta_ = static_cast<double>(first_ - last_) / static_cast<double>(A - 1);
    i_ = 0;
    q_.reset(p.N);
  }
  bool next_chunk(int, int64_t* start, int64_t* size) override {
    std::lock_guard<std::mutex> g(m_);
    const auto cs = static_cast<int64_t>(std::llround(first_ - i_ * delta_));
    ++i_;
    return q_.claim(std::max(cs, last_), start, size);  // past chunk A: size l
  }

 private:
  int64_t first_ = 1, last_ = 1, i_ = 0;
  double delta_ = 0;
  SharedQueue q_;
  std::mutex m_;
};

// ---------------------------------------------------------------- Static Steal
// LLVM kmp_sch_static_steal. Each thread owns its STATIC share (in chunks of
// min_chunk) and takes from the front. When empty, it steals the last quarter
// (at least 1 chunk) of another thread's share, like newer LLVM versions.
class StaticSteal final : public SchedAlgorithm {
 public:
  void begin(const LoopParams& p) override {
    N_ = p.N;
    P_ = p.P;
    c_ = min_chunk(p);
    if (static_cast<int>(slots_.size()) != P_) slots_ = std::vector<Padded<Range>>(P_);
    const int64_t n = ceil_div(N_, c_), base = n / P_, extra = n % P_;
    for (int t = 0; t < P_; ++t) {
      auto& r = slots_[t].v;
      r.lo = t * base + std::min<int64_t>(t, extra);
      r.hi = r.lo + base + (t < extra ? 1 : 0);
    }
  }
  bool next_chunk(int tid, int64_t* start, int64_t* size) override {
    int64_t k;
    while (!take_own(tid, &k))
      if (!steal(tid)) return false;
    *start = k * c_;
    *size = std::min(c_, N_ - *start);
    return true;
  }

 private:
  struct Range {
    int64_t lo = 0, hi = 0;  // chunk indices [lo, hi)
    std::mutex m;
  };

  bool take_own(int tid, int64_t* k) {
    auto& r = slots_[tid].v;
    std::lock_guard<std::mutex> g(r.m);
    if (r.lo >= r.hi) return false;
    *k = r.lo++;
    return true;
  }
  // Never holds two locks at once, so no deadlock.
  bool steal(int tid) {
    for (int d = 1; d < P_; ++d) {
      auto& v = slots_[(tid + d) % P_].v;
      int64_t lo, hi;
      {
        std::lock_guard<std::mutex> g(v.m);
        const int64_t left = v.hi - v.lo;
        if (left <= 0) continue;
        hi = v.hi;
        lo = hi - std::max<int64_t>(left / 4, 1);
        v.hi = lo;
      }
      auto& mine = slots_[tid].v;
      std::lock_guard<std::mutex> g(mine.m);
      mine.lo = lo;
      mine.hi = hi;
      return true;
    }
    return false;
  }

  int64_t N_ = 0, c_ = 1;
  int P_ = 1;
  std::vector<Padded<Range>> slots_;
};

// ---------------------------------------------------------------- mFAC2
// Paper p. 4-5; LB4OMP kmp_sch_fac2a. FAC2 hands out batches of P equal chunks,
// each batch half the size of the previous one. No mutex: an atomic counter k
// gives the batch j = k / P and the chunk size is computed from j directly.
class MFac2 final : public SchedAlgorithm {
 public:
  void begin(const LoopParams& p) override {
    N_ = p.N;
    P_ = p.P;
    minc_ = min_chunk(p);
    k_.store(0, std::memory_order_relaxed);
    q_.reset(p.N);
  }
  bool next_chunk(int, int64_t* start, int64_t* size) override {
    const int64_t j = k_.fetch_add(1, std::memory_order_relaxed) / P_;
    return q_.claim(std::max(fac2_chunk(N_, P_, j), minc_), start, size);
  }

 private:
  int64_t N_ = 0, minc_ = 1;
  int P_ = 1;
  alignas(kCacheLine) std::atomic<int64_t> k_{0};
  SharedQueue q_;
};

// ---------------------------------------------------------------- AWF-B/C/D/E
// Paper p. 5 (Banicescu et al. [5]); LB4OMP kmp_sch_awf_b..e.
// Each thread has a weight: faster threads (less time per iteration) get > 1.
//   w_i = n / (pi_i * sum_j 1/pi_j)     pi = time per iteration (weighted by chunk number)
//   chunk = ceil(w_i * base)
//   base = ceil(R / 2P) (FAC2), R = iterations left
//   B, D: one base per batch (batch j = ticket / P), weight refreshed once per batch
//   C, E: base and weight refreshed at every request
//   B, C: time = chunk body only.   D, E: time = whole request + body (incl. overhead)
// Weights persist across loop instances.
class AWF final : public SchedAlgorithm {
 public:
  explicit AWF(char variant)
      : batched_(variant == 'B' || variant == 'D'), total_time_(variant == 'D' || variant == 'E') {}

  void begin(const LoopParams& p) override {
    P_ = p.P;
    minc_ = min_chunk(p);
    k_.store(0, std::memory_order_relaxed);
    q_.reset(p.N);
    if (static_cast<int>(th_.size()) != P_) th_ = std::vector<Padded<ThreadState>>(P_);
    for (auto& t : th_) t.v.batch = -1;  // timings stay, batch numbering restarts
    batch_j_ = -1;
  }

  bool next_chunk(int tid, int64_t* start, int64_t* size) override {
    auto& s = th_[tid].v;
    s.stamp = now_s();
    const int64_t j = batched_ ? k_.fetch_add(1, std::memory_order_relaxed) / P_ : 0;
    if (!batched_ || j != s.batch) {  // B/D: once per batch; C/E: every request
      s.batch = j;
      s.weight = weight(tid);
      s.base = batched_ ? batch_base(j) : ceil_div(q_.remaining(), 2 * P_);
    }
    const auto cs = static_cast<int64_t>(std::ceil(s.weight * static_cast<double>(s.base)));
    return q_.claim(std::max(cs, minc_), start, size);
  }

  bool needs_chunk_timing() const override { return true; }

  void chunk_done(int tid, int64_t size, double seconds) override {
    auto& s = th_[tid].v;
    const double t = total_time_ ? now_s() - s.stamp : seconds;
    const auto j = static_cast<double>(++s.n_chunks);  // later chunks count more
    s.sum_time += j * t;
    s.sum_iters += j * static_cast<double>(size);
    if (s.sum_iters > 0) s.pi.store(s.sum_time / s.sum_iters, std::memory_order_relaxed);
  }

 private:
  struct ThreadState {
    int64_t n_chunks = 0, batch = -1, base = 1;
    double sum_time = 0, sum_iters = 0, stamp = 0, weight = 1;
    std::atomic<double> pi{0};  // read by other threads
  };

  double weight(int tid) const {
    const double mine = th_[tid].v.pi.load(std::memory_order_relaxed);
    if (mine <= 0) return 1.0;  // no data yet
    double inv_sum = 0;
    int n = 0;
    for (const auto& t : th_) {
      const double pi = t.v.pi.load(std::memory_order_relaxed);
      if (pi > 0) {
        inv_sum += 1.0 / pi;
        ++n;
      }
    }
    return n / (mine * inv_sum);
  }

  // FAC2 base R_j / (2P), fixed by the first thread that enters batch j, shared by the rest.
  int64_t batch_base(int64_t j) {
    std::lock_guard<std::mutex> g(batch_m_);
    if (j > batch_j_) {
      batch_j_ = j;
      batch_base_ = ceil_div(q_.remaining(), 2 * P_);
    }
    return batch_base_;
  }

  const bool batched_, total_time_;
  int64_t minc_ = 1, batch_j_ = -1, batch_base_ = 1;
  std::mutex batch_m_;
  int P_ = 1;
  alignas(kCacheLine) std::atomic<int64_t> k_{0};
  SharedQueue q_;
  std::vector<Padded<ThreadState>> th_;
};

// ---------------------------------------------------------------- mAF
// Paper Eqs. 6-7 (Banicescu & Liu [4]); LB4OMP kmp_sch_af_a.
// Each thread keeps the mean (mu) and std-dev (sigma) of its time per iteration.
// First chunk = 100 iterations (Eq. 6, n = 1), then
//   D = sum sigma_i^2 / mu_i,   T = (sum 1/mu_i)^-1
//   chunk = (D + 2TR - sqrt(D^2 + 4DTR)) / (2 mu_i)
// Stats persist across loop instances. LB4OMP's sub-chunk "divider" is omitted.
class MAF final : public SchedAlgorithm {
 public:
  void begin(const LoopParams& p) override {
    minc_ = min_chunk(p);
    q_.reset(p.N);
    if (static_cast<int>(th_.size()) != p.P) th_ = std::vector<Padded<Stats>>(p.P);
  }

  bool next_chunk(int tid, int64_t* start, int64_t* size) override {
    const auto& me = th_[tid].v;
    const double mu = me.mu.load(std::memory_order_relaxed);
    int64_t cs = 100;  // no measurements yet
    if (mu > 0) {
      const double sigma = me.sigma.load(std::memory_order_relaxed);
      double D = 0, inv_T = 0;
      for (const auto& t : th_) {
        double m = t.v.mu.load(std::memory_order_relaxed);
        double s = t.v.sigma.load(std::memory_order_relaxed);
        if (m <= 0) {  // that thread has no data yet: use mine
          m = mu;
          s = sigma;
        }
        D += s * s / m;
        inv_T += 1.0 / m;
      }
      const double T = 1.0 / inv_T, R = static_cast<double>(q_.remaining());
      // Eq. 6 rewritten as 2T²R² / (mu (b + sqrt(..))), b = D + 2TR: same value, no cancellation.
      const double b = D + 2 * T * R;
      const double x = 2 * T * T * R * R / (mu * (b + std::sqrt(D * D + 4 * D * T * R)));
      cs = std::isfinite(x) ? static_cast<int64_t>(std::ceil(x)) : minc_;
    }
    return q_.claim(std::max(cs, minc_), start, size);
  }

  bool needs_chunk_timing() const override { return true; }

  void chunk_done(int tid, int64_t size, double seconds) override {
    if (size <= 0) return;
    auto& s = th_[tid].v;
    const double avg = seconds / static_cast<double>(size);
    ++s.n;
    s.sum += avg;
    s.sum2 += avg * avg;
    const double mu = s.sum / s.n;
    const double var = s.n > 1 ? (s.sum2 - s.n * mu * mu) / (s.n - 1) : 0.0;
    s.mu.store(mu, std::memory_order_relaxed);
    s.sigma.store(std::sqrt(std::max(var, 0.0)), std::memory_order_relaxed);
  }

 private:
  struct Stats {
    int64_t n = 0;
    double sum = 0, sum2 = 0;
    std::atomic<double> mu{0}, sigma{0};  // read by other threads
  };

  int64_t minc_ = 1;
  SharedQueue q_;
  std::vector<Padded<Stats>> th_;
};

// ---------------------------------------------------------------- Auto(LLVM)
// LLVM maps schedule(auto) to kmp_sch_guided_analytical_chunked
//   kmp_global.cpp:   __kmp_auto = kmp_sch_guided_analytical_chunked
//   kmp_dispatch.cpp: case kmp_sch_auto -> schedule = __kmp_auto
// After chunk i, rem(i) = ceil(N * x^i) iterations are left, x = 1 - 1/(2P),
// i.e. each chunk is ~R/(2P) (GSS uses R/P). Once chunks would fall below the
// chunk param c (index `cross`), it hands out fixed chunks of c. Tiny loops,
// (2c+1)P >= N, are plain dynamic from the start. Lock-free: one fetch_add.
class AutoLLVM final : public SchedAlgorithm {
 public:
  void begin(const LoopParams& p) override {
    N_ = p.N;
    c_ = min_chunk(p);
    x_ = 1.0L - 0.5L / p.P;
    idx_.store(0, std::memory_order_relaxed);
    cross_ = 0;
    const long double target = (2.0L * c_ + 1) * p.P / N_;
    for (long double v = 1; v > target; v *= x_) ++cross_;
    dyn_start_ = N_ - left_after(cross_);
  }
  bool next_chunk(int, int64_t* start, int64_t* size) override {
    for (;;) {
      const int64_t i = idx_.fetch_add(1, std::memory_order_relaxed);
      int64_t s, e;
      if (i < cross_) {
        s = N_ - left_after(i);
        e = N_ - left_after(i + 1);
      } else {
        s = dyn_start_ + (i - cross_) * c_;
        e = std::min(s + c_, N_);
      }
      if (s >= N_) return false;
      if (e > s) {
        *start = s;
        *size = e - s;
        return true;
      }
    }
  }

 private:
  int64_t left_after(int64_t i) const {
    return static_cast<int64_t>(std::ceil(N_ * std::pow(x_, static_cast<long double>(i))));
  }

  int64_t N_ = 0, c_ = 1, cross_ = 0, dyn_start_ = 0;
  long double x_ = 0.5L;
  alignas(kCacheLine) std::atomic<int64_t> idx_{0};
};

}  // namespace

std::unique_ptr<SchedAlgorithm> make_tss() { return std::make_unique<TSS>(); }
std::unique_ptr<SchedAlgorithm> make_static_steal() { return std::make_unique<StaticSteal>(); }
std::unique_ptr<SchedAlgorithm> make_mfac2() { return std::make_unique<MFac2>(); }
std::unique_ptr<SchedAlgorithm> make_awf(char v) { return std::make_unique<AWF>(v); }
std::unique_ptr<SchedAlgorithm> make_maf() { return std::make_unique<MAF>(); }
std::unique_ptr<SchedAlgorithm> make_auto_llvm() { return std::make_unique<AutoLLVM>(); }

}  // namespace lb
