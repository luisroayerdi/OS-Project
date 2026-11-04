// The three benchmark applications (scaled-down versions of paper Sect. 4.1).
// Provided and working; nobody needs to edit this file. If you change problem
// sizes, change them in size_preset() only and say so in the report.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "lb/app.h"

namespace lb {

bool parse_size(const std::string& s, Size* out) {
  if (s == "tiny") { *out = Size::Tiny; return true; }
  if (s == "small") { *out = Size::Small; return true; }
  if (s == "medium") { *out = Size::Medium; return true; }
  return false;
}

namespace {

// Problem sizes. Paper values (Table 2) are far beyond a laptop:
//   Mandelbrot N = 262,144 per loop | STREAM N = 2e9 | TC Kronecker scale 20.
struct Preset {
  int mandel_w, mandel_max_iter;
  int64_t stream_n;
  int tc_scale;
};
Preset size_preset(Size s) {
  switch (s) {
    case Size::Tiny: return {64, 64, int64_t(1) << 16, 10};
    case Size::Small: return {256, 256, int64_t(1) << 22, 14};
    case Size::Medium: return {512, 512, int64_t(1) << 24, 17};  // 512^2 = paper's N
  }
  return {256, 256, int64_t(1) << 22, 14};
}

// ============================================================ Mandelbrot
// 3 loops inside one time-step loop (paper: L0 constant imbalance,
// L1 increasing, L2 decreasing). Each loop iteration = one pixel, row-major,
// so STATIC gives each thread a horizontal band -> imbalance.
//   L0: fixed full view           -> constant workload
//   L1: zooms INTO the seahorse valley over the T time-steps -> more pixels
//       near the boundary, work and imbalance grow
//   L2: the same zoom played backwards -> decreasing
class Mandelbrot final : public App {
 public:
  Mandelbrot(int w, int max_iter) : W_(w), H_(w), max_iter_(max_iter) {}
  const char* name() const override { return "mandelbrot"; }
  int num_loops() const override { return 3; }
  int64_t iterations(int) const override { return int64_t(W_) * H_; }
  void setup(ThreadTeam&) override {
    for (auto& im : img_) im.assign(size_t(W_) * H_, 0);
  }

  LoopResult run_loop(int loop, int t, int T, ThreadTeam& team, SchedAlgorithm& algo,
                      const LoopParams& lp, ChunkTrace* trace) override {
    // view
    const double cx_zoom = -0.743643887037151, cy_zoom = 0.131825904205330;
    double cx = -0.5, cy = 0.0, width = 3.0;
    if (loop > 0) {
      const double progress = T > 1 ? double(loop == 1 ? t : (T - 1 - t)) / (T - 1) : 0.0;
      width = 3.0 * std::pow(1e-3, progress);  // 3.0 -> 0.003
      cx = -0.5 + (cx_zoom + 0.5) * std::min(1.0, progress * 4.0);
      cy = cy_zoom * std::min(1.0, progress * 4.0);
    }
    const double scale = width / W_;
    const double x0 = cx - width / 2, y0 = cy - width / 2;
    int* out = img_[loop].data();
    const int W = W_, max_iter = max_iter_;
    auto body = [=](int, int64_t b, int64_t e) {
      for (int64_t i = b; i < e; ++i) {
        const int64_t row = i / W, col = i % W;
        const double cr = x0 + double(col) * scale, ci = y0 + double(row) * scale;
        double zr = 0, zi = 0;
        int k = 0;
        while (k < max_iter && zr * zr + zi * zi <= 4.0) {
          const double tmp = zr * zr - zi * zi + cr;
          zi = 2 * zr * zi + ci;
          zr = tmp;
          ++k;
        }
        out[i] = k;
      }
    };
    return parallel_for(team, algo, lp, body, trace);
  }

  std::string checksum() const override {
    long long s = 0;
    for (auto& im : img_) s += std::accumulate(im.begin(), im.end(), 0LL);
    return std::to_string(s);
  }

 private:
  int W_, H_, max_iter_;
  std::vector<int> img_[3];
};

// ============================================================ STREAM Triad
// a[i] = b[i] + s * c[i]. Memory-bound, perfectly balanced. The worst case
// for dynamic scheduling (overhead + lost locality), paper Sect. 4.1.
class StreamTriad final : public App {
 public:
  explicit StreamTriad(int64_t n) : N_(n) {}
  const char* name() const override { return "stream"; }
  int num_loops() const override { return 1; }
  int64_t iterations(int) const override { return N_; }

  void setup(ThreadTeam& team) override {
    a_.reset(new double[N_]);
    b_.reset(new double[N_]);
    c_.reset(new double[N_]);
    // first touch with a STATIC partition (matters on NUMA Linux boxes)
    double *a = a_.get(), *b = b_.get(), *c = c_.get();
    const int64_t N = N_;
    const int P = team.size();
    team.run([=](int tid) {
      const int64_t lo = N * tid / P, hi = N * (tid + 1) / P;
      for (int64_t i = lo; i < hi; ++i) {
        a[i] = 0.0;
        b[i] = 1.0;
        c[i] = 2.0;
      }
    });
  }

