// Reference implementations: STATIC, SS, GSS.
// They are complete and tested. Member A: read these first. They show the
// three patterns you will reuse:
//   STATIC -> per-thread private state, no synchronisation at all
//   SS     -> one shared atomic counter (fetch_add)
//   GSS    -> chunk size depends on the remaining work: compare-exchange loop
#include <algorithm>
#include <atomic>
#include <vector>

#include "algos_internal.h"

namespace lb {

namespace {

// ---------------------------------------------------------------- STATIC
// Eq. 1, Cs = N/P. Default chunk: thread i gets one contiguous block.
// With a chunk parameter c: schedule(static, c), i.e. chunks of c iterations
// handed out round-robin (chunk k goes to thread k mod P).
class Static final : public SchedAlgorithm {
 public:
  void begin(const LoopParams& p) override {
    p_ = p;
    slots_.assign(p.P, {});
    for (int tid = 0; tid < p.P; ++tid) {
      auto& s = slots_[tid].v;
      if (p.chunk <= 0) {
        // balanced block partition: first N%P threads get one extra iteration
        const int64_t base = p.N / p.P, extra = p.N % p.P;
        s.next = tid * base + std::min<int64_t>(tid, extra);
        s.end = s.next + base + (tid < extra ? 1 : 0);
      } else {
        s.next = static_cast<int64_t>(tid) * p.chunk;
        s.end = p.N;
      }
    }
  }

  bool next_chunk(int tid, int64_t* start, int64_t* size) override {
    auto& s = slots_[tid].v;
    if (s.next >= s.end) return false;
    *start = s.next;
    if (p_.chunk <= 0) {
      *size = s.end - s.next;
      s.next = s.end;
    } else {
      *size = std::min(p_.chunk, p_.N - s.next);
      s.next += p_.chunk * p_.P;  // round-robin stride
    }
    return true;
  }

 private:
  struct Slot {
    int64_t next = 0, end = 0;
  };
  LoopParams p_;
  std::vector<Padded<Slot>> slots_;
};

// ---------------------------------------------------------------- SS
// Eq. 2, Cs = 1 (or the chunk parameter): schedule(dynamic, c).
class SelfScheduling final : public SchedAlgorithm {
 public:
  void begin(const LoopParams& p) override {
    N_ = p.N;
    c_ = p.min_chunk();
    next_.store(0, std::memory_order_relaxed);
  }
  bool next_chunk(int, int64_t* start, int64_t* size) override {
    const int64_t s = next_.fetch_add(c_, std::memory_order_relaxed);
    if (s >= N_) return false;
    *start = s;
    *size = std::min(c_, N_ - s);
    return true;
  }

 private:
  int64_t N_ = 0, c_ = 1;
  alignas(kCacheLine) std::atomic<int64_t> next_{0};
};

// ---------------------------------------------------------------- GSS
// Eq. 3, Cs_i = ceil(R_i / P), bounded below by the chunk parameter.
class Guided final : public SchedAlgorithm {
 public:
  void begin(const LoopParams& p) override {
    N_ = p.N;
    P_ = p.P;
    minc_ = p.min_chunk();
    next_.store(0, std::memory_order_relaxed);
  }
  bool next_chunk(int, int64_t* start, int64_t* size) override {
    int64_t s = next_.load(std::memory_order_relaxed);
    for (;;) {
      if (s >= N_) return false;
      const int64_t R = N_ - s;
      int64_t cs = std::max<int64_t>((R + P_ - 1) / P_, minc_);
      cs = std::min(cs, R);
      if (next_.compare_exchange_weak(s, s + cs, std::memory_order_relaxed)) {
        *start = s;
        *size = cs;
        return true;
      }
      // s was reloaded by compare_exchange_weak; retry
    }
  }

 private:
  int64_t N_ = 0, minc_ = 1;
  int P_ = 1;
  alignas(kCacheLine) std::atomic<int64_t> next_{0};
};

}  // namespace

std::unique_ptr<SchedAlgorithm> make_static() { return std::make_unique<Static>(); }
std::unique_ptr<SchedAlgorithm> make_ss() { return std::make_unique<SelfScheduling>(); }
std::unique_ptr<SchedAlgorithm> make_gss() { return std::make_unique<Guided>(); }

}  // namespace lb
