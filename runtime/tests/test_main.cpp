// lb_tests: correctness checks for everybody's code. Run with `make test`.
//
// Result per check: PASS | FAIL | TODO (still NotImplemented) | INFO.
// Exit code is non-zero only if something FAILs, so TODOs never block you.
//
//   [A] every scheduling algorithm hands out each iteration exactly once,
//       for many (N, P, chunk) combinations and several consecutive loop
//       instances; triangle count is identical under every algorithm.
//   [B] expChunk vs. the values reported in the paper; expert selectors in a
//       synthetic environment (no real loops needed).
//   [C] RL selectors: explore-first covers all K*K (state, action) pairs,
//       then they settle on the best algorithm in a synthetic environment.
#include <atomic>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "lb/app.h"
#include "lb/selector.h"

using namespace lb;

namespace {

int n_fail = 0, n_todo = 0, n_pass = 0;

void report(const char* tag, const std::string& what, const std::string& status,
            const std::string& detail = "") {
  if (status == "FAIL") ++n_fail;
  if (status == "TODO") ++n_todo;
  if (status == "PASS") ++n_pass;
  std::printf("  [%s] %-44s %-4s %s\n", tag, what.c_str(), status.c_str(), detail.c_str());
}

// Runs `check` and converts NotImplemented into TODO, exceptions into FAIL.
void guarded(const char* tag, const std::string& what, const std::function<std::string()>& check,
             bool informational = false) {
  try {
    const std::string err = check();
    report(tag, what, err.empty() ? (informational ? "INFO" : "PASS") : "FAIL", err);
  } catch (const NotImplemented& e) {
    report(tag, what, "TODO", e.what());
  } catch (const std::exception& e) {
    report(tag, what, "FAIL", std::string("exception: ") + e.what());
  }
}

// ---------------------------------------------------------------- [A] coverage
std::string check_coverage(Algo a) {
  auto algo = make_algorithm(a);
  const int64_t Ns[] = {1, 7, 1000, 100003};
  const int Ps[] = {1, 3, 8};
  const int64_t chunks[] = {0, 1, 17};
  for (int P : Ps) {
    ThreadTeam team(P);
    for (int64_t N : Ns) {
      for (int64_t c : chunks) {
        std::vector<std::atomic<uint8_t>> hits(N);
        for (int inst = 0; inst < 3; ++inst) {  // several instances: adaptive state
          for (auto& h : hits) h.store(0);
          // uneven iteration cost so adaptive algorithms see something
          auto body = [&](int, int64_t b, int64_t e) {
            for (int64_t i = b; i < e; ++i) {
              if (i < 0 || i >= N) return;
              hits[i].fetch_add(1);
              volatile double x = 0;
              for (int k = 0; k < (i % 97 == 0 ? 200 : 2); ++k) x = x + k;
            }
          };
          LoopParams lp{N, P, c};
          LoopResult r = parallel_for(team, *algo, lp, body);
          for (int64_t i = 0; i < N; ++i) {
            if (hits[i].load() != 1) {
              char buf[160];
              std::snprintf(buf, sizeof buf, "N=%lld P=%d chunk=%lld inst=%d: iter %lld run %d times",
                            (long long)N, P, (long long)c, inst, (long long)i, int(hits[i].load()));
              return buf;
            }
          }
          if (r.n_chunks < 1) return "no chunks reported";
        }
      }
    }
  }
  return "";
}

// ---------------------------------------------------------------- [A] TC invariance
void check_tc_invariance() {
  auto app = make_app("tc", Size::Tiny);
  ThreadTeam team(4);
  app->setup(team);
  auto ref_algo = make_algorithm(Algo::STATIC);
  app->run_loop(0, 0, 1, team, *ref_algo, {app->iterations(0), 4, 0}, nullptr);
  const std::string ref = app->checksum();
  report("A", "triangle count reference (STATIC)", "INFO", ref + " triangles");
  for (Algo a : full_portfolio()) {
    guarded("A", std::string("tc result identical: ") + algo_name(a), [&]() -> std::string {
      auto algo = make_algorithm(a);
      for (int64_t c : {int64_t(0), int64_t(5)}) {
        app->run_loop(0, 0, 1, team, *algo, {app->iterations(0), 4, c}, nullptr);
        if (app->checksum() != ref) return "got " + app->checksum() + " expected " + ref;
      }
      return "";
    });
  }
}

// ---------------------------------------------------------------- [B] expChunk
void check_exp_chunk() {
  struct Ref { const char* what; int64_t N; int P; int64_t paper; };
  const Ref refs[] = {{"STREAM Triad, Cascade-Lake", 2000000000LL, 56, 217},
                      {"STREAM Triad, Broadwell", 2000000000LL, 20, 305},
                      {"SPHYNX, Cascade-Lake", 1000000LL, 56, 34},
                      {"SPHYNX, EPYC", 1000000LL, 128, 30}};
  for (const Ref& r : refs) {
    try {
      const int64_t v = exp_chunk(r.N, r.P);
      char buf[128];
      std::snprintf(buf, sizeof buf, "N=%lld P=%d -> ours %lld, paper %lld", (long long)r.N, r.P,
                    (long long)v, (long long)r.paper);
      if (v < 1 || v > r.N) report("B", std::string("expChunk ") + r.what, "FAIL", buf);
      else report("B", std::string("expChunk ") + r.what, v == r.paper ? "PASS" : "INFO", buf);
    } catch (const NotImplemented& e) {
      report("B", std::string("expChunk ") + r.what, "TODO", e.what());
    }
  }
}

// ---------------------------------------------------------------- synthetic environment
// Fake loop: each algorithm k has a mean time; `best` is the fastest.
// LIB is made LOWEST for a *different* (slow) algorithm, like SS in the
// paper, so the LIB reward is lured toward the wrong choice.
struct SynthEnv {
  std::vector<Algo> portfolio = full_portfolio();
  int best = 5;
  int lowest_lib = 1;
  std::mt19937_64 rng{7};
  Observation run(int t, Algo a) {
    const int k = static_cast<int>(a);
    std::normal_distribution<double> noise(0.0, 0.01);
    double mean = 1.0 + 0.15 * std::abs(k - best);
    if (k == lowest_lib) mean = 3.0;
    const double lib = (k == lowest_lib) ? 0.5 : 5.0 + 2.0 * k;
    return {t, a, mean * (1.0 + noise(rng)), lib};
  }
};

struct SelRun {
  std::vector<Algo> picks;
  bool in_portfolio = true;
};

SelRun drive(const std::string& method, const std::string& reward, int T) {
  SynthEnv env;
  SelectorConfig cfg;
  cfg.portfolio = env.portfolio;
  cfg.reward = reward;
  auto sel = make_selector(method, cfg);
  SelRun r;
  for (int t = 0; t < T; ++t) {
    const Algo a = sel->select(t);
    bool ok = false;
    for (Algo p : cfg.portfolio) ok |= (p == a);
    r.in_portfolio &= ok;
    r.picks.push_back(a);
    sel->observe(env.run(t, a));
  }
  return r;
}

std::string mode_of_tail(const SelRun& r, int tail, Algo* mode) {
  std::map<int, int> cnt;
  for (size_t i = r.picks.size() - tail; i < r.picks.size(); ++i) ++cnt[int(r.picks[i])];
  int best = -1, bc = -1;
  for (auto& kv : cnt)
    if (kv.second > bc) { bc = kv.second; best = kv.first; }
  *mode = static_cast<Algo>(best);
  char buf[96];
  std::snprintf(buf, sizeof buf, "last %d steps: %s %d%%", tail, algo_name(*mode), 100 * bc / tail);
  return buf;
}

void check_expert_selectors() {
  for (const char* m : {"randomsel", "exhaustivesel", "expertsel"}) {
    guarded("B", std::string(m) + ": picks stay in portfolio", [&]() -> std::string {
      SelRun r = drive(m, "LT", 300);
      return r.in_portfolio ? "" : "selected an algorithm outside the portfolio";
    });
  }
  guarded("B", "exhaustivesel: settles on fastest algo", [&]() -> std::string {
    SelRun r = drive("exhaustivesel", "LT", 300);
    Algo mode;
    const std::string d = mode_of_tail(r, 100, &mode);
    return static_cast<int>(mode) == SynthEnv().best ? "" : d;
  });
  guarded("B", "expertsel: behaviour (informational)", [&]() -> std::string {
    SelRun r = drive("expertsel", "LT", 300);
    Algo mode;
    std::printf("  [B]   expertsel %s\n", mode_of_tail(r, 100, &mode).c_str());
    return "";
  }, true);
}

void check_rl_selectors() {
  const int K = kNumAlgos;
  for (const char* m : {"qlearn", "sarsa"}) {
    guarded("C", std::string(m) + ": explore-first covers K*K pairs", [&]() -> std::string {
      SelRun r = drive(m, "LT", K * K + 1);
      if (!r.in_portfolio) return "selected an algorithm outside the portfolio";
      std::vector<int> seen(K * K, 0);
      for (int t = 1; t <= K * K; ++t) ++seen[int(r.picks[t - 1]) * K + int(r.picks[t])];
      int missing = 0;
      for (int s : seen) missing += (s == 0);
      return missing == 0 ? "" : std::to_string(missing) + " (s,a) pairs never visited in first K*K+1 steps";
    });
    // Informational, not PASS/FAIL: with the paper's reward (+0.01 only for a
    // NEW minimum, -2 otherwise) a correct agent is not guaranteed to lock
    // onto the synthetic best. Explain what you see in the report.
    guarded("C", std::string(m) + ",LT: behaviour (informational)", [&]() -> std::string {
      SelRun r = drive(m, "LT", 500);
      Algo mode;
      std::printf("  [C]   %s,LT  %s  (synthetic best = %s)\n", m, mode_of_tail(r, 100, &mode).c_str(),
                  algo_name(static_cast<Algo>(SynthEnv().best)));
      return "";
    }, true);
    guarded("C", std::string(m) + ",LIB: behaviour (informational)", [&]() -> std::string {
      SelRun r = drive(m, "LIB", 500);
      Algo mode;
      std::printf("  [C]   %s,LIB %s  (paper: LIB lures RL to low-imbalance, slow algos)\n", m,
                  mode_of_tail(r, 100, &mode).c_str());
      return "";
    }, true);
  }
}

}  // namespace

int main() {
  std::printf("== [A] Member A: scheduling algorithms ==\n");
  for (Algo a : full_portfolio())
    guarded("A", std::string("coverage: ") + algo_name(a), [&] { return check_coverage(a); });
  check_tc_invariance();

  std::printf("\n== [B] Member B: expChunk + expert selectors ==\n");
  check_exp_chunk();
  check_expert_selectors();

  std::printf("\n== [C] Member C: RL selectors ==\n");
  check_rl_selectors();

  std::printf("\nSUMMARY: %d PASS, %d TODO, %d FAIL\n", n_pass, n_todo, n_fail);
  return n_fail == 0 ? 0 : 1;
}
