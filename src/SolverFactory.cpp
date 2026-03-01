#include "SolverFactory.hpp"
#include "GreedySolver.hpp"
#include "DnCDPSolver.hpp"
#include "BacktrackingSolver.hpp"

namespace GreedyTangle {

std::unique_ptr<ICPUSolver> CreateSolver(SolverMode mode) {
  switch (mode) {
  case SolverMode::GREEDY:
    return std::make_unique<GreedySolver>();
  case SolverMode::DIVIDE_AND_CONQUER_DP:
    return std::make_unique<DnCDPSolver>();
  case SolverMode::BACKTRACKING:
    return std::make_unique<BacktrackingSolver>();
  default:
    return std::make_unique<GreedySolver>();
  }
}

}
