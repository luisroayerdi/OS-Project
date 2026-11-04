// lb_bench: runs ONE experiment = one (app, selection method, chunk mode, rep)
// and writes one CSV row per loop instance. Normally you do not call it by
// hand: scripts/run_campaign.py does. Run `build/lb_bench --help`.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "lb/app.h"
#include "lb/selector.h"

using namespace lb;

namespace {

const char* kUsage = R"(lb_bench - OpenMP-style loop scheduling + algorithm selection benchmark

Usage: lb_bench [options]
  --app NAME          mandelbrot | stream | tc                      (default mandelbrot)
  --method M          fixed:<ALGO> | randomsel | exhaustivesel | expertsel | qlearn | sarsa
                                                                    (default fixed:STATIC)
  --chunk C           default | exp | <integer>                     (default default)
  --reward R          LT | LIB   (qlearn/sarsa only)                (default LT)
  --alpha A --gamma G --alpha-decay D   RL parameters               (0.5 0.5 0.05)
  --portfolio LIST    comma list of algorithms selectors may use     (default all 12)
  --threads P         (default: hardware threads)
  --timesteps T       time-steps / loop instances per loop          (default 500)
  --size S            tiny | small | medium                         (default small)
  --rep K             repetition index (also changes the RNG seed)  (default 0)
  --seed S            base RNG seed                                 (default 1)
  --system NAME       label written into the CSV                    (default unknown)
  --out FILE          CSV output (default stdout)
  --max-seconds S     stop early once this much loop time was spent (0 = never)
  --trace-chunks FILE write every chunk of loop 0 at --trace-timestep to FILE
  --trace-timestep T  (default 1)
  --pin               pin threads to cores (Linux only)
  --list              list algorithms and methods, then exit

Exit codes: 0 ok | 2 bad arguments | 3 something is not implemented yet | 1 other error
)";

struct Args {
  std::string app = "mandelbrot", method = "fixed:STATIC", chunk = "default", reward = "LT";
  std::string size = "small", out, trace, system = "unknown", portfolio;
  int threads = 0, timesteps = 500, rep = 0, trace_t = 1;
  uint64_t seed = 1;
  double alpha = 0.5, gamma = 0.5, alpha_decay = 0.05, max_seconds = 0;
  bool pin = false, list = false;
};

[[noreturn]] void usage_error(const std::string& msg) {
  std::cerr << "error: " << msg << "\n\n" << kUsage;
  std::exit(2);
}

Args parse(int argc, char** argv) {
  Args a;
  std::map<std::string, std::string*> str = {
      {"--app", &a.app},       {"--method", &a.method},       {"--chunk", &a.chunk},
      {"--reward", &a.reward}, {"--size", &a.size},           {"--out", &a.out},
      {"--trace-chunks", &a.trace}, {"--system", &a.system},  {"--portfolio", &a.portfolio}};
  std::map<std::string, int*> ints = {{"--threads", &a.threads}, {"--timesteps", &a.timesteps},
                                      {"--rep", &a.rep},         {"--trace-timestep", &a.trace_t}};
  std::map<std::string, double*> dbl = {{"--alpha", &a.alpha},
                                        {"--gamma", &a.gamma},
                                        {"--alpha-decay", &a.alpha_decay},
                                        {"--max-seconds", &a.max_seconds}};
  for (int i = 1; i < argc; ++i) {
    const std::string k = argv[i];
    if (k == "--help" || k == "-h") { std::cout << kUsage; std::exit(0); }
    if (k == "--pin") { a.pin = true; continue; }
    if (k == "--list") { a.list = true; continue; }
    if (i + 1 >= argc) usage_error("missing value for " + k);
    const std::string v = argv[++i];
    try {
      if (str.count(k)) *str[k] = v;
      else if (ints.count(k)) *ints[k] = std::stoi(v);
      else if (dbl.count(k)) *dbl[k] = std::stod(v);
      else if (k == "--seed") a.seed = std::stoull(v);
      else usage_error("unknown option " + k);
    } catch (const std::logic_error&) {
      usage_error("bad value for " + k + ": " + v);
    }
  }
  return a;
}

std::string method_family(const std::string& m) { return m.rfind("fixed:", 0) == 0 ? "fixed" : m; }
std::string method_arg(const std::string& m) {
  Algo a;
  if (m.rfind("fixed:", 0) == 0 && parse_algo(m.substr(6), &a)) return algo_name(a);
  return "";
}

