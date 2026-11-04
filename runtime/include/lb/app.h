// Benchmark applications (paper Sect. 4.1). We keep 3 of the 6:
//   mandelbrot : compute-bound, 3 loops (constant / increasing / decreasing imbalance)
//   stream     : STREAM Triad, memory-bound, perfectly regular
//   tc         : Triangle Counting on a Kronecker (R-MAT) graph, highly imbalanced
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "lb/parallel_for.h"

namespace lb {

enum class Size { Tiny, Small, Medium };
bool parse_size(const std::string& s, Size* out);

class App {
 public:
  virtual ~App() = default;
  virtual const char* name() const = 0;
  virtual int num_loops() const = 0;
  virtual int64_t iterations(int loop) const = 0;
  // Allocate and initialise data (first-touch with the team). Not timed.
  virtual void setup(ThreadTeam& team) = 0;
  // Execute loop `loop` for time-step t (of T) with the given scheduler.
  virtual LoopResult run_loop(int loop, int t, int T, ThreadTeam& team, SchedAlgorithm& algo,
                              const LoopParams& lp, ChunkTrace* trace) = 0;
  // Something derived from the output so the compiler cannot drop the work,
  // and so you can check that results do not depend on the schedule.
  virtual std::string checksum() const = 0;
};

std::unique_ptr<App> make_app(const std::string& name, Size size);

}  // namespace lb
