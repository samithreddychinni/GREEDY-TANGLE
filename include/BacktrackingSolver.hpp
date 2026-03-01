#pragma once

#include "ICPUSolver.hpp"
#include "CPUController.hpp"
#include <vector>

namespace GreedyTangle {

class BacktrackingSolver : public ICPUSolver {
public:
  static constexpr float GRID_SPACING = 80.0f;
  static constexpr float MARGIN = 60.0f;
  static constexpr float WINDOW_WIDTH = 1024.0f;
  static constexpr float WINDOW_HEIGHT = 768.0f;
  static constexpr int MAX_DEPTH = 3;

  BacktrackingSolver() = default;

  CPUMove FindBestMove(std::vector<Node> nodes,
                       const std::vector<Edge> &edges) override;

  std::string GetName() const override { return "Backtracking"; }

  int GetLastCandidatesEvaluated() const override {
    return lastCandidatesEvaluated_;
  }

private:
  struct MoveCandidate {
    int node_id;
    Vec2 position;
  };

  std::vector<Vec2> GenerateCandidatePositions(int node_id,
                                               const std::vector<Node> &nodes);

  void Backtrack(std::vector<Node> &nodes,
                 const std::vector<Edge> &edges,
                 int depth,
                 int currentIntersections,
                 int &bestIntersections,
                 MoveCandidate &bestFirstMove);

  int lastCandidatesEvaluated_ = 0;
};

}
