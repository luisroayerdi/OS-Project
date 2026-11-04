// =============================================================================
//  OWNER: Member B  (expert-based selection + expChunk)
// =============================================================================
// Implement RandomSel, ExhaustiveSel, ExpertSel (paper Sect. 3.2) and the
// expert chunk parameter expChunk.
//
// Primary source: the Auto4OMP paper, paper ref [25]:
//   Korndorfer et al., "Automated Algorithm Selection in Shared-Memory
//   Parallel Applications" (Auto4OMP), and its code (Auto4OMP branch of
//   LB4OMP). Sect. 3.1 Eq. 1 there = expChunk, Sect. 3.3.3 / Fig. 5 / Tab. 1 /
//   Algorithm 1 there = ExpertSel fuzzy rules.
//
// You can develop WITHOUT Member A's algorithms: `make test` runs every
// selector against a synthetic environment (runtime/tests/test_main.cpp),
// and the real benchmark accepts `--portfolio STATIC,SS,GSS`.
//
// Contract (see lb/selector.h): select(t) is called before loop instance t,
// observe() right after it. Both run on the master thread.
// =============================================================================
#include <cmath>
#include <limits>
#include <random>
#include <vector>

#include "lb/selector.h"

namespace lb {

// ---------------------------------------------------------------- expChunk
// Table 2: "a point at 1/golden ratio (1.618) = 0.618 on the curve between
// N/(iP) and 1, with i increasing in steps of 2" (exact formula: ref [25]
// Eq. 1). Values reported in the paper (Figs. 6/8) to check against:
//   STREAM Triad, N = 2e9, P = 56  -> 217      SPHYNX, N = 1e6, P = 56  -> 34
//   STREAM Triad, N = 2e9, P = 20  -> 305      SPHYNX, N = 1e6, P = 128 -> 30
// (`make test` prints your values next to these.)
int64_t exp_chunk(int64_t N, int P) {
  (void)N; (void)P;
  throw NotImplemented("exp_chunk (Member B)");
}

namespace {

// ---------------------------------------------------------------- RandomSel
// t = 0: STATIC (portfolio[0]). Then every time-step:
//   Pj = LIB / 10  (LIB of the previous instance, in %),  RND ~ U(0,1)
//   if Pj > RND: pick a new algorithm uniformly at random from the portfolio
//   else       : keep the current one
class RandomSel final : public Selector {
 public:
  explicit RandomSel(const SelectorConfig& c) : cfg_(c), rng_(c.seed), current_(c.portfolio[0]) {}
  Algo select(int t) override {
    (void)t;
    throw NotImplemented("RandomSel (Member B)");
  }
  void observe(const Observation& o) override { last_lib_ = o.lib_pct; }

 private:
  SelectorConfig cfg_;
  std::mt19937_64 rng_;
  Algo current_;
  double last_lib_ = 0.0;
};

// ---------------------------------------------------------------- ExhaustiveSel
// Try every portfolio algorithm once (one per time-step), then keep the one
// with the lowest loop time. Keep computing LIB; if it deviates > 10% from the
// recorded average for the chosen algorithm, re-trigger the search.
// Decide (and document in docs/TEAM_PLAN.md) exactly what "10% variation
// from the recorded average" means in your implementation.
class ExhaustiveSel final : public Selector {
 public:
  explicit ExhaustiveSel(const SelectorConfig& c) : cfg_(c) {}
  Algo select(int t) override {
    (void)t;
    throw NotImplemented("ExhaustiveSel (Member B)");
  }
  void observe(const Observation& o) override { (void)o; }

 private:
  SelectorConfig cfg_;
};

// ---------------------------------------------------------------- ExpertSel
// t = 0: STATIC. Then two fuzzy-logic systems over (T_par, LIB): one for the
// initial pick from absolute values, one for later adjustments based on the
// CHANGE of T_par and LIB. Rules / membership functions: ref [25].
// If you have to simplify, write down exactly how in docs/TEAM_PLAN.md:
// that is part of the "why do our results differ" discussion.
class ExpertSel final : public Selector {
 public:
  explicit ExpertSel(const SelectorConfig& c) : cfg_(c) {}
  Algo select(int t) override {
    (void)t;
    throw NotImplemented("ExpertSel (Member B)");
  }
  void observe(const Observation& o) override { (void)o; }

 private:
  SelectorConfig cfg_;
};

}  // namespace

std::unique_ptr<Selector> make_random_sel(const SelectorConfig& c) {
  return std::make_unique<RandomSel>(c);
}
std::unique_ptr<Selector> make_exhaustive_sel(const SelectorConfig& c) {
  return std::make_unique<ExhaustiveSel>(c);
}
std::unique_ptr<Selector> make_expert_sel(const SelectorConfig& c) {
  return std::make_unique<ExpertSel>(c);
}

}  // namespace lb
