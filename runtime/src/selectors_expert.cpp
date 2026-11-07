// =============================================================================
//  OWNER: Member B  (expert-based selection + expChunk)
// =============================================================================
// RandomSel, ExhaustiveSel, ExpertSel (paper Sect. 3.2) and the expert chunk
// parameter expChunk.
//
// Sources:
//   [paper] Korndorfer et al. 2025, Sect. 3.2 and Table 2.
//   [25]    Mohammed, Korndorfer, Eleliemy, Ciorba, "Automated Scheduling
//           Algorithm Selection and Chunk Parameter Calculation in OpenMP",
//           IEEE TPDS 33(12), 2022. Sect. 3.1 Eq. 1 = expChunk, Sect. 3.2 =
//           portfolio order, Sect. 3.3.3 Fig. 5 / Table 1 / Algorithm 1 =
//           ExpertSel.
// Every place where the sources disagree or leave a detail open is recorded in
// the decision log in docs/TEAM_PLAN.md.
//
// Contract (see lb/selector.h): select(t) is called before loop instance t,
// observe() right after it. Both run on the master thread.
// =============================================================================
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

#include "lb/selector.h"

namespace lb {

// ---------------------------------------------------------------- expChunk
// [25] Eq. 1 with phi = 1.618:
//   chunk = floor(N / (2^f * 2P)),  f = floor(log2(N/P) * 1/phi)
// This gives the values reported in [25] Fig. 1 and in the paper for SPHYNX
// (48, 34, 30). Loops with N <= P have no interval to search, so they get 1.
int64_t exp_chunk(int64_t N, int P) {
  const double ratio = static_cast<double>(N) / P;
  if (ratio <= 1.0) return 1;
  constexpr double kPhi = 1.618;
  const int f = static_cast<int>(std::floor(std::log2(ratio) / kPhi));
  const int64_t chunk = N / ((int64_t(1) << f) * 2 * P);
  return std::max<int64_t>(chunk, 1);
}

namespace {

// Portfolio in increasing order of scheduling overhead and load balancing
// capacity, [25] Sect. 3.2: STATIC, SS, GSS, GAC, TSS, Static Steal, mFAC2,
// AWF-B, AWF-C, AWF-D, AWF-E, mAF. GAC is schedule(auto), our AutoLLVM. The
// paper calls the two ends DLS_0 = STATIC and DLS_n = mAF.
int overhead_rank(Algo a) {
  switch (a) {
    case Algo::STATIC: return 0;
    case Algo::SS: return 1;
    case Algo::GSS: return 2;
    case Algo::AUTO_LLVM: return 3;
    case Algo::TSS: return 4;
    case Algo::STATIC_STEAL: return 5;
    case Algo::MFAC2: return 6;
    case Algo::AWF_B: return 7;
    case Algo::AWF_C: return 8;
    case Algo::AWF_D: return 9;
    case Algo::AWF_E: return 10;
    case Algo::MAF: return 11;
  }
  return kNumAlgos;
}

std::vector<Algo> in_overhead_order(std::vector<Algo> portfolio) {
  std::stable_sort(portfolio.begin(), portfolio.end(),
                   [](Algo x, Algo y) { return overhead_rank(x) < overhead_rank(y); });
  return portfolio;
}

// ---------------------------------------------------------------- RandomSel
// [paper] Sect. 3.2: at each loop instance draw RND ~ U(0,1) and compute
// Pj = LIB / 10. If Pj > RND, pick an algorithm at random from the portfolio
// (which may be the current one); otherwise keep the current one.
class RandomSel final : public Selector {
 public:
  explicit RandomSel(const SelectorConfig& c)
      : cfg_(c), rng_(c.seed), current_(in_overhead_order(c.portfolio).front()) {}

  Algo select(int t) override {
    if (t == 0) return current_;  // no LIB measured yet: start at DLS_0
    std::uniform_real_distribution<double> rnd(0.0, 1.0);
    const double jump_probability = last_lib_ / 10.0;
    if (jump_probability > rnd(rng_)) {
      std::uniform_int_distribution<size_t> pick(0, cfg_.portfolio.size() - 1);
      current_ = cfg_.portfolio[pick(rng_)];
    }
    return current_;
  }
  void observe(const Observation& o) override { last_lib_ = o.lib_pct; }