int run(const Args& a) {
  if (a.list) {
    std::cout << "algorithms:";
    for (Algo x : full_portfolio()) std::cout << ' ' << algo_name(x);
    std::cout << "\nmethods: fixed:<ALGO> randomsel exhaustivesel expertsel qlearn sarsa\n";
    return 0;
  }
  Size size;
  if (!parse_size(a.size, &size)) usage_error("bad --size " + a.size);
  if (!is_known_method(a.method)) usage_error("bad --method " + a.method);
  if (a.reward != "LT" && a.reward != "LIB") usage_error("--reward must be LT or LIB");
  if (a.timesteps < 1) usage_error("--timesteps must be >= 1");
  const bool is_rl = a.method == "qlearn" || a.method == "sarsa";

  auto app = make_app(a.app, size);
  if (!app) usage_error("bad --app " + a.app);

  int P = a.threads > 0 ? a.threads : static_cast<int>(std::thread::hardware_concurrency());
  if (P < 1) P = 1;
  ThreadTeam team(P, a.pin);
  app->setup(team);

  const int L = app->num_loops();
  std::vector<int64_t> chunk_param(L, 0);
  for (int l = 0; l < L; ++l) {
    if (a.chunk == "default") chunk_param[l] = 0;
    else if (a.chunk == "exp") chunk_param[l] = exp_chunk(app->iterations(l), P);
    else {
      try { chunk_param[l] = std::stoll(a.chunk); } catch (...) { usage_error("bad --chunk"); }
    }
  }

  SelectorConfig base;
  base.portfolio = a.portfolio.empty() ? full_portfolio() : parse_portfolio(a.portfolio);
  base.reward = a.reward;
  base.alpha = a.alpha;
  base.gamma = a.gamma;
  base.alpha_decay = a.alpha_decay;

  // one selector and one set of algorithm objects per loop (LB4OMP: per loop id)
  std::vector<std::unique_ptr<Selector>> selectors;
  std::vector<std::map<Algo, std::unique_ptr<SchedAlgorithm>>> algos(L);
  for (int l = 0; l < L; ++l) {
    SelectorConfig c = base;
    c.seed = a.seed * 1000003ULL + uint64_t(a.rep) * 101ULL + uint64_t(l);
    selectors.push_back(make_selector(a.method, c));
  }

  std::ofstream fout;
  std::ostream* out = &std::cout;
  if (!a.out.empty()) {
    fout.open(a.out);
    if (!fout) { std::cerr << "cannot open " << a.out << "\n"; return 1; }
    out = &fout;
  }
  *out << "system,app,loop_id,timestep,T,method,method_arg,reward,alpha,gamma,chunk_mode,"
          "chunk_param,rep,threads,N,algo,t_par_s,lib_pct,exec_imb_pct,n_chunks\n";
  const std::string fam = method_family(a.method), marg = method_arg(a.method);
  const std::string chunk_mode = (a.chunk == "default" || a.chunk == "exp") ? a.chunk : "fixed";

  ChunkTrace trace;
  double spent = 0;
  int done_steps = 0;
  char buf[512];
  for (int t = 0; t < a.timesteps; ++t) {
    for (int l = 0; l < L; ++l) {
      const Algo choice = selectors[l]->select(t);
      auto& slot = algos[l][choice];
      if (!slot) slot = make_algorithm(choice);
      LoopParams lp{app->iterations(l), P, chunk_param[l]};
      ChunkTrace* tr = (!a.trace.empty() && l == 0 && t == a.trace_t) ? &trace : nullptr;
      const LoopResult r = app->run_loop(l, t, a.timesteps, team, *slot, lp, tr);
      selectors[l]->observe({t, choice, r.t_par, r.lib_pct()});
      spent += r.t_par;
      std::snprintf(buf, sizeof buf,
                    "%s,%s,%d,%d,%d,%s,%s,%s,%.3g,%.3g,%s,%lld,%d,%d,%lld,%s,%.9f,%.4f,%.4f,%lld\n",
                    a.system.c_str(), app->name(), l, t, a.timesteps, fam.c_str(), marg.c_str(),
                    is_rl ? a.reward.c_str() : "", is_rl ? a.alpha : 0.0, is_rl ? a.gamma : 0.0,
                    chunk_mode.c_str(), static_cast<long long>(chunk_param[l]), a.rep, P,
                    static_cast<long long>(lp.N), algo_name(choice), r.t_par, r.lib_pct(),
                    r.exec_imbalance_pct(), static_cast<long long>(r.n_chunks));
      *out << buf;
    }
    ++done_steps;
    if (a.max_seconds > 0 && spent > a.max_seconds) break;
  }
  out->flush();

  if (!a.trace.empty()) {
    std::ofstream tf(a.trace);
    tf << "system,app,loop_id,timestep,method_arg,chunk_mode,chunk_param,threads,N,chunk_id,tid,"
          "start,size,t_assign_s\n";
    for (auto& e : trace.entries)
      tf << a.system << ',' << app->name() << ",0," << a.trace_t << ',' << marg << ','
         << chunk_mode << ',' << chunk_param[0] << ',' << P << ',' << app->iterations(0) << ','
         << e.chunk_id << ',' << e.tid << ',' << e.start << ',' << e.size << ',' << e.t_assign
         << '\n';
  }

  std::cerr << "[lb_bench] " << a.app << ' ' << a.method << " chunk=" << a.chunk
            << (is_rl ? " reward=" + a.reward : "") << " P=" << P << " steps=" << done_steps << '/'
            << a.timesteps << " loop_time=" << spent << "s checksum=" << app->checksum()
            << (done_steps < a.timesteps ? " TRUNCATED(max-seconds)" : "") << '\n';
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const Args a = parse(argc, argv);
  try {
    return run(a);
  } catch (const NotImplemented& e) {
    std::cerr << "[lb_bench] NOT IMPLEMENTED: " << e.what() << '\n';
    return 3;
  } catch (const std::invalid_argument& e) {
    std::cerr << "[lb_bench] error: " << e.what() << '\n';
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "[lb_bench] error: " << e.what() << '\n';
    return 1;
  }
}