  LoopResult run_loop(int, int, int, ThreadTeam& team, SchedAlgorithm& algo, const LoopParams& lp,
                      ChunkTrace* trace) override {
    double* __restrict a = a_.get();
    const double* __restrict b = b_.get();
    const double* __restrict c = c_.get();
    const double s = 3.0;
    auto body = [=](int, int64_t lo, int64_t hi) {
      for (int64_t i = lo; i < hi; ++i) a[i] = b[i] + s * c[i];
    };
    return parallel_for(team, algo, lp, body, trace);
  }

  std::string checksum() const override {
    double s = 0;
    for (int64_t i = 0; i < N_; i += 4096) s += a_[i];
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.1f", s);
    return buf;
  }

 private:
  int64_t N_;
  std::unique_ptr<double[]> a_, b_, c_;
};

// ============================================================ Triangle Counting
// GAP-style ordered triangle counting (paper ref [6]) on an R-MAT /
// Kronecker graph (Graph500 parameters A=.57 B=.19 C=.19, edge factor 16).
// One loop iteration = one vertex; vertex degrees are extremely skewed and
// high-degree vertices cluster at low ids -> strong imbalance with STATIC.
class TriangleCount final : public App {
 public:
  explicit TriangleCount(int scale) : scale_(scale) {}
  const char* name() const override { return "tc"; }
  int num_loops() const override { return 1; }
  int64_t iterations(int) const override { return int64_t(1) << scale_; }

  void setup(ThreadTeam& team) override {
    build_graph();
    counts_.assign(team.size(), {});
  }

  LoopResult run_loop(int, int, int, ThreadTeam& team, SchedAlgorithm& algo, const LoopParams& lp,
                      ChunkTrace* trace) override {
    for (auto& c : counts_) c.v = 0;
    const int64_t* off = offsets_.data();
    const int32_t* nbr = neighbors_.data();
    Padded<long long>* counts = counts_.data();
    auto body = [=](int tid, int64_t lo, int64_t hi) {
      long long local = 0;
      for (int64_t u = lo; u < hi; ++u) {
        for (int64_t i = off[u]; i < off[u + 1]; ++i) {
          const int32_t v = nbr[i];
          if (v > u) break;
          int64_t it = off[u];
          for (int64_t j = off[v]; j < off[v + 1]; ++j) {
            const int32_t w = nbr[j];
            if (w > v) break;
            while (nbr[it] < w) ++it;
            if (nbr[it] == w) ++local;
          }
        }
      }
      counts[tid].v += local;
    };
    LoopResult r = parallel_for(team, algo, lp, body, trace);
    triangles_ = 0;
    for (auto& c : counts_) triangles_ += c.v;
    return r;
  }

  std::string checksum() const override { return std::to_string(triangles_); }

 private:
  void build_graph() {
    const int64_t n = int64_t(1) << scale_;
    const int64_t m = n * 16;
    std::mt19937_64 rng(20250727);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    const double A = 0.57, B = 0.19, C = 0.19;
    std::vector<std::pair<int32_t, int32_t>> edges;
    edges.reserve(size_t(2 * m));
    for (int64_t e = 0; e < m; ++e) {
      int64_t u = 0, v = 0;
      for (int bit = 0; bit < scale_; ++bit) {
        const double r = U(rng);
        const int ub = (r > A + B) ? 1 : 0;                       // C or D quadrant
        const int vb = (r > A && r <= A + B) || (r > A + B + C);  // B or D quadrant
        u = (u << 1) | ub;
        v = (v << 1) | vb;
      }
      if (u == v) continue;
      edges.emplace_back(int32_t(u), int32_t(v));
      edges.emplace_back(int32_t(v), int32_t(u));
    }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    offsets_.assign(n + 1, 0);
    for (auto& e : edges) ++offsets_[e.first + 1];
    for (int64_t i = 0; i < n; ++i) offsets_[i + 1] += offsets_[i];
    neighbors_.resize(edges.size());
    for (size_t i = 0; i < edges.size(); ++i) neighbors_[i] = edges[i].second;  // sorted
  }

  int scale_;
  std::vector<int64_t> offsets_;
  std::vector<int32_t> neighbors_;
  std::vector<Padded<long long>> counts_;
  long long triangles_ = 0;
};

}  // namespace

std::unique_ptr<App> make_app(const std::string& name, Size size) {
  const Preset p = size_preset(size);
  if (name == "mandelbrot") return std::make_unique<Mandelbrot>(p.mandel_w, p.mandel_max_iter);
  if (name == "stream") return std::make_unique<StreamTriad>(p.stream_n);
  if (name == "tc") return std::make_unique<TriangleCount>(p.tc_scale);
  return nullptr;
}

}  // namespace lb
