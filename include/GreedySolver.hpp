#pragma once

#include "ICPUSolver.hpp"
#include "CPUController.hpp"
#include <vector>

namespace GreedyTangle {


class GreedySolver : public ICPUSolver {
public:
  static constexpr float GRID_SPACING = 80.0f;
  static constexpr float MARGIN = 60.0f;
  static constexpr float WINDOW_WIDTH = 1024.0f;
  static constexpr float WINDOW_HEIGHT = 768.0f;

  GreedySolver() = default;

  CPUMove FindBestMove(std::vector<Node> nodes,
                       const std::vector<Edge> &edges) override;

  std::string GetName() const override { return "Greedy"; }

  int GetLastCandidatesEvaluated() const override {
    return lastCandidatesEvaluated_;
  }

private:
  std::vector<Vec2> GenerateCandidatePositions(int node_id,
                                               const std::vector<Node> &nodes);

  int CountIntersectionsWithMove(std::vector<Node> nodes,
                                 const std::vector<Edge> &edges, int node_id,
                                 Vec2 new_position);

  int lastCandidatesEvaluated_ = 0;
};

}
