// Scheduling-algorithm SELECTION methods (paper Sect. 3.2 - 3.5).
//
// One Selector object per loop (LB4OMP gives every loop its own id and state).
// For every loop instance (time-step) the benchmark does:
//
//   Algo a = selector.select(t);            // which algorithm to use now
//   LoopResult r = run loop with a;
//   selector.observe({t, a, r.t_par, r.lib_pct()});
//
// select() and observe() are always called by the master thread, never
// concurrently, so selectors need no locking.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "lb/algorithm.h"

namespace lb {

struct Observation {
  int timestep;
  Algo algo;       // algorithm that was used
  double t_par;    // loop time (s)  -> "LT" reward
  double lib_pct;  // Eq. 8 LIB (%)  -> RandomSel jump prob. and "LIB" reward
};

struct SelectorConfig {
  std::vector<Algo> portfolio;  // algorithms the selector may choose from
  uint64_t seed = 1;

  // RL configuration (paper Sect. 3.5, Table 2) -- Member C
  std::string reward = "LT";  // "LT" (loop time) or "LIB" (load imbalance)
  double alpha = 0.5;
  double gamma = 0.5;
  double alpha_decay = 0.05;
  double r_pos = 0.01;
  double r_neutral = -2.0;
  double r_neg = -4.0;
};

class Selector {
 public:
  virtual ~Selector() = default;
  virtual Algo select(int timestep) = 0;
  virtual void observe(const Observation& obs) = 0;
};

// method: "fixed:<ALGO>", "randomsel", "exhaustivesel", "expertsel", "qlearn", "sarsa"
std::unique_ptr<Selector> make_selector(const std::string& method, const SelectorConfig& cfg);
bool is_known_method(const std::string& method);

// Factories implemented in selectors_expert.cpp (Member B) / selectors_rl.cpp (Member C)
std::unique_ptr<Selector> make_random_sel(const SelectorConfig& cfg);
std::unique_ptr<Selector> make_exhaustive_sel(const SelectorConfig& cfg);
std::unique_ptr<Selector> make_expert_sel(const SelectorConfig& cfg);
std::unique_ptr<Selector> make_qlearn(const SelectorConfig& cfg);
std::unique_ptr<Selector> make_sarsa(const SelectorConfig& cfg);

// Expert chunk parameter (expChunk), paper Sect. 3.2 / Table 2 -- Member B.
int64_t exp_chunk(int64_t N, int P);

}  // namespace lb
