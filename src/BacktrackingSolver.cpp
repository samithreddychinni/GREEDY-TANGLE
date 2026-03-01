#include "BacktrackingSolver.hpp"
#include "MathUtils.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>

namespace GreedyTangle {

CPUMove BacktrackingSolver::FindBestMove(std::vector<Node> nodes,
                                         const std::vector<Edge> &edges) {
  auto start_time = std::chrono::steady_clock::now();

  int current_intersections = CountIntersections(nodes, edges);
  lastCandidatesEvaluated_ = 0;

  CPUMove best_move;
  best_move.intersections_before = current_intersections;

  if (current_intersections == 0) {
    return best_move;
  }

  int bestIntersections = current_intersections;
  MoveCandidate bestFirstMove{-1, Vec2()};

  Backtrack(nodes, edges, 0, current_intersections,
            bestIntersections, bestFirstMove);

  if (bestFirstMove.node_id >= 0) {
    best_move.node_id = bestFirstMove.node_id;
    best_move.from_position = nodes[bestFirstMove.node_id].position;
    best_move.to_position = bestFirstMove.position;
    best_move.intersections_after = bestIntersections;
    best_move.intersection_reduction = current_intersections - bestIntersections;
  }

  if (!best_move.isValid() || best_move.intersection_reduction <= 0) {
    float max_min_distance = 0.0f;

    for (size_t node_idx = 0; node_idx < nodes.size(); ++node_idx) {
      Vec2 original_position = nodes[node_idx].position;
      std::vector<Vec2> candidates =
          GenerateCandidatePositions(static_cast<int>(node_idx), nodes);

      for (const Vec2 &candidate : candidates) {
        ++lastCandidatesEvaluated_;

        nodes[node_idx].position = candidate;
        int new_intersections = CountIntersections(nodes, edges);
        nodes[node_idx].position = original_position;

        int reduction = current_intersections - new_intersections;

        if (reduction == 0) {
          float min_dist = std::numeric_limits<float>::max();
          for (size_t other = 0; other < nodes.size(); ++other) {
            if (other != node_idx) {
              Vec2 diff = candidate - nodes[other].position;
              float dist = diff.magnitude();
              min_dist = std::min(min_dist, dist);
            }
          }

          if (min_dist > max_min_distance) {
            max_min_distance = min_dist;
            best_move.node_id = static_cast<int>(node_idx);
            best_move.from_position = original_position;
            best_move.to_position = candidate;
            best_move.intersections_after = new_intersections;
            best_move.intersection_reduction = 0;
          }
        }
      }
    }
  }

  auto end_time = std::chrono::steady_clock::now();
  best_move.computation_time_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(end_time -
                                                            start_time)
          .count();

  if (best_move.isValid()) {
    std::cout << "[Backtracking] Found move: Node " << best_move.node_id << " -> ("
              << best_move.to_position.x << ", " << best_move.to_position.y
              << ") reduction=" << best_move.intersection_reduction
              << " time=" << best_move.computation_time_ms << "ms" << std::endl;
  } else {
    std::cout << "[Backtracking] No valid move found (stuck)" << std::endl;
  }

  return best_move;
}

void BacktrackingSolver::Backtrack(std::vector<Node> &nodes,
                                   const std::vector<Edge> &edges,
                                   int depth,
                                   int currentIntersections,
                                   int &bestIntersections,
                                   MoveCandidate &bestFirstMove) {
  if (currentIntersections == 0) {
    return;
  }

  if (depth >= MAX_DEPTH) {
    return;
  }

  for (size_t node_idx = 0; node_idx < nodes.size(); ++node_idx) {
    Vec2 original_position = nodes[node_idx].position;

    std::vector<Vec2> candidates =
        GenerateCandidatePositions(static_cast<int>(node_idx), nodes);

    for (const Vec2 &candidate : candidates) {
      ++lastCandidatesEvaluated_;

      nodes[node_idx].position = candidate;
      int new_intersections = CountIntersections(nodes, edges);

      if (new_intersections < currentIntersections) {
        if (new_intersections < bestIntersections) {
          bestIntersections = new_intersections;

          if (depth == 0) {
            bestFirstMove.node_id = static_cast<int>(node_idx);
            bestFirstMove.position = candidate;
          }
        }
        Backtrack(nodes, edges, depth + 1, new_intersections,
                  bestIntersections, bestFirstMove);
      }
      nodes[node_idx].position = original_position;
    }
  }
}

std::vector<Vec2>
BacktrackingSolver::GenerateCandidatePositions(int node_id,
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

}
