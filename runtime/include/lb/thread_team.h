// A persistent team of P threads (the OpenMP "thread pool").
// The calling (master) thread participates as tid 0.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace lb {

class ThreadTeam {
 public:
  // pin=true pins thread i to core i (Linux only; macOS has no affinity API).
  explicit ThreadTeam(int P, bool pin = false);
  ~ThreadTeam();
  ThreadTeam(const ThreadTeam&) = delete;
  ThreadTeam& operator=(const ThreadTeam&) = delete;

  int size() const { return P_; }

  // Runs fn(tid) on every thread and blocks until all have returned
  // (the implicit barrier at the end of an OpenMP parallel-for).
  void run(const std::function<void(int)>& fn);

 private:
  void worker(int tid);

  int P_;
  bool pin_;
  std::vector<std::thread> workers_;
  std::mutex m_;
  std::condition_variable cv_start_, cv_done_;
  const std::function<void(int)>* job_ = nullptr;
  uint64_t generation_ = 0;
  int remaining_ = 0;
  bool stop_ = false;
  std::exception_ptr error_;
};

}  // namespace lb
