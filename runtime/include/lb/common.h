// Shared utilities for the whole runtime.
#pragma once

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace lb {

using Clock = std::chrono::steady_clock;

// Monotonic wall-clock time in seconds.
inline double now_s() {
  return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

// Thrown by every TODO stub. The benchmark exits with code 3 and the campaign
// runner records the job as "not_implemented" instead of crashing.
struct NotImplemented : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Avoid false sharing between per-thread counters.
constexpr int kCacheLine = 64;

template <class T>
struct alignas(kCacheLine) Padded {
  T v{};
};

}  // namespace lb