 private:
  SelectorConfig cfg_;
  std::mt19937_64 rng_;
  Algo current_;
  double last_lib_ = 0.0;
};

// ---------------------------------------------------------------- ExhaustiveSel
// [paper] Sect. 3.2: try every algorithm from DLS_0 to DLS_n, one per time-step,
// then keep the one with the shortest observed time. LIB is still computed, and
// "10% variation from the recorded average" with the selected algorithm
// re-triggers the search. See the decision log for how that is read here.
class ExhaustiveSel final : public Selector {
 public:
  explicit ExhaustiveSel(const SelectorConfig& c)
      : order_(in_overhead_order(c.portfolio)), best_(order_.front()), selected_(order_.front()) {}

  Algo select(int) override { return searching_ ? order_[trial_] : selected_; }

  void observe(const Observation& o) override {
    if (searching_) {
      if (o.t_par < best_time_) {
        best_time_ = o.t_par;
        best_lib_ = o.lib_pct;
        best_ = o.algo;
      }
      if (++trial_ == order_.size()) {
        searching_ = false;
        selected_ = best_;
        lib_sum_ = best_lib_;  // the trial is the first record for this algorithm
        lib_count_ = 1;
      }
      return;
    }
    if (o.lib_pct > lib_sum_ / lib_count_ + kRetriggerPoints) {
      searching_ = true;
      trial_ = 0;
      best_time_ = std::numeric_limits<double>::infinity();
      return;
    }
    lib_sum_ += o.lib_pct;
    ++lib_count_;
  }

 private:
  static constexpr double kRetriggerPoints = 10.0;

  std::vector<Algo> order_;
  Algo best_;
  Algo selected_;
  bool searching_ = true;
  size_t trial_ = 0;
  double best_time_ = std::numeric_limits<double>::infinity();
  double best_lib_ = 0.0;
  double lib_sum_ = 0.0;
  int lib_count_ = 0;
};

// ---------------------------------------------------------------- ExpertSel
// [25] Sect. 3.3.3. Instance 0 runs STATIC. Instance 1 uses the first fuzzy
// system (Table 1) to pick an initial algorithm from T_par and LIB. Later
// instances use the second (Algorithm 1) to move along the overhead-ordered
// portfolio by a step derived from T_par, LIB and their changes.

constexpr double kInf = std::numeric_limits<double>::infinity();

// Trapezoidal membership function: 0 at a, rising to 1 at b, 1 until c, falling
// to 0 at d. a == b or c == d is a vertical edge; +-infinity is a shoulder.
struct Trapezoid {
  double a, b, c, d;

  double membership(double x) const {
    if (x < a || x > d) return 0.0;
    if (x < b) return (x - a) / (b - a);
    if (x <= c) return 1.0;
    return (d - x) / (d - c);
  }
  // Area under the function once it is cut at height mu.
  double cut_area(double mu) const {
    const double base = d - a;
    const double top = base - mu * ((b - a) + (d - c));
    return mu * (base + top) / 2.0;
  }
  // Centre of gravity of the whole function.
  double center() const {
    const double rise = (b - a) / 2.0, flat = c - b, fall = (d - c) / 2.0;
    const double moment =
        rise * (a + 2.0 * (b - a) / 3.0) + flat * (b + c) / 2.0 + fall * (c + (d - c) / 3.0);
    return moment / (rise + flat + fall);
  }
};

// [25] Fig. 5, inputs. T_par in seconds, the others in percent.
constexpr Trapezoid kTparShort{-kInf, -kInf, 0.01, 0.03};
constexpr Trapezoid kTparMedium{0.0, 0.1, 1.0, 10.0};
constexpr Trapezoid kTparLong{1.0, 10.0, kInf, kInf};
constexpr Trapezoid kLibLow{-kInf, -kInf, 1.0, 2.0};
constexpr Trapezoid kLibModerate{1.0, 2.0, 4.0, 5.0};
constexpr Trapezoid kLibHigh{4.0, 10.0, kInf, kInf};
constexpr Trapezoid kDTparImproved{-kInf, -kInf, -3.0, -0.5};
constexpr Trapezoid kDTparNoChange{-1.0, -0.5, 0.5, 1.0};
constexpr Trapezoid kDTparDegraded{0.5, 3.0, kInf, kInf};
constexpr Trapezoid kDLibImproved{-kInf, -kInf, -3.0, 0.0};
constexpr Trapezoid kDLibNoChange{-3.0, -1.5, 1.5, 3.0};
constexpr Trapezoid kDLibDegraded{0.0, 3.0, kInf, kInf};

// [25] Fig. 5, outputs. The DLS axis is the position in the ordered portfolio,
// so it ends at mAF = 11. Delta-DLS is in portfolio steps.
constexpr Trapezoid kDlsSimple{0.0, 0.0, 0.5, 1.0};
constexpr Trapezoid kDlsModerate{0.5, 2.0, 4.0, 6.0};
constexpr Trapezoid kDlsAggressive{4.0, 7.0, 11.0, 11.0};
constexpr Trapezoid kStepLess{-4.0, -4.0, -2.0, -0.5};
constexpr Trapezoid kStepNoChange{-1.0, -0.5, 0.5, 1.0};
constexpr Trapezoid kStepMore{0.5, 2.0, 4.0, 4.0};

// [25] Sect. 3.3.3 defuzzification: cut each output at its truth value,
// integrate, normalise the areas, multiply by each output's centre, and sum.
// Returns `fallback` when no rule fired.
double defuzzify(const Trapezoid (&sets)[3], const double (&truth)[3], double fallback) {
  double total = 0.0, weighted = 0.0;
  for (int i = 0; i < 3; ++i) {
    const double area = sets[i].cut_area(truth[i]);
    total += area;
    weighted += area * sets[i].center();
  }
  return total > 0.0 ? weighted / total : fallback;
}

class ExpertSel final : public Selector {
 public:
  explicit ExpertSel(const SelectorConfig& c) : order_(in_overhead_order(c.portfolio)) {}

