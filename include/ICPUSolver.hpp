#pragma once

#include "GraphData.hpp"
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
};

}
