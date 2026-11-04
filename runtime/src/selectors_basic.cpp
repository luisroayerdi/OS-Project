// Selector registry + the trivial "fixed" selector (always the same algorithm).
// "fixed:<ALGO>" runs are what the paper calls the individual scheduling
// algorithms in Fig. 6; the Oracle is computed offline from them
// (scripts/aggregate.py).
#include <stdexcept>

#include "lb/selector.h"

namespace lb {

namespace {
class Fixed final : public Selector {
 public:
  explicit Fixed(Algo a) : a_(a) {}
  Algo select(int) override { return a_; }
  void observe(const Observation&) override {}

 private:
  Algo a_;
};
}  // namespace

bool is_known_method(const std::string& m) {
  if (m.rfind("fixed:", 0) == 0) {
    Algo a;
    return parse_algo(m.substr(6), &a);
  }
  return m == "randomsel" || m == "exhaustivesel" || m == "expertsel" || m == "qlearn" ||
         m == "sarsa";
}

std::unique_ptr<Selector> make_selector(const std::string& m, const SelectorConfig& cfg) {
  if (m.rfind("fixed:", 0) == 0) {
    Algo a;
    if (!parse_algo(m.substr(6), &a)) throw std::invalid_argument("unknown algorithm: " + m);
    return std::make_unique<Fixed>(a);
  }
  if (cfg.portfolio.empty()) throw std::invalid_argument("selector needs a non-empty portfolio");
  if (m == "randomsel") return make_random_sel(cfg);
  if (m == "exhaustivesel") return make_exhaustive_sel(cfg);
  if (m == "expertsel") return make_expert_sel(cfg);
  if (m == "qlearn") return make_qlearn(cfg);
  if (m == "sarsa") return make_sarsa(cfg);
  throw std::invalid_argument("unknown method: " + m);
}

}  // namespace lb
