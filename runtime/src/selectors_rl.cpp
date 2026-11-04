// =============================================================================
//  OWNER: Member C  (reinforcement-learning selection)
// =============================================================================
// Implement Q-Learn and SARSA selection (paper Sect. 3.4 - 3.5, Table 2).
//
// Model (paper Sect. 3.5):
//   * state  = algorithm used in the previous loop instance  (K states)
//   * action = algorithm to use in the next loop instance     (K actions)
//     -> Q table K x K, all zeros (K = 12 -> 144 entries)
//   * reward Eq. 11, x = loop time (reward "LT") or LIB % (reward "LIB"):
//       r+ = 0.01   if x <= min_t(x)       (best seen so far)
//       r- = -4.0   if x >= max_t(x)       (worst seen so far)
//       r0 = -2.0   otherwise
//     min/max are over all loop instances executed so far (this loop only).
//   * SARSA  (Eq. 9):  Q(s,a) += alpha * (r + gamma * Q(s',a')        - Q(s,a))
//     Q-Learn (Eq. 10): Q(s,a) += alpha * (r + gamma * max_a' Q(s',a') - Q(s,a))
//   * policy: explore-first. The first K*K instances visit EVERY (s, a) pair
//     once ("all possible orders"); afterwards act greedily: a = argmax Q(s,.)
//   * alpha = gamma = 0.5 by default; after the learning phase alpha decays by
//     alpha_decay (0.05) every instance. Is that subtractive or multiplicative?
//     Check the RL4OMP code (paper ref [28]) and document your choice.
//
// Things to work out (good slide material):
//   * explore-first order: a sequence of K*K actions in which every ordered
//     pair (previous algo, next algo) appears exactly once -> an Eulerian
//     circuit of the complete directed graph with self-loops on K nodes.
//   * SARSA needs a' (the NEXT action) before it can update Q(s,a), so the
//     update for instance t happens inside select(t+1).
//   * reward edge case at t = 0 (min == max).
//
// You can develop WITHOUT Member A's algorithms: `make test` runs your agents
// in a synthetic environment and checks the explore-first coverage.
// =============================================================================
#include <algorithm>
#include <limits>
#include <vector>

#include "lb/selector.h"

namespace lb {

namespace {

// Shared machinery for both agents. Fill in the TODOs; keep the interface.
class RLAgent : public Selector {
 public:
  explicit RLAgent(const SelectorConfig& c)
      : cfg_(c), K_(static_cast<int>(c.portfolio.size())), Q_(K_ * K_, 0.0), alpha_(c.alpha) {}

  void observe(const Observation& o) override {
    (void)o;
    // TODO: x = (cfg_.reward == "LIB") ? o.lib_pct : o.t_par;
    //       update xmin_/xmax_, compute reward(x), remember it for the update.
  }

 protected:
  // Eq. 11
  double reward(double x) const {
    (void)x;
    throw NotImplemented("RL reward (Member C)");
  }

  // Build explore_plan_: K*K action indices covering every (s,a) pair once.
  void build_explore_plan() { throw NotImplemented("RL explore-first plan (Member C)"); }

  double& q(int s, int a) { return Q_[s * K_ + a]; }
  int index_of(Algo a) const {
    for (int i = 0; i < K_; ++i)
      if (cfg_.portfolio[i] == a) return i;
    return -1;
  }

  SelectorConfig cfg_;
  int K_;
  std::vector<double> Q_;
  double alpha_;
  std::vector<int> explore_plan_;
  double xmin_ = std::numeric_limits<double>::infinity();
  double xmax_ = -std::numeric_limits<double>::infinity();
};

class QLearn final : public RLAgent {
 public:
  using RLAgent::RLAgent;
  Algo select(int t) override {
    (void)t;
    throw NotImplemented("Q-Learn (Member C)");
  }
};

class Sarsa final : public RLAgent {
 public:
  using RLAgent::RLAgent;
  Algo select(int t) override {
    (void)t;
    throw NotImplemented("SARSA (Member C)");
  }
};

}  // namespace

std::unique_ptr<Selector> make_qlearn(const SelectorConfig& c) { return std::make_unique<QLearn>(c); }
std::unique_ptr<Selector> make_sarsa(const SelectorConfig& c) { return std::make_unique<Sarsa>(c); }

}  // namespace lb
