#pragma once

#include "GraphData.hpp"
#include <atomic>
#include <string>
#include <vector>

namespace GreedyTangle {

struct CPUMove;

enum class SolverMode {
  GREEDY,
  DIVIDE_AND_CONQUER_DP,
  BACKTRACKING
};


class ICPUSolver {
public:
  virtual ~ICPUSolver() = default;


  virtual CPUMove FindBestMove(std::vector<Node> nodes,
                               const std::vector<Edge> &edges) = 0;


  virtual std::string GetName() const = 0;


  virtual int GetLastCandidatesEvaluated() const = 0;

  // Cancellation support: set a flag that solvers check in their hot loops
  void SetCancelFlag(std::atomic<bool> *flag) { cancelFlag_ = flag; }

protected:
  bool IsCancelled() const {
    return cancelFlag_ && cancelFlag_->load(std::memory_order_relaxed);
  }

  std::atomic<bool> *cancelFlag_ = nullptr;
};

}
