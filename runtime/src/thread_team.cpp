#include "lb/thread_team.h"

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

namespace lb {

namespace {
void pin_to_core(int core) {
#ifdef __linux__
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(core, &set);
  pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#else
  (void)core;  // macOS: no thread affinity API. Documented as a known difference.
#endif
}
}  // namespace

ThreadTeam::ThreadTeam(int P, bool pin) : P_(P < 1 ? 1 : P), pin_(pin) {
  if (pin_) pin_to_core(0);
  workers_.reserve(P_ - 1);
  for (int tid = 1; tid < P_; ++tid) workers_.emplace_back([this, tid] { worker(tid); });
}

ThreadTeam::~ThreadTeam() {
  {
    std::lock_guard<std::mutex> g(m_);
    stop_ = true;
  }
  cv_start_.notify_all();
  for (auto& w : workers_) w.join();
}

void ThreadTeam::run(const std::function<void(int)>& fn) {
  {
    std::lock_guard<std::mutex> g(m_);
    job_ = &fn;
    remaining_ = P_ - 1;
    error_ = nullptr;
    ++generation_;
  }
  cv_start_.notify_all();

  std::exception_ptr master_error;
  try {
    fn(0);
  } catch (...) {
    master_error = std::current_exception();
  }

  std::unique_lock<std::mutex> lk(m_);
  cv_done_.wait(lk, [this] { return remaining_ == 0; });
  job_ = nullptr;
  if (master_error) std::rethrow_exception(master_error);
  if (error_) std::rethrow_exception(error_);
}

void ThreadTeam::worker(int tid) {
  if (pin_) pin_to_core(tid);
  uint64_t seen = 0;
  std::unique_lock<std::mutex> lk(m_);
  for (;;) {
    cv_start_.wait(lk, [&] { return stop_ || generation_ != seen; });
    if (stop_) return;
    seen = generation_;
    const auto* job = job_;
    lk.unlock();
    std::exception_ptr err;
    try {
      (*job)(tid);
    } catch (...) {
      err = std::current_exception();
    }
    lk.lock();
    if (err && !error_) error_ = err;
    if (--remaining_ == 0) cv_done_.notify_one();
  }
}

}  // namespace lb
