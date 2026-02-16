#include "GreedySolver.hpp"
#include "MathUtils.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>

namespace GreedyTangle {

CPUMove GreedySolver::FindBestMove(std::vector<Node> nodes,
                                   const std::vector<Edge> &edges) {
  auto start_time = std::chrono::steady_clock::now();

  int current_intersections = CountIntersections(nodes, edges);
  lastCandidatesEvaluated_ = 0;

  CPUMove best_move;
  best_move.intersections_before = current_intersections;
  int best_reduction = 0;

  if (current_intersections == 0) {
    return best_move;
  }

  for (size_t node_idx = 0; node_idx < nodes.size(); ++node_idx) {
    Vec2 original_position = nodes[node_idx].position;

    std::vector<Vec2> candidates =
        GenerateCandidatePositions(static_cast<int>(node_idx), nodes);

    for (const Vec2 &candidate : candidates) {
      ++lastCandidatesEvaluated_;

      int new_intersections = CountIntersectionsWithMove(
          nodes, edges, static_cast<int>(node_idx), candidate);

      int reduction = current_intersections - new_intersections;

      if (reduction > best_reduction) {
        best_reduction = reduction;
        best_move.node_id = static_cast<int>(node_idx);
        best_move.from_position = original_position;
        best_move.to_position = candidate;
        best_move.intersections_after = new_intersections;
        best_move.intersection_reduction = reduction;
      }
    }
  }

  auto end_time = std::chrono::steady_clock::now();
  best_move.computation_time_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(end_time -
                                                            start_time)
          .count();

  if (best_move.isValid()) {
    std::cout << "[Greedy] Found move: Node " << best_move.node_id << " -> ("
              << best_move.to_position.x << ", " << best_move.to_position.y
              << ") reduction=" << best_move.intersection_reduction
              << " time=" << best_move.computation_time_ms << "ms" << std::endl;
  } else {
    std::cout << "[Greedy] No valid move found (stuck in local minimum)"
              << std::endl;
  }

  return best_move;
}

}
