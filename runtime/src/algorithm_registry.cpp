#include <algorithm>
#include <cctype>
#include <sstream>

#include "algos_internal.h"
#include "lb/algorithm.h"

namespace lb {

namespace {
const char* kNames[kNumAlgos] = {"STATIC", "SS",    "GSS",   "TSS",   "StaticSteal", "mFAC2",
                                 "AWF-B",  "AWF-C", "AWF-D", "AWF-E", "mAF",         "AutoLLVM"};

std::string norm(std::string s) {
  std::string out;
  for (char c : s)
    if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(c));
  return out;
}
}  // namespace

const char* algo_name(Algo a) { return kNames[static_cast<int>(a)]; }

bool parse_algo(const std::string& s, Algo* out) {
  const std::string n = norm(s);
  for (int i = 0; i < kNumAlgos; ++i) {
    if (norm(kNames[i]) == n) {
      *out = static_cast<Algo>(i);
      return true;
    }
  }
  // aliases
  if (n == "dynamic") { *out = Algo::SS; return true; }
  if (n == "guided") { *out = Algo::GSS; return true; }
  if (n == "auto") { *out = Algo::AUTO_LLVM; return true; }
  if (n == "fac2") { *out = Algo::MFAC2; return true; }
  if (n == "af") { *out = Algo::MAF; return true; }
  return false;
}

std::vector<Algo> full_portfolio() {
  std::vector<Algo> v;
  for (int i = 0; i < kNumAlgos; ++i) v.push_back(static_cast<Algo>(i));
  return v;
}

std::vector<Algo> parse_portfolio(const std::string& csv) {
  std::vector<Algo> v;
  std::stringstream ss(csv);
  std::string tok;
  while (std::getline(ss, tok, ',')) {
    Algo a;
    if (!parse_algo(tok, &a)) throw std::invalid_argument("unknown algorithm in portfolio: " + tok);
    v.push_back(a);
  }
  if (v.empty()) throw std::invalid_argument("empty portfolio");
  return v;
}

std::unique_ptr<SchedAlgorithm> make_algorithm(Algo a) {
  switch (a) {
    case Algo::STATIC: return make_static();
    case Algo::SS: return make_ss();
    case Algo::GSS: return make_gss();
    case Algo::TSS: return make_tss();
    case Algo::STATIC_STEAL: return make_static_steal();
    case Algo::MFAC2: return make_mfac2();
    case Algo::AWF_B: return make_awf('B');
    case Algo::AWF_C: return make_awf('C');
    case Algo::AWF_D: return make_awf('D');
    case Algo::AWF_E: return make_awf('E');
    case Algo::MAF: return make_maf();
    case Algo::AUTO_LLVM: return make_auto_llvm();
  }
  throw std::invalid_argument("bad Algo");
}

}  // namespace lb
