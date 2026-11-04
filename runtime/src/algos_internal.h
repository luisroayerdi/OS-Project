// Factory functions for the individual algorithms (used by algorithm_registry.cpp).
#pragma once

#include <memory>

#include "lb/algorithm.h"

namespace lb {

// algos_reference.cpp  (provided, working; use them as examples)
std::unique_ptr<SchedAlgorithm> make_static();
std::unique_ptr<SchedAlgorithm> make_ss();
std::unique_ptr<SchedAlgorithm> make_gss();

// algos_portfolio.cpp  (Member A)
std::unique_ptr<SchedAlgorithm> make_tss();
std::unique_ptr<SchedAlgorithm> make_static_steal();
std::unique_ptr<SchedAlgorithm> make_mfac2();
std::unique_ptr<SchedAlgorithm> make_awf(char variant);  // 'B', 'C', 'D', 'E'
std::unique_ptr<SchedAlgorithm> make_maf();
std::unique_ptr<SchedAlgorithm> make_auto_llvm();

}  // namespace lb
