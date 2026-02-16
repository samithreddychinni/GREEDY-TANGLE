#include "SolverFactory.hpp"
#include "GreedySolver.hpp"

namespace GreedyTangle {

std::unique_ptr<ICPUSolver> CreateSolver(SolverMode mode) {
  switch (mode) {
  case SolverMode::GREEDY:
    return std::make_unique<GreedySolver>();
  case SolverMode::DIVIDE_AND_CONQUER_DP:
    return std::make_unique<GreedySolver>();
  default:
    return std::make_unique<GreedySolver>();
  }
}

}
