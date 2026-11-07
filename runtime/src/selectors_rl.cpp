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
#include <stdexcept>
#include <vector>

#include "lb/selector.h"

namespace lb {

namespace {

// Shared machinery for both agents.
//
// Timeline for one loop (t = loop instance):
//   select(t)  -> a_t       state s_t = a_{t-1} (no state at t = 0)
//   observe(t) -> r_t       reward of executing a_t in state s_t
// The transition (s_t, a_t, r_t, s_{t+1} = a_t) is complete after observe(t);
// Q-Learn updates there. SARSA additionally needs a_{t+1}, so it updates in
// select(t+1) right after choosing a_{t+1}.
//
// Explore-first: an Eulerian circuit v_0 v_1 ... v_{K*K} (v_{K*K} == v_0) of
// the complete directed graph with self-loops on K nodes. Instance t plays
// v_t, so the K*K transitions (v_{t-1}, v_t), t = 1..K*K, visit every
// (state, action) pair exactly once. Learning phase = instances 0..K*K;
// greedy from K*K + 1 on (K = 12: 145 instances, i.e. 144 transitions).
class RLAgent : public Selector {
 public:
  explicit RLAgent(const SelectorConfig& c)
      : cfg_(c), K_(static_cast<int>(c.portfolio.size())), Q_(K_ * K_, 0.0), alpha_(c.alpha) {
    build_explore_plan();
  }

  void observe(const Observation& o) override {
    const double x = (cfg_.reward == "LIB") ? o.lib_pct : o.t_par;
    xmin_ = std::min(xmin_, x);
    xmax_ = std::max(xmax_, x);
    const int a = index_of(o.algo);
    if (a < 0) throw std::logic_error("RL agent observed an algorithm outside its portfolio");
    if (prev_ >= 0) {
      pending_ = {prev_, a, reward(x)};
      has_pending_ = true;
      on_transition();
    }
    prev_ = a;
  }

 protected:
  struct Transition {
    int s = 0, a = 0;
    double r = 0.0;
  };

  // Eq. 11. min/max include the current instance, so the first instance (and
  // any new best) gets r+, any new worst gets r-, everything else r0. At t = 0
  // min == max == x and r+ wins; that reward is never used (no state yet).
  double reward(double x) const {
    if (x <= xmin_) return cfg_.r_pos;
    if (x >= xmax_) return cfg_.r_neg;
    return cfg_.r_neutral;
  }

  // Hierholzer's algorithm on the complete digraph with self-loops (K*K edges).
  void build_explore_plan() {
    explore_plan_.clear();
    if (K_ <= 0) return;
    std::vector<int> next_out(K_, 0);  // next unused out-edge (u -> next_out[u]) of node u
    std::vector<int> stack{0};
    while (!stack.empty()) {
      const int u = stack.back();
      if (next_out[u] < K_) {
        stack.push_back(next_out[u]++);
      } else {
        explore_plan_.push_back(u);
        stack.pop_back();
      }
    }
    std::reverse(explore_plan_.begin(), explore_plan_.end());  // K*K + 1 nodes, first == last
  }

  // Next action index: explore plan first, then greedy argmax_a Q(s, a)
  // (ties -> lowest portfolio index). alpha decays every greedy instance.
  int choose() {
    const int t = step_++;
    if (t < static_cast<int>(explore_plan_.size())) return explore_plan_[t];
    alpha_ = std::max(kAlphaFloor, alpha_ * (1.0 - cfg_.alpha_decay));
    return greedy(prev_);
  }

  int greedy(int s) {
    int best = 0;
    for (int a = 1; a < K_; ++a)
      if (q(s, a) > q(s, best)) best = a;
    return best;
  }

  double max_q(int s) { return q(s, greedy(s)); }

  virtual void on_transition() {}

  double& q(int s, int a) { return Q_[s * K_ + a]; }
  int index_of(Algo a) const {
    for (int i = 0; i < K_; ++i)
      if (cfg_.portfolio[i] == a) return i;
    return -1;
  }

  static constexpr double kAlphaFloor = 0.01;

  SelectorConfig cfg_;
  int K_;
  std::vector<double> Q_;
  double alpha_;
  std::vector<int> explore_plan_;
  double xmin_ = std::numeric_limits<double>::infinity();
  double xmax_ = -std::numeric_limits<double>::infinity();
  int prev_ = -1;  // state = index of the algorithm used in the previous instance
  int step_ = 0;
  Transition pending_;
  bool has_pending_ = false;
};

// Eq. 10: Q(s,a) += alpha * (r + gamma * max_a' Q(s',a') - Q(s,a)), s' = a.
class QLearn final : public RLAgent {
 public:
  using RLAgent::RLAgent;
  Algo select(int /*t*/) override { return cfg_.portfolio[choose()]; }

 private:
  void on_transition() override {
    const Transition& tr = pending_;
    q(tr.s, tr.a) += alpha_ * (tr.r + cfg_.gamma * max_q(tr.a) - q(tr.s, tr.a));
    has_pending_ = false;
  }
};

// Eq. 9: Q(s,a) += alpha * (r + gamma * Q(s',a') - Q(s,a)), a' = action actually
// taken next, so the update for instance t runs in select(t+1).
class Sarsa final : public RLAgent {
 public:
  using RLAgent::RLAgent;
  Algo select(int /*t*/) override {
    const int a_next = choose();
    if (has_pending_) {
      const Transition& tr = pending_;
      q(tr.s, tr.a) += alpha_ * (tr.r + cfg_.gamma * q(tr.a, a_next) - q(tr.s, tr.a));
      has_pending_ = false;
    }
    return cfg_.portfolio[a_next];
  }
};

}  // namespace

std::unique_ptr<Selector> make_qlearn(const SelectorConfig& c) { return std::make_unique<QLearn>(c); }
std::unique_ptr<Selector> make_sarsa(const SelectorConfig& c) { return std::make_unique<Sarsa>(c); }

}  // namespace lb
