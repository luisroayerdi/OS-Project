// Loop scheduling algorithms (the "portfolio", paper Sect. 2 and 3.1).
//
// In LB4OMP these live inside the LLVM OpenMP runtime (kmp_dispatch.cpp).
// Here every algorithm is a small class that hands out chunks of loop
// iterations to threads. The runtime (parallel_for.h) calls it like this:
//
//   master:  algo.begin(params)                      once per loop instance
//   each of the P threads, concurrently:
//            while (algo.next_chunk(tid, &start, &size)) {
//              run iterations [start, start+size)
//              algo.chunk_done(tid, size, seconds)   only if needs_chunk_timing()
//            }
//   master:  algo.end(finish_times)                  once all threads are done
//
// ONE OBJECT PER LOOP lives for the whole run (all time-steps), so adaptive
// algorithms (AWF-*, mAF) can carry state such as weights or mu/sigma
// across loop instances, as LB4OMP does.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "lb/common.h"

namespace lb {

// Same order as in the paper (Table 2 / Fig. 6 x-axis).
enum class Algo : int {
  STATIC = 0,
  SS,
  GSS,
  TSS,
  STATIC_STEAL,
  MFAC2,
  AWF_B,
  AWF_C,
  AWF_D,
  AWF_E,
  MAF,
  AUTO_LLVM,
};
constexpr int kNumAlgos = 12;

// CSV/CLI token, e.g. "STATIC", "StaticSteal", "AWF-B", "AutoLLVM".
const char* algo_name(Algo a);
bool parse_algo(const std::string& s, Algo* out);  // case-insensitive
std::vector<Algo> full_portfolio();
std::vector<Algo> parse_portfolio(const std::string& csv);  // "STATIC,SS,GSS"

struct LoopParams {
  int64_t N = 0;      // number of loop iterations
  int P = 1;          // number of threads
  int64_t chunk = 0;  // chunk parameter. 0 = "default" (OpenMP default), >0 = user/expChunk.

  // Paper Sect. 2: for every algorithm except STATIC and SS the chunk parameter
  // acts as a lower bound: chunk_delivered = max(chunk_algorithm, chunk_param).
  int64_t min_chunk() const { return chunk > 0 ? chunk : 1; }
};

class SchedAlgorithm {
 public:
  virtual ~SchedAlgorithm() = default;

  // Master thread, workers idle. Reset per-instance state (remaining
  // iterations, counters...). Keep cross-instance state (weights) untouched.
  virtual void begin(const LoopParams& p) = 0;

  // Called CONCURRENTLY by all P threads. Writes the next chunk
  // [*start, *start + *size) for thread `tid`. Returns false when this thread
  // should stop (no work left). Must hand out every iteration exactly once.
  virtual bool next_chunk(int tid, int64_t* start, int64_t* size) = 0;

  // Return true if you need chunk_done() callbacks (adaptive algorithms).
  virtual bool needs_chunk_timing() const { return false; }

  // Called by thread `tid` after it finished a chunk. Concurrent across tids.
  virtual void chunk_done(int tid, int64_t size, double seconds) {
    (void)tid; (void)size; (void)seconds;
  }

  // Master thread after the loop instance. finish_s[tid] = time at which
  // thread tid ran out of work, relative to the loop start.
  virtual void end(const std::vector<double>& finish_s) { (void)finish_s; }
};

// Throws NotImplemented for algorithms that are still TODO.
std::unique_ptr<SchedAlgorithm> make_algorithm(Algo a);

}  // namespace lb
