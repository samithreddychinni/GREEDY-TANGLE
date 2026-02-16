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

std::vector<Vec2>
GreedySolver::GenerateCandidatePositions(int node_id,
                                         const std::vector<Node> &nodes) {
  std::vector<Vec2> candidates;

  for (float x = MARGIN; x <= WINDOW_WIDTH - MARGIN; x += GRID_SPACING) {
    for (float y = MARGIN; y <= WINDOW_HEIGHT - MARGIN; y += GRID_SPACING) {
      candidates.emplace_back(x, y);
    }
  }

  if (node_id >= 0 && node_id < static_cast<int>(nodes.size())) {
    const Node &target = nodes[node_id];
    float radius = 40.0f;

    for (int neighbor_id : target.adjacencyList) {
      if (neighbor_id >= 0 && neighbor_id < static_cast<int>(nodes.size())) {
        const Vec2 &neighbor_pos = nodes[neighbor_id].position;

        for (int i = 0; i < 8; ++i) {
          float angle = 2.0f * M_PI * static_cast<float>(i) / 8.0f;
          Vec2 offset(std::cos(angle) * radius, std::sin(angle) * radius);
          Vec2 candidate = neighbor_pos + offset;

          candidate.x =
              std::max(MARGIN, std::min(WINDOW_WIDTH - MARGIN, candidate.x));
          candidate.y =
              std::max(MARGIN, std::min(WINDOW_HEIGHT - MARGIN, candidate.y));

          candidates.push_back(candidate);
        }
      }
    }
  }

  if (node_id >= 0 && node_id < static_cast<int>(nodes.size())) {
    const Node &target = nodes[node_id];
    if (!target.adjacencyList.empty()) {
      Vec2 centroid(0, 0);
      for (int neighbor_id : target.adjacencyList) {
        if (neighbor_id >= 0 && neighbor_id < static_cast<int>(nodes.size())) {
          centroid = centroid + nodes[neighbor_id].position;
        }
      }
      centroid =
          centroid * (1.0f / static_cast<float>(target.adjacencyList.size()));
      candidates.push_back(centroid);
    }
  }

  return candidates;
}

int GreedySolver::CountIntersectionsWithMove(std::vector<Node> nodes,
                                             const std::vector<Edge> &edges,
                                             int node_id, Vec2 new_position) {
  if (node_id >= 0 && node_id < static_cast<int>(nodes.size())) {
    nodes[node_id].position = new_position;
  }

  return CountIntersections(nodes, edges);
}

}