  Algo select(int) override {
    if (observations_ == 1) {
      position_ = clamp_position(std::lround(initial_dls()));
    } else if (observations_ > 1) {
      position_ = clamp_position(position_ + std::lround(delta_dls()));
    }
    return order_[position_];
  }

  void observe(const Observation& o) override {
    prev_tpar_ = tpar_;
    prev_lib_ = lib_;
    tpar_ = o.t_par;
    lib_ = o.lib_pct;
    ++observations_;
  }

 private:
  long clamp_position(long p) const {
    return std::clamp<long>(p, 0, static_cast<long>(order_.size()) - 1);
  }

  // [25] Table 1. MIN = and, MAX = or.
  double initial_dls() const {
    const double shrt = kTparShort.membership(tpar_), med = kTparMedium.membership(tpar_),
                 lng = kTparLong.membership(tpar_);
    const double low = kLibLow.membership(lib_), mod = kLibModerate.membership(lib_),
                 high = kLibHigh.membership(lib_);

    const double simple = std::max({std::min(shrt, low), std::min(shrt, mod), std::min(shrt, high),
                                    std::min(med, low), std::min(lng, low)});
    const double moderate = std::max({std::min(med, mod), std::min(med, high), std::min(lng, mod)});
    const double aggressive = std::min(lng, high);

    const Trapezoid sets[3] = {kDlsSimple, kDlsModerate, kDlsAggressive};
    const double truth[3] = {simple, moderate, aggressive};
    return defuzzify(sets, truth, 0.0);
  }

  // [25] Algorithm 1. Lines are numbered as in the paper.
  double delta_dls() const {
    // Percent change from the previous instance.
    const double dtpar = tpar_ > 0.0 ? (tpar_ - prev_tpar_) / tpar_ * 100.0 : 0.0;
    const double dlib = lib_ - prev_lib_;

    const double t_degraded = kDTparDegraded.membership(dtpar);
    const double t_same = kDTparNoChange.membership(dtpar);
    const double t_improved = kDTparImproved.membership(dtpar);
    const double l_degraded = kDLibDegraded.membership(dlib);
    const double l_same = kDLibNoChange.membership(dlib);
    const double l_improved = kDLibImproved.membership(dlib);
    const double lib_low = kLibLow.membership(lib_);

    const double less = std::max({std::min(t_degraded, l_improved),       // 1
                                  std::min(t_degraded, l_same),           // 2
                                  std::min(t_same, l_improved),           // 3
                                  kTparShort.membership(tpar_),           // 4
                                  std::min(t_degraded, lib_low)});        // 5
    const double same = std::max({std::min(t_same, l_same),               // 6
                                  lib_low,                                // 7
                                  t_improved});                           // 8
    const double more = std::max({std::min(kLibHigh.membership(lib_),     // 9
                                           kTparLong.membership(tpar_)),  //
                                  std::min(t_degraded, l_degraded),       // 10
                                  std::min(t_same, l_degraded)});         // 11

    const Trapezoid sets[3] = {kStepLess, kStepNoChange, kStepMore};
    const double truth[3] = {less, same, more};
    return defuzzify(sets, truth, 0.0);
  }

  std::vector<Algo> order_;
  long position_ = 0;  // starts at STATIC
  int observations_ = 0;
  double tpar_ = 0.0, lib_ = 0.0, prev_tpar_ = 0.0, prev_lib_ = 0.0;
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
