// parallel_for: executes one loop instance with a given scheduling algorithm
// and measures what the paper measures (Table 2, "Metrics"):
//   - T_loop_par: parallel loop execution time
//   - percent load imbalance LIB (Eq. 8), used by RandomSel / RL-LIB reward
//   - execution imbalance (Table 2 metric)
#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <numeric>
#include <vector>

#include "lb/algorithm.h"
#include "lb/common.h"
#include "lb/thread_team.h"

namespace lb {

struct LoopResult {
  double t_par = 0.0;          // seconds, loop start -> last thread done
  std::vector<double> finish;  // per-thread finish time relative to loop start
  int64_t n_chunks = 0;        // scheduling rounds (work requests that got work)

  double max_finish() const {
    return finish.empty() ? 0.0 : *std::max_element(finish.begin(), finish.end());
  }
  double mean_finish() const {
    return finish.empty() ? 0.0
                          : std::accumulate(finish.begin(), finish.end(), 0.0) / finish.size();
  }
  // Eq. 8: LIB = (1 - mean/max) * 100
  double lib_pct() const {
    const double mx = max_finish();
    return mx > 0 ? (1.0 - mean_finish() / mx) * 100.0 : 0.0;
  }
  // Table 2: (max - mean)/max * P/(P-1) * 100
  double exec_imbalance_pct() const {
    const double mx = max_finish();
    const double P = static_cast<double>(finish.size());
    if (mx <= 0 || P < 2) return 0.0;
    return (mx - mean_finish()) / mx * P / (P - 1.0) * 100.0;
  }
};

// Optional record of every chunk handed out (used for paper Fig. 1 / Fig. 2).
struct ChunkTrace {
  struct Entry {
    int64_t chunk_id;
    int tid;
    int64_t start;
    int64_t size;
    double t_assign;  // seconds since loop start
  };
  std::mutex m;
  std::vector<Entry> entries;
};

// body(tid, begin, end) must execute iterations [begin, end).
template <class Body>
LoopResult parallel_for(ThreadTeam& team, SchedAlgorithm& algo, const LoopParams& lp,
                        Body&& body, ChunkTrace* trace = nullptr) {
  algo.begin(lp);
  const int P = team.size();
  const bool timed = algo.needs_chunk_timing();

  LoopResult r;
  r.finish.assign(P, 0.0);
  std::vector<Padded<int64_t>> chunks(P);

  const double t0 = now_s();
  team.run([&](int tid) {
    int64_t start = 0, size = 0;
    int64_t my_chunks = 0;
    while (algo.next_chunk(tid, &start, &size)) {
      if (trace) {
        std::lock_guard<std::mutex> g(trace->m);
        trace->entries.push_back({static_cast<int64_t>(trace->entries.size()), tid, start, size,
                                  now_s() - t0});
      }
      if (timed) {
        const double c0 = now_s();
        body(tid, start, start + size);
        algo.chunk_done(tid, size, now_s() - c0);
      } else {
        body(tid, start, start + size);
      }
      ++my_chunks;
    }
    chunks[tid].v = my_chunks;
    r.finish[tid] = now_s() - t0;
  });
  r.t_par = now_s() - t0;
  for (auto& c : chunks) r.n_chunks += c.v;
  algo.end(r.finish);
  return r;
}

}  // namespace lb
