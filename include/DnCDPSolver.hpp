#pragma once

#include "ICPUSolver.hpp"
#include "CPUController.hpp"
#include <vector>

namespace GreedyTangle {

class DnCDPSolver : public ICPUSolver {
public:
  static constexpr float MARGIN = 60.0f;
  static constexpr float WINDOW_WIDTH = 1024.0f;
  static constexpr float WINDOW_HEIGHT = 768.0f;
  static constexpr int BASE_CASE_THRESHOLD = 3;
  static constexpr float GRID_SPACING = 80.0f;
  static constexpr float BOUNDARY_MARGIN = 100.0f;

  DnCDPSolver() = default;

  CPUMove FindBestMove(std::vector<Node> nodes,
                       const std::vector<Edge> &edges) override;

  std::string GetName() const override { return "D&C + DP"; }

  int GetLastCandidatesEvaluated() const override {
    return lastCandidatesEvaluated_;
  }

private:
  struct Partition {
    std::vector<int> nodeIndices;
    float xMin, xMax, yMin, yMax;
  };

  struct DPState {
    int nodeIndex;
    Vec2 bestPosition;
    int costAtPosition;
  };

  Partition CreatePartition(const std::vector<int> &nodeIndices,
                            const std::vector<Node> &nodes);

  std::pair<Partition, Partition> SplitPartition(
      const Partition &partition, const std::vector<Node> &nodes);

  std::vector<Vec2> GenerateDPCandidates(const Partition &partition);

  std::vector<int> OrderNodesByDegree(const std::vector<int> &nodeIndices,
                                      const std::vector<Node> &nodes);

  int EvaluatePlacement(std::vector<Node> &nodes,
                        const std::vector<Edge> &edges,
                        int nodeIndex, Vec2 position);

  CPUMove SolvePartition(std::vector<Node> &nodes,
                         const std::vector<Edge> &edges,
                         const Partition &partition);

  CPUMove SolveBaseCase(std::vector<Node> &nodes,
                        const std::vector<Edge> &edges,
                        const Partition &partition);

  CPUMove SolveDP(std::vector<Node> &nodes,
                  const std::vector<Edge> &edges,
                  const Partition &partition);

  CPUMove SolveGreedyFallback(std::vector<Node> &nodes,
                              const std::vector<Edge> &edges);

  void BoundaryRefinement(std::vector<Node> &nodes,
                          const std::vector<Edge> &edges,
                          float splitX);

  std::vector<Edge> GetRelevantEdges(const std::vector<int> &nodeIndices,
                                     const std::vector<Edge> &edges);

  int lastCandidatesEvaluated_ = 0;
};

}
